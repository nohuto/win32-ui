// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/dialog/dialog_resizer.h"

#include <algorithm>

namespace win32ui
{

void DialogResizer::Attach(HWND dialog, const std::vector<AnchorRule>& rules)
{
    items_.clear();
    client_ = {};
    if (!dialog)
    {
        return;
    }

    RECT client = {};
    if (!GetClientRect(dialog, &client))
    {
        return;
    }
    client_ = {client.right - client.left, client.bottom - client.top};

    items_.reserve(rules.size());
    for (const AnchorRule& rule : rules)
    {
        HWND control = GetDlgItem(dialog, rule.id);
        RECT rect = {};
        if (!control || !GetWindowRect(control, &rect))
        {
            continue;
        }
        MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&rect), 2);
        items_.push_back({rule.id, rule.anchors, rect});
    }
}

void DialogResizer::Apply(HWND dialog) const
{
    if (!dialog || items_.empty() || client_.cx <= 0 || client_.cy <= 0)
    {
        return;
    }
    RECT client = {};
    if (!GetClientRect(dialog, &client))
    {
        return;
    }
    const LONG dx = (client.right - client.left) - client_.cx;
    const LONG dy = (client.bottom - client.top) - client_.cy;

    const auto place = [&](HDWP defer) {
        for (const Item& item : items_)
        {
            HWND control = GetDlgItem(dialog, item.id);
            if (!control)
            {
                continue;
            }
            const LONG left = (item.anchors & kAnchorLeft) ? item.rect.left : item.rect.left + dx;
            const LONG right = (item.anchors & kAnchorRight) ? item.rect.right + dx : item.rect.right;
            const LONG top = (item.anchors & kAnchorTop) ? item.rect.top : item.rect.top + dy;
            const LONG bottom = (item.anchors & kAnchorBottom) ? item.rect.bottom + dy : item.rect.bottom;
            const int width = static_cast<int>(std::max<LONG>(0, right - left));
            const int height = static_cast<int>(std::max<LONG>(0, bottom - top));
            if (!defer)
            {
                SetWindowPos(control, nullptr, static_cast<int>(left), static_cast<int>(top), width, height, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            else if (!(defer = DeferWindowPos(defer, control, nullptr, static_cast<int>(left), static_cast<int>(top), width, height, SWP_NOZORDER | SWP_NOACTIVATE)))
            {
                return false;
            }
        }
        return !defer || EndDeferWindowPos(defer);
    };
    if (!place(BeginDeferWindowPos(static_cast<int>(items_.size()))))
    {
        place(nullptr);
    }
    InvalidateRect(dialog, nullptr, TRUE);
}

void DialogResizer::Rescale(UINT from_dpi, UINT to_dpi)
{
    if (from_dpi == 0 || from_dpi == to_dpi)
    {
        return;
    }
    const auto scale = [&](LONG value) { return static_cast<LONG>(MulDiv(value, static_cast<int>(to_dpi), static_cast<int>(from_dpi))); };
    for (Item& item : items_)
    {
        item.rect = {scale(item.rect.left), scale(item.rect.top), scale(item.rect.right), scale(item.rect.bottom)};
    }
    client_ = {scale(client_.cx), scale(client_.cy)};
}

} // namespace win32ui
