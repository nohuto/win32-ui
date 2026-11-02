// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/dialog/dialog_layout.h"
#include "win32ui/list_view/sort.h"

#include <windows.h>

#include <commctrl.h>

#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

struct ListViewHooks
{
    const DialogTheme* frame = nullptr;
    void (*columns_changed)(void* context, HWND list) = nullptr;
    bool (*cell_text)(void* context, HWND list, int row, int subitem, std::wstring* text) = nullptr;
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
    bool available = true;
};

bool AttachListView(HWND list, const ListViewHooks* hooks, DWORD extra_styles = 0);
bool AttachDialogListView(HWND list, const ListViewHooks* hooks, DWORD extra_styles = 0);
void RefreshListView(HWND list);

void ShowListGridButton(HWND list);
// call before grid buttons are shown
void SetListGridIcons(int dark_icon, int light_icon);
void SetListGridChanged(void (*changed)(void* context, bool shown), void* context);
void SetAllListGridLines(bool shown);
bool AllListGridLines();
LRESULT DrawListGrid(HWND list, NMLVCUSTOMDRAW* draw, LRESULT result);
bool ListTypeSearchActive(HWND list);

void SetListColumns(HWND list, const std::vector<ListColumn>& columns);
std::vector<ListColumn> ListColumns(HWND list);
int FitListColumn(HWND list, int column);
bool ListColumnRect(HWND list, int subitem, RECT* rect);

std::wstring ListCellText(HWND list, int row, int subitem);
std::wstring SelectedRowsText(HWND list);
std::vector<int> SelectedListItems(HWND list);
void SetListRows(HWND list, const TextRows& rows);
void DrawCellTextMatch(HDC dc, const RECT& rect, std::wstring_view text, int start, int length, COLORREF color);

struct ListSelection
{
    std::vector<int> rows;
    int focused = -1;
    int mark = -1;
    bool all = false;
};
ListSelection CaptureListSelection(HWND list);
void ApplyListSelection(HWND list, const ListSelection& selection);
void SelectAllListItems(HWND list);
void InvertListSelection(HWND list);
bool SelectOnlyListRow(HWND list, int row, bool ensure_visible = true);
int ContextMenuRow(HWND list, LPARAM screen_point, POINT* screen, bool select_clicked = false);
void AppendListRowCommands(HMENU menu, HWND list);
bool RunListRowCommand(HWND list, UINT command);

bool ListViewItemData(HWND list, int row, LPARAM* data);
bool SelectedListViewData(HWND list, LPARAM* data);
int FindListViewItemByData(HWND list, LPARAM data);

}
