// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/theme/preset_editor.h"
#include "win32ui/theme/theme_presets.h"

#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

struct ColorField
{
    const wchar_t* key;
    PresetEditorText text;
    COLORREF ThemeColors::* member;
};

inline constexpr ColorField kColorFields[] = {
    {L"background", PresetEditorText::background, &ThemeColors::background},
    {L"panel", PresetEditorText::panel, &ThemeColors::panel},
    {L"surface", PresetEditorText::surface, &ThemeColors::surface},
    {L"field", PresetEditorText::field, &ThemeColors::field},
    {L"header", PresetEditorText::header, &ThemeColors::header},
    {L"border", PresetEditorText::border, &ThemeColors::border},
    {L"text", PresetEditorText::text, &ThemeColors::text},
    {L"muted_text", PresetEditorText::muted_text, &ThemeColors::muted_text},
    {L"accent", PresetEditorText::accent, &ThemeColors::accent},
    {L"selection", PresetEditorText::selection, &ThemeColors::selection},
    {L"selection_text", PresetEditorText::selection_text, &ThemeColors::selection_text},
    {L"hover", PresetEditorText::hover, &ThemeColors::hover},
    {L"focus", PresetEditorText::focus, &ThemeColors::focus},
};

bool EqualName(std::wstring_view left, std::wstring_view right);

// writes thirteen color keys with CRLF
std::wstring FormatThemePresets(const std::vector<ThemePreset>& presets);
std::wstring FormatThemeColor(COLORREF color);
bool ParseThemeColor(std::wstring_view text, COLORREF* color);

} // namespace win32ui
