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

// app supplies and translates list menu labels
enum class ListLabel
{
    fit_column,
    fit_all,
    reset_columns,
    copy,
    select_all,
    // grid button name and tooltip
    grid_lines,
    // suffix for cell tooltips truncated at the limit
    more_text,
};

struct ListViewHooks
{
    const wchar_t* (*label)(ListLabel label) = nullptr;
    // themed list border, null for none
    const DialogTheme* frame = nullptr;
    // called after user divider changes or header menu sizing, visibility and reset commands
    // app set widths dont call it
    void (*columns_changed)(void* context, HWND list) = nullptr;
    // reads owner data cell text from the app model, false falls back to the list
    bool (*cell_text)(void* context, HWND list, int row, int subitem, std::wstring* text) = nullptr;
    // grid icon at dpi, library destroys it after copying
    HICON (*grid_icon)(UINT dpi) = nullptr;
    // called when a grid button changes grid visibility for all lists
    void (*grid_changed)(bool shown) = nullptr;
    // custom dialog list menu replacing Copy and Select all, false uses the library menu
    // row is the target row or -1, right click selection is already applied
    bool (*row_menu)(void* context, HWND list, int row, POINT screen) = nullptr;
    void* context = nullptr;
};

// logical column, list index equals subitem
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

// attaches report styles plus extra_styles, themed border and header colors/menu, without a focus rectangle
// hooks must outlive the list and must not destroy or reparent it, reattaching updates them
// typed text matches the first cell, selects the first prefix match, else the first later row, else the last
// Backspace removes a character, one second pause resets the search, truncated cells get tooltips
// divider double click fits the column, unavailable columns cannot be dragged or fitted open
bool AttachListView(HWND list, const ListViewHooks* hooks, DWORD extra_styles = 0);
// dialog lists also support Ctrl+C, Ctrl+A and a menu for both
bool AttachDialogListView(HWND list, const ListViewHooks* hooks, DWORD extra_styles = 0);
// call after theme or DPI changes to reopen header styles and update grid button and tooltip
void RefreshListView(HWND list);

// header button toggles grid lines for all attached lists
// app painted lists must pass custom draw results through DrawListGrid
void ShowListGridButton(HWND list);
// shared grid setting also applies to lists attached later, doesnt call grid_changed
// all attached lists must live on the UI thread calling this
void SetAllListGridLines(bool shown);
bool AllListGridLines();
// draws grid over app painting, result is the app return value for this stage
LRESULT DrawListGrid(HWND list, NMLVCUSTOMDRAW* draw, LRESULT result);
// while type to select is active, spaces extend text instead of reaching app key handling
bool ListTypeSearchActive(HWND list);

// sets columns in subitem order, updates existing columns and removes extras
// hidden columns keep their subitem at width 0, defaults are used by Reset columns
void SetListColumns(HWND list, const std::vector<ListColumn>& columns);
// columns with user set widths and visibility
std::vector<ListColumn> ListColumns(HWND list);
// fits cells and full header text, which LVSCW_AUTOSIZE_USEHEADER can truncate
// unavailable columns keep width 0
int FitListColumn(HWND list, int column);

// shown columns in display order, tab separated cells and CRLF separated rows
// no header or final line break, cell tabs and line breaks become spaces
std::wstring SelectedRowsText(HWND list);
// selects all rows unless LVS_SINGLESEL, preserving focus and scroll position
void SelectAllListItems(HWND list);
// context menu row and screen position, hit row or -1 for mouse input
// keyboard uses focused or first selected row
int ContextMenuRow(HWND list, LPARAM screen_point, POINT* screen);

// item data for non owner data lists, false for a missing row
bool ListViewItemData(HWND list, int row, LPARAM* data);
bool SelectedListViewData(HWND list, LPARAM* data);
// returns row with this item data, or -1
int FindListViewItemByData(HWND list, LPARAM data);

} // namespace win32ui
