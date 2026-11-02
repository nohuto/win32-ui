// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

constexpr int HexDigitValue(wchar_t character) noexcept
{
    return character >= L'0' && character <= L'9'   ? character - L'0'
           : character >= L'a' && character <= L'f' ? character - L'a' + 10
           : character >= L'A' && character <= L'F' ? character - L'A' + 10
                                                    : -1;
}

std::wstring_view TrimView(std::wstring_view text) noexcept;
std::wstring TrimWhitespace(std::wstring_view text);
bool IsBlank(std::wstring_view text) noexcept;
std::wstring ToLower(std::wstring_view text);

int CompareInsensitive(std::wstring_view left, std::wstring_view right) noexcept;
bool EqualsInsensitive(std::wstring_view left, std::wstring_view right) noexcept;
bool StartsWithInsensitive(std::wstring_view text, std::wstring_view prefix) noexcept;
bool EndsWithInsensitive(std::wstring_view text, std::wstring_view suffix) noexcept;
size_t FindInsensitive(std::wstring_view text, std::wstring_view needle) noexcept;
bool ContainsInsensitive(std::wstring_view text, std::wstring_view needle) noexcept;

std::optional<uint64_t> ParseUnsigned(std::wstring_view text, int base = 10, bool hex_prefix = false) noexcept;
bool ParseBool(std::wstring_view text) noexcept;

std::wstring FormatHex(uint64_t value, int min_digits = 0);
std::wstring FormatFixed(double value, int digits);
std::wstring HexBytes(std::span<const unsigned char> data, wchar_t separator = L' ', bool uppercase = false, size_t max_bytes = 0);
// edits wrap huge lines very slowly
std::wstring HexByteLines(std::span<const unsigned char> data);
std::wstring HexDump(std::span<const unsigned char> data, int group_bytes, bool utf16);
bool ParseHexBytes(std::wstring_view text, std::vector<unsigned char>* bytes, size_t* error_at = nullptr);
bool DecodeBase64(std::wstring_view text, bool url, std::vector<unsigned char>* bytes, std::wstring* error);
bool DecodePercent(std::wstring_view text, std::vector<unsigned char>* bytes, std::wstring* error);

std::string WideToMultiByte(std::wstring_view text, unsigned code_page);
std::wstring MultiByteToWide(std::string_view text, unsigned code_page);
std::optional<std::string> WideToUtf8(std::wstring_view text);
std::optional<std::wstring> Utf8ToWide(std::string_view text);

std::vector<std::wstring> SplitLines(std::wstring_view text);
std::wstring JoinLines(std::span<const std::wstring> lines);
std::vector<std::wstring_view> LineViews(std::wstring_view text);
std::wstring EditLineBreaks(std::wstring_view text);
std::wstring PlainLineBreaks(std::wstring_view text);
std::wstring SingleLineText(std::wstring_view text);
std::wstring CsvField(std::wstring_view field);
std::wstring ExpandEnvironment(const wchar_t* text);

}
