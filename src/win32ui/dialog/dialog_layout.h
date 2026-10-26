// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <initializer_list>

namespace win32ui
{

// app owns the theme, must outlive every attached window
struct DialogTheme
{
    // themes the dialog and all controls, including after theme changes
    void (*apply)(HWND dialog) = nullptr;
    // WM_CTLCOLOR result, app retains brush ownership
    HBRUSH (*control_color)(UINT message, HDC hdc, HWND control) = nullptr;
    // NM_CUSTOMDRAW for app painted controls, returns true when handled
    bool (*custom_draw)(NMHDR* header, LRESULT* result) = nullptr;
    // sets app text and controls before fitting template controls
    void (*prepare)(HWND dialog) = nullptr;
};

HWND CreateControl(HWND parent, const wchar_t* class_name, const wchar_t* text, DWORD style, int id);
void Place(HWND control, int x, int y, int width, int height);

// single line text width for buttons, labels and links, 0 for other controls or wrapped labels
int TextFitWidth(HWND control);
// right aligns buttons ending at right, skipping nulls, returns total width, or 0 for none
int PlaceButtonRow(std::initializer_list<HWND> buttons, int right, int y, int min_width, int height, int gap);
bool GrowDialogWidth(HWND dialog, int client_width);
void FitDialogHeight(HWND dialog, int client_height);
// fits visible controls to their text, including translations
// grows the dialog within the monitor work area when needed
void FitDialogText(HWND dialog);

// matches closed combo heights to edit, drop down window height includes the open list
void MatchComboHeights(HWND edit, std::initializer_list<HWND> combos);
// hides the group or row around a control, moves lower controls up and shrinks the dialog
void CollapseSection(HWND dialog, int control_id);

// one pixel border in the theme border color, painted after the system frame
void AttachThemedBorder(HWND control);

} // namespace win32ui
