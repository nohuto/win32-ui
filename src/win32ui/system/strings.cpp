// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/strings.h"

#include "win32ui/system/strings_internal.h"
#include "win32ui/system_api.h"
#include "win32ui/text/text.h"

#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <iterator>

namespace win32ui
{
namespace
{

using FindStringOrdinalFn = int(WINAPI*)(DWORD, LPCWSTR, int, LPCWSTR, int, BOOL);

constexpr DWORD kFindFromStart = 0x00400000;
// CompareStringOrdinal and FindStringOrdinal take int lengths
constexpr size_t kMaxPiece = INT_MAX;
// decimal digits besides ASCII that wcstoul of the CRT takes (corecrt_internal_strtox.h)
constexpr wchar_t kDigitZeros[] = {0x0660, 0x06F0, 0x0966, 0x09E6, 0x0A66, 0x0AE6, 0x0B66, 0x0C66, 0x0CE6, 0x0D66, 0x0E50, 0x0ED0, 0x0F20, 0x1040, 0x17E0, 0x1810, 0xFF10};

int DigitValue(wchar_t character) noexcept
{
    if (character < 0x80)
    {
        return HexDigitValue(character);
    }
    for (const wchar_t zero : kDigitZeros)
    {
        if (character >= zero && character < zero + 10)
        {
            return character - zero;
        }
    }
    return -1;
}

int CompareOrdinal(std::wstring_view left, std::wstring_view right) noexcept
{
    return CompareOrdinalPieces(left, right, kMaxPiece);
}

std::optional<std::string> ToMultiByte(std::wstring_view text, UINT code_page, DWORD flags)
{
    if (text.empty())
    {
        return std::string();
    }
    if (text.size() > INT_MAX)
    {
        return std::nullopt;
    }
    const int length = static_cast<int>(text.size());
    const int size = WideCharToMultiByte(code_page, flags, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return std::nullopt;
    }
    std::string output(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(code_page, flags, text.data(), length, output.data(), size, nullptr, nullptr) != size)
    {
        return std::nullopt;
    }
    return output;
}

std::optional<std::wstring> ToWide(std::string_view text, UINT code_page, DWORD flags)
{
    if (text.empty())
    {
        return std::wstring();
    }
    if (text.size() > INT_MAX)
    {
        return std::nullopt;
    }
    const int length = static_cast<int>(text.size());
    const int size = MultiByteToWideChar(code_page, flags, text.data(), length, nullptr, 0);
    if (size <= 0)
    {
        return std::nullopt;
    }
    std::wstring output(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(code_page, flags, text.data(), length, output.data(), size) != size)
    {
        return std::nullopt;
    }
    return output;
}

}

std::wstring_view TrimView(std::wstring_view text) noexcept
{
    while (!text.empty() && iswspace(text.front()))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && iswspace(text.back()))
    {
        text.remove_suffix(1);
    }
    return text;
}

std::wstring TrimWhitespace(std::wstring_view text)
{
    return std::wstring(TrimView(text));
}

bool IsBlank(std::wstring_view text) noexcept
{
    return TrimView(text).empty();
}

std::wstring ToLower(std::wstring_view text)
{
    std::wstring result(text);
    for (size_t at = 0; at < result.size(); at += MAXDWORD)
    {
        CharLowerBuffW(result.data() + at, static_cast<DWORD>(std::min<size_t>(result.size() - at, MAXDWORD)));
    }
    return result;
}

int CompareInsensitive(std::wstring_view left, std::wstring_view right) noexcept
{
    return CompareOrdinal(left, right);
}

bool EqualsInsensitive(std::wstring_view left, std::wstring_view right) noexcept
{
    return left.size() == right.size() && CompareOrdinal(left, right) == 0;
}

bool StartsWithInsensitive(std::wstring_view text, std::wstring_view prefix) noexcept
{
    return prefix.size() <= text.size() && EqualsInsensitive(text.substr(0, prefix.size()), prefix);
}

bool EndsWithInsensitive(std::wstring_view text, std::wstring_view suffix) noexcept
{
    return suffix.size() <= text.size() && EqualsInsensitive(text.substr(text.size() - suffix.size()), suffix);
}

