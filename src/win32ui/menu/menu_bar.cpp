// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/menu/menu_bar.h"

#include "win32ui/gdi/fonts.h"
#include "win32ui/gdi/gdi_cache.h"
#include "win32ui/theme/palette.h"
#include "win32ui/window_metrics.h"

#include <oleacc.h>

#include <algorithm>
#include <iterator>
#include <string>

namespace win32ui
{

struct MenuBar::Item
{
    MSAAMENUINFO msaa; // must be first for owner drawn menu accessibility
    std::wstring text;
    int width = 0;
    int height = 0;
};

MenuBar::MenuBar() = default;

MenuBar::~MenuBar() = default;

const MenuBar::Item* MenuBar::Find(ULONG_PTR data) const
{
    // other owner drawn menus have separate item data
    for (const auto& item : items_)
    {
        if (reinterpret_cast<ULONG_PTR>(item.get()) == data)
        {
            return item.get();
        }
    }
    return nullptr;
}

void MenuBar::Attach(HWND window, HMENU bar)
{
    HMENU old = GetMenu(window);
    SetMenu(window, bar);
    // keep items belonging to a bar still in use
    if (old && old != bar)
    {
        DestroyMenu(old);
        items_.clear();
    }
    window_ = window;
    menu_ = bar;
}

void MenuBar::ApplyTheme()
{
    if (!menu_)
    {
        return;
    }
    // Windows sizes menu rows at window DPI even with a layout DPI override
    const UINT dpi = DpiForWindow(window_);
    const LOGFONTW menu_font = MenuFontLogFont(dpi);
    font_.reset(CreateFontIndirectW(&menu_font));
    const HFONT font = font_.get();
    MENUINFO info = {sizeof(info)};
    info.fMask = MIM_BACKGROUND;
    info.hbrBack = ThemeBrush(ThemeFill::background);
    SetMenuInfo(menu_, &info);

    HDC dc = GetDC(window_);
    HGDIOBJ old_font = font ? SelectObject(dc, font) : nullptr;
    TEXTMETRICW metrics = {};
    GetTextMetricsW(dc, &metrics);
    const int line_height = metrics.tmHeight + metrics.tmExternalLeading;
    // keep old item data until every menu item points to new data
    std::vector<std::unique_ptr<Item>> items;
    const int count = GetMenuItemCount(menu_);
    for (int index = 0; index < count; ++index)
    {
        wchar_t text[128] = {};
        MENUITEMINFOW item = {sizeof(item)};
        item.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_SUBMENU | MIIM_ID | MIIM_STATE;
        item.dwTypeData = text;
        item.cch = static_cast<UINT>(std::size(text));
        if (!GetMenuItemInfoW(menu_, static_cast<UINT>(index), TRUE, &item))
        {
            continue;
        }
        auto data = std::make_unique<Item>();
        data->text = text;
        data->msaa.dwMSAASignature = MSAA_MENU_SIG;
        data->msaa.cchWText = static_cast<DWORD>(data->text.size());
        data->msaa.pszWText = data->text.data();
        RECT measure = {};
        DrawTextW(dc, text, -1, &measure, DT_SINGLELINE | DT_CALCRECT);
        data->width = measure.right - measure.left + Scale(10, dpi);
        data->height = std::max(line_height + Scale(4, dpi), SystemMetricForDpi(SM_CYMENU, dpi) - 1);
        item.fMask |= MIIM_DATA;
        item.fType |= MFT_OWNERDRAW;
        item.dwItemData = reinterpret_cast<ULONG_PTR>(data.get());
        items.push_back(std::move(data));
        // Windows caches bar item measurements for the item lifetime
        RemoveMenu(menu_, static_cast<UINT>(index), MF_BYPOSITION);
        InsertMenuItemW(menu_, static_cast<UINT>(index), TRUE, &item);
    }
    if (old_font)
    {
        SelectObject(dc, old_font);
    }
    ReleaseDC(window_, dc);
    items_ = std::move(items);
    DrawMenuBar(window_);
}

bool MenuBar::Measure(MEASUREITEMSTRUCT* info) const
{
    const Item* data = info && info->CtlType == ODT_MENU ? Find(info->itemData) : nullptr;
    if (!data)
    {
        return false;
    }
    info->itemWidth = static_cast<UINT>(data->width);
    info->itemHeight = static_cast<UINT>(data->height);
    return true;
}

bool MenuBar::Draw(const DRAWITEMSTRUCT* info) const
{
    const Item* data = info && info->CtlType == ODT_MENU ? Find(info->itemData) : nullptr;
    if (!data)
    {
        return false;
    }
    const ThemeColors& colors = CurrentThemeColors();
    const bool selected = (info->itemState & (ODS_SELECTED | ODS_HOTLIGHT)) != 0;
    const bool disabled = (info->itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
    RECT rect = info->rcItem;
    FillRect(info->hDC, &rect, selected ? CachedBrush(colors.hover) : ThemeBrush(ThemeFill::background));
    SetBkMode(info->hDC, TRANSPARENT);
    SetTextColor(info->hDC, disabled ? colors.muted_text : colors.text);
    HGDIOBJ old = font_ ? SelectObject(info->hDC, font_.get()) : nullptr;
    UINT format = DT_SINGLELINE | DT_VCENTER | DT_CENTER;
    if (info->itemState & ODS_NOACCEL)
    {
        format |= DT_HIDEPREFIX;
    }
    DrawTextW(info->hDC, data->text.c_str(), -1, &rect, format);
    if (old)
    {
        SelectObject(info->hDC, old);
    }
    return true;
}

void MenuBar::PaintSeparator() const
{
    if (!menu_)
    {
        return;
    }
    MENUBARINFO bar = {sizeof(bar)};
    RECT window_rect = {};
    if (!GetMenuBarInfo(window_, OBJID_MENU, 0, &bar) || !GetWindowRect(window_, &window_rect))
    {
        return;
    }
    RECT line = bar.rcBar;
    OffsetRect(&line, -window_rect.left, -window_rect.top);
    line.top = line.bottom;
    line.bottom = line.top + 1;
    if (HDC dc = GetWindowDC(window_))
    {
        FillRect(dc, &line, ThemeBrush(ThemeFill::background));
        ReleaseDC(window_, dc);
    }
}

} // namespace win32ui
