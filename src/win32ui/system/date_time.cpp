// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/date_time.h"

#include "win32ui/system/clock.h"
#include "win32ui/system/date_time_internal.h"
#include "win32ui/system_api.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <optional>

namespace win32ui
{
namespace
{

using ZoneConvertFn = BOOL(WINAPI*)(const DYNAMIC_TIME_ZONE_INFORMATION*, const SYSTEMTIME*, LPSYSTEMTIME);

// LOCALE_SSHORTTIME, Windows 7 and later
constexpr LCTYPE kShortTime = 0x00000079;

std::wstring LocaleText(LCTYPE type)
{
    wchar_t text[80] = {};
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, type, text, static_cast<int>(std::size(text)));
    return text;
}

// Vista has no short time pattern and gets the one with seconds
std::wstring TimePattern(bool with_seconds)
{
    std::wstring pattern = with_seconds ? std::wstring() : LocaleText(kShortTime);
    return pattern.empty() ? LocaleText(LOCALE_STIMEFORMAT) : pattern;
}

// patterns hold at most 80 characters, their output can be longer
template <typename Format>
std::optional<std::wstring> Formatted(const Format& format)
{
    wchar_t buffer[80] = {};
    const int length = format(buffer, static_cast<int>(std::size(buffer)));
    if (length > 0)
    {
        return std::wstring(buffer, static_cast<size_t>(length - 1));
    }
    const int needed = GetLastError() == ERROR_INSUFFICIENT_BUFFER ? format(nullptr, 0) : 0;
    if (needed <= 0)
    {
        return std::nullopt;
    }
    std::wstring text(static_cast<size_t>(needed), L'\0');
    if (format(text.data(), needed) != needed)
    {
        return std::nullopt;
    }
    text.resize(static_cast<size_t>(needed - 1));
    return text;
}

// daylight saving dates changed over the years in many zones, with dynamic daylight time disabled the fixed rules of
// the zone apply to every year
bool DynamicZone(DYNAMIC_TIME_ZONE_INFORMATION* zone) noexcept
{
    return GetDynamicTimeZoneInformation(zone) != TIME_ZONE_ID_INVALID && !zone->DynamicDaylightTimeDisabled;
}

// SystemTimeToTzSpecificLocalTimeEx costs about 5 us for a date of another year than the last one (registry reads), a
// list of 20000 keys spread over the years took 114 ms instead of 1 ms with FileTimeToLocalFileTime, so each UTC year
// keeps the ticks where its offset changes, found with the same function, and a date only looks them up
constexpr ULONGLONG kTicksPerMs = 10'000;
constexpr ULONGLONG kTicksPerDay = 86'400'000 * kTicksPerMs;
// near New Year the function can apply the rules of the other year, those days are converted one by one
constexpr ULONGLONG kYearMargin = 2 * kTicksPerDay;
// years whose two changes lie a month or more apart (see ChangesFarApart), so weekly samples see every change
constexpr ULONGLONG kSampleStep = 7 * kTicksPerDay;
constexpr size_t kMaxChanges = 8;
constexpr size_t kCachedYears = 32;
// reading the zone costs more than a cached conversion, so a series of dates reads it once a second, a single date
// reads it again, the zone may have changed since the last one (a dialog showing a date it converts back later)
constexpr LONGLONG kSeriesGapPerSecond = 500;

struct YearOffsets
{
    ULONGLONG begin = 0;
    ULONGLONG end = 0;
    // 0 = convert one by one
    size_t count = 0;
    ULONGLONG at[kMaxChanges] = {};
    LONGLONG offset[kMaxChanges] = {};
};

struct OffsetCache
{
    SRWLOCK lock = SRWLOCK_INIT;
    DYNAMIC_TIME_ZONE_INFORMATION current = {};
    bool current_dynamic = false;
    bool current_read = false;
    // performance counter ticks
    LONGLONG current_read_at = 0;
    DYNAMIC_TIME_ZONE_INFORMATION zone = {};
    YearOffsets years[kCachedYears];
    size_t next = 0;
};

OffsetCache g_offsets;
// per thread, a series on another thread must not keep a single conversion here on an old zone
thread_local LONGLONG t_last_conversion = 0;

bool SameZone(const DYNAMIC_TIME_ZONE_INFORMATION& left, const DYNAMIC_TIME_ZONE_INFORMATION& right) noexcept
{
    return left.Bias == right.Bias && left.StandardBias == right.StandardBias && left.DaylightBias == right.DaylightBias && left.DynamicDaylightTimeDisabled == right.DynamicDaylightTimeDisabled &&
           std::memcmp(&left.StandardDate, &right.StandardDate, sizeof(SYSTEMTIME)) == 0 && std::memcmp(&left.DaylightDate, &right.DaylightDate, sizeof(SYSTEMTIME)) == 0 &&
           std::wcsncmp(left.TimeZoneKeyName, right.TimeZoneKeyName, std::size(left.TimeZoneKeyName)) == 0;
}

// local minus UTC at a whole ms
bool ProbeOffset(const DYNAMIC_TIME_ZONE_INFORMATION& zone, ULONGLONG tick, LONGLONG* offset) noexcept
{
    const FILETIME utc = FileTimeFromTicks(tick);
    SYSTEMTIME system = {};
    SYSTEMTIME local = {};
    FILETIME local_file = {};
    if (!FileTimeToSystemTime(&utc, &system) || !ZoneTimeToLocal(zone, system, &local) || !SystemTimeToFileTime(&local, &local_file))
    {
        return false;
    }
    *offset = static_cast<LONGLONG>(FileTimeTicks(local_file) - tick);
    return true;
}

// valid rules can put both daylight saving changes into one week, then samples could miss the period between them
bool ChangesFarApart(const DYNAMIC_TIME_ZONE_INFORMATION& zone, WORD year) noexcept
{
    // Vista SP1 and later, the headers declare it from Windows 7 on
    using YearRulesFn = BOOL(WINAPI*)(USHORT, PDYNAMIC_TIME_ZONE_INFORMATION, LPTIME_ZONE_INFORMATION);
    static const auto year_rules = ImportProc<YearRulesFn>(L"kernel32.dll", "GetTimeZoneInformationForYear");
    DYNAMIC_TIME_ZONE_INFORMATION copy = zone;
    TIME_ZONE_INFORMATION rules = {};
    if (!year_rules || !year_rules(year, &copy, &rules))
    {
        return false;
    }
    if (!rules.DaylightDate.wMonth || !rules.StandardDate.wMonth)
    {
        return true;
    }
    const int months = (rules.DaylightDate.wMonth - rules.StandardDate.wMonth + 12) % 12;
    return months >= 2 && months <= 10;
}

// count stays 0 when a sample fails, the changes may lie close together or the year changes more often than fits
void FillYear(const DYNAMIC_TIME_ZONE_INFORMATION& zone, WORD utc_year, YearOffsets* year) noexcept
{
    year->count = 0;
    LONGLONG current = 0;
    if (!ChangesFarApart(zone, utc_year) || !ProbeOffset(zone, year->begin, &current))
    {
        return;
    }
    size_t count = 1;
    year->at[0] = year->begin;
    year->offset[0] = current;
    const ULONGLONG last = year->end - kTicksPerMs;
    for (ULONGLONG sample = year->begin; sample < last;)
    {
        const ULONGLONG next = (std::min)(sample + kSampleStep, last);
        LONGLONG next_offset = 0;
        if (!ProbeOffset(zone, next, &next_offset))
        {
            return;
        }
        // each change between the samples, found to the ms
        for (ULONGLONG low = sample; next_offset != current;)
        {
            ULONGLONG high = next;
            LONGLONG high_offset = next_offset;
            while (high - low > kTicksPerMs)
            {
                const ULONGLONG middle = low + (high - low) / kTicksPerMs / 2 * kTicksPerMs;
                LONGLONG middle_offset = 0;
                if (!ProbeOffset(zone, middle, &middle_offset))
                {
                    return;
                }
                if (middle_offset == current)
                {
                    low = middle;
                }
                else
                {
                    high = middle;
                    high_offset = middle_offset;
                }
            }
            if (count == kMaxChanges)
            {
                return;
            }
            year->at[count] = high;
            year->offset[count] = high_offset;
            ++count;
            current = high_offset;
            low = high;
        }
        sample = next;
    }
    year->count = count;
}

const YearOffsets* YearOf(const DYNAMIC_TIME_ZONE_INFORMATION& zone, ULONGLONG tick, WORD utc_year) noexcept
{
    OffsetCache& cache = g_offsets;
    if (!SameZone(cache.zone, zone))
    {
        cache.zone = zone;
        for (YearOffsets& year : cache.years)
        {
            year = {};
        }
        cache.next = 0;
    }
    for (const YearOffsets& year : cache.years)
    {
        if (tick >= year.begin && tick < year.end)
        {
            return &year;
        }
    }
    const SYSTEMTIME first = {utc_year, 1, 0, 1, 0, 0, 0, 0};
    const SYSTEMTIME after = {static_cast<WORD>(utc_year + 1), 1, 0, 1, 0, 0, 0, 0};
    FILETIME begin = {};
    FILETIME end = {};
    if (!SystemTimeToFileTime(&first, &begin) || !SystemTimeToFileTime(&after, &end) || tick < FileTimeTicks(begin) + kYearMargin || tick >= FileTimeTicks(end) - kYearMargin)
    {
        return nullptr;
    }
    YearOffsets& year = cache.years[cache.next];
    cache.next = (cache.next + 1) % kCachedYears;
    year.begin = FileTimeTicks(begin) + kYearMargin;
    year.end = FileTimeTicks(end) - kYearMargin;
    FillYear(zone, utc_year, &year);
    return &year;
}

// zone null = the current zone, false when it has no dynamic rules
bool CachedToLocal(const DYNAMIC_TIME_ZONE_INFORMATION* zone, const FILETIME& utc, const SYSTEMTIME& system, SYSTEMTIME* local) noexcept
{
    // SYSTEMTIME holds whole ms
    const ULONGLONG tick = FileTimeTicks(utc) / kTicksPerMs * kTicksPerMs;
    OffsetCache& cache = g_offsets;
    AcquireSRWLockExclusive(&cache.lock);
    if (!zone)
    {
        const int64_t frequency = QpcClock::Frequency();
        const int64_t now = QpcClock::Now();
        if (!cache.current_read || now - t_last_conversion > frequency / kSeriesGapPerSecond || now - cache.current_read_at > frequency)
        {
            cache.current = {};
            cache.current_dynamic = DynamicZone(&cache.current);
            cache.current_read = true;
            cache.current_read_at = now;
        }
        t_last_conversion = now;
        if (!cache.current_dynamic)
        {
            ReleaseSRWLockExclusive(&cache.lock);
            return false;
        }
    }
    const DYNAMIC_TIME_ZONE_INFORMATION& rules = zone ? *zone : cache.current;
    const YearOffsets* year = YearOf(rules, tick, system.wYear);
    LONGLONG offset = 0;
    const bool cached = year && year->count;
    for (size_t index = cached ? year->count : 0; index-- > 0;)
    {
        if (tick >= year->at[index])
        {
            offset = year->offset[index];
            break;
        }
    }
    if (!cached)
    {
        // copy, the current zone can change once the lock is gone
        const DYNAMIC_TIME_ZONE_INFORMATION copy = rules;
        ReleaseSRWLockExclusive(&cache.lock);
        return ZoneTimeToLocal(copy, system, local);
    }
    ReleaseSRWLockExclusive(&cache.lock);
    const ULONGLONG converted = tick + static_cast<ULONGLONG>(offset);
    const FILETIME local_file = FileTimeFromTicks(converted);
    return FileTimeToSystemTime(&local_file, local) != FALSE;
}

}

