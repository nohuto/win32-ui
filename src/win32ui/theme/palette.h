// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string_view>

namespace win32ui
{

struct ThemeColors
{
    // window, dialog, menu bar backgrounds
    COLORREF background = 0;
    // lists, trees, under list headers, splitter
    COLORREF panel = 0;
    // combo boxes, list boxes, status bar, checked toolbar button, selected tab
    COLORREF surface = 0;
    COLORREF field = 0;
    // pane captions
    COLORREF header = 0;
    // outlines, dividers, group box frames, tree lines
    COLORREF border = 0;
    COLORREF text = 0;
    COLORREF muted_text = 0;
    // progress bar moving block
    COLORREF accent = 0;
    // pressed headers/buttons, selected items & their text
    COLORREF selection = 0;
    COLORREF selection_text = 0;
    COLORREF hover = 0;
    COLORREF focus = 0;
    COLORREF error = 0;
    // dark visual styles, caption and popup menus, with light glyphs
    bool dark = false;

    bool operator==(const ThemeColors&) const = default;
};

enum class ThemeMode
{
    system,
    light,
    dark,
    custom,
};

enum class ThemeFill
{
    background,
    panel,
    surface,
    field,
    header,
};

const ThemeColors& BuiltInThemeColors(bool dark);
// call before creating windows and on user changes
bool SetThemeMode(ThemeMode mode, const ThemeColors* custom = nullptr);
ThemeMode CurrentThemeMode();
const ThemeColors& CurrentThemeColors();
bool ThemeDark();
HBRUSH ThemeBrush(ThemeFill fill);
bool ThemeLightGlyphs();
// handles top level WM_SETTINGCHANGE, WM_SYSCOLORCHANGE, WM_THEMECHANGED
bool ThemeMessage(UINT message, LPARAM lparam);
const wchar_t* ThemeModeName(ThemeMode mode);
bool ParseThemeMode(std::wstring_view name, ThemeMode* mode);

} // namespace win32ui
