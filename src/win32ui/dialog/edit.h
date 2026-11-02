// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

void KeepSingleLine(HWND edit);
void AllowLineBreaks(HWND edit);
// suppresses control character beep
void AddSelectAllShortcut(HWND window);
void SelectAllOnFocus(HWND edit);
// call again after a size, font or dpi change
void SetEditPadding(HWND edit, int horizontal_dip, int min_vertical_dip);

struct EditMenuCommand
{
    // id must differ from EM_SETSEL, WM_CUT, WM_COPY, WM_PASTE, WM_CLEAR and WM_UNDO
    UINT id = 0;
    const wchar_t* text = nullptr;
    const wchar_t* shortcut = nullptr;
    bool enabled = true;
};

UINT ShowEditContextMenu(HWND edit, LPARAM screen_point, const EditMenuCommand* extra = nullptr);

}
