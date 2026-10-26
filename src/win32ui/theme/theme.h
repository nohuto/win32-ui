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
    // validation messages only
    error,
    // label for a disabled control, disabling the static itself would emboss the text
    disabled,
};

// themes window and descendants, including list/tree colors, combo/group painting and dark captions
void ApplyThemeToWindow(HWND window);
// overrides a control class, uses Explorer in light mode
// next theme walk restores the default class
void SetThemeClass(HWND window, const wchar_t* dark_class);
// call after SetThemeMode or ThemeMessage reports a change
// updates window, library controls, owned tooltips and open dialogs, then redraws once
// app updates its menu bar and icons afterward
void ReapplyTheme(HWND top_level);
// WM_CTLCOLOR result with text roles, library retains brush ownership
HBRUSH ThemeControlColor(UINT message, HDC hdc, HWND control);
// NM_CUSTOMDRAW for check boxes, radio buttons and trees, true when handled
bool ThemeCustomDraw(NMHDR* header, LRESULT* result);
void SetTextRole(HWND label, TextRole role);

// apply, control_color and custom_draw for RegisterDialogClass
// app adds prepare and keeps its copy alive for the run
DialogTheme ThemeDialogHooks();

} // namespace win32ui
