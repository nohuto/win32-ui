// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#include "win32ui/theme/theme.h"

#include "win32ui/dialog/dialog_layout_internal.h"
#include "win32ui/dialog/dialog_window.h"
#include "win32ui/dialog/dialog_window_internal.h"
#include "win32ui/gdi/buffered_paint.h"
#include "win32ui/gdi/gdi_cache.h"
#include "win32ui/list_view/list_view.h"
#include "win32ui/splitter/splitter_internal.h"
#include "win32ui/status_bar/status_bar.h"
#include "win32ui/tab_strip/tab_strip.h"
#include "win32ui/toolbar/toolbar.h"
#include "win32ui/theme/palette_internal.h"
#include "win32ui/theme/theme_internal.h"
#include "win32ui/window_metrics.h"

#include <dwmapi.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <memory>
#include <string>

namespace win32ui
{

namespace
{

constexpr UINT_PTR kComboSubclassId = 1;
constexpr UINT_PTR kGroupBoxSubclassId = 2;
constexpr UINT_PTR kTreeSubclassId = 3;
// check box text and edge spacing
constexpr int kCheckPaddingDip = 4;
constexpr int kGroupTextInsetDip = 7;

void AllowDarkForWindow(HWND window, bool dark)
{
    static const auto allow = reinterpret_cast<BOOL(WINAPI*)(HWND, BOOL)>(UxthemeOrdinal(133));
    if (window && allow)
    {
        allow(window, dark ? TRUE : FALSE);
    }
}

void SetCaptionDark(HWND window, bool dark)
{
    // DWMWA_USE_IMMERSIVE_DARK_MODE is 20 from 20H1 on and 19 before
    const BOOL value = dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(window, 20, &value, sizeof(value))))
    {
        DwmSetWindowAttribute(window, 19, &value, sizeof(value));
    }
}

// visual style class for the current theme
void ThemeClass(HWND window, const wchar_t* dark_class = L"DarkMode_Explorer", const wchar_t* light_class = L"Explorer")
{
    if (!window)
    {
        return;
    }
    const bool dark = CurrentThemeColors().dark;
    AllowDarkForWindow(window, dark);
    SetWindowTheme(window, dark ? dark_class : light_class, nullptr);
}