size_t FindInsensitive(std::wstring_view text, std::wstring_view needle) noexcept
{
    if (needle.empty() || needle.size() > text.size())
    {
        return std::wstring_view::npos;
    }
    // Windows 7 and later
    static const auto find = ImportProc<FindStringOrdinalFn>(L"kernel32.dll", "FindStringOrdinal");
    if (!find || text.size() > kMaxPiece)
    {
        return FindInsensitiveScan(text, needle);
    }
    const int found = find(kFindFromStart, text.data(), static_cast<int>(text.size()), needle.data(), static_cast<int>(needle.size()), TRUE);
    return found < 0 ? std::wstring_view::npos : static_cast<size_t>(found);
}

int CompareOrdinalPieces(std::wstring_view left, std::wstring_view right, size_t piece) noexcept
{
    for (;;)
    {
        if (left.empty() || right.empty())
        {
            return static_cast<int>(!left.empty()) - static_cast<int>(!right.empty());
        }
        const size_t left_piece = std::min(left.size(), piece);
        const size_t right_piece = std::min(right.size(), piece);
        const int result = CompareStringOrdinal(left.data(), static_cast<int>(left_piece), right.data(), static_cast<int>(right_piece), TRUE) - CSTR_EQUAL;
        if (result != 0 || (left_piece == left.size() && right_piece == right.size()))
        {
            return result;
        }
        left.remove_prefix(left_piece);
        right.remove_prefix(right_piece);
    }
}

size_t FindInsensitiveScan(std::wstring_view text, std::wstring_view needle) noexcept
{
    if (needle.empty())
    {
        return std::wstring_view::npos;
    }
    for (size_t start = 0; start + needle.size() <= text.size(); ++start)
    {
        if (CompareOrdinal(text.substr(start, needle.size()), needle) == 0)
        {
            return start;
        }
    }
    return std::wstring_view::npos;
}

bool ContainsInsensitive(std::wstring_view text, std::wstring_view needle) noexcept
{
    return FindInsensitive(text, needle) != std::wstring_view::npos;
}

std::optional<uint64_t> ParseUnsigned(std::wstring_view text, int base, bool hex_prefix) noexcept
{
    if (base != 2 && base != 16)
    {
        base = 10;
    }
    text = TrimView(text);
    // like the CRT, any zero digit starts a prefix
    const bool prefix = text.size() > 2 && DigitValue(text[0]) == 0;
    if ((base == 16 || hex_prefix) && prefix && (text[1] == L'x' || text[1] == L'X'))
    {
        base = 16;
        text.remove_prefix(2);
    }
    else if (base == 2 && prefix && (text[1] == L'b' || text[1] == L'B'))
    {
        text.remove_prefix(2);
    }
    uint64_t value = 0;
    bool digits = false;
    for (const wchar_t character : text)
    {
        if (base == 2 && (character == L'_' || character == L'\'' || iswspace(character)))
        {
            continue;
        }
        const int digit = DigitValue(character);
        if (digit < 0 || digit >= base || value > (UINT64_MAX - static_cast<uint64_t>(digit)) / static_cast<uint64_t>(base))
        {
            return std::nullopt;
        }
        value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
        digits = true;
    }
    return digits ? std::optional<uint64_t>(value) : std::nullopt;
}

bool ParseBool(std::wstring_view text) noexcept
{
    return text == L"1" || EqualsInsensitive(text, L"true") || EqualsInsensitive(text, L"yes");
}

std::wstring FormatHex(uint64_t value, int min_digits)
{
    wchar_t digits[16] = {};
    int count = 0;
    do
    {
        digits[count++] = L"0123456789ABCDEF"[value & 0xF];
        value >>= 4;
    } while (value != 0);
    const int width = std::max(count, std::clamp(min_digits, 0, 16));
    std::wstring text(L"0x");
    text.append(static_cast<size_t>(width - count), L'0');
    while (count > 0)
    {
        text.push_back(digits[--count]);
    }
    return text;
}

std::wstring FormatFixed(double value, int digits)
{
    if (!std::isfinite(value))
    {
        return {};
    }
    constexpr int kMaxDigits = 100;
    // sign, every integer digit of the largest double, point, digits
    char buffer[1 + std::numeric_limits<double>::max_exponent10 + 1 + 1 + kMaxDigits] = {};
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value, std::chars_format::fixed, std::clamp(digits, 0, kMaxDigits));
    return result.ec == std::errc{} ? std::wstring(std::begin(buffer), result.ptr) : std::wstring();
}

