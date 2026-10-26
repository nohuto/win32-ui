// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

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
    // one based line, opening section for missing_name, zero for success or non parse errors
    int line = 0;
};

const std::vector<ThemePreset>& BuiltInThemePresets();
// parse errors clear output, null output returns bad_line with line zero
ThemePresetResult ParseThemePresets(std::wstring_view content, std::vector<ThemePreset>* presets);
// reads at most 4 MiB, UTF-8 with optional BOM or UTF-16LE with BOM
// without BOM, falls back to ANSI if UTF-8 fails
// read/encoding errors preserve output, empty files return read_failed
ThemePresetResult ReadThemePresets(const std::wstring& path, std::vector<ThemePreset>* presets);
// writes UTF-8 without BOM, flushes a sibling temporary file before replacing destination
// over 4 MiB returns too_large and leaves destination unchanged
ThemePresetResult WriteThemePresets(const std::wstring& path, const std::vector<ThemePreset>& presets);
// adds missing built ins and updates old built in colors
// user colors with a built in name move to the first free <name> 2, <name> 3 as the built in returns
// active_name follows the renamed preset, true if anything changed
bool MergeBuiltInThemePresets(std::vector<ThemePreset>* presets, std::wstring& active_name);
// case insensitive, missing names use first preset, empty lists return null
const ThemePreset* FindThemePreset(const std::vector<ThemePreset>& presets, std::wstring_view name);

} // namespace win32ui
