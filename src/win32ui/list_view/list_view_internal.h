// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <commctrl.h>

namespace win32ui
{

bool DrawDialogListView(NMHDR* header, LRESULT* result);
void ResetListColumns(HWND list);
int ColumnSubItem(HWND list, int position);
void RecordListSort(HWND list, int subitem, int bits);

}
