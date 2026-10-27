// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <commctrl.h>

namespace win32ui
{

struct SplitterHooks
{
    // current pane size
    int (*size)(void* context, HWND splitter) = nullptr;
    // drag start size plus movement
    void (*resize)(void* context, HWND splitter, int size, bool done) = nullptr;
    void* context = nullptr;
};

HWND CreateSplitter(HWND parent, int id, const wchar_t* name, const SplitterHooks* hooks, bool columns, bool resizes_after = false);

} // namespace win32ui