LRESULT DrawCheckBox(const NMCUSTOMDRAW* draw)
{
    const LONG_PTR style = GetWindowLongPtrW(draw->hdr.hwndFrom, GWL_STYLE);
    const int type = static_cast<int>(style & BS_TYPEMASK);
    const bool is_checkbox = type == BS_AUTOCHECKBOX || type == BS_CHECKBOX || type == BS_AUTO3STATE || type == BS_3STATE;
    const bool is_radio = type == BS_AUTORADIOBUTTON || type == BS_RADIOBUTTON;
    if ((!is_checkbox && !is_radio) || draw->dwDrawStage != CDDS_PREPAINT)
    {
        return CDRF_DODEFAULT;
    }

    HDC hdc = draw->hdc;
    const RECT rect = draw->rc;
    const ThemeColors& colors = CurrentThemeColors();
    HBRUSH fill = ThemeBrush(ThemeFill::background);
    if (HWND parent = GetParent(draw->hdr.hwndFrom))
    {
        if (const auto parent_fill = reinterpret_cast<HBRUSH>(SendMessageW(parent, WM_CTLCOLORBTN, reinterpret_cast<WPARAM>(hdc), reinterpret_cast<LPARAM>(draw->hdr.hwndFrom))))
        {
            fill = parent_fill;
        }
    }
    FillRect(hdc, &rect, fill);

    const int part = is_radio ? BP_RADIOBUTTON : BP_CHECKBOX;
    const bool disabled = (draw->uItemState & CDIS_DISABLED) != 0;
    const bool pressed = (draw->uItemState & CDIS_SELECTED) != 0;
    const bool hot = (draw->uItemState & CDIS_HOT) != 0;
    const bool focused = (draw->uItemState & CDIS_FOCUS) != 0;
    const bool show_cues = (draw->uItemState & CDIS_SHOWKEYBOARDCUES) != 0;
    const LRESULT check = SendMessageW(draw->hdr.hwndFrom, BM_GETCHECK, 0, 0);
    const bool checked = check == BST_CHECKED;
    const bool mixed = check == BST_INDETERMINATE;
    int state = 0;
    if (is_checkbox)
    {
        state = disabled  ? (checked ? CBS_CHECKEDDISABLED : mixed ? CBS_MIXEDDISABLED
                                                                   : CBS_UNCHECKEDDISABLED)
                : pressed ? (checked ? CBS_CHECKEDPRESSED : mixed ? CBS_MIXEDPRESSED
                                                                  : CBS_UNCHECKEDPRESSED)
                : hot     ? (checked ? CBS_CHECKEDHOT : mixed ? CBS_MIXEDHOT
                                                              : CBS_UNCHECKEDHOT)
                          : (checked ? CBS_CHECKEDNORMAL : mixed ? CBS_MIXEDNORMAL
                                                                 : CBS_UNCHECKEDNORMAL);
    }
    else
    {
        state = disabled  ? (checked ? RBS_CHECKEDDISABLED : RBS_UNCHECKEDDISABLED)
                : pressed ? (checked ? RBS_CHECKEDPRESSED : RBS_UNCHECKEDPRESSED)
                : hot     ? (checked ? RBS_CHECKEDHOT : RBS_UNCHECKEDHOT)
                          : (checked ? RBS_CHECKEDNORMAL : RBS_UNCHECKEDNORMAL);
    }

    HTHEME button_theme = OpenThemeData(draw->hdr.hwndFrom, L"Button");
    SIZE box_size = {};
    if (button_theme)
    {
        GetThemePartSize(button_theme, hdc, part, state, nullptr, TS_TRUE, &box_size);
    }
    if (box_size.cx <= 0 || box_size.cy <= 0)
    {
        const UINT dpi = WindowDpi(draw->hdr.hwndFrom);
        box_size.cx = SystemMetricForDpi(SM_CXMENUCHECK, dpi);
        box_size.cy = SystemMetricForDpi(SM_CYMENUCHECK, dpi);
    }
    const int padding = Scale(kCheckPaddingDip, WindowDpi(draw->hdr.hwndFrom));
    const bool left_text = (style & BS_LEFTTEXT) != 0;
    RECT box = rect;
    box.left = left_text ? rect.right - padding - box_size.cx : rect.left + padding;
    box.right = box.left + box_size.cx;
    box.top = rect.top + (rect.bottom - rect.top - box_size.cy) / 2;
    box.bottom = box.top + box_size.cy;
    if (button_theme)
    {
        DrawThemeBackground(button_theme, hdc, part, state, &box, nullptr);
    }
    else
    {
        DrawFrameControl(hdc, &box, DFC_BUTTON, (is_radio ? DFCS_BUTTONRADIO : DFCS_BUTTONCHECK) | (checked ? DFCS_CHECKED : 0) | (disabled ? DFCS_INACTIVE : 0));
    }

    const std::wstring text = WindowText(draw->hdr.hwndFrom);
    RECT text_rect = rect;
    if (left_text)
    {
        text_rect.right = box.left - padding;
        text_rect.left += padding;
    }
    else
    {
        text_rect.left = box.right + padding;
        text_rect.right -= padding;
    }
    const COLORREF text_color = disabled ? colors.muted_text : colors.text;
    const UINT format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | (show_cues ? 0 : DT_HIDEPREFIX);
    if (button_theme)
    {
        DTTOPTS options = {};
        options.dwSize = sizeof(options);
        options.dwFlags = DTT_TEXTCOLOR;
        options.crText = text_color;
        DrawThemeTextEx(button_theme, hdc, part, state, text.c_str(), -1, format, &text_rect, &options);
        CloseThemeData(button_theme);
    }
    else
    {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, text_color);
        DrawTextW(hdc, text.c_str(), -1, &text_rect, format);
    }

    if (focused && show_cues)
    {
        RECT focus_rect = text_rect;
        RECT measured = text_rect;
        TEXTMETRICW metrics = {};
        if (DrawTextW(hdc, text.c_str(), -1, &measured, format | DT_CALCRECT) > 0 && GetTextMetricsW(hdc, &metrics))
        {
            focus_rect.right = std::min(focus_rect.right, measured.right);
            const LONG top = (rect.top + rect.bottom - (measured.bottom - measured.top)) / 2;
            focus_rect.top = top + metrics.tmInternalLeading;
            focus_rect.bottom = top + metrics.tmHeight;
        }
        InflateRect(&focus_rect, 1, 1);
        DrawFocusRect(hdc, &focus_rect);
    }
    return CDRF_SKIPDEFAULT;
}

