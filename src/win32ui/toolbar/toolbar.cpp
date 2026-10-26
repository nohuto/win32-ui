// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/toolbar/toolbar.h"

#include "win32ui/accessible_name.h"
#include "win32ui/dialog/dialog_layout_internal.h"
#include "win32ui/gdi/gdi_cache.h"
#include "win32ui/gdi/icon_loader.h"
#include "win32ui/theme/palette.h"
#include "win32ui/window_metrics.h"

#include <algorithm>
#include <memory>

namespace win32ui
{

namespace
{

constexpr UINT_PTR kToolbarSubclassId = 1;
// button size is icon plus comctl32 padding at 96 DPI
constexpr int kButtonMarginDip = 6;
constexpr int kPaddingXDip = 7;
constexpr int kPaddingYDip = 6;
constexpr int kBarMarginDip = 4;

struct ToolbarState
{
    const ToolbarHooks* hooks = nullptr;
    // commands in image order, separators have no image
    std::vector<int> commands;
    HIMAGELIST images = nullptr;
    // last prepaint hover button, postpaint no longer includes CDIS_HOT
    DWORD_PTR hover_item = static_cast<DWORD_PTR>(-1);
    std::wstring tip;
};

std::vector<HWND> g_toolbars;

LRESULT CALLBACK ToolbarProc(HWND toolbar, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data);

ToolbarState* StateOf(HWND toolbar)
{
    DWORD_PTR data = 0;
    return toolbar && GetWindowSubclass(toolbar, ToolbarProc, kToolbarSubclassId, &data) ? reinterpret_cast<ToolbarState*>(data) : nullptr;
}

// toolbar colors, read from the palette at paint time
struct ToolbarColors
{
    COLORREF background = CLR_NONE;
    // dropdown arrow, disabled color is halfway between muted and background
    COLORREF text = CLR_NONE;
    COLORREF muted = CLR_NONE;
    COLORREF hover = CLR_NONE;
    COLORREF checked = CLR_NONE;
    COLORREF border = CLR_NONE;
    // true paints hover/checked boxes and arrows, false keeps native button styles over the background
    bool paint_buttons = false;
};

ToolbarColors ColorsOf()
{
    const ThemeColors& colors = CurrentThemeColors();
    ToolbarColors result;
    result.background = colors.background;
    result.text = colors.text;
    result.muted = colors.muted_text;
    result.hover = colors.hover;
    result.checked = colors.surface;
    result.border = colors.border;
    // light themes keep native button styles
    result.paint_buttons = ThemeDark();
    return result;
}

HWND ToolbarOfTip(HWND tip)
{
    const auto found = std::ranges::find_if(g_toolbars, [tip](HWND toolbar) { return reinterpret_cast<HWND>(SendMessageW(toolbar, TB_GETTOOLTIPS, 0, 0)) == tip; });
    return found == g_toolbars.end() ? nullptr : *found;
}

void LoadIcons(HWND toolbar, ToolbarState* state)
{
    const UINT dpi = WindowDpi(toolbar);
    const int size = ScaleForDpi(kIconDip, dpi);
    HIMAGELIST images = ImageList_Create(size, size, ILC_COLOR32, static_cast<int>(state->commands.size()), 0);
    if (!images)
    {
        return;
    }
    ImageList_SetBkColor(images, CLR_NONE);
    for (const int command : state->commands)
    {
        HICON icon = state->hooks && state->hooks->icon ? state->hooks->icon(state->hooks->context, command, dpi) : nullptr;
        ImageListAddOrBlank(images, icon, size);
        if (icon)
        {
            DestroyIcon(icon);
        }
    }
    // dropdown arrows make these messages add 6 px after DPI changes, temporarily remove them
    const LRESULT extended = SendMessageW(toolbar, TB_GETEXTENDEDSTYLE, 0, 0);
    SendMessageW(toolbar, TB_SETEXTENDEDSTYLE, 0, extended & ~TBSTYLE_EX_DRAWDDARROWS);
    SendMessageW(toolbar, TB_SETPADDING, 0, MAKELPARAM(ScaleForDpi(kPaddingXDip, dpi), ScaleForDpi(kPaddingYDip, dpi)));
    SendMessageW(toolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(size, size));
    const int button = size + ScaleForDpi(kButtonMarginDip, dpi);
    SendMessageW(toolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(button, button));
    SendMessageW(toolbar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(images));
    SendMessageW(toolbar, TB_SETEXTENDEDSTYLE, 0, extended);
    // keep old image list alive until the toolbar switches
    if (state->images)
    {
        ImageList_Destroy(state->images);
    }
    state->images = images;
}

void DrawButtonBox(HDC hdc, const RECT& rect, COLORREF fill, COLORREF border)
{
    RECT box = rect;
    InflateRect(&box, -1, -1);
    HGDIOBJ old_brush = SelectObject(hdc, CachedBrush(fill));
    HGDIOBJ old_pen = SelectObject(hdc, CachedPen(border));
    RoundRect(hdc, box.left, box.top, box.right, box.bottom, 4, 4);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
}

LRESULT ItemPrepaint(HWND toolbar, ToolbarState* state, const ToolbarColors& colors, NMTBCUSTOMDRAW* draw)
{
    const int index = static_cast<int>(SendMessageW(toolbar, TB_COMMANDTOINDEX, draw->nmcd.dwItemSpec, 0));
    TBBUTTON button = {};
    if (index >= 0 && SendMessageW(toolbar, TB_GETBUTTON, index, reinterpret_cast<LPARAM>(&button)) && (button.fsStyle & BTNS_SEP))
    {
        return CDRF_DODEFAULT;
    }
    POINT cursor = {};
    GetCursorPos(&cursor);
    ScreenToClient(toolbar, &cursor);
    const bool hovered = (draw->nmcd.uItemState & CDIS_HOT) || PtInRect(&draw->nmcd.rc, cursor);
    draw->hbrMonoDither = CachedBrush(colors.background);
    draw->hbrLines = CachedBrush(colors.background);
    draw->hpenLines = CachedPen(colors.border);
    draw->clrText = colors.text;
    draw->clrTextHighlight = colors.text;
    draw->clrBtnFace = colors.background;
    draw->clrBtnHighlight = colors.checked;
    draw->clrHighlightHotTrack = colors.hover;
    draw->nStringBkMode = TRANSPARENT;
    draw->nHLStringBkMode = TRANSPARENT;
    // disabled buttons have no hover box, pressed buttons look hovered
    const bool hover_box = hovered && !(draw->nmcd.uItemState & CDIS_DISABLED);
    state->hover_item = hover_box ? draw->nmcd.dwItemSpec : static_cast<DWORD_PTR>(-1);
    if (hover_box)
    {
        DrawButtonBox(draw->nmcd.hdc, draw->nmcd.rc, colors.hover, colors.border);
    }
    else if (draw->nmcd.uItemState & CDIS_CHECKED)
    {
        DrawButtonBox(draw->nmcd.hdc, draw->nmcd.rc, colors.checked, colors.border);
    }
    // native hot, pressed and checked backgrounds would cover custom boxes
    LRESULT result = TBCDRF_USECDCOLORS | TBCDRF_NOBACKGROUND | TBCDRF_NOEDGES;
    if (index >= 0 && (button.fsStyle & BTNS_DROPDOWN))
    {
        result |= CDRF_NOTIFYPOSTPAINT;
    }
    return result;
}

// stock arrow is dark gray on dark backgrounds and light blue when pressed
LRESULT DrawDropdownArrow(HWND toolbar, const ToolbarState* state, const ToolbarColors& colors, NMTBCUSTOMDRAW* draw)
{
    const int index = static_cast<int>(SendMessageW(toolbar, TB_COMMANDTOINDEX, draw->nmcd.dwItemSpec, 0));
    RECT arrow = {};
    RECT item = {};
    if (index < 0 || !SendMessageW(toolbar, TB_GETITEMDROPDOWNRECT, index, reinterpret_cast<LPARAM>(&arrow)) || !SendMessageW(toolbar, TB_GETITEMRECT, index, reinterpret_cast<LPARAM>(&item)))
    {
        return CDRF_DODEFAULT;
    }
    // postpaint has a smaller rc and different state, read both from the toolbar
    const LRESULT button_state = SendMessageW(toolbar, TB_GETSTATE, draw->nmcd.dwItemSpec, 0);
    const bool disabled = !(button_state & TBSTATE_ENABLED);
    const int saved = SaveDC(draw->nmcd.hdc);
    IntersectClipRect(draw->nmcd.hdc, arrow.left, arrow.top, arrow.right, arrow.bottom);
    FillRect(draw->nmcd.hdc, &arrow, CachedBrush(colors.background));
    // clip the full button box to the arrow region so corners match
    if (state->hover_item == draw->nmcd.dwItemSpec)
    {
        DrawButtonBox(draw->nmcd.hdc, item, colors.hover, colors.border);
    }
    else if (button_state & TBSTATE_CHECKED)
    {
        DrawButtonBox(draw->nmcd.hdc, item, colors.checked, colors.border);
    }
    RestoreDC(draw->nmcd.hdc, saved);
    const int width = std::max<int>(5, (arrow.right - arrow.left) / 2) | 1;
    const int rows = width / 2 + 1;
    const int x = (arrow.left + arrow.right - width) / 2;
    const int y = (arrow.top + arrow.bottom - rows) / 2;
    const COLORREF muted = RGB((GetRValue(colors.muted) + GetRValue(colors.background)) / 2, (GetGValue(colors.muted) + GetGValue(colors.background)) / 2, (GetBValue(colors.muted) + GetBValue(colors.background)) / 2);
    HBRUSH glyph = CachedBrush(disabled ? muted : colors.text);
    for (int row = 0; row < rows; ++row)
    {
        const RECT line = {x + row, y + row, x + width - row, y + row + 1};
        FillRect(draw->nmcd.hdc, &line, glyph);
    }
    return CDRF_DODEFAULT;
}

LRESULT CustomDraw(HWND toolbar, ToolbarState* state, NMTBCUSTOMDRAW* draw)
{
    const ToolbarColors colors = ColorsOf();
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
    {
        FillRect(draw->nmcd.hdc, &draw->nmcd.rc, CachedBrush(colors.background));
    }
    if (!colors.paint_buttons)
    {
        return CDRF_DODEFAULT;
    }
    switch (draw->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        return ItemPrepaint(toolbar, state, colors, draw);
    case CDDS_ITEMPOSTPAINT:
        return DrawDropdownArrow(toolbar, state, colors, draw);
    default:
        return CDRF_DODEFAULT;
    }
}

LRESULT CALLBACK ToolbarProc(HWND toolbar, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data)
{
    auto* state = reinterpret_cast<ToolbarState*>(data);
    switch (message)
    {
    case WM_SETFOCUS:
        // focus uses the hot button, arrow keys move it
        if (SendMessageW(toolbar, TB_GETHOTITEM, 0, 0) < 0)
        {
            const int count = static_cast<int>(SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0));
            for (int index = 0; index < count; ++index)
            {
                TBBUTTON button = {};
                if (SendMessageW(toolbar, TB_GETBUTTON, index, reinterpret_cast<LPARAM>(&button)) && !(button.fsStyle & BTNS_SEP) && (button.fsState & TBSTATE_ENABLED))
                {
                    SendMessageW(toolbar, TB_SETHOTITEM, index, 0);
                    break;
                }
            }
        }
        break;
    case WM_KILLFOCUS:
        SendMessageW(toolbar, TB_SETHOTITEM, static_cast<WPARAM>(-1), 0);
        break;
    case WM_NCDESTROY:
        if (state->images)
        {
            ImageList_Destroy(state->images);
        }
        std::erase(g_toolbars, toolbar);
        RemoveWindowSubclass(toolbar, ToolbarProc, kToolbarSubclassId);
        delete state;
        break;
    default:
        break;
    }
    return DefSubclassProc(toolbar, message, wparam, lparam);
}

} // namespace

