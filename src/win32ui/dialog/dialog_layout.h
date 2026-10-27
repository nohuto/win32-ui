// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <initializer_list>

namespace win32ui
{

// app owns the theme
struct DialogTheme
{
    // themes the dialog and all controls
    void (*apply)(HWND dialog) = nullptr;
    // WM_CTLCOLOR result
    HBRUSH (*control_color)(UINT message, HDC hdc, HWND control) = nullptr;
    // NM_CUSTOMDRAW for app painted controls, ret true when handled
    bool (*custom_draw)(NMHDR* header, LRESULT* result) = nullptr;
    // sets app text and controls before fitting template controls
    void (*prepare)(HWND dialog) = nullptr;
};

HWND CreateControl(HWND parent, const wchar_t* class_name, const wchar_t* text, DWORD style, int id);
void Place(HWND control, int x, int y, int width, int height);

int TextFitWidth(HWND control);
int PlaceButtonRow(std::initializer_list<HWND> buttons, int right, int y, int min_width, int height, int gap);
bool GrowDialogWidth(HWND dialog, int client_width);
void FitDialogHeight(HWND dialog, int client_height);
void FitDialogText(HWND dialog);

void MatchComboHeights(HWND edit, std::initializer_list<HWND> combos);
void CollapseSection(HWND dialog, int control_id);

// one pixel border
void AttachThemedBorder(HWND control);

} // namespace win32ui
