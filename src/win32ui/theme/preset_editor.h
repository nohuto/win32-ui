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

// app supplies and translates preset editor labels
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
    // name prompt titles, editing built ins creates a copy
    new_title,
    rename_title,
    copy_title,
    // suffix for a copied preset name, includes leading space
    copy_suffix,
    // names of thirteen colors
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
    // name for a new, renamed or copied preset, false on cancel, empty leaves it unchanged
    bool (*prompt_name)(HWND owner, const wchar_t* title, const std::wstring& initial, std::wstring* name) = nullptr;
    bool (*confirm_delete)(HWND owner, const std::wstring& name) = nullptr;
    // app chooses, reads/writes files and reports errors, imports add returned presets
    bool (*import_presets)(HWND owner, std::vector<ThemePreset>* presets) = nullptr;
    void (*export_presets)(HWND owner, const std::vector<ThemePreset>& presets) = nullptr;
    // Apply and OK pass back presets and selection
    void (*apply)(void* context, const std::vector<ThemePreset>& presets, const std::wstring& active_name) = nullptr;
    // labels and frames for both lists, editor supplies row menus
    const ListViewHooks* list_hooks = nullptr;
    void* context = nullptr;
};

// modal preset editor using 28 built ins as templates
// built ins cannot be renamed or deleted, editing prompts for a named copy
// app RC must include <win32ui/theme/preset_editor.rc>, hooks must outlive the call
void ShowThemePresetEditor(HWND owner, const std::vector<ThemePreset>& presets, const std::wstring& active_name, const PresetEditorHooks* hooks);

} // namespace win32ui