struct ComboState
{
    bool hot = false;
};

void PaintComboBox(HWND combo, HDC hdc, const ComboState* state)
{
    RECT rect = {};
    GetClientRect(combo, &rect);
    if (rect.right <= rect.left || rect.bottom <= rect.top)
    {
        return;
    }
    const ThemeColors& colors = CurrentThemeColors();
    const bool enabled = IsWindowEnabled(combo) != FALSE;
    const bool hot = state && state->hot && enabled;
    const auto font = reinterpret_cast<HFONT>(SendMessageW(combo, WM_GETFONT, 0, 0));
    const HGDIOBJ old_font = font ? SelectObject(hdc, font) : nullptr;
    SetBkMode(hdc, TRANSPARENT);

    COMBOBOXINFO info = {sizeof(info)};
    RECT button = rect;
    if (GetComboBoxInfo(combo, &info))
    {
        button = info.rcButton;
    }
    else
    {
        button.left = rect.right - SystemMetricForDpi(SM_CXVSCROLL, WindowDpi(combo));
    }
    if (button.left > rect.left)
    {
        button.left -= 1;
    }

    const LONG_PTR kind = GetWindowLongPtrW(combo, GWL_STYLE) & CBS_DROPDOWNLIST;
    const bool focused = (kind == CBS_DROPDOWNLIST && GetFocus() == combo) || (kind == CBS_DROPDOWN && info.hwndItem && GetFocus() == info.hwndItem);
    const COLORREF border = !enabled ? colors.border : focused ? colors.focus
                                                   : hot       ? colors.hover
                                                               : colors.border;
    const COLORREF text = enabled ? colors.text : colors.muted_text;
    const COLORREF fill = hot ? colors.hover : colors.surface;

    HTHEME combo_theme = OpenThemeData(combo, VSCLASS_COMBOBOX);
    FillRect(hdc, kind == CBS_DROPDOWNLIST ? &rect : &button, CachedBrush(fill));
    if (combo_theme)
    {
        RECT arrow = button;
        OffsetRect(&arrow, 0, -1);
        DrawThemeBackground(combo_theme, hdc, CP_DROPDOWNBUTTONRIGHT, enabled ? CBXSR_NORMAL : CBXSR_DISABLED, &arrow, nullptr);
    }
    else
    {
        const HGDIOBJ old_pen = SelectObject(hdc, CachedPen(text, 1));
        const HGDIOBJ old_brush = SelectObject(hdc, CachedBrush(text));
        const int mid_x = (button.left + button.right) / 2;
        const int mid_y = (button.top + button.bottom) / 2;
        const POINT arrow[3] = {{mid_x - 4, mid_y - 2}, {mid_x + 4, mid_y - 2}, {mid_x, mid_y + 3}};
        Polygon(hdc, arrow, 3);
        SelectObject(hdc, old_brush);
        SelectObject(hdc, old_pen);
    }

    if (kind == CBS_DROPDOWNLIST)
    {
        std::wstring label;
        const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
        const int length = index != CB_ERR ? static_cast<int>(SendMessageW(combo, CB_GETLBTEXTLEN, index, 0)) : CB_ERR;
        if (length > 0)
        {
            label.resize(static_cast<size_t>(length) + 1);
            label.resize(SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(label.data())) == CB_ERR ? 0 : wcslen(label.c_str()));
        }
        else if (length == CB_ERR)
        {
            label = WindowText(combo);
        }
        RECT text_rect = info.rcItem;
        InflateRect(&text_rect, -2, 0);
        const UINT format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
        if (combo_theme)
        {
            DTTOPTS options = {};
            options.dwSize = sizeof(options);
            options.dwFlags = DTT_TEXTCOLOR;
            options.crText = text;
            DrawThemeTextEx(combo_theme, hdc, CP_DROPDOWNITEM, enabled ? CBXSR_NORMAL : CBXSR_DISABLED, label.c_str(), -1, format, &text_rect, &options);
        }
        else
        {
            SetTextColor(hdc, text);
            DrawTextW(hdc, label.c_str(), -1, &text_rect, format);
        }
        if (enabled && focused && SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0) == FALSE)
        {
            DrawFocusRect(hdc, &info.rcItem);
        }
    }
    else
    {
        const POINT edge[] = {{button.left - 1, button.top}, {button.left - 1, button.bottom}};
        const HGDIOBJ old_pen = SelectObject(hdc, CachedPen(border, 1));
        Polyline(hdc, edge, static_cast<int>(std::size(edge)));
        SelectObject(hdc, old_pen);
    }

    const HGDIOBJ old_pen = SelectObject(hdc, CachedPen(border, 1));
    const HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    if (combo_theme)
    {
        CloseThemeData(combo_theme);
    }
    if (old_font)
    {
        SelectObject(hdc, old_font);
    }
}