namespace
{

void AppendHexBytes(std::wstring* output, std::span<const unsigned char> data, wchar_t separator, bool uppercase)
{
    const wchar_t* digits = uppercase ? L"0123456789ABCDEF" : L"0123456789abcdef";
    for (size_t index = 0; index < data.size(); ++index)
    {
        if (index != 0 && separator)
        {
            output->push_back(separator);
        }
        output->push_back(digits[data[index] >> 4]);
        output->push_back(digits[data[index] & 0x0F]);
    }
}

}

std::wstring HexBytes(std::span<const unsigned char> data, wchar_t separator, bool uppercase, size_t max_bytes)
{
    const size_t count = max_bytes == 0 ? data.size() : std::min(max_bytes, data.size());
    std::wstring output;
    output.reserve(count * (separator ? 3 : 2) + 4);
    AppendHexBytes(&output, data.first(count), separator, uppercase);
    if (count < data.size())
    {
        output += L" ...";
    }
    return output;
}

std::wstring HexByteLines(std::span<const unsigned char> data)
{
    std::wstring output;
    output.reserve(data.size() * 3 + data.size() / 16 * 2);
    for (size_t start = 0; start < data.size(); start += 16)
    {
        if (start != 0)
        {
            output += L"\r\n";
        }
        AppendHexBytes(&output, data.subspan(start, std::min<size_t>(16, data.size() - start)), L' ', true);
    }
    return output;
}

bool ParseHexBytes(std::wstring_view text, std::vector<unsigned char>* bytes, size_t* error_at)
{
    bytes->clear();
    int high = -1;
    size_t high_at = 0;
    for (size_t index = 0; index < text.size(); ++index)
    {
        const wchar_t character = text[index];
        if (const int value = HexDigitValue(character); value >= 0)
        {
            if (high < 0)
            {
                high = value;
                high_at = index;
            }
            else
            {
                bytes->push_back(static_cast<unsigned char>((high << 4) | value));
                high = -1;
            }
            continue;
        }
        if (std::wstring_view(L" \t\r\n,;:-").find(character) != std::wstring_view::npos)
        {
            continue;
        }
        if ((character == L'x' || character == L'X') && high == 0 && index > 0 && text[index - 1] == L'0')
        {
            high = -1;
            continue;
        }
        if (error_at)
        {
            *error_at = index;
        }
        return false;
    }
    if (high >= 0 && error_at)
    {
        *error_at = high_at;
    }
    return high < 0;
}

namespace
{

int Base64Index(wchar_t character) noexcept
{
    return character >= L'A' && character <= L'Z'   ? character - L'A'
           : character >= L'a' && character <= L'z' ? character - L'a' + 26
           : character >= L'0' && character <= L'9' ? character - L'0' + 52
           : character == L'+'                      ? 62
           : character == L'/'                      ? 63
                                                    : -1;
}

std::wstring AtOffset(const wchar_t* message, size_t offset)
{
    return std::wstring(message) + L"\r\n" + Text(L"Offset") + L": " + std::to_wstring(offset);
}

size_t PaddingOf(std::wstring_view text) noexcept
{
    size_t padding = 0;
    while (padding < 2 && padding < text.size() && text[text.size() - 1 - padding] == L'=')
    {
        ++padding;
    }
    return padding;
}

bool DecodeBase64Body(std::wstring_view text, size_t padding, std::vector<unsigned char>* bytes, std::wstring* error)
{
    const size_t body = text.size() - padding;
    for (size_t index = 0; index < body; ++index)
    {
        if (Base64Index(text[index]) < 0)
        {
            *error = AtOffset(Text(L"Invalid Base64 character."), index);
            return false;
        }
    }
    // unused padding bits must be zero
    if ((padding == 2 && (Base64Index(text[body - 1]) & 0x0F) != 0) || (padding == 1 && (Base64Index(text[body - 1]) & 0x03) != 0))
    {
        *error = Text(L"Invalid Base64 padding bits.");
        return false;
    }
    if (text.empty())
    {
        return true;
    }
    if (text.size() > MAXDWORD)
    {
        *error = Text(L"Input is too large to decode.");
        return false;
    }
    DWORD size = 0;
    if (!CryptStringToBinaryW(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr))
    {
        *error = Text(L"The text couldn't be decoded.");
        return false;
    }
    bytes->resize(size);
    if (size != 0 && !CryptStringToBinaryW(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, bytes->data(), &size, nullptr, nullptr))
    {
        bytes->clear();
        *error = Text(L"The text couldn't be decoded.");
        return false;
    }
    bytes->resize(size);
    return true;
}

}

