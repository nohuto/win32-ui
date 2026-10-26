// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <commctrl.h>

namespace win32ui
{

// dialog list custom draw, app colors without hot or focus effects, native selection, then grid
// returns false for notifications from other lists
bool DrawDialogListView(NMHDR* header, LRESULT* result);
// defaults passed to SetListColumns, restored by Reset columns
void ResetListColumns(HWND list);
// subitem at column, or -1 if missing
int ColumnSubItem(HWND list, int column);

} // namespace win32ui