LRESULT CALLBACK ComboProc(HWND combo, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data)
{
    auto* state = reinterpret_cast<ComboState*>(data);
    const bool paint = (GetWindowLongPtrW(combo, GWL_STYLE) & CBS_DROPDOWNLIST) != CBS_SIMPLE;
    switch (message)
    {
    case WM_NCDESTROY:
        RemoveWindowSubclass(combo, ComboProc, kComboSubclassId);
        delete state;
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        if (paint)
        {
            return reinterpret_cast<LRESULT>(ThemeControlColor(message, reinterpret_cast<HDC>(wparam), reinterpret_cast<HWND>(lparam)));
        }
        break;
    case WM_MOUSEMOVE:
        if (!state->hot)
        {
            state->hot = true;
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, combo, 0};
            TrackMouseEvent(&track);
            InvalidateRect(combo, nullptr, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        if (state->hot)
        {
            state->hot = false;
            InvalidateRect(combo, nullptr, FALSE);
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(combo, nullptr, FALSE);
        break;
    case WM_ERASEBKGND:
        if (paint)
        {
            return 1;
        }
        break;
    case WM_PRINTCLIENT:
        if (paint)
        {
            PaintComboBox(combo, reinterpret_cast<HDC>(wparam), state);
            return 0;
        }
        break;
    case WM_PAINT:
        if (paint)
        {
            // editable combo text paints over the buffer
            if ((GetWindowLongPtrW(combo, GWL_STYLE) & CBS_DROPDOWNLIST) != CBS_DROPDOWN)
            {
                PaintBuffered(combo);
                return 0;
            }
            PAINTSTRUCT paint_struct = {};
            PaintComboBox(combo, BeginPaint(combo, &paint_struct), state);
            EndPaint(combo, &paint_struct);
            return 0;
        }
        break;
    default:
        break;
    }
    return DefSubclassProc(combo, message, wparam, lparam);
}

void PaintGroupBox(HWND group, HDC hdc)
{
    const ThemeColors& colors = CurrentThemeColors();
    RECT rect = {};
    GetClientRect(group, &rect);
    FillRect(hdc, &rect, ThemeBrush(ThemeFill::background));
    const auto font = reinterpret_cast<HFONT>(SendMessageW(group, WM_GETFONT, 0, 0));
    const HGDIOBJ old_font = font ? SelectObject(hdc, font) : nullptr;
    const std::wstring text = WindowText(group);
    SIZE text_size = {};
    GetTextExtentPoint32W(hdc, text.empty() ? L"M" : text.c_str(), text.empty() ? 1 : static_cast<int>(text.size()), &text_size);
    RECT frame = rect;
    frame.top += text_size.cy / 2;
    RECT text_rect = rect;
    if (!text.empty())
    {
        const bool centered = (GetWindowLongPtrW(group, GWL_STYLE) & BS_CENTER) == BS_CENTER;
        text_rect.left += centered ? (rect.right - rect.left - text_size.cx) / 2 : Scale(kGroupTextInsetDip, WindowDpi(group));
        text_rect.right = text_rect.left + text_size.cx + 4;
        text_rect.bottom = text_rect.top + text_size.cy;
        ExcludeClipRect(hdc, text_rect.left, text_rect.top, text_rect.right, text_rect.bottom);
    }
    const HGDIOBJ old_pen = SelectObject(hdc, CachedPen(colors.border, 1));
    const HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, frame.left, frame.top, frame.right, frame.bottom);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    SelectClipRgn(hdc, nullptr);
    if (!text.empty())
    {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, IsWindowEnabled(group) ? colors.text : colors.muted_text);
        DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &text_rect, DT_SINGLELINE | DT_LEFT | DT_NOPREFIX);
    }
    if (old_font)
    {
        SelectObject(hdc, old_font);
    }
}

