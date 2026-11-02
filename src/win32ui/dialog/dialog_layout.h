// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

namespace win32ui
{

// app owns the theme
struct DialogTheme
{
    void (*apply)(HWND dialog) = nullptr;
    HBRUSH (*control_color)(UINT message, HDC hdc, HWND control) = nullptr;
    bool (*custom_draw)(NMHDR* header, LRESULT* result) = nullptr;
    void (*prepare)(HWND dialog) = nullptr;
};

HWND CreateControl(HWND parent, const wchar_t* class_name, const wchar_t* text, DWORD style, int id);
void Place(HWND control, int x, int y, int width, int height);

int TextFitWidth(HWND control);
int TextHeight(HWND window, HFONT font, std::wstring_view text, int width, UINT format);
int PlaceButtonRow(std::initializer_list<HWND> buttons, int right, int y, int min_width, int height, int gap);
bool GrowDialogWidth(HWND dialog, int client_width);
void FitDialogHeight(HWND dialog, int client_height);
void FitDialogText(HWND dialog);

std::vector<int> ShareSpace(std::span<const int> wanted, int total, int minimum = 0);

void MatchComboHeights(HWND edit, std::initializer_list<HWND> combos);
void CollapseSection(HWND dialog, int control_id);

void AttachThemedBorder(HWND control);

}
