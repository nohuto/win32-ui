// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/list_view/sort.h"

#include "win32ui/list_view/list_view.h"
#include "win32ui/list_view/list_view_internal.h"

#include <commctrl.h>

namespace win32ui
{

namespace
{

struct SortContext
{
    ListItemCompare compare = nullptr;
    void* context = nullptr;
    int column = 0;
    bool ascending = true;
    const SortKeys* earlier = nullptr;
};

// LVM_SORTITEMS keeps items that compare equal in their order
int CALLBACK CompareItems(LPARAM left, LPARAM right, LPARAM parameter)
{
    const auto& sort = *reinterpret_cast<const SortContext*>(parameter);
    const int result = CompareSorted(sort.column, sort.ascending, *sort.earlier, [&](int column) { return sort.compare(left, right, column, sort.context); });
    return result < 0 ? -1 : result > 0 ? 1
                                        : 0;
}

} // namespace

void UpdateListSortState(int column, bool toggle, int* sort_column, bool* ascending, SortKeys* earlier)
{
    if (!sort_column || !ascending || column < 0)
    {
        return;
    }
    if (toggle && *sort_column == column)
    {
        *ascending = !*ascending;
        return;
    }
    if (earlier && *sort_column != column && *sort_column >= 0)
    {
        std::erase_if(*earlier, [&](const std::pair<int, bool>& key) { return key.first == column || key.first == *sort_column; });
        earlier->insert(earlier->begin(), {*sort_column, *ascending});
    }
    *sort_column = column;
    if (toggle)
    {
        *ascending = true;
    }
}

bool SortListViewItems(HWND list, int column, bool toggle, int* sort_column, bool* ascending, SortKeys* earlier, ListItemCompare compare, void* context)
{
    if (!list || !sort_column || !ascending || !compare || column < 0 || (GetWindowLongPtrW(list, GWL_STYLE) & LVS_OWNERDATA))
    {
        return false;
    }
    SortContext sort;
    sort.compare = compare;
    sort.context = context;
    // selection mark is a row index and must follow the moved item
    LPARAM mark = 0;
    const bool has_mark = ListViewItemData(list, ListView_GetSelectionMark(list), &mark);
    UpdateListSortState(column, toggle, sort_column, ascending, earlier);
    const SortKeys none;
    sort.column = *sort_column;
    sort.ascending = *ascending;
    sort.earlier = earlier && !toggle ? earlier : &none;
    ListView_SortItems(list, CompareItems, reinterpret_cast<LPARAM>(&sort));
    if (has_mark)
    {
        ListView_SetSelectionMark(list, FindListViewItemByData(list, mark));
    }
    UpdateListViewSort(list, *sort_column, *ascending);
    InvalidateRect(list, nullptr, TRUE);
    return true;
}

void UpdateListViewSort(HWND list, int column, bool ascending)
{
    HWND header = list ? ListView_GetHeader(list) : nullptr;
    const int count = header ? Header_GetItemCount(header) : 0;
    RecordListSort(list, column, column >= 0 ? (ascending ? HDF_SORTUP : HDF_SORTDOWN) : 0);
    bool changed = false;
    for (int display = 0; display < count; ++display)
    {
        HDITEMW item = {};
        item.mask = HDI_FORMAT;
        if (!Header_GetItem(header, display, &item))
        {
            continue;
        }
        const int current = item.fmt & (HDF_SORTUP | HDF_SORTDOWN);
        const int wanted = column >= 0 && ColumnSubItem(list, display) == column ? (ascending ? HDF_SORTUP : HDF_SORTDOWN) : 0;
        if (current == wanted)
        {
            continue;
        }
        item.fmt = (item.fmt & ~(HDF_SORTUP | HDF_SORTDOWN)) | wanted;
        Header_SetItem(header, display, &item);
        changed = true;
    }
    if (changed)
    {
        InvalidateRect(header, nullptr, TRUE);
    }
}

} // namespace win32ui