LRESULT CALLBACK GroupBoxProc(HWND group, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR)
{
    switch (message)
    {
    case WM_NCDESTROY:
        RemoveWindowSubclass(group, GroupBoxProc, kGroupBoxSubclassId);
        break;
    case WM_ENABLE:
        {
            // group frame shows through controls, repaint the parent area
            const LRESULT result = DefSubclassProc(group, message, wparam, lparam);
            RECT rect = {};
            if (HWND parent = GetParent(group); parent && GetWindowRect(group, &rect))
            {
                MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rect), 2);
                RedrawWindow(parent, &rect, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            }
            return result;
        }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintBuffered(group);
        return 0;
    case WM_PRINTCLIENT:
        PaintGroupBox(group, reinterpret_cast<HDC>(wparam));
        return 0;
    default:
        break;
    }
    return DefSubclassProc(group, message, wparam, lparam);
}

// dark trees skip native erase to avoid a window color flash, printing fills the panel color
LRESULT CALLBACK TreeProc(HWND tree, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR)
{
    switch (message)
    {
    case WM_NCDESTROY:
        RemoveWindowSubclass(tree, TreeProc, kTreeSubclassId);
        break;
    case WM_ERASEBKGND:
        if (ThemeDark())
        {
            return 1;
        }
        break;
    case WM_PRINTCLIENT:
        if (ThemeDark())
        {
            RECT rect = {};
            GetClientRect(tree, &rect);
            FillRect(reinterpret_cast<HDC>(wparam), &rect, ThemeBrush(ThemeFill::panel));
        }
        break;
    default:
        break;
    }
    return DefSubclassProc(tree, message, wparam, lparam);
}

void Subclass(HWND window, SUBCLASSPROC proc, UINT_PTR id)
{
    if (!GetWindowSubclass(window, proc, id, nullptr))
    {
        SetWindowSubclass(window, proc, id, 0);
    }
}