HWND CreateToolbar(HWND parent, int id, const wchar_t* name, const ToolbarHooks* hooks, const std::vector<ToolbarButton>& buttons)
{
    HWND toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, name, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NOPARENTALIGN, 0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    if (!toolbar)
    {
        return nullptr;
    }
    auto created = std::make_unique<ToolbarState>();
    if (!SetWindowSubclass(toolbar, ToolbarProc, kToolbarSubclassId, reinterpret_cast<DWORD_PTR>(created.get())))
    {
        DestroyWindow(toolbar);
        return nullptr;
    }
    ToolbarState* state = created.release();
    g_toolbars.push_back(toolbar);
    state->hooks = hooks;
    SendMessageW(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    // keep button names for accessibility without drawing them
    SendMessageW(toolbar, TB_SETMAXTEXTROWS, 0, 0);
    const bool dropdown = std::ranges::any_of(buttons, [](const ToolbarButton& button) { return button.dropdown; });
    SendMessageW(toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DOUBLEBUFFER | (dropdown ? TBSTYLE_EX_DRAWDDARROWS : 0));
    std::vector<TBBUTTON> items;
    for (const ToolbarButton& button : buttons)
    {
        TBBUTTON item = {};
        if (!button.command)
        {
            item.fsStyle = BTNS_SEP;
        }
        else
        {
            item.idCommand = button.command;
            item.iBitmap = static_cast<int>(state->commands.size());
            item.fsState = TBSTATE_ENABLED;
            item.fsStyle = static_cast<BYTE>(BTNS_BUTTON | (button.dropdown ? BTNS_DROPDOWN : 0));
            item.iString = reinterpret_cast<INT_PTR>(button.name.c_str());
            state->commands.push_back(button.command);
        }
        items.push_back(item);
    }
    LoadIcons(toolbar, state);
    if (!items.empty())
    {
        SendMessageW(toolbar, TB_ADDBUTTONSW, items.size(), reinterpret_cast<LPARAM>(items.data()));
    }
    SendMessageW(toolbar, TB_AUTOSIZE, 0, 0);
    // toolbar does not retain window text
    SetAccessibleName(toolbar, name ? name : L"");
    ApplyFrame(state->hooks, toolbar);
    ApplyFrame(state->hooks, reinterpret_cast<HWND>(SendMessageW(toolbar, TB_GETTOOLTIPS, 0, 0)));
    return toolbar;
}

void RefreshToolbar(HWND toolbar)
{
    ToolbarState* state = StateOf(toolbar);
    if (!state)
    {
        return;
    }
    // WM_SETREDRAW would show a hidden toolbar
    const bool shown = IsWindowVisible(toolbar) != FALSE;
    if (shown)
    {
        SendMessageW(toolbar, WM_SETREDRAW, FALSE, 0);
    }
    LoadIcons(toolbar, state);
    SendMessageW(toolbar, TB_AUTOSIZE, 0, 0);
    if (shown)
    {
        SendMessageW(toolbar, WM_SETREDRAW, TRUE, 0);
    }
    ApplyFrame(state->hooks, toolbar);
    ApplyFrame(state->hooks, reinterpret_cast<HWND>(SendMessageW(toolbar, TB_GETTOOLTIPS, 0, 0)));
    InvalidateRect(toolbar, nullptr, TRUE);
}

int ToolbarHeight(HWND toolbar)
{
    return HIWORD(SendMessageW(toolbar, TB_GETBUTTONSIZE, 0, 0)) + Scale(kBarMarginDip, WindowDpi(toolbar));
}

void EnableToolbarButton(HWND toolbar, int command, bool enabled)
{
    const LRESULT state = SendMessageW(toolbar, TB_GETSTATE, command, 0);
    if (state != -1 && ((state & TBSTATE_ENABLED) != 0) != enabled)
    {
        SendMessageW(toolbar, TB_ENABLEBUTTON, command, MAKELPARAM(enabled ? TRUE : FALSE, 0));
    }
}

bool ToolbarNotify(NMHDR* header, LRESULT* result)
{
    if (!header || !result)
    {
        return false;
    }
    if (header->code == TTN_GETDISPINFOW)
    {
        HWND toolbar = ToolbarOfTip(header->hwndFrom);
        ToolbarState* state = StateOf(toolbar);
        if (!state)
        {
            return false;
        }
        auto* info = reinterpret_cast<NMTTDISPINFOW*>(header);
        const int command = static_cast<int>(info->hdr.idFrom);
        state->tip = state->hooks && state->hooks->tip ? state->hooks->tip(state->hooks->context, command) : std::wstring();
        if (state->tip.empty())
        {
            const int length = static_cast<int>(SendMessageW(toolbar, TB_GETBUTTONTEXTW, command, 0));
            state->tip.resize(static_cast<size_t>(std::max(length, 0)) + 1);
            SendMessageW(toolbar, TB_GETBUTTONTEXTW, command, reinterpret_cast<LPARAM>(state->tip.data()));
            state->tip.resize(static_cast<size_t>(std::max(length, 0)));
        }
        const wchar_t* shortcut = state->hooks && state->hooks->shortcut ? state->hooks->shortcut(command) : nullptr;
        if (shortcut && *shortcut)
        {
            state->tip.append(L" (").append(shortcut).append(L")");
        }
        info->lpszText = state->tip.data();
        *result = 0;
        return true;
    }
    HWND toolbar = header->hwndFrom;
    ToolbarState* state = StateOf(toolbar);
    if (!state)
    {
        return false;
    }
    if (header->code == NM_CUSTOMDRAW)
    {
        *result = CustomDraw(toolbar, state, reinterpret_cast<NMTBCUSTOMDRAW*>(header));
        return true;
    }
    if (header->code == TBN_DROPDOWN)
    {
        const auto* info = reinterpret_cast<const NMTOOLBARW*>(header);
        RECT button = info->rcButton;
        MapWindowPoints(toolbar, nullptr, reinterpret_cast<POINT*>(&button), 2);
        if (state->hooks && state->hooks->dropdown)
        {
            state->hooks->dropdown(state->hooks->context, info->iItem, button);
        }
        *result = TBDDRET_DEFAULT;
        return true;
    }
    return false;
}

} // namespace win32ui
