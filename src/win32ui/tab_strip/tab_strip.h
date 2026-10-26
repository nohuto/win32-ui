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
    // themes strip and single row scroll arrows
    const DialogTheme* frame = nullptr;
    // whether index shows a close button, null allows closing when at least two tabs exist
    bool (*closable)(void* context, int index) = nullptr;
    // app handles or ignores close requests, null hides close buttons
    void (*close)(void* context, int index) = nullptr;
    // accessible close button name, tab label becomes its description
    // null omits close buttons from the accessible tree
    const wchar_t* close_name = nullptr;
    void* context = nullptr;
};

// native button tabs managed by app TabCtrl_ calls
// parent receives TCN_SELCHANGING and TCN_SELCHANGE, including arrow key selection
// truncated labels get full text tooltips, focus mark follows window keyboard cues
// name is the accessible name, hooks must outlive the strip, all strips use one UI thread
HWND CreateTabStrip(HWND parent, int id, const wchar_t* name, const TabStripHooks* hooks);
// minimum tab width, also used by app layout
inline constexpr int kTabMinWidthDip = 90;
// call after tab, font, theme or DPI changes
// updates close button space, minimum width and themes for strip and scroll arrows
void RefreshTabStrip(HWND strip);
// row height at the current font and DPI
int TabStripRowHeight(HWND strip);
// fits rows within width and max_height, else uses one scrolling row with selection visible
// returns height, repeated inputs have no effect
int FitTabStrip(HWND strip, int width, int max_height);
// draws the close glyph centered in rect
void DrawCloseGlyph(HDC hdc, const RECT& rect, COLORREF color, UINT dpi);

} // namespace win32ui
