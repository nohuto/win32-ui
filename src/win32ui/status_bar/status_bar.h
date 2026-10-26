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
    // fits this text in the bar font, ignored for fill parts
    std::wstring sample;
    // minimum width for parts that dont fill
    int min_dip = 0;
    // at most one fill part takes remaining space
    // later parts align right, neighboring parts shrink first in narrow bars
    bool fill = false;

    bool operator==(const StatusPart&) const = default;
};

struct StatusBarHooks
{
    // themes the bar, tooltip and progress bar
    const DialogTheme* frame = nullptr;
    // part tooltip, empty falls back to part text when truncated
    std::wstring (*part_tip)(void* context, int part) = nullptr;
    // called after ShowStatusMessage expires so the app can restore its own text
    void (*message_ended)(void* context) = nullptr;
    void* context = nullptr;
};

// status bar with size grip at the bottom of parent
// name is the accessible name, hooks must outlive the bar
HWND CreateStatusBar(HWND parent, int id, const wchar_t* name, const StatusBarHooks* hooks);
// attaches to a template status bar, parts use plain text without SBT_ drawing types
bool AttachStatusBar(HWND bar, const wchar_t* name, const StatusBarHooks* hooks);
// sets parts left to right, remeasured on size, font and DPI changes
// repeating the same parts has no effect
void SetStatusParts(HWND bar, const std::vector<StatusPart>& parts);
void SetStatusText(HWND bar, int part, std::wstring_view text);
// keeps text for ms milliseconds, or until the next message when ms is 0
// empty text clears without message_ended, app chooses placement through StatusMessage
void ShowStatusMessage(HWND bar, std::wstring_view text, UINT ms = 8000);
// current message, empty after it ends
std::wstring StatusMessage(HWND bar);
// moving progress over part while shown, repeated calls have no effect
// SetStatusParts hides it if the part is removed
void ShowStatusProgress(HWND bar, int part, bool shown);
// call after theme, font or DPI changes to update height, parts, tooltip and progress
void RefreshStatusBar(HWND bar);

} // namespace win32ui
