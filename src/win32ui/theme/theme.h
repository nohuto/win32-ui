// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/dialog/dialog_layout.h"
#include "win32ui/theme/palette.h"

#include <windows.h>

#include <commctrl.h>

namespace win32ui
{

// static label role for WM_CTLCOLORSTATIC
enum class TextRole
{
    normal,
    error,
    disabled,
};

void ApplyThemeToWindow(HWND window);
void SetThemeClass(HWND window, const wchar_t* dark_class);
void ReapplyTheme(HWND top_level);
HBRUSH ThemeControlColor(UINT message, HDC hdc, HWND control);
// NM_CUSTOMDRAW for check boxes, radio buttons and trees
bool ThemeCustomDraw(NMHDR* header, LRESULT* result);
void SetTextRole(HWND label, TextRole role);

DialogTheme ThemeDialogHooks();

} // namespace win32ui