std::wstring FormatDateTimeWith(const SYSTEMTIME& time, const wchar_t* date_pattern, const wchar_t* time_pattern)
{
    const std::optional<std::wstring> date = Formatted([&](wchar_t* buffer, int size) {
        return GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, date_pattern ? 0 : DATE_SHORTDATE, &time, date_pattern, buffer, size, nullptr);
    });
    const std::optional<std::wstring> clock = date ? Formatted([&](wchar_t* buffer, int size) {
        return GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &time, time_pattern, buffer, size);
    })
                                                   : std::nullopt;
    return clock ? *date + L' ' + *clock : std::wstring();
}

std::wstring FormatDateTime(const SYSTEMTIME& time, bool with_seconds)
{
    return FormatDateTimeWith(time, nullptr, TimePattern(with_seconds).c_str());
}

std::wstring DateTimePattern()
{
    return LocaleText(LOCALE_SSHORTDATE) + L' ' + TimePattern(false);
}

// both Windows 7 and later, they pick the rules of the year by the local time (the UTC year is wrong near New Year)
bool ZoneTimeToLocal(const DYNAMIC_TIME_ZONE_INFORMATION& zone, const SYSTEMTIME& utc, SYSTEMTIME* local) noexcept
{
    static const auto to_local = ImportProc<ZoneConvertFn>(L"kernel32.dll", "SystemTimeToTzSpecificLocalTimeEx");
    return to_local && to_local(&zone, &utc, local);
}

