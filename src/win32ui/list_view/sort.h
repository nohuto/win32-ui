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
    int result = compare(column);
    result = (result > 0) - (result < 0);
    result = ascending ? result : -result;
    for (size_t i = 0; result == 0 && i < earlier.size(); ++i)
    {
        result = compare(earlier[i].first);
        result = (result > 0) - (result < 0);
        result = earlier[i].second ? result : -result;
    }
    return result;
}

// compares one column by item lParam, zero for equal values
using ListItemCompare = int(CALLBACK*)(LPARAM left, LPARAM right, int column, void* context);

void UpdateListSortState(int column, bool toggle, int* sort_column, bool* ascending, SortKeys* earlier);
bool SortListViewItems(HWND list, int column, bool toggle, int* sort_column, bool* ascending, SortKeys* earlier, ListItemCompare compare, void* context);
// sort arrow for logical subitem column
void UpdateListViewSort(HWND list, int column, bool ascending);

} // namespace win32ui
