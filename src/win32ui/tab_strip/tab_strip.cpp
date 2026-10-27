// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/tab_strip/tab_strip.h"

#include "win32ui/accessible_name.h"
#include "win32ui/dialog/dialog_layout_internal.h"
#include "win32ui/gdi/buffered_paint.h"
#include "win32ui/gdi/fonts_internal.h"
#include "win32ui/gdi/gdi_cache.h"
#include "win32ui/theme/palette.h"
#include "win32ui/window_metrics.h"

#include <commctrl.h>
#include <oleacc.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

namespace win32ui
{

namespace
{

constexpr UINT_PTR kTabSubclassId = 1;
// button tabs have their own gaps, so use little padding
constexpr int kPadXDip = 10;
constexpr int kPadYDip = 2;
constexpr int kCloseDip = 14;
constexpr int kCloseGapDip = 6;
constexpr int kRowMinDip = 24;
constexpr int kTipMaxWidthDip = 600;
constexpr size_t kMaxLabel = 32768;

struct TabState
{
    const TabStripHooks* hooks = nullptr;
    int hot = -1;
    int close_hot = -1;
    int close_down = -1;
    bool tracking = false;
    std::array<int, 5> fit_inputs = {};
    int fit_height = 0;
    int row_height = 0;
    // blank item image that widens only tabs with an x
    HIMAGELIST close_room = nullptr;
    int close_room_width = 0;
    HWND tip = nullptr;
    int tip_tools = 0;
    std::wstring tip_text;
    LPARAM tab_changes = 0;
    std::shared_ptr<TabState*> accessible_link;
};

UINT CloseMessage()
{
    static const UINT message = RegisterWindowMessageW(L"win32ui.TabStripClose");
    return message;
}

LRESULT CALLBACK TabProc(HWND strip, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data);

TabState* StateOf(HWND strip)
{
    DWORD_PTR data = 0;
    return strip && GetWindowSubclass(strip, TabProc, kTabSubclassId, &data) ? reinterpret_cast<TabState*>(data) : nullptr;
}

// tab colors
struct TabStripColors
{
    COLORREF background = CLR_NONE;
    COLORREF tab = CLR_NONE;
    COLORREF tab_text = CLR_NONE;
    COLORREF selected = CLR_NONE;
    COLORREF selected_text = CLR_NONE;
    COLORREF hover = CLR_NONE;
    COLORREF hover_text = CLR_NONE;
    COLORREF pressed = CLR_NONE;
    COLORREF pressed_text = CLR_NONE;
    COLORREF border = CLR_NONE;
};

TabStripColors ColorsOf()
{
    const ThemeColors& theme = CurrentThemeColors();
    TabStripColors colors;
    colors.background = theme.background;
    colors.tab = theme.panel;
    colors.tab_text = theme.muted_text;
    colors.selected = theme.surface;
    colors.selected_text = theme.text;
    colors.hover = theme.hover;
    colors.hover_text = theme.text;
    colors.pressed = theme.selection;
    colors.pressed_text = theme.selection_text;
    colors.border = theme.border;
    return colors;
}

std::wstring TabText(HWND strip, int index)
{
    std::wstring text(256, L'\0');
    for (;;)
    {
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = text.data();
        item.cchTextMax = static_cast<int>(text.size());
        if (!TabCtrl_GetItem(strip, index, &item) || !item.pszText)
        {
            return {};
        }
        // native control truncates labels to the supplied buffer
        const size_t length = wcsnlen(item.pszText, text.size());
        if (length + 1 < text.size() || text.size() >= kMaxLabel)
        {
            return std::wstring(item.pszText, length);
        }
        text.assign(text.size() * 2, L'\0');
    }
}

bool CanClose(HWND strip, const TabState* state, int index)
{
    return state->hooks && state->hooks->close && TabCtrl_GetItemCount(strip) > 1 && (!state->hooks->closable || state->hooks->closable(state->hooks->context, index));
}

bool CloseRect(HWND strip, const TabState* state, int index, RECT* rect)
{
    RECT item = {};
    if (index < 0 || !CanClose(strip, state, index) || !TabCtrl_GetItemRect(strip, index, &item))
    {
        return false;
    }
    const UINT dpi = WindowDpi(strip);
    const int height = item.bottom - item.top;
    const int size = std::min(Scale(kCloseDip, dpi), std::max(Scale(8, dpi), height - Scale(6, dpi)));
    rect->right = item.right - Scale(kCloseGapDip, dpi);
    rect->left = rect->right - size;
    rect->top = item.top + (height - size) / 2;
    rect->bottom = rect->top + size;
    return rect->left < rect->right;
}

// padding applies to every tab, so the x space comes from an item image
void ReserveCloseRoom(HWND strip, TabState* state, UINT dpi)
{
    // the tab control adds the image width plus the horizontal padding
    const int width = std::max(1, Scale(kCloseDip + 2 * kCloseGapDip, dpi) - 2 * Scale(kPadXDip, dpi));
    if (!state->close_room || state->close_room_width != width)
    {
        HIMAGELIST images = ImageList_Create(width, 1, ILC_COLOR32, 1, 0);
        if (!images || !ImageList_SetImageCount(images, 1))
        {
            if (images)
            {
                ImageList_Destroy(images);
            }
            return;
        }
        TabCtrl_SetImageList(strip, images);
        if (state->close_room)
        {
            ImageList_Destroy(state->close_room);
        }
        state->close_room = images;
        state->close_room_width = width;
    }
    const int count = TabCtrl_GetItemCount(strip);
    for (int index = 0; index < count; ++index)
    {
        TCITEMW item = {};
        item.mask = TCIF_IMAGE;
        item.iImage = CanClose(strip, state, index) ? 0 : -1;
        TabCtrl_SetItem(strip, index, &item);
    }
}

// label space left of the close button
RECT LabelRect(HWND strip, const TabState* state, int index, const RECT& item)
{
    const UINT dpi = WindowDpi(strip);
    RECT close = {};
    RECT text = item;
    text.left += Scale(kPadXDip, dpi);
    text.right = CloseRect(strip, state, index, &close) ? std::max(text.left, close.left - Scale(kCloseGapDip, dpi)) : item.right - Scale(kPadXDip, dpi);
    return text;
}

bool LabelCut(HWND strip, const TabState* state, int index)
{
    RECT item = {};
    if (!TabCtrl_GetItemRect(strip, index, &item))
    {
        return false;
    }
    const RECT text = LabelRect(strip, state, index, item);
    const std::wstring label = TabText(strip, index);
    SIZE size = {};
    HDC dc = GetDC(strip);
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(strip, WM_GETFONT, 0, 0));
    const HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &size);
    if (old)
    {
        SelectObject(dc, old);
    }
    ReleaseDC(strip, dc);
    // clip labels at the strip edge for oversized or partly scrolled tabs
    RECT client = {};
    GetClientRect(strip, &client);
    return size.cx > std::min(text.right, client.right) - std::max(text.left, client.left);
}

