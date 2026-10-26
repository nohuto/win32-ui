// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

void AppendJsonString(std::wstring* out, std::wstring_view text);

// balance container calls, empty keys omit the member name
class JsonWriter
{
  public:
    void BeginObject(std::wstring_view key = {});
    void EndObject();
    void BeginArray(std::wstring_view key);
    void EndArray();

    void String(std::wstring_view key, std::wstring_view value);
    void Unsigned(std::wstring_view key, std::uint64_t value);
    void Signed(std::wstring_view key, std::int64_t value);
    // fixed decimals use a period, nonfinite values become null
    void Number(std::wstring_view key, double value, int digits);
    void Bool(std::wstring_view key, bool value);
    void Null(std::wstring_view key);

    std::wstring Take();

  private:
    void Prefix(std::wstring_view key);
    void Close(wchar_t bracket);

    std::wstring out_;
    std::vector<bool> stack_;
};

} // namespace win32ui