void ApplyControl(HWND window)
{
    wchar_t name[64] = {};
    GetClassNameW(window, name, static_cast<int>(std::size(name)));
    const ThemeColors& colors = CurrentThemeColors();
    // theme every window so plain scroll bars follow too
    // list headers keep the items view theme because it supplies the sort arrow
    if (_wcsicmp(name, WC_HEADERW) == 0)
    {
        ThemeClass(window, L"DarkMode_ItemsView", L"ItemsView");
    }
    else
    {
        ThemeClass(window);
    }
    if (_wcsicmp(name, WC_COMBOBOXW) == 0)
    {
        if (!GetWindowSubclass(window, ComboProc, kComboSubclassId, nullptr))
        {
            auto combo_state = std::make_unique<ComboState>();
            if (SetWindowSubclass(window, ComboProc, kComboSubclassId, reinterpret_cast<DWORD_PTR>(combo_state.get())))
            {
                combo_state.release();
            }
        }
        COMBOBOXINFO info = {sizeof(info)};
        if (GetComboBoxInfo(window, &info))
        {
            ThemeClass(info.hwndList, L"CFD");
        }
        ThemeClass(window, L"CFD");
    }
    else if (_wcsicmp(name, WC_EDITW) == 0)
    {
        RedrawWindow(window, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
    }
    else if (_wcsicmp(name, WC_LISTVIEWW) == 0)
    {
        ListView_SetBkColor(window, colors.panel);
        ListView_SetTextBkColor(window, colors.panel);
        ListView_SetTextColor(window, colors.text);
        ThemeClass(ListView_GetToolTips(window));
        if (HWND header = ListView_GetHeader(window))
        {
            ThemeClass(header, L"DarkMode_ItemsView", L"ItemsView");
            InvalidateRect(header, nullptr, TRUE);
        }
    }
    else if (_wcsicmp(name, WC_TREEVIEWW) == 0)
    {
        TreeView_SetBkColor(window, colors.panel);
        TreeView_SetTextColor(window, colors.text);
        TreeView_SetLineColor(window, colors.border);
        Subclass(window, TreeProc, kTreeSubclassId);
    }
    else if (_wcsicmp(name, WC_BUTTONW) == 0 && (GetWindowLongPtrW(window, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX)
    {
        Subclass(window, GroupBoxProc, kGroupBoxSubclassId);
    }
    else if (_wcsicmp(name, TOOLBARCLASSNAMEW) == 0)
    {
        ThemeClass(reinterpret_cast<HWND>(SendMessageW(window, TB_GETTOOLTIPS, 0, 0)));
    }
    InvalidateRect(window, nullptr, TRUE);
}

void RefreshLibraryControl(HWND window)
{
    wchar_t name[64] = {};
    GetClassNameW(window, name, static_cast<int>(std::size(name)));
    // refresh leaves controls outside the library unchanged
    if (_wcsicmp(name, WC_LISTVIEWW) == 0)
    {
        RefreshListView(window);
    }
    else if (_wcsicmp(name, TOOLBARCLASSNAMEW) == 0)
    {
        RefreshToolbar(window);
    }
    else if (_wcsicmp(name, STATUSCLASSNAMEW) == 0)
    {
        RefreshStatusBar(window);
    }
    else if (_wcsicmp(name, WC_TABCONTROLW) == 0)
    {
        RefreshTabStrip(window);
    }
    else if (_wcsicmp(name, L"win32ui.Splitter") == 0)
    {
        RefreshSplitter(window);
    }
}

LPCWSTR RoleProperty()
{
    // use an integer atom, string names add an atom reference on every SetProp
    static const LPCWSTR atom = MAKEINTATOM(GlobalAddAtomW(L"win32ui.TextRole"));
    return atom;
}

} // namespace

void ApplyThemeToWindow(HWND window)
{
    if (!window || !IsWindow(window))
    {
        return;
    }
    if ((GetWindowLongPtrW(window, GWL_STYLE) & WS_CAPTION) == WS_CAPTION)
    {
        const bool dark = ThemeDark();
        AllowDarkForWindow(window, dark);
        SetCaptionDark(window, dark);
        SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    ApplyControl(window);
    EnumChildWindows(
        window,
        [](HWND child, LPARAM) -> BOOL {
            ApplyControl(child);
            return TRUE;
        },
        0
    );
}

void SetThemeClass(HWND window, const wchar_t* dark_class)
{
    ThemeClass(window, dark_class, L"Explorer");
}

void ReapplyTheme(HWND top_level)
{
    // SetWindowTheme sends WM_THEMECHANGED, reapplying from that message would recurse
    static bool reapplying = false;
    if (!top_level || !IsWindow(top_level) || reapplying)
    {
        return;
    }
    reapplying = true;
    ApplyThemeToWindow(top_level);
    EnumChildWindows(
        top_level,
        [](HWND child, LPARAM) -> BOOL {
            RefreshLibraryControl(child);
            return TRUE;
        },
        0
    );
    // tooltips are owned popups, not children, include those owned by dialogs too
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND window, LPARAM parameter) -> BOOL {
            if (GetAncestor(window, GA_ROOTOWNER) == reinterpret_cast<HWND>(parameter) && ClassIs(window, TOOLTIPS_CLASSW))
            {
                ThemeClass(window);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(top_level)
    );
    ReapplyThemeToDialogs();
    RedrawWindow(top_level, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    reapplying = false;
}

HBRUSH ThemeControlColor(UINT message, HDC hdc, HWND control)
{
    const ThemeColors& colors = CurrentThemeColors();
    switch (message)
    {
    case WM_CTLCOLOREDIT:
        SetTextColor(hdc, colors.text);
        SetBkColor(hdc, colors.field);
        return ThemeBrush(ThemeFill::field);
    case WM_CTLCOLORLISTBOX:
        SetTextColor(hdc, colors.text);
        SetBkColor(hdc, colors.surface);
        return ThemeBrush(ThemeFill::surface);
    case WM_CTLCOLORSTATIC:
        {
            if (ClassIs(control, WC_EDITW))
            {
                // read only or disabled edits
                const bool enabled = IsWindowEnabled(control) != FALSE;
                SetTextColor(hdc, enabled ? colors.text : colors.muted_text);
                SetBkColor(hdc, enabled ? colors.field : colors.background);
                return ThemeBrush(enabled ? ThemeFill::field : ThemeFill::background);
            }
            const TextRole role = GetTextRole(control);
            const COLORREF color = control && !IsWindowEnabled(control) ? colors.muted_text
                                   : role == TextRole::error            ? colors.error
                                   : role == TextRole::disabled         ? colors.muted_text
                                                                        : colors.text;
            SetTextColor(hdc, color);
            SetBkColor(hdc, colors.background);
            SetBkMode(hdc, TRANSPARENT);
            return ThemeBrush(ThemeFill::background);
        }
    case WM_CTLCOLORBTN:
        SetTextColor(hdc, control && !IsWindowEnabled(control) ? colors.muted_text : colors.text);
        SetBkColor(hdc, colors.surface);
        SetBkMode(hdc, TRANSPARENT);
        return ThemeBrush(ThemeFill::background);
    default:
        SetTextColor(hdc, colors.text);
        SetBkColor(hdc, colors.background);
        SetBkMode(hdc, TRANSPARENT);
        return ThemeBrush(ThemeFill::background);
    }
}

bool ThemeCustomDraw(NMHDR* header, LRESULT* result)
{
    if (!header || !result || header->code != NM_CUSTOMDRAW)
    {
        return false;
    }
    if (ClassIs(header->hwndFrom, WC_BUTTONW))
    {
        const LRESULT drawn = DrawCheckBox(reinterpret_cast<const NMCUSTOMDRAW*>(header));
        if (drawn == CDRF_DODEFAULT)
        {
            return false;
        }
        *result = drawn;
        return true;
    }
    if (ClassIs(header->hwndFrom, WC_TREEVIEWW) && ThemeDark())
    {
        // dark visual style gives hovered items no background
        auto* draw = reinterpret_cast<NMTVCUSTOMDRAW*>(header);
        const ThemeColors& colors = CurrentThemeColors();
        *result = CDRF_DODEFAULT;
        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
        {
            *result = CDRF_NOTIFYITEMDRAW;
        }
        else if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && !(draw->nmcd.uItemState & CDIS_SELECTED))
        {
            draw->clrText = colors.text;
            draw->clrTextBk = (draw->nmcd.uItemState & CDIS_HOT) ? colors.hover : colors.panel;
            *result = CDRF_NEWFONT;
        }
        return true;
    }
    return false;
}

void SetTextRole(HWND label, TextRole role)
{
    if (!label || !IsWindow(label) || GetTextRole(label) == role)
    {
        return;
    }
    SetPropW(label, RoleProperty(), reinterpret_cast<HANDLE>(static_cast<uintptr_t>(role) + 1));
    InvalidateRect(label, nullptr, TRUE);
}

TextRole GetTextRole(HWND label)
{
    const uintptr_t value = label ? reinterpret_cast<uintptr_t>(GetPropW(label, RoleProperty())) : 0;
    return value ? static_cast<TextRole>(value - 1) : TextRole::normal;
}

DialogTheme ThemeDialogHooks()
{
    DialogTheme hooks;
    hooks.apply = ApplyThemeToWindow;
    hooks.control_color = ThemeControlColor;
    hooks.custom_draw = ThemeCustomDraw;
    return hooks;
}

} // namespace win32ui
