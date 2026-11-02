// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace win32ui
{

constexpr uint64_t FileTimeTicks(const FILETIME& time) noexcept
{
    return (uint64_t{time.dwHighDateTime} << 32) | time.dwLowDateTime;
}

constexpr FILETIME FileTimeFromTicks(uint64_t ticks) noexcept
{
    return {static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
}

std::wstring FormatDateTime(const SYSTEMTIME& time, bool with_seconds = false);
std::wstring DateTimePattern();
// daylight saving rule of that date, FileTimeToLocalFileTime applies the rule of today
bool FileTimeToLocal(const FILETIME& utc, SYSTEMTIME* local) noexcept;
bool LocalToFileTime(const SYSTEMTIME& local, FILETIME* utc) noexcept;
std::wstring FormatFileTime(const FILETIME& utc, bool with_seconds = false);

}
