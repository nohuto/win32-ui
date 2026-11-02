// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace win32ui
{

HWND CreateTooltip(HWND owner);

void EraseDialogBackground(HWND dialog, HDC hdc, HBRUSH background);
bool IsOnSizeGrip(HWND dialog, LPARAM screen_point);

template <typename Hooks>
void ApplyFrame(const Hooks* hooks, HWND window)
{
    if (window && hooks && hooks->frame && hooks->frame->apply)
    {
        hooks->frame->apply(window);
    }
}

}
