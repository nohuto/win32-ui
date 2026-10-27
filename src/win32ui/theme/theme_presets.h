// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/theme/palette.h"

#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

struct ThemePreset
{
    std::wstring name;
    ThemeColors colors;
};

enum class ThemePresetError
{
    none,
    read_failed,
    too_large,
    bad_encoding,
    missing_name,
    bad_line,
    write_failed,
};

struct ThemePresetResult
{
    ThemePresetError error = ThemePresetError::none;
    int line = 0;
};

const std::vector<ThemePreset>& BuiltInThemePresets();

ThemePresetResult ParseThemePresets(std::wstring_view content, std::vector<ThemePreset>* presets);
ThemePresetResult ReadThemePresets(const std::wstring& path, std::vector<ThemePreset>* presets);
ThemePresetResult WriteThemePresets(const std::wstring& path, const std::vector<ThemePreset>& presets);

bool MergeBuiltInThemePresets(std::vector<ThemePreset>* presets, std::wstring& active_name);
const ThemePreset* FindThemePreset(const std::vector<ThemePreset>& presets, std::wstring_view name);

} // namespace win32ui
