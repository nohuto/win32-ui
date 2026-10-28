// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/json/writer.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cwchar>
#include <iterator>
#include <limits>
#include <utility>

namespace win32ui
{

namespace
{

std::wstring FormatFixed(double value, int digits)
{
    if (!std::isfinite(value))
    {
        return L"";
    }
    constexpr int kMaxDigits = 16;
    constexpr int kIntegerDigits = std::numeric_limits<double>::max_exponent10 + 1;
    char buffer[1 + kIntegerDigits + 1 + kMaxDigits] = {};
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value, std::chars_format::fixed, std::clamp(digits, 0, kMaxDigits));
    return result.ec == std::errc{} ? std::wstring(buffer, result.ptr) : L"";
}

} // namespace

void AppendJsonString(std::wstring* out, std::wstring_view text)
{
    out->push_back(L'"');
    std::size_t start = 0;
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const wchar_t character = text[index];
        const bool high = character >= 0xD800 && character <= 0xDBFF;
        const bool paired = high && index + 1 < text.size() && text[index + 1] >= 0xDC00 && text[index + 1] <= 0xDFFF;
        if (paired)
        {
            ++index;
            continue;
        }
        if (character >= 0x20 && character != L'"' && character != L'\\' && character != 0x7F && !high &&
            (character < 0xDC00 || character > 0xDFFF))
        {
            continue;
        }
        out->append(text.substr(start, index - start));
        start = index + 1;
        switch (character)
        {
        case L'"':
            out->append(L"\\\"");
            break;
        case L'\\':
            out->append(L"\\\\");
            break;
        case L'\b':
            out->append(L"\\b");
            break;
        case L'\f':
            out->append(L"\\f");
            break;
        case L'\n':
            out->append(L"\\n");
            break;
        case L'\r':
            out->append(L"\\r");
            break;
        case L'\t':
            out->append(L"\\t");
            break;
        default:
            {
                wchar_t buffer[8] = {};
                swprintf_s(buffer, L"\\u%04X", static_cast<unsigned>(character));
                out->append(buffer);
                break;
            }
        }
    }
    out->append(text.substr(start));
    out->push_back(L'"');
}

void JsonWriter::BeginObject(std::wstring_view key)
{
    Prefix(key);
    out_ += L"{";
    stack_.push_back({true});
}

void JsonWriter::EndObject()
{
    Close(L'}');
}

void JsonWriter::BeginArray(std::wstring_view key)
{
    Prefix(key);
    out_ += L"[";
    stack_.push_back({false});
}

void JsonWriter::EndArray()
{
    Close(L']');
}

void JsonWriter::String(std::wstring_view key, std::wstring_view value)
{
    Prefix(key);
    AppendJsonString(&out_, value);
}

void JsonWriter::Unsigned(std::wstring_view key, std::uint64_t value)
{
    Prefix(key);
    out_ += std::to_wstring(value);
}

void JsonWriter::Signed(std::wstring_view key, std::int64_t value)
{
    Prefix(key);
    out_ += std::to_wstring(value);
}

void JsonWriter::Number(std::wstring_view key, double value, int digits)
{
    const std::wstring text = FormatFixed(value, digits);
    Prefix(key);
    out_ += text.empty() ? L"null" : text;
}

void JsonWriter::Bool(std::wstring_view key, bool value)
{
    Prefix(key);
    out_ += value ? L"true" : L"false";
}

void JsonWriter::Null(std::wstring_view key)
{
    Prefix(key);
    out_ += L"null";
}

std::wstring JsonWriter::Take()
{
    out_ += L"\n";
    stack_.clear();
    return std::move(out_);
}

void JsonWriter::Prefix(std::wstring_view key)
{
    if (!stack_.empty())
    {
        out_ += stack_.back().first ? L"\n" : L",\n";
        stack_.back().first = false;
        out_.append(stack_.size() * 2, L' ');
    }
    if (!stack_.empty() && stack_.back().object)
    {
        AppendJsonString(&out_, key);
        out_ += L": ";
    }
}

void JsonWriter::Close(wchar_t bracket)
{
    const bool empty = stack_.back().first;
    stack_.pop_back();
    if (!empty)
    {
        out_ += L"\n";
        out_.append(stack_.size() * 2, L' ');
    }
    out_.push_back(bracket);
}

} // namespace win32ui
