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
    // accessible name and fallback tooltip
    std::wstring name;
    bool dropdown = false;
};

struct ToolbarHooks
{
    const DialogTheme* frame = nullptr;
    HICON (*icon)(void* context, int command, UINT dpi) = nullptr;
    std::wstring (*tip)(void* context, int command) = nullptr;
    const wchar_t* (*shortcut)(int command) = nullptr;
    void (*dropdown)(void* context, int command, RECT button) = nullptr;
    void* context = nullptr;
};

HWND CreateToolbar(HWND parent, int id, const wchar_t* name, const ToolbarHooks* hooks, const std::vector<ToolbarButton>& buttons);
void RefreshToolbar(HWND toolbar);
int ToolbarHeight(HWND toolbar);
void EnableToolbarButton(HWND toolbar, int command, bool enabled);
bool ToolbarNotify(NMHDR* header, LRESULT* result);

} // namespace win32ui
