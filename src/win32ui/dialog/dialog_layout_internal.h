// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>

namespace win32ui
{

// case insensitive class name match, false for a null window
bool ClassIs(HWND window, const wchar_t* name);
std::wstring WindowText(HWND window);
// topmost tooltip for owner, visible while inactive and keeping ampersands
HWND CreateTooltip(HWND owner);

void EraseDialogBackground(HWND dialog, HDC hdc, HBRUSH background);
bool IsOnSizeGrip(HWND dialog, LPARAM screen_point);

// frame hook for a control and its owned windows
template <typename Hooks>
void ApplyFrame(const Hooks* hooks, HWND window)
{
    if (window && hooks && hooks->frame && hooks->frame->apply)
    {
        hooks->frame->apply(window);
    }
}

} // namespace win32ui
