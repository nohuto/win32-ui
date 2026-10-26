// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/dialog/combo_box.h"

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

} // namespace

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

} // namespace win32ui
