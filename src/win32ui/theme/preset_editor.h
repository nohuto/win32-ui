// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/list_view/list_view.h"
#include "win32ui/theme/theme_presets.h"

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

enum class PresetEditorText
{
    title,
    presets,
    colors,
    templates,
    color_column,
    hex_column,
    dark,
    new_preset,
    duplicate,
    rename,
    delete_preset,
    import_presets,
    export_presets,
    edit_color,
    apply_template,
    apply,
    ok,
    cancel,
    new_title,
    rename_title,
    copy_title,
    copy_suffix,
    background,
    panel,
    surface,
    field,
    header,
    border,
    text,
    muted_text,
    accent,
    selection,
    selection_text,
    hover,
    focus,
};

struct PresetEditorHooks
{
    const wchar_t* (*text)(PresetEditorText text) = nullptr;
    bool (*prompt_name)(HWND owner, const wchar_t* title, const std::wstring& initial, std::wstring* name) = nullptr;
    bool (*confirm_delete)(HWND owner, const std::wstring& name) = nullptr;
    bool (*import_presets)(HWND owner, std::vector<ThemePreset>* presets) = nullptr;
    void (*export_presets)(HWND owner, const std::vector<ThemePreset>& presets) = nullptr;
    void (*apply)(void* context, const std::vector<ThemePreset>& presets, const std::wstring& active_name) = nullptr;
    const ListViewHooks* list_hooks = nullptr;
    void* context = nullptr;
};

void ShowThemePresetEditor(HWND owner, const std::vector<ThemePreset>& presets, const std::wstring& active_name, const PresetEditorHooks* hooks);

} // namespace win32ui
