// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <commctrl.h>

namespace win32ui
{

struct SplitterHooks
{
    // current pane size, queried at drag start and on each arrow key
    int (*size)(void* context, HWND splitter) = nullptr;
    // drag start size plus movement, then once with done on release or capture loss
    // arrow keys send one size with done, app clamps, stores and lays out the size including the bar
    // callback may destroy the bar
    void (*resize)(void* context, HWND splitter, int size, bool done) = nullptr;
    void* context = nullptr;
};

// splitter between two panes, owns capture, cursor and painting
// columns means side by side panes, resizes_after means the pane below or right of the bar
// app assigns keyboard focus, arrow keys along the drag call the resize hook
// name is the accessible name, hooks must outlive the bar, all bars use one UI thread
HWND CreateSplitter(HWND parent, int id, const wchar_t* name, const SplitterHooks* hooks, bool columns, bool resizes_after = false);

} // namespace win32ui