void SyncTips(HWND strip, TabState* state)
{
    if (!state->tip)
    {
        return;
    }
    const int count = TabCtrl_GetItemCount(strip);
    TTTOOLINFOW tool = {sizeof(tool)};
    tool.hwnd = strip;
    for (int index = 0; index < std::max(count, state->tip_tools); ++index)
    {
        tool.uId = static_cast<UINT_PTR>(index);
        if (index >= count)
        {
            SendMessageW(state->tip, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
            continue;
        }
        TabCtrl_GetItemRect(strip, index, &tool.rect);
        if (index < state->tip_tools)
        {
            SendMessageW(state->tip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
            continue;
        }
        tool.uFlags = TTF_SUBCLASS;
        tool.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(state->tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    state->tip_tools = count;
}

std::vector<int> ClosableTabs(HWND strip, const TabState* state)
{
    std::vector<int> tabs;
    const int count = TabCtrl_GetItemCount(strip);
    for (int index = 0; index < count; ++index)
    {
        if (CanClose(strip, state, index))
        {
            tabs.push_back(index);
        }
    }
    return tabs;
}

class TabAccessible final : public IAccessible
{
  public:
    TabAccessible(HWND strip, IAccessible* standard, std::shared_ptr<TabState*> link)
        : strip_(strip), standard_(standard), link_(std::move(link))
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override
    {
        if (!object)
        {
            return E_POINTER;
        }
        *object = id == IID_IUnknown || id == IID_IDispatch || id == IID_IAccessible ? static_cast<IAccessible*>(this) : nullptr;
        if (!*object)
        {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG left = InterlockedDecrement(&references_);
        if (left == 0)
        {
            standard_->Release();
            delete this;
        }
        return static_cast<ULONG>(left);
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override
    {
        if (count)
        {
            *count = 0;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** parent) override
    {
        return Connected() ? standard_->get_accParent(parent) : CO_E_OBJNOTCONNECTED;
    }
    HRESULT STDMETHODCALLTYPE get_accChildCount(long* count) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        if (!count)
        {
            return E_POINTER;
        }
        *count = static_cast<long>(TabCtrl_GetItemCount(strip_) + ClosableTabs(strip_, *link_).size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT child, IDispatch** dispatch) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        if (CloseIndex(child) < 0)
        {
            return standard_->get_accChild(child, dispatch);
        }
        *dispatch = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT child, BSTR* name) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        if (CloseIndex(child) < 0)
        {
            return standard_->get_accName(child, name);
        }
        *name = SysAllocString((*link_)->hooks->close_name);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT child, BSTR* value) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        return CloseIndex(child) < 0 ? standard_->get_accValue(child, value) : Nothing(value);
    }
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT child, BSTR* description) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        const int index = CloseIndex(child);
        if (index < 0)
        {
            return standard_->get_accDescription(child, description);
        }
        *description = SysAllocString(TabText(strip_, index).c_str());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child, VARIANT* role) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        if (CloseIndex(child) < 0)
        {
            return standard_->get_accRole(child, role);
        }
        role->vt = VT_I4;
        role->lVal = ROLE_SYSTEM_PUSHBUTTON;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT child, VARIANT* state_out) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        const int index = CloseIndex(child);
        if (index < 0)
        {
            return standard_->get_accState(child, state_out);
        }
        RECT close = {};
        RECT client = {};
        RECT shown = {};
        GetClientRect(strip_, &client);
        const bool visible = IsWindowVisible(strip_) && CloseRect(strip_, *link_, index, &close) && IntersectRect(&shown, &close, &client);
        state_out->vt = VT_I4;
        state_out->lVal = (visible ? 0 : STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN) | (IsWindowEnabled(strip_) ? 0 : STATE_SYSTEM_UNAVAILABLE);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT child, BSTR* help) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        return CloseIndex(child) < 0 ? standard_->get_accHelp(child, help) : Nothing(help);
    }
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* file, VARIANT child, long* topic) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        return CloseIndex(child) < 0 ? standard_->get_accHelpTopic(file, child, topic) : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT child, BSTR* shortcut) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        return CloseIndex(child) < 0 ? standard_->get_accKeyboardShortcut(child, shortcut) : Nothing(shortcut);
    }
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* child) override
    {
        return Connected() ? standard_->get_accFocus(child) : CO_E_OBJNOTCONNECTED;
    }
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* children) override
    {
        return Connected() ? standard_->get_accSelection(children) : CO_E_OBJNOTCONNECTED;
    }
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT child, BSTR* action) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        return CloseIndex(child) < 0 ? standard_->get_accDefaultAction(child, action) : get_accName(child, action);
    }
    HRESULT STDMETHODCALLTYPE accSelect(long flags, VARIANT child) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        return CloseIndex(child) < 0 ? standard_->accSelect(flags, child) : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE accLocation(long* left, long* top, long* width, long* height, VARIANT child) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        const int index = CloseIndex(child);
        if (index < 0)
        {
            return standard_->accLocation(left, top, width, height, child);
        }
        RECT close = {};
        if (!CloseRect(strip_, *link_, index, &close))
        {
            return S_FALSE;
        }
        MapWindowPoints(strip_, nullptr, reinterpret_cast<POINT*>(&close), 2);
        *left = close.left;
        *top = close.top;
        *width = close.right - close.left;
        *height = close.bottom - close.top;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accNavigate(long direction, VARIANT start, VARIANT* end) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        return CloseIndex(start) < 0 ? standard_->accNavigate(direction, start, end) : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE accHitTest(long x, long y, VARIANT* child) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        POINT point = {x, y};
        ScreenToClient(strip_, &point);
        RECT client = {};
        GetClientRect(strip_, &client);
        // close buttons outside the strip cannot be hit
        const std::vector<int> closes = PtInRect(&client, point) ? ClosableTabs(strip_, *link_) : std::vector<int>();
        for (size_t position = 0; position < closes.size(); ++position)
        {
            RECT close = {};
            if (CloseRect(strip_, *link_, closes[position], &close) && PtInRect(&close, point))
            {
                child->vt = VT_I4;
                child->lVal = static_cast<long>(TabCtrl_GetItemCount(strip_) + position + 1);
                return S_OK;
            }
        }
        return standard_->accHitTest(x, y, child);
    }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT child) override
    {
        if (!Connected())
        {
            return CO_E_OBJNOTCONNECTED;
        }
        const int index = CloseIndex(child);
        if (index < 0)
        {
            return standard_->accDoDefaultAction(child);
        }
        if (!IsWindowEnabled(strip_))
        {
            return E_FAIL;
        }
        return PostMessageW(strip_, CloseMessage(), static_cast<WPARAM>(index), (*link_)->tab_changes) ? S_OK : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) override
    {
        return E_NOTIMPL;
    }

  private:
    bool Connected() const
    {
        return *link_ != nullptr;
    }
    int CloseIndex(const VARIANT& child) const
    {
        if (child.vt != VT_I4)
        {
            return -1;
        }
        const long position = child.lVal - TabCtrl_GetItemCount(strip_) - 1;
        if (position < 0)
        {
            return -1;
        }
        const std::vector<int> closes = ClosableTabs(strip_, *link_);
        return position < static_cast<long>(closes.size()) ? closes[static_cast<size_t>(position)] : -1;
    }
    static HRESULT Nothing(BSTR* text)
    {
        *text = nullptr;
        return S_FALSE;
    }

    HWND strip_;
    IAccessible* standard_;
    std::shared_ptr<TabState*> link_;
    LONG references_ = 1;
};

