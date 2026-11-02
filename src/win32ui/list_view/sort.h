// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace win32ui
{

int CompareListText(std::wstring_view left, std::wstring_view right, bool numbers = false);

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

using ListItemCompare = int(CALLBACK*)(LPARAM left, LPARAM right, int column, void* context);

using TextRows = std::vector<std::vector<std::wstring>>;
int CALLBACK CompareTextRows(LPARAM left, LPARAM right, int column, void* rows);

void UpdateListSortState(int column, bool toggle, int* sort_column, bool* ascending, SortKeys* earlier);
bool SortListViewItems(HWND list, int column, bool toggle, int* sort_column, bool* ascending, SortKeys* earlier, ListItemCompare compare, void* context);
void UpdateListViewSort(HWND list, int column, bool ascending);

}
