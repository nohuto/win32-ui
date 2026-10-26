// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <utility>
#include <vector>

namespace win32ui
{

// earlier sort columns, newest first, used to break ties
using SortKeys = std::vector<std::pair<int, bool>>;

template <typename Compare>
int CompareSorted(int column, bool ascending, const SortKeys& earlier, Compare compare)
{
    int result = ascending ? compare(column) : -compare(column);
    for (size_t i = 0; result == 0 && i < earlier.size(); ++i)
    {
        result = earlier[i].second ? compare(earlier[i].first) : -compare(earlier[i].first);
    }
    return result;
}

// compares one column by item lParam, zero for equal values
using ListItemCompare = int(CALLBACK*)(LPARAM left, LPARAM right, int column, void* context);

// toggle reverses the current sort column, another column makes it the newest earlier key
// column -1 changes nothing
void UpdateListSortState(int column, bool toggle, int* sort_column, bool* ascending, SortKeys* earlier);
// sorts non owner data lists with per item lParam by logical subitem column
// toggle keeps shown order for ties, refills use earlier sort keys
// selection, focus and selection mark follow items, false if nothing was sorted
bool SortListViewItems(HWND list, int column, bool toggle, int* sort_column, bool* ascending, SortKeys* earlier, ListItemCompare compare, void* context);
// sort arrow for logical subitem column, -1 clears it
void UpdateListViewSort(HWND list, int column, bool ascending);

} // namespace win32ui