void ForgetTabs(HWND strip, TabState* state)
{
    ++state->tab_changes;
    state->hot = -1;
    state->close_hot = -1;
    if (state->close_down >= 0)
    {
        state->close_down = -1;
        if (GetCapture() == strip)
        {
            ReleaseCapture();
        }
    }
}

void UpdateHot(HWND strip, TabState* state, POINT point)
{
    TCHITTESTINFO hit = {};
    hit.pt = point;
    const int index = TabCtrl_HitTest(strip, &hit);
    RECT close = {};
    const int close_hot = CloseRect(strip, state, index, &close) && PtInRect(&close, point) ? index : -1;
    if (index != state->hot || close_hot != state->close_hot)
    {
        state->hot = index;
        state->close_hot = close_hot;
        InvalidateRect(strip, nullptr, FALSE);
    }
}

void DrawItem(HDC hdc, HWND strip, const TabState* state, const TabStripColors& colors, int index, const RECT& item, bool selected)
{
    const bool hot = index == state->hot && !selected;
    FillRect(hdc, &item, CachedBrush(selected ? colors.selected : hot ? colors.hover
                                                                      : colors.tab));
    const HGDIOBJ old_pen = SelectObject(hdc, CachedPen(colors.border));
    MoveToEx(hdc, item.left, item.bottom, nullptr);
    LineTo(hdc, item.left, item.top);
    LineTo(hdc, item.right, item.top);
    LineTo(hdc, item.right, item.bottom);
    LineTo(hdc, item.left, item.bottom);
    SelectObject(hdc, old_pen);

    const UINT dpi = WindowDpi(strip);
    const COLORREF label = selected ? colors.selected_text : hot ? colors.hover_text
                                                                 : colors.tab_text;
    RECT close = {};
    const bool has_close = CloseRect(strip, state, index, &close);
    RECT text = LabelRect(strip, state, index, item);
    SetTextColor(hdc, label);
    SetBkMode(hdc, TRANSPARENT);
    const std::wstring content = TabText(strip, index);
    DrawTextW(hdc, content.c_str(), static_cast<int>(content.size()), &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (!has_close)
    {
        return;
    }
    const bool down = index == state->close_down;
    const bool close_hot = index == state->close_hot;
    if (down || close_hot)
    {
        FillRect(hdc, &close, CachedBrush(down ? colors.pressed : colors.hover));
    }
    DrawCloseGlyph(hdc, close, down ? colors.pressed_text : close_hot ? colors.hover_text
                                                                      : label,
                   dpi);
}

void Paint(HWND strip, const TabState* state, HDC hdc)
{
    const TabStripColors colors = ColorsOf();
    RECT client = {};
    GetClientRect(strip, &client);
    FillRect(hdc, &client, CachedBrush(colors.background));
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(strip, WM_GETFONT, 0, 0));
    const HGDIOBJ old_font = font ? SelectObject(hdc, font) : nullptr;
    const int count = TabCtrl_GetItemCount(strip);
    const int current = TabCtrl_GetCurSel(strip);
    for (int index = 0; index < count; ++index)
    {
        RECT item = {};
        if (TabCtrl_GetItemRect(strip, index, &item))
        {
            DrawItem(hdc, strip, state, colors, index, item, index == current);
        }
    }
    // only keyboard input focuses the strip
    RECT focused = {};
    if (GetFocus() == strip && !(SendMessageW(strip, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) && TabCtrl_GetItemRect(strip, TabCtrl_GetCurFocus(strip), &focused))
    {
        const int inset = Scale(3, WindowDpi(strip));
        InflateRect(&focused, -inset, -inset);
        SetTextColor(hdc, RGB(0, 0, 0));
        SetBkColor(hdc, RGB(255, 255, 255));
        DrawFocusRect(hdc, &focused);
    }
    if (old_font)
    {
        SelectObject(hdc, old_font);
    }
}

void SetMultiline(HWND strip, bool multiline)
{
    const LONG_PTR style = GetWindowLongPtrW(strip, GWL_STYLE);
    const LONG_PTR wanted = multiline ? style | TCS_MULTILINE : style & ~static_cast<LONG_PTR>(TCS_MULTILINE);
    if (wanted != style)
    {
        SetWindowLongPtrW(strip, GWL_STYLE, wanted);
        SetWindowPos(strip, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

// measure row height at width by resizing without redraw
int RowsHeight(HWND strip, int width, int row_height)
{
    RECT window = {};
    GetWindowRect(strip, &window);
    const int height = std::max<int>(row_height, window.bottom - window.top);
    if (window.right - window.left != width || window.bottom - window.top != height)
    {
        SetWindowPos(strip, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    }
    int last_bottom = -1;
    const int count = TabCtrl_GetItemCount(strip);
    for (int index = 0; index < count; ++index)
    {
        RECT item = {};
        if (TabCtrl_GetItemRect(strip, index, &item))
        {
            last_bottom = std::max<int>(last_bottom, item.bottom);
        }
    }
    return std::max(row_height, last_bottom + 1);
}

// notify parent like a click so it can keep the current tab
void SelectFocused(HWND strip)
{
    const int focus = TabCtrl_GetCurFocus(strip);
    const int current = TabCtrl_GetCurSel(strip);
    if (focus < 0 || focus == current)
    {
        return;
    }
    HWND parent = GetParent(strip);
    const int count = TabCtrl_GetItemCount(strip);
    NMHDR header = {strip, static_cast<UINT_PTR>(GetDlgCtrlID(strip)), static_cast<UINT>(TCN_SELCHANGING)};
    if (SendMessageW(parent, WM_NOTIFY, header.idFrom, reinterpret_cast<LPARAM>(&header)))
    {
        TabCtrl_SetCurFocus(strip, current);
        return;
    }
    // parent callback changed tabs, index may now name another tab or none
    if (!IsWindow(strip) || TabCtrl_GetItemCount(strip) != count)
    {
        return;
    }
    TabCtrl_SetCurSel(strip, focus);
    header.code = static_cast<UINT>(TCN_SELCHANGE);
    SendMessageW(parent, WM_NOTIFY, header.idFrom, reinterpret_cast<LPARAM>(&header));
}

LRESULT CALLBACK TabProc(HWND strip, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data)
{
    auto* state = reinterpret_cast<TabState*>(data);
    const POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    if (message == CloseMessage())
    {
        // tab changes can shift a posted close index, ignore stale presses and disabled strips
        if (const int index = static_cast<int>(wparam); lparam == state->tab_changes && IsWindowEnabled(strip) && index < TabCtrl_GetItemCount(strip) && CanClose(strip, state, index))
        {
            ForgetTabs(strip, state);
            state->hooks->close(state->hooks->context, index);
        }
        return 0;
    }
    switch (message)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintBuffered(strip);
        return 0;
    case WM_PRINTCLIENT:
        SyncTips(strip, state);
        Paint(strip, state, reinterpret_cast<HDC>(wparam));
        return 0;
    case WM_NOTIFY:
        if (const auto* header = reinterpret_cast<NMHDR*>(lparam); header->hwndFrom == state->tip && header->code == TTN_GETDISPINFOW)
        {
            // only truncated labels get tooltips, empty text hides them
            const int index = static_cast<int>(header->idFrom);
            state->tip_text = LabelCut(strip, state, index) ? TabText(strip, index) : std::wstring();
            reinterpret_cast<NMTTDISPINFOW*>(lparam)->lpszText = state->tip_text.data();
            return 0;
        }
        break;
    case WM_GETOBJECT:
        if (static_cast<LONG>(lparam) == OBJID_CLIENT && state->hooks && state->hooks->close && state->hooks->close_name)
        {
            IAccessible* standard = nullptr;
            if (SUCCEEDED(CreateStdAccessibleObject(strip, OBJID_CLIENT, IID_IAccessible, reinterpret_cast<void**>(&standard))))
            {
                if (!state->accessible_link)
                {
                    state->accessible_link = std::make_shared<TabState*>(state);
                }
                auto* accessible = new TabAccessible(strip, standard, state->accessible_link);
                const LRESULT result = LresultFromObject(IID_IAccessible, wparam, accessible);
                accessible->Release();
                return result;
            }
        }
        break;
    case WM_MOUSEMOVE:
        UpdateHot(strip, state, point);
        if (!state->tracking)
        {
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, strip, 0};
            state->tracking = TrackMouseEvent(&track) != FALSE;
        }
        break;
    case WM_MOUSELEAVE:
        state->tracking = false;
        if (state->hot != -1 || state->close_hot != -1)
        {
            state->hot = -1;
            state->close_hot = -1;
            InvalidateRect(strip, nullptr, FALSE);
        }
        break;
    case WM_LBUTTONDOWN:
        {
            TCHITTESTINFO hit = {};
            hit.pt = point;
            const int index = TabCtrl_HitTest(strip, &hit);
            RECT close = {};
            // close button preserves selection
            if (CloseRect(strip, state, index, &close) && PtInRect(&close, point))
            {
                state->close_down = index;
                SetCapture(strip);
                InvalidateRect(strip, nullptr, FALSE);
                return 0;
            }
            break;
        }
    case WM_LBUTTONUP:
        if (state->close_down >= 0)
        {
            const int index = state->close_down;
            state->close_down = -1;
            ReleaseCapture();
            InvalidateRect(strip, nullptr, FALSE);
            RECT close = {};
            if (CloseRect(strip, state, index, &close) && PtInRect(&close, point))
            {
                state->hot = -1;
                state->close_hot = -1;
                // hook may delete tabs or destroy the strip, dont access it afterward
                state->hooks->close(state->hooks->context, index);
            }
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (state->close_down >= 0)
        {
            state->close_down = -1;
            InvalidateRect(strip, nullptr, FALSE);
        }
        break;
    case WM_KEYDOWN:
        // arrow keys move button tab focus and selection together
        if (wparam == VK_LEFT || wparam == VK_RIGHT || wparam == VK_UP || wparam == VK_DOWN || wparam == VK_HOME || wparam == VK_END)
        {
            const LRESULT result = DefSubclassProc(strip, message, wparam, lparam);
            SelectFocused(strip);
            return result;
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_UPDATEUISTATE:
        InvalidateRect(strip, nullptr, FALSE);
        break;
    case TCM_INSERTITEMW:
    case TCM_INSERTITEMA:
    case TCM_DELETEITEM:
    case TCM_DELETEALLITEMS:
        ForgetTabs(strip, state);
        state->row_height = 0;
        break;
    case WM_SETFONT:
        state->row_height = 0;
        break;
    case WM_PARENTNOTIFY:
        // single row scroll arrows appear dynamically and need the theme
        if (LOWORD(wparam) == WM_CREATE)
        {
            ApplyFrame(state->hooks, reinterpret_cast<HWND>(lparam));
        }
        break;
    case WM_NCDESTROY:
        // top level tooltip would otherwise outlive the strip
        if (state->tip && IsWindow(state->tip))
        {
            DestroyWindow(state->tip);
        }
        if (state->accessible_link)
        {
            *state->accessible_link = nullptr;
        }
        if (state->close_room)
        {
            TabCtrl_SetImageList(strip, nullptr);
            ImageList_Destroy(state->close_room);
        }
        RemoveWindowSubclass(strip, TabProc, kTabSubclassId);
        delete state;
        break;
    default:
        break;
    }
    return DefSubclassProc(strip, message, wparam, lparam);
}

} // namespace

HWND CreateTabStrip(HWND parent, int id, const wchar_t* name, const TabStripHooks* hooks)
{
    HWND strip = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_BUTTONS | TCS_MULTILINE | TCS_RAGGEDRIGHT | TCS_FOCUSNEVER, 0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    if (!strip)
    {
        return nullptr;
    }
    auto state = std::make_unique<TabState>();
    state->hooks = hooks;
    if (!SetWindowSubclass(strip, TabProc, kTabSubclassId, reinterpret_cast<DWORD_PTR>(state.get())))
    {
        DestroyWindow(strip);
        return nullptr;
    }
    // top level owner, destroyed by the strip
    state->tip = CreateTooltip(strip);
    state.release();
    SetAccessibleName(strip, name ? name : L"");
    RefreshTabStrip(strip);
    return strip;
}

void RefreshTabStrip(HWND strip)
{
    TabState* state = StateOf(strip);
    if (!state)
    {
        return;
    }
    // renames and font changes can change rows without changing cached fit inputs
    state->fit_inputs = {};
    state->row_height = 0;
    state->hot = -1;
    state->close_hot = -1;
    const UINT dpi = WindowDpi(strip);
    TabCtrl_SetPadding(strip, Scale(kPadXDip, dpi), Scale(kPadYDip, dpi));
    ReserveCloseRoom(strip, state, dpi);
    SendMessageW(strip, TCM_SETMINTABWIDTH, 0, Scale(kTabMinWidthDip, dpi));
    ApplyFrame(state->hooks, strip);
    ApplyFrame(state->hooks, FindWindowExW(strip, nullptr, UPDOWN_CLASSW, nullptr));
    if (state->tip)
    {
        ApplyFrame(state->hooks, state->tip);
        SendMessageW(state->tip, TTM_SETMAXTIPWIDTH, 0, Scale(kTipMaxWidthDip, dpi));
    }
    InvalidateRect(strip, nullptr, FALSE);
}

int TabStripRowHeight(HWND strip)
{
    if (!strip)
    {
        return 0;
    }
    const UINT dpi = WindowDpi(strip);
    const int minimum = std::max<int>(Scale(kRowMinDip, dpi), WindowFontMetrics(strip).tmHeight + Scale(kPadYDip, dpi) * 2 + 2);
    RECT item = {};
    return TabCtrl_GetItemRect(strip, 0, &item) ? std::max<int>(minimum, item.bottom - item.top) : minimum;
}

int FitTabStrip(HWND strip, int width, int max_height)
{
    TabState* state = StateOf(strip);
    if (!state)
    {
        return 0;
    }
    if (!state->row_height)
    {
        state->row_height = TabStripRowHeight(strip);
    }
    const int row_height = state->row_height;
    const int count = TabCtrl_GetItemCount(strip);
    int widths = 0;
    for (int index = 0; index < count; ++index)
    {
        RECT item = {};
        if (TabCtrl_GetItemRect(strip, index, &item))
        {
            widths += item.right - item.left;
        }
    }
    const std::array<int, 5> inputs = {width, row_height, max_height, count, widths};
    if (inputs == state->fit_inputs)
    {
        return state->fit_height;
    }
    state->fit_inputs = inputs;
    SetMultiline(strip, true);
    state->fit_height = RowsHeight(strip, width, row_height);
    if (state->fit_height > max_height)
    {
        // rows exceed available height
        SetMultiline(strip, false);
        state->fit_height = row_height;
        // reselect to scroll the single row to the active tab, TCM_SETCURSEL sends no notification
        if (const int current = TabCtrl_GetCurSel(strip); current > 0)
        {
            TabCtrl_SetCurSel(strip, 0);
            TabCtrl_SetCurSel(strip, current);
        }
    }
    return state->fit_height;
}

void DrawCloseGlyph(HDC hdc, const RECT& rect, COLORREF color, UINT dpi)
{
    const int radius = Scale(3, dpi);
    const int center_x = (rect.left + rect.right) / 2;
    const int center_y = (rect.top + rect.bottom) / 2;
    const HGDIOBJ old_pen = SelectObject(hdc, CachedPen(color, std::max(1, Scale(1, dpi))));
    MoveToEx(hdc, center_x - radius, center_y - radius, nullptr);
    LineTo(hdc, center_x + radius + 1, center_y + radius + 1);
    MoveToEx(hdc, center_x + radius, center_y - radius, nullptr);
    LineTo(hdc, center_x - radius - 1, center_y + radius + 1);
    SelectObject(hdc, old_pen);
}

} // namespace win32ui
