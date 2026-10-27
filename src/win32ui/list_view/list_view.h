// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/dialog/dialog_layout.h"

#include <windows.h>

#include <commctrl.h>

#include <string>
#include <vector>

namespace win32ui
{

enum class ListLabel
{
    fit_column,
    fit_all,
    reset_columns,
    copy,
    select_all,
    grid_lines,
    more_text,
};

struct ListViewHooks
{
    const wchar_t* (*label)(ListLabel label) = nullptr;
    // themed list border, null for none
    const DialogTheme* frame = nullptr;
    // called after user divider changes or header menu sizing
    void (*columns_changed)(void* context, HWND list) = nullptr;
    // reads owner data cell text from the app model, false falls back to the list
    bool (*cell_text)(void* context, HWND list, int row, int subitem, std::wstring* text) = nullptr;
    // grid icon at dpi, library destroys it after copying
    HICON (*grid_icon)(UINT dpi) = nullptr;
    // called when a grid button changes grid visibility for all lists
    void (*grid_changed)(bool shown) = nullptr;
    bool (*row_menu)(void* context, HWND list, int row, POINT screen) = nullptr;
    void* context = nullptr;
};

struct ListColumn
{
    std::wstring title;
    int width_dip = 100;
    int default_width_dip = 100;
    int format = LVCFMT_LEFT;
    bool visible = true;
    // false keeps the column hidden and out of the header menu
    bool available = true;
};

bool AttachListView(HWND list, const ListViewHooks* hooks, DWORD extra_styles = 0);
bool AttachDialogListView(HWND list, const ListViewHooks* hooks, DWORD extra_styles = 0);
void RefreshListView(HWND list);

void ShowListGridButton(HWND list);
void SetAllListGridLines(bool shown);
bool AllListGridLines();
// draws grid over app painting
LRESULT DrawListGrid(HWND list, NMLVCUSTOMDRAW* draw, LRESULT result);
bool ListTypeSearchActive(HWND list);

void SetListColumns(HWND list, const std::vector<ListColumn>& columns);
// columns with user set widths and visibility
std::vector<ListColumn> ListColumns(HWND list);
int FitListColumn(HWND list, int column);
// header rectangle in list client coordinates, false while hidden
bool ListColumnRect(HWND list, int subitem, RECT* rect);

std::wstring SelectedRowsText(HWND list);
// selects all rows unless LVS_SINGLESEL
void SelectAllListItems(HWND list);
int ContextMenuRow(HWND list, LPARAM screen_point, POINT* screen);

// item data for non owner data lists, false for missing row
bool ListViewItemData(HWND list, int row, LPARAM* data);
bool SelectedListViewData(HWND list, LPARAM* data);
int FindListViewItemByData(HWND list, LPARAM data);

} // namespace win32ui