bool ZoneLocalToUtc(const DYNAMIC_TIME_ZONE_INFORMATION& zone, const SYSTEMTIME& local, SYSTEMTIME* utc) noexcept
{
    static const auto to_utc = ImportProc<ZoneConvertFn>(L"kernel32.dll", "TzSpecificLocalTimeToSystemTimeEx");
    return to_utc && to_utc(&zone, &local, utc);
}

bool FileTimeToLocal(const FILETIME& utc, SYSTEMTIME* local) noexcept
{
    SYSTEMTIME system = {};
    if (!local || !FileTimeToSystemTime(&utc, &system))
    {
        return false;
    }
    return CachedToLocal(nullptr, utc, system, local) || SystemTimeToTzSpecificLocalTime(nullptr, &system, local) != FALSE;
}

bool CachedZoneTimeToLocal(const DYNAMIC_TIME_ZONE_INFORMATION& zone, const FILETIME& utc, SYSTEMTIME* local) noexcept
{
    SYSTEMTIME system = {};
    return local && FileTimeToSystemTime(&utc, &system) && CachedToLocal(&zone, utc, system, local);
}

bool LocalToFileTime(const SYSTEMTIME& local, FILETIME* utc) noexcept
{
    if (!utc)
    {
        return false;
    }
    SYSTEMTIME system = {};
    DYNAMIC_TIME_ZONE_INFORMATION zone = {};
    const bool converted = (DynamicZone(&zone) && ZoneLocalToUtc(zone, local, &system)) || TzSpecificLocalTimeToSystemTime(nullptr, &local, &system);
    return converted && SystemTimeToFileTime(&system, utc);
}

std::wstring FormatFileTime(const FILETIME& utc, bool with_seconds)
{
    SYSTEMTIME local = {};
    return FileTimeToLocal(utc, &local) ? FormatDateTime(local, with_seconds) : std::wstring();
}

}
