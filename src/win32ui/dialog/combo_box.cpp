// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/dialog/combo_box.h"

#include "win32ui/system/strings.h"
#include "win32ui/window.h"
#include "win32ui/window_metrics.h"

#include <algorithm>

namespace win32ui
{

void SetComboItems(HWND combo, const std::vector<std::wstring>& items)
{
    if (!combo)
    {
        return;
    }
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto& item : items)
    {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
}

void FilterComboItems(HWND combo, std::span<const std::wstring> labels, int kept, bool filtering)
{
    if (!combo)
    {
        return;
    }
    const std::wstring filter = filtering ? WindowText(combo) : std::wstring();
    const DWORD selection = filtering ? static_cast<DWORD>(SendMessageW(combo, CB_GETEDITSEL, 0, 0)) : 0;
    const RedrawScope redraw(combo, RDW_INVALIDATE | RDW_ERASE);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < labels.size(); ++i)
    {
        if (static_cast<int>(i) != kept && !filter.empty() && !ContainsInsensitive(labels[i], filter))
        {
            continue;
        }
        const int index = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(labels[i].c_str())));
        SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index), static_cast<LPARAM>(i));
    }
    // a sorted combo moves earlier items while adding
    int kept_index = -1;
    for (int index = 0, count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0)); kept >= 0 && index < count && kept_index < 0; ++index)
    {
        kept_index = SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0) == kept ? index : -1;
    }
    if (filtering)
    {
        SetWindowTextW(combo, filter.c_str());
        SendMessageW(combo, CB_SETEDITSEL, 0, static_cast<LPARAM>(selection));
    }
    else
    {
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(kept_index), 0);
        SendMessageW(combo, CB_SETEDITSEL, 0, MAKELPARAM(-1, 0));
    }
    FitDroppedWidth(combo);
}

void SetComboIndex(HWND combo, int index)
{
    if (combo && static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0)) != index)
    {
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
        InvalidateRect(combo, nullptr, FALSE);
    }
}

bool SelectComboText(HWND combo, const wchar_t* text)
{
    const LRESULT index = combo && text && *text ? SendMessageW(combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(text)) : CB_ERR;
    return index != CB_ERR && SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0) != CB_ERR;
}

namespace
{

int WidestItem(HWND combo)
{
    const HDC dc = combo ? GetDC(combo) : nullptr;
    if (!dc)
    {
        return 0;
    }
    auto* font = reinterpret_cast<HFONT>(SendMessageW(combo, WM_GETFONT, 0, 0));
    HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    int widest = 0;
    std::wstring text;
    for (int index = 0; index < count; ++index)
    {
        const int length = static_cast<int>(SendMessageW(combo, CB_GETLBTEXTLEN, static_cast<WPARAM>(index), 0));
        if (length <= 0)
        {
            continue;
        }
        text.resize(static_cast<size_t>(length));
        SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(text.data()));
        SIZE size = {};
        if (GetTextExtentPoint32W(dc, text.c_str(), length, &size) && size.cx > widest)
        {
            widest = size.cx;
        }
    }
    if (previous)
    {
        SelectObject(dc, previous);
    }
    ReleaseDC(combo, dc);
    return widest;
}

}

int ComboFitWidth(HWND combo)
{
    const int widest = WidestItem(combo);
    if (!widest)
    {
        return 0;
    }
    // frame and arrow use space outside the item rect, item text keeps an inner margin
    const UINT dpi = DpiForWindow(combo);
    COMBOBOXINFO info = {sizeof(info)};
    RECT window = {};
    const int beside = GetComboBoxInfo(combo, &info) && GetWindowRect(combo, &window) ? (window.right - window.left) - (info.rcItem.right - info.rcItem.left)
                                                                                      : SystemMetricForDpi(SM_CXVSCROLL, dpi) + 2 * SystemMetricForDpi(SM_CXEDGE, dpi);
    return widest + beside + Scale(8, dpi);
}

void FitDroppedWidth(HWND combo)
{
    if (!combo)
    {
        return;
    }
    RECT rect = {};
    GetWindowRect(combo, &rect);
    const UINT dpi = DpiForWindow(combo);
    const int padding = SystemMetricForDpi(SM_CXVSCROLL, dpi) + Scale(8, dpi);
    SendMessageW(combo, CB_SETDROPPEDWIDTH, static_cast<WPARAM>(std::max<int>(rect.right - rect.left, WidestItem(combo) + padding)), 0);
}

}
