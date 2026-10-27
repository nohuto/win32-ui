// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/dialog/dialog_layout.h"

#include <windows.h>

#include <commctrl.h>

namespace win32ui
{

struct TabStripHooks
{
    const DialogTheme* frame = nullptr;
    bool (*closable)(void* context, int index) = nullptr;
    void (*close)(void* context, int index) = nullptr;
    const wchar_t* close_name = nullptr;
    void* context = nullptr;
};

HWND CreateTabStrip(HWND parent, int id, const wchar_t* name, const TabStripHooks* hooks);

inline constexpr int kTabMinWidthDip = 90;

void RefreshTabStrip(HWND strip);
int TabStripRowHeight(HWND strip);
int FitTabStrip(HWND strip, int width, int max_height);
void DrawCloseGlyph(HDC hdc, const RECT& rect, COLORREF color, UINT dpi);

} // namespace win32ui