bool DecodeBase64(std::wstring_view text, bool url, std::vector<unsigned char>* bytes, std::wstring* error)
{
    bytes->clear();
    // before the URL form copies it
    if (text.size() > MAXDWORD)
    {
        *error = Text(L"Input is too large to decode.");
        return false;
    }
    if (!url)
    {
        if (text.size() % 4 != 0)
        {
            *error = Text(L"Base64 length isn't a multiple of four.");
            return false;
        }
        return DecodeBase64Body(text, PaddingOf(text), bytes, error);
    }
    std::wstring normalized;
    normalized.reserve(text.size() + 2);
    for (size_t index = 0; index < text.size(); ++index)
    {
        const wchar_t character = text[index];
        if (character == L'-' || character == L'_' || character == L'=' || (Base64Index(character) >= 0 && character != L'+' && character != L'/'))
        {
            normalized.push_back(character == L'-' ? L'+' : (character == L'_' ? L'/' : character));
            continue;
        }
        *error = AtOffset(Text(L"Invalid Base64URL character."), index);
        return false;
    }
    size_t padding = PaddingOf(normalized);
    if (const size_t first = normalized.find(L'='); first != std::wstring::npos && first != normalized.size() - padding)
    {
        *error = Text(L"Base64URL padding isn't at the end.");
        return false;
    }
    const size_t remainder = normalized.size() % 4;
    if (remainder == 1)
    {
        *error = Text(L"Base64URL length isn't valid.");
        return false;
    }
    if (remainder != 0)
    {
        if (padding != 0)
        {
            *error = Text(L"Base64URL padding is incomplete.");
            return false;
        }
        padding = 4 - remainder;
        normalized.append(padding, L'=');
    }
    return DecodeBase64Body(normalized, padding, bytes, error);
}

bool DecodePercent(std::wstring_view text, std::vector<unsigned char>* bytes, std::wstring* error)
{
    bytes->clear();
    bytes->reserve(text.size());
    for (size_t index = 0; index < text.size(); ++index)
    {
        const wchar_t character = text[index];
        if (character == L'%')
        {
            if (index + 2 >= text.size())
            {
                *error = AtOffset(Text(L"Incomplete percent escape."), index);
                return false;
            }
            const int high = HexDigitValue(text[index + 1]);
            const int low = HexDigitValue(text[index + 2]);
            if (high < 0 || low < 0)
            {
                *error = AtOffset(Text(L"Invalid percent escape."), index);
                return false;
            }
            bytes->push_back(static_cast<unsigned char>((high << 4) | low));
            index += 2;
            continue;
        }
        if (character > 0x7F)
        {
            *error = Text(L"Percent encoded text must be ASCII.");
            return false;
        }
        bytes->push_back(static_cast<unsigned char>(character));
    }
    return true;
}

std::wstring HexDump(std::span<const unsigned char> data, int group_bytes, bool utf16)
{
    constexpr size_t kBytesPerLine = 16;
    static constexpr wchar_t kHexDigits[] = L"0123456789ABCDEF";
    static const std::array<wchar_t, 256> ansi = [] {
        std::array<wchar_t, 256> entries = {};
        for (int index = 0; index < 256; ++index)
        {
            const char narrow = static_cast<char>(index);
            wchar_t wide = 0;
            const bool converted = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, &narrow, 1, &wide, 1) == 1;
            entries[static_cast<size_t>(index)] = converted && iswprint(wide) ? wide : L'.';
        }
        return entries;
    }();
    const size_t group = group_bytes == 2 || group_bytes == 4 || group_bytes == 8 ? static_cast<size_t>(group_bytes) : 1;
    const int offset_width = data.size() > 0xFFFFFFFF ? 16 : (data.size() > 0xFFFF ? 8 : 4);
    std::wstring text;
    text.reserve(data.size() * 5);
    for (size_t offset = 0; offset < data.size(); offset += kBytesPerLine)
    {
        wchar_t offset_text[24] = {};
        swprintf_s(offset_text, L"%0*llX", offset_width, static_cast<unsigned long long>(offset));
        text.append(offset_text).append(L"  ");
        for (size_t start = 0; start < kBytesPerLine; start += group)
        {
            for (size_t index = group; index-- > 0;)
            {
                const size_t position = offset + start + index;
                if (position < data.size())
                {
                    text.push_back(kHexDigits[data[position] >> 4]);
                    text.push_back(kHexDigits[data[position] & 0x0F]);
                }
                else
                {
                    text.append(L"  ");
                }
            }
            text.push_back(L' ');
        }
        text.append(L"  ");
        if (utf16)
        {
            for (size_t index = 0; index + 1 < kBytesPerLine && offset + index + 1 < data.size(); index += 2)
            {
                const wchar_t value = static_cast<wchar_t>(data[offset + index] | (data[offset + index + 1] << 8));
                text.push_back(iswprint(value) ? value : L'.');
            }
        }
        else
        {
            for (size_t index = 0; index < kBytesPerLine && offset + index < data.size(); ++index)
            {
                text.push_back(ansi[data[offset + index]]);
            }
        }
        text.append(L"\r\n");
    }
    return text;
}

