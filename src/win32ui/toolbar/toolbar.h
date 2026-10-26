// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/dialog/dialog_layout.h"

#include <windows.h>

#include <commctrl.h>

#include <string>
#include <vector>

namespace win32ui
{

struct ToolbarButton
{
    // 0 for a separator
    int command = 0;
    // accessible name and fallback tooltip, never drawn
    std::wstring name;
    // arrow beside the button calls the dropdown hook
    bool dropdown = false;
};

struct ToolbarHooks
{
    // themes toolbar and tooltip
    const DialogTheme* frame = nullptr;
    // command icon at dpi, library destroys it after copying, null leaves a blank button
    HICON (*icon)(void* context, int command, UINT dpi) = nullptr;
    // command tooltip, empty falls back to button name
    std::wstring (*tip)(void* context, int command) = nullptr;
    // shortcut appended to tooltip in parentheses, null for none
    const wchar_t* (*shortcut)(int command) = nullptr;
    // dropdown arrow click, button is the screen rect
    void (*dropdown)(void* context, int command, RECT button) = nullptr;
    void* context = nullptr;
};

// flat toolbar with kIconDip square icons, name is the accessible name
// hooks must outlive it, parent must pass WM_NOTIFY to ToolbarNotify
// arrow keys move focused buttons, all toolbars use one UI thread
HWND CreateToolbar(HWND parent, int id, const wchar_t* name, const ToolbarHooks* hooks, const std::vector<ToolbarButton>& buttons);
// call after theme or DPI changes, updates icons, padding, button size and tooltip theme
void RefreshToolbar(HWND toolbar);
// height at current DPI, buttons plus margin
int ToolbarHeight(HWND toolbar);
// changes enabled state only
void EnableToolbarButton(HWND toolbar, int command, bool enabled);
// custom draw, dropdown and tooltip handling for CreateToolbar controls
// false for notifications from other controls
bool ToolbarNotify(NMHDR* header, LRESULT* result);

} // namespace win32ui
