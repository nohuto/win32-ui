// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace win32ui
{

enum class JsonErrorCode
{
    kNone,
    kExpectedCharacter,
    kTooDeep,
    kUnterminatedComment,
    kUnexpectedToken,
    kStringTooLong,
    kUnterminatedString,
    kUnescapedControl,
    kMalformedEscape,
    kUnsupportedEscape,
    kUnsignedExpected,
    kLeadingZero,
    kNumberOutOfRange,
    kIntegersOnly,
    kBooleanExpected,
    kEmptyFraction,
    kEmptyExponent,
    kTrailingContent,
    kCustom
};

struct JsonPosition
{
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
};

struct JsonDiagnostic
{
    JsonErrorCode code = JsonErrorCode::kNone;
    JsonPosition position;
    wchar_t expected = 0;
    bool first_failure = false;
};

using JsonErrorCallback = void (*)(void* context, const JsonDiagnostic& diagnostic, std::wstring_view custom_text);

struct JsonErrorSink
{
    JsonErrorCallback callback = nullptr;
    void* context = nullptr;
};

class JsonReader
{
  public:
    explicit JsonReader(std::wstring_view text, JsonErrorSink errors = {}, int max_depth = 16) noexcept;

    JsonReader(const JsonReader&) = default;
    JsonReader& operator=(const JsonReader&) = default;

    template <typename Member>
    bool Object(Member&& member)
    {
        if (!Open(L'{'))
        {
            return false;
        }
        if (!Empty(L'}'))
        {
            std::wstring key;
            do
            {
                if (!String(&key, 256) || !Expect(L':') || !member(static_cast<const std::wstring&>(key)))
                {
                    return false;
                }
            } while (Comma() && !Empty(L'}'));
        }
        return Close(L'}');
    }

    template <typename Element>
    bool Array(Element&& element)
    {
        if (!Open(L'['))
        {
            return false;
        }
        if (!Empty(L']'))
        {
            do
            {
                if (!element())
                {
                    return false;
                }
            } while (Comma() && !Empty(L']'));
        }
        return Close(L']');
    }

    bool String(std::wstring* out, std::size_t limit = SIZE_MAX);
    bool Unsigned(std::uint64_t* out);
    bool Signed(std::int64_t* out);
    bool Bool(bool* out);
    bool Skip();
    bool End();
    bool Fail(std::wstring_view app_text);
    wchar_t Next();

  private:
    bool Open(wchar_t bracket);
    bool Empty(wchar_t bracket);
    bool Close(wchar_t bracket);
    bool Comma();
    bool Expect(wchar_t character);
    bool Word(std::wstring_view word);
    void SkipSpace();
    bool Fail(JsonErrorCode code, wchar_t expected = 0, std::wstring_view custom_text = {});

    std::wstring_view text_;
    std::size_t cursor_ = 0;
    JsonErrorSink errors_;
    JsonPosition error_position_;
    int max_depth_ = 16;
    int depth_ = 0;
    bool failed_ = false;
};

} // namespace win32ui
