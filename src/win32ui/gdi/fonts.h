// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/handle_owner.h"

#include <windows.h>

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace win32ui
{

// replaces Windows message font, keeping point size at every DPI, nullptr restores it
void SetCustomFont(const LOGFONTW* font);

// Windows message font at dpi, following Windows text size, default is Segoe UI 9 pt
// dpi 0 uses system DPI
LOGFONTW MessageFontLogFont(UINT dpi);
// Windows menu font at dpi, menu bar height follows it
LOGFONTW MenuFontLogFont(UINT dpi);
// custom font if set, otherwise Windows message font
LOGFONTW UIFontLogFont(UINT dpi);

// point size at system DPI, uses zero_height_fallback when height is 0
int FontPointSize(const LOGFONTW& font, int zero_height_fallback = 0);
int FontHeight(int point_size);
int FontHeight(int point_size, UINT dpi);

// UI font for one window and DPI, with line height and text width cache
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
    // grows a DIP length by the line height increase over Segoe UI 9 pt, never shrinks it
    int Grow(int dip) const noexcept
    {
        return MulDiv(dip, grow_percent_, 100);
    }
    // text width in the UI font, cached per font for up to 512 texts
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
