// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/list_view/column_model_internal.h"

#include <algorithm>
#include <cwchar>

namespace win32ui
{

namespace
{

constexpr size_t kMaxTitle = 256;

bool OwnerData(HWND list)
{
    return (GetWindowLongPtrW(list, GWL_STYLE) & LVS_OWNERDATA) != 0;
}

} // namespace

// a subclass above the list can send its own messages first, only this exact call passes untranslated
LRESULT ColumnModel::Native(HWND list, UINT message, WPARAM wparam, LPARAM lparam)
{
    const Pending outer = pending_;
    pending_ = {message, wparam, lparam};
    const LRESULT result = SendMessageW(list, message, wparam, lparam);
    pending_ = outer;
    return result;
}

bool ColumnModel::TakeNative(UINT message, WPARAM wparam, LPARAM lparam)
{
    if (pending_.message == 0 || pending_.message != message || pending_.wparam != wparam || pending_.lparam != lparam)
    {
        return false;
    }
    pending_ = {};
    return true;
}

bool ColumnModel::IsRawNotify(const NMHDR* header) const
{
    return raw_notify_ && raw_notify_ == header;
}

void ColumnModel::Rebuild()
{
    positions_.assign(columns_.size(), -1);
    subitems_.clear();
    for (size_t index = 0; index < columns_.size(); ++index)
    {
        if (columns_[index].shown)
        {
            positions_[index] = static_cast<int>(subitems_.size());
            subitems_.push_back(static_cast<int>(index));
        }
    }
}

void ColumnModel::Load(HWND list)
{
    HWND header = ListView_GetHeader(list);
    const int count = header ? Header_GetItemCount(header) : 0;
    if (count <= 0 || !columns_.empty())
    {
        return;
    }
    columns_.resize(static_cast<size_t>(count));
    for (int position = 0; position < count; ++position)
    {
        wchar_t title[kMaxTitle] = {};
        LVCOLUMNW item = {};
        item.mask = LVCF_TEXT | LVCF_FMT | LVCF_WIDTH;
        item.pszText = title;
        item.cchTextMax = static_cast<int>(kMaxTitle);
        Native(list, LVM_GETCOLUMNW, static_cast<WPARAM>(position), reinterpret_cast<LPARAM>(&item));
        Column& column = columns_[static_cast<size_t>(position)];
        column.title = title;
        column.format = item.fmt;
        column.width = item.cx;
    }
    order_.resize(static_cast<size_t>(count));
    if (!Native(list, LVM_GETCOLUMNORDERARRAY, static_cast<WPARAM>(count), reinterpret_cast<LPARAM>(order_.data())))
    {
        for (int index = 0; index < count; ++index)
        {
            order_[static_cast<size_t>(index)] = index;
        }
    }
    Rebuild();
    // columns hidden at width 0 before the list was attached
    for (int subitem = count - 1; subitem > 0; --subitem)
    {
        if (columns_[static_cast<size_t>(subitem)].width == 0)
        {
            Hide(list, subitem);
        }
    }
}

int ColumnModel::Count() const
{
    return static_cast<int>(columns_.size());
}

bool ColumnModel::AnyHidden() const
{
    return subitems_.size() != columns_.size();
}

int ColumnModel::Position(int subitem) const
{
    if (subitem < 0 || static_cast<size_t>(subitem) >= positions_.size())
    {
        return subitem;
    }
    return positions_[static_cast<size_t>(subitem)];
}

// zero width native columns left by a divider drag become hidden ones, the first column gets a width back
void ColumnModel::Reconcile(HWND list, int first_width)
{
    for (int subitem = Count() - 1; subitem >= 0; --subitem)
    {
        const int position = Position(subitem);
        if (position < 0 || Native(list, LVM_GETCOLUMNWIDTH, static_cast<WPARAM>(position), 0) > 0)
        {
            continue;
        }
        if (subitem == 0)
        {
            Native(list, LVM_SETCOLUMNWIDTH, 0, MAKELPARAM(std::max(first_width, 1), 0));
        }
        else
        {
            Hide(list, subitem);
        }
    }
}

int ColumnModel::InsertPosition(int subitem) const
{
    const size_t end = std::min(static_cast<size_t>(std::max(subitem, 0)), columns_.size());
    return static_cast<int>(std::count_if(columns_.begin(), columns_.begin() + static_cast<std::ptrdiff_t>(end), [](const Column& column) { return column.shown; }));
}

int ColumnModel::Subitem(int position) const
{
    if (position < 0)
    {
        return position;
    }
    if (static_cast<size_t>(position) < subitems_.size())
    {
        return subitems_[static_cast<size_t>(position)];
    }
    // past the known columns positions and subitems advance together
    return static_cast<int>(columns_.size()) + position - static_cast<int>(subitems_.size());
}

std::wstring ColumnModel::Title(int subitem) const
{
    return subitem >= 0 && static_cast<size_t>(subitem) < columns_.size() ? columns_[static_cast<size_t>(subitem)].title : std::wstring();
}

void ColumnModel::RecordSort(int subitem, int bits)
{
    for (size_t index = 0; index < columns_.size(); ++index)
    {
        columns_[index].sort = static_cast<int>(index) == subitem ? bits : 0;
    }
}

// user drags reorder native columns, hidden ones keep their place between them
void ColumnModel::SyncOrder(HWND list)
{
    std::vector<int> shown;
    for (const int subitem : order_)
    {
        if (columns_[static_cast<size_t>(subitem)].shown)
        {
            shown.push_back(subitem);
        }
    }
    std::vector<int> native(shown.size());
    if (native.empty() || !Native(list, LVM_GETCOLUMNORDERARRAY, native.size(), reinterpret_cast<LPARAM>(native.data())))
    {
        return;
    }
    size_t next = 0;
    for (int& subitem : order_)
    {
        if (columns_[static_cast<size_t>(subitem)].shown && next < native.size())
        {
            subitem = Subitem(native[next++]);
        }
    }
}

void ColumnModel::ApplyOrder(HWND list)
{
    std::vector<int> native;
    for (const int subitem : order_)
    {
        if (columns_[static_cast<size_t>(subitem)].shown)
        {
            native.push_back(Position(subitem));
        }
    }
    if (!native.empty())
    {
        Native(list, LVM_SETCOLUMNORDERARRAY, native.size(), reinterpret_cast<LPARAM>(native.data()));
    }
}

void ColumnModel::Hide(HWND list, int subitem)
{
    Column& column = columns_[static_cast<size_t>(subitem)];
    if (!column.shown)
    {
        return;
    }
    SyncOrder(list);
    const int position = Position(subitem);
    const int width = static_cast<int>(Native(list, LVM_GETCOLUMNWIDTH, static_cast<WPARAM>(position), 0));
    column.width = width > 0 ? width : column.width;
    HDITEMW header_item = {};
    header_item.mask = HDI_FORMAT;
    HWND header = ListView_GetHeader(list);
    // a format change can clear the arrow first, the recorded sort stays then
    if (const int sort = header && Header_GetItem(header, position, &header_item) ? header_item.fmt & (HDF_SORTUP | HDF_SORTDOWN) : 0)
    {
        column.sort = sort;
    }
    if (!OwnerData(list))
    {
        // the native list drops a deleted columns text, keep it by item id so sorting while hidden is safe
        const int rows = ListView_GetItemCount(list);
        std::vector<wchar_t> buffer(1024);
        for (int row = 0; row < rows; ++row)
        {
            const UINT id = static_cast<UINT>(Native(list, LVM_MAPINDEXTOID, static_cast<WPARAM>(row), 0));
            for (;;)
            {
                LVITEMW item = {};
                item.iSubItem = position;
                item.pszText = buffer.data();
                item.cchTextMax = static_cast<int>(buffer.size());
                const size_t length = static_cast<size_t>(std::max<LRESULT>(0, Native(list, LVM_GETITEMTEXTW, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(&item))));
                if (length + 1 < buffer.size())
                {
                    if (length)
                    {
                        column.texts[id].assign(buffer.data(), length);
                    }
                    break;
                }
                buffer.resize(buffer.size() * 2);
            }
        }
    }
    Native(list, LVM_DELETECOLUMN, static_cast<WPARAM>(position), 0);
    column.shown = false;
    Rebuild();
    ApplyOrder(list);
}

void ColumnModel::Show(HWND list, int subitem, int width)
{
    Column& column = columns_[static_cast<size_t>(subitem)];
    if (column.shown)
    {
        return;
    }
    SyncOrder(list);
    const int position = InsertPosition(subitem);
    LVCOLUMNW item = {};
    item.mask = LVCF_TEXT | LVCF_FMT | LVCF_WIDTH | LVCF_SUBITEM;
    item.pszText = column.title.data();
    item.fmt = column.format;
    item.cx = width > 0 ? width : std::max(column.width, 1);
    item.iSubItem = subitem;
    if (Native(list, LVM_INSERTCOLUMNW, static_cast<WPARAM>(position), reinterpret_cast<LPARAM>(&item)) < 0)
    {
        return;
    }
    column.shown = true;
    Rebuild();
    for (auto entry = column.texts.begin(); entry != column.texts.end(); ++entry)
    {
        const int row = static_cast<int>(Native(list, LVM_MAPIDTOINDEX, entry->first, 0));
        if (row >= 0)
        {
            LVITEMW text = {};
            text.iSubItem = position;
            text.pszText = entry->second.data();
            Native(list, LVM_SETITEMTEXTW, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(&text));
        }
    }
    column.texts.clear();
    if (column.sort)
    {
        HWND header = ListView_GetHeader(list);
        HDITEMW header_item = {};
        header_item.mask = HDI_FORMAT;
        if (header && Header_GetItem(header, position, &header_item))
        {
            header_item.fmt = (header_item.fmt & ~(HDF_SORTUP | HDF_SORTDOWN)) | column.sort;
            Header_SetItem(header, position, &header_item);
        }
    }
    if (width < 0)
    {
        // LVSCW_AUTOSIZE values need the column in place
        Native(list, LVM_SETCOLUMNWIDTH, static_cast<WPARAM>(position), width);
    }
    ApplyOrder(list);
}

bool ColumnModel::HiddenText(HWND list, UINT message, LVITEMW* item, LRESULT* result)
{
    Column& column = columns_[static_cast<size_t>(item->iSubItem)];
    const bool set = message == LVM_SETITEMTEXTW || message == LVM_SETITEMW;
    const int row = item->iItem;
    if (set)
    {
        if ((message == LVM_SETITEMW && !(item->mask & LVIF_TEXT)) || OwnerData(list))
        {
            *result = TRUE;
            return true;
        }
        const UINT id = static_cast<UINT>(Native(list, LVM_MAPINDEXTOID, static_cast<WPARAM>(row), 0));
        if (item->pszText && item->pszText != LPSTR_TEXTCALLBACKW && item->pszText[0])
        {
            column.texts[id] = item->pszText;
        }
        else
        {
            column.texts.erase(id);
        }
        *result = TRUE;
        return true;
    }
    if (message == LVM_GETITEMW && !(item->mask & LVIF_TEXT))
    {
        *result = TRUE;
        return true;
    }
    if (!item->pszText || item->cchTextMax <= 0)
    {
        *result = 0;
        return true;
    }
    std::wstring text;
    if (OwnerData(list))
    {
        NMLVDISPINFOW info = {};
        info.hdr.hwndFrom = list;
        info.hdr.idFrom = static_cast<UINT_PTR>(GetDlgCtrlID(list));
        info.hdr.code = LVN_GETDISPINFOW;
        info.item.mask = LVIF_TEXT;
        info.item.iItem = row;
        info.item.iSubItem = item->iSubItem;
        info.item.pszText = item->pszText;
        info.item.cchTextMax = item->cchTextMax;
        item->pszText[0] = L'\0';
        const NMHDR* outer = raw_notify_;
        raw_notify_ = &info.hdr;
        SendMessageW(GetParent(list), WM_NOTIFY, info.hdr.idFrom, reinterpret_cast<LPARAM>(&info));
        raw_notify_ = outer;
        if (info.item.pszText && info.item.pszText != LPSTR_TEXTCALLBACKW)
        {
            text = info.item.pszText;
        }
    }
    else
    {
        const auto found = column.texts.find(static_cast<UINT>(Native(list, LVM_MAPINDEXTOID, static_cast<WPARAM>(row), 0)));
        text = found == column.texts.end() ? std::wstring() : found->second;
    }
    const size_t length = std::min(text.size(), static_cast<size_t>(item->cchTextMax - 1));
    std::wmemcpy(item->pszText, text.data(), length);
    item->pszText[length] = L'\0';
    *result = message == LVM_GETITEMW ? TRUE : static_cast<LRESULT>(length);
    return true;
}

bool ColumnModel::Translate(HWND list, UINT message, WPARAM wparam, LPARAM lparam, LRESULT* result)
{
    const int index = static_cast<int>(wparam);
    const bool known = index >= 0 && static_cast<size_t>(index) < columns_.size();
    switch (message)
    {
    case LVM_INSERTCOLUMNW:
        {
            auto* item = reinterpret_cast<LVCOLUMNW*>(lparam);
            if (!item)
            {
                return false;
            }
            const int subitem = std::clamp(index, 0, Count());
            SyncOrder(list);
            Column column;
            if ((item->mask & LVCF_TEXT) && item->pszText)
            {
                column.title = item->pszText;
            }
            column.format = item->mask & LVCF_FMT ? item->fmt : LVCFMT_LEFT;
            column.width = item->mask & LVCF_WIDTH ? item->cx : 0;
            column.shown = subitem == 0 || !(item->mask & LVCF_WIDTH) || item->cx != 0;
            LVCOLUMNW native = *item;
            native.mask &= ~static_cast<UINT>(LVCF_ORDER);
            if (column.shown && Native(list, LVM_INSERTCOLUMNW, static_cast<WPARAM>(InsertPosition(subitem)), reinterpret_cast<LPARAM>(&native)) < 0)
            {
                *result = -1;
                return true;
            }
            columns_.insert(columns_.begin() + subitem, std::move(column));
            for (int& entry : order_)
            {
                entry += entry >= subitem ? 1 : 0;
            }
            const int slot = item->mask & LVCF_ORDER ? item->iOrder : subitem;
            order_.insert(order_.begin() + std::clamp<std::ptrdiff_t>(slot, 0, static_cast<std::ptrdiff_t>(order_.size())), subitem);
            Rebuild();
            ApplyOrder(list);
            *result = subitem;
            return true;
        }
    case LVM_DELETECOLUMN:
        {
            if (!known)
            {
                return false;
            }
            SyncOrder(list);
            if (columns_[static_cast<size_t>(index)].shown && !Native(list, LVM_DELETECOLUMN, static_cast<WPARAM>(Position(index)), 0))
            {
                *result = FALSE;
                return true;
            }
            columns_.erase(columns_.begin() + index);
            std::erase(order_, index);
            for (int& entry : order_)
            {
                entry -= entry > index ? 1 : 0;
            }
            Rebuild();
            // the new first column holds the item text and must be in place
            if (!columns_.empty() && !columns_[0].shown)
            {
                Show(list, 0, 0);
            }
            *result = TRUE;
            return true;
        }
    case LVM_GETCOLUMNWIDTH:
        if (!known)
        {
            return false;
        }
        *result = columns_[static_cast<size_t>(index)].shown ? Native(list, message, static_cast<WPARAM>(Position(index)), lparam) : 0;
        return true;
    case LVM_SETCOLUMNWIDTH:
        {
            if (!known)
            {
                return false;
            }
            const int width = static_cast<int>(static_cast<short>(LOWORD(lparam)));
            Column& column = columns_[static_cast<size_t>(index)];
            if (width == 0)
            {
                if (index != 0)
                {
                    Hide(list, index);
                }
                *result = TRUE;
                return true;
            }
            if (!column.shown)
            {
                Show(list, index, width);
                *result = TRUE;
                return true;
            }
            *result = Native(list, message, static_cast<WPARAM>(Position(index)), lparam);
            return true;
        }
    case LVM_GETCOLUMNW:
    case LVM_SETCOLUMNW:
        {
            auto* item = reinterpret_cast<LVCOLUMNW*>(lparam);
            if (!known || !item)
            {
                return false;
            }
            Column& column = columns_[static_cast<size_t>(index)];
            if (message == LVM_SETCOLUMNW)
            {
                if ((item->mask & LVCF_TEXT) && item->pszText)
                {
                    column.title = item->pszText;
                }
                if (item->mask & LVCF_FMT)
                {
                    column.format = item->fmt;
                }
                const bool hide = (item->mask & LVCF_WIDTH) && item->cx == 0 && index != 0;
                const bool show = (item->mask & LVCF_WIDTH) && item->cx != 0;
                if (item->mask & LVCF_ORDER)
                {
                    SyncOrder(list);
                    std::erase(order_, index);
                    order_.insert(order_.begin() + std::clamp<std::ptrdiff_t>(item->iOrder, 0, static_cast<std::ptrdiff_t>(order_.size())), index);
                }
                if (column.shown)
                {
                    // width 0 hides the column, first column ignores it
                    LVCOLUMNW native = *item;
                    native.mask &= ~static_cast<UINT>(LVCF_ORDER);
                    if ((native.mask & LVCF_WIDTH) && native.cx == 0)
                    {
                        native.mask &= ~static_cast<UINT>(LVCF_WIDTH);
                    }
                    *result = Native(list, message, static_cast<WPARAM>(Position(index)), reinterpret_cast<LPARAM>(&native));
                    if (hide)
                    {
                        Hide(list, index);
                    }
                }
                else
                {
                    if (show)
                    {
                        Show(list, index, item->cx);
                    }
                    *result = TRUE;
                }
                if (item->mask & LVCF_ORDER)
                {
                    ApplyOrder(list);
                }
                return true;
            }
            if (column.shown)
            {
                *result = Native(list, message, static_cast<WPARAM>(Position(index)), lparam);
                if (item->mask & LVCF_SUBITEM)
                {
                    item->iSubItem = index;
                }
                if (item->mask & LVCF_ORDER)
                {
                    SyncOrder(list);
                    item->iOrder = static_cast<int>(std::find(order_.begin(), order_.end(), index) - order_.begin());
                }
                return true;
            }
            if ((item->mask & LVCF_TEXT) && item->pszText && item->cchTextMax > 0)
            {
                wcsncpy_s(item->pszText, static_cast<size_t>(item->cchTextMax), column.title.c_str(), _TRUNCATE);
            }
            if (item->mask & LVCF_FMT)
            {
                item->fmt = column.format;
            }
            if (item->mask & LVCF_WIDTH)
            {
                item->cx = 0;
            }
            if (item->mask & LVCF_SUBITEM)
            {
                item->iSubItem = index;
            }
            if (item->mask & LVCF_ORDER)
            {
                SyncOrder(list);
                item->iOrder = static_cast<int>(std::find(order_.begin(), order_.end(), index) - order_.begin());
            }
            *result = TRUE;
            return true;
        }
    case LVM_GETCOLUMNORDERARRAY:
    case LVM_SETCOLUMNORDERARRAY:
        {
            auto* order = reinterpret_cast<int*>(lparam);
            if (!order || columns_.empty() || static_cast<size_t>(index) != columns_.size())
            {
                return false;
            }
            if (message == LVM_GETCOLUMNORDERARRAY)
            {
                SyncOrder(list);
                std::copy(order_.begin(), order_.end(), order);
                *result = TRUE;
                return true;
            }
            std::vector<int> wanted(order, order + index);
            std::vector<int> sorted = wanted;
            std::sort(sorted.begin(), sorted.end());
            for (int expected = 0; expected < index; ++expected)
            {
                if (sorted[static_cast<size_t>(expected)] != expected)
                {
                    *result = FALSE;
                    return true;
                }
            }
            order_ = std::move(wanted);
            ApplyOrder(list);
            *result = TRUE;
            return true;
        }
    case LVM_GETITEMTEXTW:
    case LVM_SETITEMTEXTW:
    case LVM_GETITEMW:
    case LVM_SETITEMW:
        {
            auto* item = reinterpret_cast<LVITEMW*>(lparam);
            if (!item || item->iSubItem <= 0 || static_cast<size_t>(item->iSubItem) >= columns_.size() || !AnyHidden())
            {
                return false;
            }
            if (message == LVM_GETITEMTEXTW || message == LVM_SETITEMTEXTW)
            {
                item->iItem = index;
            }
            if (!columns_[static_cast<size_t>(item->iSubItem)].shown)
            {
                return HiddenText(list, message, item, result);
            }
            const int subitem = item->iSubItem;
            item->iSubItem = Position(subitem);
            *result = Native(list, message, wparam, lparam);
            item->iSubItem = subitem;
            return true;
        }
    case LVM_GETSUBITEMRECT:
        {
            auto* rect = reinterpret_cast<RECT*>(lparam);
            if (!rect || rect->top <= 0 || static_cast<size_t>(rect->top) >= columns_.size() || !AnyHidden())
            {
                return false;
            }
            const int position = Position(rect->top);
            if (position < 0)
            {
                *result = FALSE;
                return true;
            }
            rect->top = position;
            *result = Native(list, message, wparam, lparam);
            return true;
        }
    case LVM_SUBITEMHITTEST:
        {
            auto* hit = reinterpret_cast<LVHITTESTINFO*>(lparam);
            if (!hit || !AnyHidden())
            {
                return false;
            }
            *result = Native(list, message, wparam, lparam);
            hit->iSubItem = Subitem(hit->iSubItem);
            return true;
        }
    case LVM_DELETEALLITEMS:
        for (Column& column : columns_)
        {
            column.texts.clear();
        }
        return false;
    case LVM_DELETEITEM:
        if (AnyHidden() && !OwnerData(list))
        {
            const UINT id = static_cast<UINT>(Native(list, LVM_MAPINDEXTOID, wparam, 0));
            for (Column& column : columns_)
            {
                column.texts.erase(id);
            }
        }
        return false;
    default:
        return false;
    }
}

int* ColumnModel::NotifySubitem(NMHDR* header) const
{
    switch (header->code)
    {
    case LVN_GETDISPINFOW:
    case LVN_SETDISPINFOW:
    case LVN_BEGINLABELEDITW:
    case LVN_ENDLABELEDITW:
        return &reinterpret_cast<NMLVDISPINFOW*>(header)->item.iSubItem;
    case NM_CUSTOMDRAW:
        return &reinterpret_cast<NMLVCUSTOMDRAW*>(header)->iSubItem;
    case LVN_COLUMNCLICK:
    case LVN_ITEMCHANGING:
    case LVN_ITEMCHANGED:
    case LVN_INSERTITEM:
    case LVN_DELETEITEM:
    case LVN_BEGINDRAG:
    case LVN_BEGINRDRAG:
    case LVN_HOTTRACK:
        return &reinterpret_cast<NMLISTVIEW*>(header)->iSubItem;
    case NM_CLICK:
    case NM_DBLCLK:
    case NM_RCLICK:
    case NM_RDBLCLK:
    case LVN_ITEMACTIVATE:
        return &reinterpret_cast<NMITEMACTIVATE*>(header)->iSubItem;
    case LVN_GETINFOTIPW:
        return &reinterpret_cast<NMLVGETINFOTIPW*>(header)->iSubItem;
    default:
        return nullptr;
    }
}

} // namespace win32ui
