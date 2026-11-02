// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/json/reader.h"

#include "win32ui/system/strings.h"
#include "win32ui/text/text.h"

#include <cwchar>

namespace win32ui
{

namespace
{

const wchar_t* ErrorMessage(JsonErrorCode code) noexcept
{
    switch (code)
    {
    case JsonErrorCode::kExpectedCharacter:
        return TextNoop(L"Expected '%c'.");
    case JsonErrorCode::kTooDeep:
        return TextNoop(L"The file is nested too deeply.");
    case JsonErrorCode::kUnterminatedComment:
        return TextNoop(L"An unterminated block comment is not allowed.");
    case JsonErrorCode::kUnexpectedToken:
        return TextNoop(L"Unexpected token.");
    case JsonErrorCode::kStringTooLong:
        return TextNoop(L"A text member is longer than the format allows.");
    case JsonErrorCode::kUnterminatedString:
        return TextNoop(L"The file ends inside a string.");
    case JsonErrorCode::kUnescapedControl:
        return TextNoop(L"A string contains an unescaped control character.");
    case JsonErrorCode::kMalformedEscape:
        return TextNoop(L"A string contains a malformed escape.");
    case JsonErrorCode::kUnsupportedEscape:
        return TextNoop(L"A string contains an unsupported escape.");
    case JsonErrorCode::kUnsignedExpected:
        return TextNoop(L"An unsigned number was expected.");
    case JsonErrorCode::kLeadingZero:
        return TextNoop(L"A number has a leading zero.");
    case JsonErrorCode::kNumberOutOfRange:
        return TextNoop(L"A number is out of range.");
    case JsonErrorCode::kIntegersOnly:
        return TextNoop(L"Only integers are supported.");
    case JsonErrorCode::kBooleanExpected:
        return TextNoop(L"A boolean was expected.");
    case JsonErrorCode::kEmptyFraction:
        return TextNoop(L"A number has an empty fraction.");
    case JsonErrorCode::kEmptyExponent:
        return TextNoop(L"A number has an empty exponent.");
    case JsonErrorCode::kTrailingContent:
        return TextNoop(L"The file contains trailing content.");
    default:
        return L"";
    }
}

void WriteErrorText(void* context, const JsonDiagnostic& diagnostic, std::wstring_view custom_text)
{
    auto* destination = static_cast<std::wstring*>(context);
    if (!destination || !destination->empty() || !diagnostic.first_failure || diagnostic.code == JsonErrorCode::kNone)
    {
        return;
    }
    if (diagnostic.code == JsonErrorCode::kCustom)
    {
        if (custom_text.empty())
        {
            destination->assign(Text(L"The file isn't valid JSON."));
        }
        else
        {
            destination->assign(custom_text);
        }
    }
    else
    {
        destination->assign(Text(ErrorMessage(diagnostic.code)));
        // replaced by hand, a translated format must not reach printf
        if (const size_t mark = destination->find(L"%c"); diagnostic.code == JsonErrorCode::kExpectedCharacter && mark != std::wstring::npos)
        {
            destination->replace(mark, 2, 1, diagnostic.expected);
        }
    }
    destination->append(L" (")
        .append(Text(L"Line"))
        .append(L": ")
        .append(std::to_wstring(diagnostic.position.line))
        .append(L", ")
        .append(Text(L"Column"))
        .append(L": ")
        .append(std::to_wstring(diagnostic.position.column))
        .append(L")");
}

bool Digit(wchar_t character)
{
    return character >= L'0' && character <= L'9';
}

bool Hex4(std::wstring_view text, std::size_t* cursor, unsigned* value)
{
    if (text.size() - *cursor < 4)
    {
        return false;
    }
    unsigned result = 0;
    for (std::size_t index = 0; index < 4; ++index)
    {
        const int digit = HexDigitValue(text[*cursor + index]);
        if (digit < 0)
        {
            return false;
        }
        result = (result << 4) | static_cast<unsigned>(digit);
    }
    *cursor += 4;
    *value = result;
    return true;
}

}

JsonErrorSink JsonErrorText(std::wstring* destination) noexcept
{
    return {WriteErrorText, destination};
}

JsonReader::JsonReader(std::wstring_view text, JsonErrorSink errors, int max_depth) noexcept
    : text_(text), errors_(errors), max_depth_(max_depth)
{
}

bool JsonReader::Fail(std::wstring_view app_text)
{
    return Fail(JsonErrorCode::kCustom, 0, app_text);
}

bool JsonReader::Fail(JsonErrorCode code, wchar_t expected, std::wstring_view custom_text)
{
    JsonDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.position = error_position_;
    diagnostic.position.offset = cursor_;
    diagnostic.expected = expected;
    diagnostic.first_failure = !failed_;
    for (std::size_t scan = error_position_.offset; scan < cursor_; ++scan)
    {
        if (text_[scan] == L'\n')
        {
            ++diagnostic.position.line;
            diagnostic.position.column = 1;
        }
        else
        {
            ++diagnostic.position.column;
        }
    }
    error_position_ = diagnostic.position;
    cursor_ = text_.size();
    failed_ = true;
    if (errors_.callback)
    {
        errors_.callback(errors_.context, diagnostic, custom_text);
    }
    return false;
}

void JsonReader::SkipSpace()
{
    while (cursor_ < text_.size())
    {
        if (text_[cursor_] == L' ' || text_[cursor_] == L'\t' || text_[cursor_] == L'\r' || text_[cursor_] == L'\n')
        {
            ++cursor_;
            continue;
        }
        if (text_[cursor_] != L'/' || text_.size() - cursor_ < 2)
        {
            return;
        }
        if (text_[cursor_ + 1] == L'/')
        {
            cursor_ += 2;
            while (cursor_ < text_.size() && text_[cursor_] != L'\n')
            {
                ++cursor_;
            }
            continue;
        }
        if (text_[cursor_ + 1] != L'*')
        {
            return;
        }
        cursor_ += 2;
        while (text_.size() - cursor_ >= 2 && (text_[cursor_] != L'*' || text_[cursor_ + 1] != L'/'))
        {
            ++cursor_;
        }
        if (text_.size() - cursor_ < 2)
        {
            Fail(JsonErrorCode::kUnterminatedComment);
            return;
        }
        cursor_ += 2;
    }
}

wchar_t JsonReader::Next()
{
    SkipSpace();
    return cursor_ < text_.size() ? text_[cursor_] : L'\0';
}

bool JsonReader::Expect(wchar_t character)
{
    if (Next() != character)
    {
        return Fail(JsonErrorCode::kExpectedCharacter, character);
    }
    ++cursor_;
    return true;
}

bool JsonReader::Open(wchar_t bracket)
{
    if (++depth_ > max_depth_)
    {
        return Fail(JsonErrorCode::kTooDeep);
    }
    return Expect(bracket);
}

bool JsonReader::Empty(wchar_t bracket)
{
    return Next() == bracket;
}

bool JsonReader::Close(wchar_t bracket)
{
    --depth_;
    return Expect(bracket);
}

bool JsonReader::Comma()
{
    if (Next() != L',')
    {
        return false;
    }
    ++cursor_;
    return true;
}

bool JsonReader::Word(std::wstring_view word)
{
    if (text_.substr(cursor_, word.size()) != word)
    {
        return Fail(JsonErrorCode::kUnexpectedToken);
    }
    cursor_ += word.size();
    return true;
}

bool JsonReader::String(std::wstring* out, std::size_t limit)
{
    if (!Expect(L'"'))
    {
        return false;
    }
    out->clear();
    for (;;)
    {
        const std::size_t run = cursor_;
        while (cursor_ < text_.size() && text_[cursor_] != L'"' && text_[cursor_] != L'\\' && text_[cursor_] >= 0x20)
        {
            ++cursor_;
        }
        out->append(text_.substr(run, cursor_ - run));
        if (out->size() > limit)
        {
            return Fail(JsonErrorCode::kStringTooLong);
        }
        if (cursor_ == text_.size())
        {
            return Fail(JsonErrorCode::kUnterminatedString);
        }
        const wchar_t character = text_[cursor_++];
        if (character == L'"')
        {
            return true;
        }
        if (character != L'\\')
        {
            --cursor_;
            return Fail(JsonErrorCode::kUnescapedControl);
        }
        if (cursor_ == text_.size())
        {
            return Fail(JsonErrorCode::kUnterminatedString);
        }
        const wchar_t escape = text_[cursor_++];
        switch (escape)
        {
        case L'"':
        case L'\\':
        case L'/':
            out->push_back(escape);
            break;
        case L'b':
            out->push_back(L'\b');
            break;
        case L'f':
            out->push_back(L'\f');
            break;
        case L'n':
            out->push_back(L'\n');
            break;
        case L'r':
            out->push_back(L'\r');
            break;
        case L't':
            out->push_back(L'\t');
            break;
        case L'u':
            {
                unsigned unit = 0;
                if (!Hex4(text_, &cursor_, &unit))
                {
                    return Fail(JsonErrorCode::kMalformedEscape);
                }
                // one escape produces one UTF-16 unit
                out->push_back(static_cast<wchar_t>(unit));
                break;
            }
        default:
            return Fail(JsonErrorCode::kUnsupportedEscape);
        }
    }
}

bool JsonReader::Unsigned(std::uint64_t* out)
{
    if (!Digit(Next()))
    {
        return Fail(JsonErrorCode::kUnsignedExpected);
    }
    if (text_[cursor_] == L'0' && text_.size() - cursor_ >= 2 && Digit(text_[cursor_ + 1]))
    {
        return Fail(JsonErrorCode::kLeadingZero);
    }
    std::uint64_t value = 0;
    for (; cursor_ < text_.size() && Digit(text_[cursor_]); ++cursor_)
    {
        const std::uint64_t digit = static_cast<std::uint64_t>(text_[cursor_] - L'0');
        if (value > (UINT64_MAX - digit) / 10)
        {
            return Fail(JsonErrorCode::kNumberOutOfRange);
        }
        value = value * 10 + digit;
    }
    if (cursor_ < text_.size() && std::wcschr(L".eE-+", text_[cursor_]))
    {
        return Fail(JsonErrorCode::kIntegersOnly);
    }
    *out = value;
    return true;
}

bool JsonReader::Signed(std::int64_t* out)
{
    const bool negative = Next() == L'-';
    if (negative)
    {
        ++cursor_;
    }
    std::uint64_t magnitude = 0;
    if (!Unsigned(&magnitude))
    {
        return false;
    }
    const std::uint64_t limit = negative ? static_cast<std::uint64_t>(INT64_MAX) + 1 : static_cast<std::uint64_t>(INT64_MAX);
    if (magnitude > limit)
    {
        return Fail(JsonErrorCode::kNumberOutOfRange);
    }
    *out = negative ? (magnitude == limit ? INT64_MIN : -static_cast<std::int64_t>(magnitude)) : static_cast<std::int64_t>(magnitude);
    return true;
}

bool JsonReader::Bool(bool* out)
{
    const wchar_t next = Next();
    if (next == L't')
    {
        *out = true;
        return Word(L"true");
    }
    if (next == L'f')
    {
        *out = false;
        return Word(L"false");
    }
    return Fail(JsonErrorCode::kBooleanExpected);
}

bool JsonReader::Skip()
{
    switch (Next())
    {
    case L'{':
        return Object([this](const std::wstring&) { return Skip(); });
    case L'[':
        return Array([this] { return Skip(); });
    case L'"':
        {
            std::wstring ignored;
            return String(&ignored);
        }
    case L't':
        return Word(L"true");
    case L'f':
        return Word(L"false");
    case L'n':
        return Word(L"null");
    default:
        break;
    }
    if (cursor_ < text_.size() && text_[cursor_] == L'-')
    {
        ++cursor_;
    }
    const std::size_t digits = cursor_;
    while (cursor_ < text_.size() && Digit(text_[cursor_]))
    {
        ++cursor_;
    }
    if (cursor_ == digits || (text_[digits] == L'0' && cursor_ - digits > 1))
    {
        return Fail(JsonErrorCode::kUnexpectedToken);
    }
    if (cursor_ < text_.size() && text_[cursor_] == L'.')
    {
        const std::size_t fraction = ++cursor_;
        while (cursor_ < text_.size() && Digit(text_[cursor_]))
        {
            ++cursor_;
        }
        if (cursor_ == fraction)
        {
            return Fail(JsonErrorCode::kEmptyFraction);
        }
    }
    if (cursor_ < text_.size() && (text_[cursor_] == L'e' || text_[cursor_] == L'E'))
    {
        ++cursor_;
        if (cursor_ < text_.size() && (text_[cursor_] == L'+' || text_[cursor_] == L'-'))
        {
            ++cursor_;
        }
        const std::size_t exponent = cursor_;
        while (cursor_ < text_.size() && Digit(text_[cursor_]))
        {
            ++cursor_;
        }
        if (cursor_ == exponent)
        {
            return Fail(JsonErrorCode::kEmptyExponent);
        }
    }
    return true;
}

bool JsonReader::End()
{
    SkipSpace();
    return !failed_ && (cursor_ == text_.size() || Fail(JsonErrorCode::kTrailingContent));
}

}
