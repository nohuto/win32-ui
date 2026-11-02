// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/text/text.h"
#include "win32ui/theme/theme_presets.h"

#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

struct ColorField
{
    const wchar_t* key;
    const wchar_t* label;
    COLORREF ThemeColors::* member;
};

inline constexpr ColorField kColorFields[] = {
    {L"background", TextNoop(L"Background"), &ThemeColors::background},
    {L"panel", TextNoop(L"Panel"), &ThemeColors::panel},
    {L"surface", TextNoop(L"Surface"), &ThemeColors::surface},
    {L"field", TextNoop(L"Field"), &ThemeColors::field},
    {L"header", TextNoop(L"Header"), &ThemeColors::header},
    {L"border", TextNoop(L"Border"), &ThemeColors::border},
    {L"text", TextNoop(L"Text"), &ThemeColors::text},
    {L"muted_text", TextNoop(L"Muted Text"), &ThemeColors::muted_text},
    {L"accent", TextNoop(L"Accent"), &ThemeColors::accent},
    {L"selection", TextNoop(L"Selection"), &ThemeColors::selection},
    {L"selection_text", TextNoop(L"Selection Text"), &ThemeColors::selection_text},
    {L"hover", TextNoop(L"Hover"), &ThemeColors::hover},
    {L"focus", TextNoop(L"Focus"), &ThemeColors::focus},
};

std::wstring FormatThemePresets(const std::vector<ThemePreset>& presets);
std::wstring FormatThemeColor(COLORREF color);
bool ParseThemeColor(std::wstring_view text, COLORREF* color);

}