std::string WideToMultiByte(std::wstring_view text, unsigned code_page)
{
    return ToMultiByte(text, code_page, 0).value_or(std::string());
}

std::wstring MultiByteToWide(std::string_view text, unsigned code_page)
{
    return ToWide(text, code_page, 0).value_or(std::wstring());
}

std::optional<std::string> WideToUtf8(std::wstring_view text)
{
    return ToMultiByte(text, CP_UTF8, WC_ERR_INVALID_CHARS);
}

std::optional<std::wstring> Utf8ToWide(std::string_view text)
{
    return ToWide(text, CP_UTF8, MB_ERR_INVALID_CHARS);
}

std::vector<std::wstring> SplitLines(std::wstring_view text)
{
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find_first_of(L"\r\n", start);
        if (end == std::wstring_view::npos)
        {
            end = text.size();
        }
        if (const std::wstring_view line = TrimView(text.substr(start, end - start)); !line.empty())
        {
            lines.emplace_back(line);
        }
        start = end + 1;
    }
    return lines;
}

std::wstring JoinLines(std::span<const std::wstring> lines)
{
    std::wstring text;
    for (const std::wstring& line : lines)
    {
        if (line.empty())
        {
            continue;
        }
        if (!text.empty())
        {
            text.append(L"\r\n");
        }
        text.append(line);
    }
    return text;
}

std::vector<std::wstring_view> LineViews(std::wstring_view text)
{
    std::vector<std::wstring_view> lines;
    size_t start = 0;
    while (start < text.size())
    {
        const size_t end = text.find_first_of(L"\r\n", start);
        if (end == std::wstring_view::npos)
        {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + (text[end] == L'\r' && end + 1 < text.size() && text[end + 1] == L'\n' ? 2 : 1);
    }
    return lines;
}

std::wstring EditLineBreaks(std::wstring_view text)
{
    std::wstring result;
    result.reserve(text.size());
    for (const wchar_t character : text)
    {
        if (character == L'\r')
        {
            continue;
        }
        if (character == L'\n')
        {
            result.push_back(L'\r');
        }
        result.push_back(character);
    }
    return result;
}

std::wstring PlainLineBreaks(std::wstring_view text)
{
    std::wstring result;
    result.reserve(text.size());
    std::copy_if(text.begin(), text.end(), std::back_inserter(result), [](wchar_t character) { return character != L'\r'; });
    return result;
}

std::wstring SingleLineText(std::wstring_view text)
{
    std::wstring result;
    result.reserve(text.size());
    bool space = false;
    for (const wchar_t character : text)
    {
        if (character == L'\r' || character == L'\n' || character == L'\t')
        {
            space = !result.empty();
            continue;
        }
        if (space)
        {
            result.push_back(L' ');
            space = false;
        }
        result.push_back(character);
    }
    return result;
}

std::wstring CsvField(std::wstring_view field)
{
    if (field.find_first_of(L",\"\r\n") == std::wstring_view::npos && (field.empty() || (field.front() != L' ' && field.back() != L' ')))
    {
        return std::wstring(field);
    }
    std::wstring quoted = L"\"";
    for (const wchar_t character : field)
    {
        quoted.append(character == L'"' ? 2 : 1, character);
    }
    quoted.push_back(L'"');
    return quoted;
}

std::wstring ExpandEnvironment(const wchar_t* text)
{
    if (!text || !*text)
    {
        return {};
    }
    const DWORD needed = ExpandEnvironmentStringsW(text, nullptr, 0);
    if (needed == 0)
    {
        return {};
    }
    std::wstring expanded(needed, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(text, expanded.data(), needed);
    if (written == 0 || written > needed)
    {
        return {};
    }
    expanded.resize(written - 1);
    return expanded;
}

}
