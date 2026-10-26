// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string_view>

namespace win32ui
{

// built in dark/light colors or preset colors
struct ThemeColors
{
    // window, dialog and menu bar backgrounds
    COLORREF background = 0;
    // lists, trees, under list headers, the splitter
    COLORREF panel = 0;
    // combo boxes, list boxes, the status bar, a checked toolbar button, the selected tab
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
    // pressed headers/buttons, selected items and their text
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
    // app supplied preset colors
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
// call before creating windows and on user changes, copies custom for ThemeMode::custom
// rereads system dark mode, true means the app must call ReapplyTheme
// one theme per process, confined to the UI thread
bool SetThemeMode(ThemeMode mode, const ThemeColors* custom = nullptr);
ThemeMode CurrentThemeMode();
const ThemeColors& CurrentThemeColors();
// dark visual styles
bool ThemeDark();
// library owned, valid until the second theme change after this call
HBRUSH ThemeBrush(ThemeFill fill);
// light icons and lines for dark background fills
bool ThemeLightGlyphs();
// handles top level WM_SETTINGCHANGE, WM_SYSCOLORCHANGE and WM_THEMECHANGED
// rereads system colors or dark mode when relevant, true means the app must call ReapplyTheme
bool ThemeMessage(UINT message, LPARAM lparam);
// stable settings name, never displayed
const wchar_t* ThemeModeName(ThemeMode mode);
// case insensitive, false for unknown names
bool ParseThemeMode(std::wstring_view name, ThemeMode* mode);

} // namespace win32ui
