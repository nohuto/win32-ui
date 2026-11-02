// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace win32ui
{

struct TextHash
{
    using is_transparent = void;
    size_t operator()(std::wstring_view text) const noexcept
    {
        return std::hash<std::wstring_view>{}(text);
    }
};

using LanguageCatalog = std::unordered_map<std::wstring, std::wstring, TextHash, std::equal_to<>>;

void ParseLanguageCatalog(std::wstring_view content, LanguageCatalog* catalog);

}
