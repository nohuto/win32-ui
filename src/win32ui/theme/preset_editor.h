// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/theme/theme_presets.h"

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

struct PresetEditorHooks
{
    bool (*prompt_name)(HWND owner, const wchar_t* title, const std::wstring& initial, std::wstring* name) = nullptr;
    bool (*confirm_delete)(HWND owner, const std::wstring& name) = nullptr;
    bool (*import_presets)(HWND owner, std::vector<ThemePreset>* presets) = nullptr;
    void (*export_presets)(HWND owner, const std::vector<ThemePreset>& presets) = nullptr;
    void (*apply)(void* context, const std::vector<ThemePreset>& presets, const std::wstring& active_name) = nullptr;
    void* context = nullptr;
};

void ShowThemePresetEditor(HWND owner, const std::vector<ThemePreset>& presets, const std::wstring& active_name, const PresetEditorHooks* hooks);

}
