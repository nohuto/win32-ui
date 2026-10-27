// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/dialog/dialog_layout.h"

#include <windows.h>

#include <commctrl.h>

#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

struct StatusPart
{
    std::wstring sample;
    int min_dip = 0;
    bool fill = false;

    bool operator==(const StatusPart&) const = default;
};

struct StatusBarHooks
{
    const DialogTheme* frame = nullptr;
    std::wstring (*part_tip)(void* context, int part) = nullptr;
    void (*message_ended)(void* context) = nullptr;
    void* context = nullptr;
};

HWND CreateStatusBar(HWND parent, int id, const wchar_t* name, const StatusBarHooks* hooks);

bool AttachStatusBar(HWND bar, const wchar_t* name, const StatusBarHooks* hooks);
void SetStatusParts(HWND bar, const std::vector<StatusPart>& parts);
void SetStatusText(HWND bar, int part, std::wstring_view text);
void ShowStatusMessage(HWND bar, std::wstring_view text, UINT ms = 8000);

std::wstring StatusMessage(HWND bar);

void ShowStatusProgress(HWND bar, int part, bool shown);
void RefreshStatusBar(HWND bar);

} // namespace win32ui
