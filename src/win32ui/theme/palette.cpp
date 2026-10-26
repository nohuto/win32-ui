// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/theme/palette.h"

#include "win32ui/handle_owner.h"
#include "win32ui/system_api.h"
#include "win32ui/theme/palette_internal.h"

#include <array>
#include <cwchar>

namespace win32ui
{

namespace
{

constexpr ThemeColors kDark = {RGB(20, 20, 20), RGB(20, 20, 20), RGB(34, 34, 34), RGB(14, 14, 14), RGB(34, 34, 34), RGB(66, 66, 66), RGB(200, 200, 200), RGB(170, 170, 170), RGB(90, 162, 255), RGB(38, 79, 120), RGB(255, 255, 255), RGB(44, 44, 44), RGB(90, 162, 255), RGB(255, 120, 110), true};
constexpr ThemeColors kLight = {RGB(245, 245, 245), RGB(255, 255, 255), RGB(242, 242, 242), RGB(235, 235, 235), RGB(242, 242, 242), RGB(204, 204, 204), RGB(32, 32, 32), RGB(96, 96, 96), RGB(0, 120, 215), RGB(229, 241, 255), RGB(32, 32, 32), RGB(236, 236, 236), RGB(0, 120, 215), RGB(186, 32, 24), false};

struct PaletteState
{
    ThemeMode mode = ThemeMode::system;
    ThemeColors custom = kDark;
    ThemeColors colors = kDark;
    // dark and custom status bars paint themselves, mode changes alone can affect them
    bool bar_custom = false;
    bool initialized = false;
    // last dark flag sent to uxtheme, -1 before first call
    int app_dark = -1;
    // keep previous ThemeFill brushes alive for controls still using earlier WM_CTLCOLOR results
    std::array<UniqueBrush, 5> brushes;
    std::array<UniqueBrush, 5> retired;
};

PaletteState& State()
{
    static PaletteState state;
    return state;
}

bool BuildAtLeast(DWORD build)
{
    const RTL_OSVERSIONINFOW& version = OsVersion();
    return version.dwMajorVersion > 10 || (version.dwMajorVersion == 10 && version.dwBuildNumber >= build);
}

// process wide dark popup menus and scroll bars
void SetAppDark(bool dark)
{
    // 1809 uses AllowDarkModeForApp, 1903 onward uses SetPreferredAppMode with ForceDark 2 and ForceLight 3
    static const FARPROC app_mode = UxthemeOrdinal(135);
    static const auto refresh_policy = reinterpret_cast<void(WINAPI*)()>(UxthemeOrdinal(104));
    static const auto flush_menus = reinterpret_cast<void(WINAPI*)()>(UxthemeOrdinal(136));
    if (app_mode && BuildAtLeast(18362))
    {
        reinterpret_cast<int(WINAPI*)(int)>(app_mode)(dark ? 2 : 3);
    }
    else if (app_mode)
    {
        reinterpret_cast<BOOL(WINAPI*)(BOOL)>(app_mode)(dark ? TRUE : FALSE);
    }
    if (refresh_policy)
    {
        refresh_policy();
    }
    if (flush_menus)
    {
        flush_menus();
    }
}

bool SystemPrefersDark()
{
    // missing values mean light
    DWORD value = 1;
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS && value == 0;
}

bool Refresh(PaletteState& state)
{
    const ThemeColors colors = state.mode == ThemeMode::custom   ? state.custom
                               : state.mode == ThemeMode::system ? BuiltInThemeColors(SystemPrefersDark())
                                                                 : BuiltInThemeColors(state.mode == ThemeMode::dark);
    const bool bar_custom = colors.dark || state.mode == ThemeMode::custom;
    const bool changed = !state.initialized || colors != state.colors || bar_custom != state.bar_custom;
    state.initialized = true;
    state.bar_custom = bar_custom;
    state.colors = colors;
    if (changed)
    {
        const COLORREF fills[] = {colors.background, colors.panel, colors.surface, colors.field, colors.header};
        for (size_t index = 0; index < state.brushes.size(); ++index)
        {
            state.retired[index] = std::move(state.brushes[index]);
            state.brushes[index].reset(CreateSolidBrush(fills[index]));
        }
    }
    if (const int app_dark = colors.dark ? 1 : 0; app_dark != state.app_dark)
    {
        state.app_dark = app_dark;
        SetAppDark(app_dark != 0);
    }
    return changed;
}

PaletteState& Ready()
{
    PaletteState& state = State();
    if (!state.initialized)
    {
        Refresh(state);
    }
    return state;
}

} // namespace

FARPROC UxthemeOrdinal(WORD ordinal)
{
    return BuildAtLeast(17763) ? ImportProc<FARPROC>(L"uxtheme.dll", MAKEINTRESOURCEA(ordinal)) : nullptr;
}

bool ThemeStatusBarCustom()
{
    return Ready().bar_custom;
}

const ThemeColors& BuiltInThemeColors(bool dark)
{
    return dark ? kDark : kLight;
}

bool SetThemeMode(ThemeMode mode, const ThemeColors* custom)
{
    PaletteState& state = State();
    state.mode = mode;
    if (custom)
    {
        state.custom = *custom;
    }
    return Refresh(state);
}

ThemeMode CurrentThemeMode()
{
    return State().mode;
}

const ThemeColors& CurrentThemeColors()
{
    return Ready().colors;
}

bool ThemeDark()
{
    return Ready().colors.dark;
}

HBRUSH ThemeBrush(ThemeFill fill)
{
    return Ready().brushes[static_cast<size_t>(fill)].get();
}

bool ThemeLightGlyphs()
{
    // custom palettes can pair any background with any text color
    const COLORREF fill = Ready().colors.background;
    return GetRValue(fill) * 299 + GetGValue(fill) * 587 + GetBValue(fill) * 114 < 128000;
}

bool ThemeMessage(UINT message, LPARAM lparam)
{
    bool relevant = message == WM_SYSCOLORCHANGE || message == WM_THEMECHANGED;
    if (message == WM_SETTINGCHANGE)
    {
        const auto* area = reinterpret_cast<const wchar_t*>(lparam);
        relevant = area && (_wcsicmp(area, L"ImmersiveColorSet") == 0 || _wcsicmp(area, L"WindowsThemeElement") == 0);
    }
    // reentrant WM_THEMECHANGED from SetWindowTheme sees no theme change
    return relevant && Refresh(Ready());
}

const wchar_t* ThemeModeName(ThemeMode mode)
{
    switch (mode)
    {
    case ThemeMode::light:
        return L"light";
    case ThemeMode::dark:
        return L"dark";
    case ThemeMode::custom:
        return L"custom";
    default:
        return L"system";
    }
}

bool ParseThemeMode(std::wstring_view name, ThemeMode* mode)
{
    for (const ThemeMode candidate : {ThemeMode::system, ThemeMode::light, ThemeMode::dark, ThemeMode::custom})
    {
        const std::wstring_view known = ThemeModeName(candidate);
        if (name.size() == known.size() && _wcsnicmp(name.data(), known.data(), known.size()) == 0)
        {
            *mode = candidate;
            return true;
        }
    }
    return false;
}

} // namespace win32ui
