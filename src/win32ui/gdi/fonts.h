// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/handle_owner.h"

#include <windows.h>

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace win32ui
{

void SetCustomFont(const LOGFONTW* font);

LOGFONTW MessageFontLogFont(UINT dpi);
LOGFONTW MenuFontLogFont(UINT dpi);
LOGFONTW UIFontLogFont(UINT dpi);

int FontPointSize(const LOGFONTW& font, int zero_height_fallback = 0);
int FontHeight(int point_size);
int FontHeight(int point_size, UINT dpi);

class FontSet
{
  public:
    void Create(UINT dpi);
    HFONT ui() const noexcept
    {
        return ui_.get();
    }
    UINT dpi() const noexcept
    {
        return dpi_;
    }
    int line_height() const noexcept
    {
        return line_height_;
    }
    int Grow(int dip) const noexcept
    {
        return MulDiv(dip, grow_percent_, 100);
    }
    int TextWidth(const wchar_t* text) const;

  private:
    struct TextHash
    {
        using is_transparent = void;

        std::size_t operator()(std::wstring_view text) const noexcept
        {
            return std::hash<std::wstring_view>{}(text);
        }
    };

    UniqueFont ui_;
    UINT dpi_ = 96;
    int line_height_ = 16;
    int grow_percent_ = 100;
    int average_char_width_ = 7;
    mutable std::unordered_map<std::wstring, int, TextHash, std::equal_to<>> widths_;
};

} // namespace win32ui
