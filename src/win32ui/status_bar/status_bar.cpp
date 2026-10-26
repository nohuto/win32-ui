// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#include "win32ui/status_bar/status_bar.h"

#include "win32ui/accessible_name.h"
#include "win32ui/dialog/dialog_layout_internal.h"
#include "win32ui/gdi/buffered_paint.h"
#include "win32ui/gdi/fonts_internal.h"
#include "win32ui/gdi/gdi_cache.h"
#include "win32ui/theme/palette.h"
#include "win32ui/theme/palette_internal.h"
#include "win32ui/window_metrics.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <vsstyle.h>

#include <algorithm>
#include <memory>

namespace win32ui
{

namespace
{

constexpr UINT_PTR kStatusSubclassId = 1;
constexpr UINT_PTR kProgressSubclassId = 1;
constexpr UINT_PTR kMessageTimer = 1;
// padding beside sample text
constexpr int kPartMarginDip = 18;
constexpr int kHeightMarginDip = 5;
constexpr int kTipMaxWidthDip = 560;
// text inset from part border
constexpr int kTextInset = 4;

struct StatusState
{
    const StatusBarHooks* hooks = nullptr;
    std::vector<StatusPart> parts;
    HWND tip = nullptr;
    std::wstring tip_text;
    std::wstring message;
    HWND progress = nullptr;
    int progress_part = -1;
};

LRESULT CALLBACK StatusProc(HWND bar, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data);

StatusState* StateOf(HWND bar)
{
    DWORD_PTR data = 0;
    return bar && GetWindowSubclass(bar, StatusProc, kStatusSubclassId, &data) ? reinterpret_cast<StatusState*>(data) : nullptr;
}

// status bar colors, read from the palette at paint time
struct StatusBarColors
{
    COLORREF background = CLR_NONE;
    COLORREF text = CLR_NONE;
    COLORREF border = CLR_NONE;
    // false keeps the native drawing
    bool custom_paint = false;
    // progress block color during custom painting
    COLORREF accent = CLR_NONE;
};

StatusBarColors ColorsOf()
{
    const ThemeColors& colors = CurrentThemeColors();
    // built in light theme keeps native status bar
    return StatusBarColors{colors.surface, colors.text, colors.border, ThemeStatusBarCustom(), colors.accent};
}

// classic progress bars add sunken edges on every theme change without a visual style, including system changes
LRESULT CALLBACK ProgressProc(HWND progress, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR)
{
    if (message == WM_THEMECHANGED)
    {
        const LRESULT result = DefSubclassProc(progress, message, wparam, lparam);
        const LONG_PTR ex_style = GetWindowLongPtrW(progress, GWL_EXSTYLE);
        if ((ex_style & WS_EX_STATICEDGE) && ColorsOf().custom_paint)
        {
            SetWindowLongPtrW(progress, GWL_EXSTYLE, ex_style & ~WS_EX_STATICEDGE);
            SetWindowPos(progress, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        return result;
    }
    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(progress, ProgressProc, kProgressSubclassId);
    }
    return DefSubclassProc(progress, message, wparam, lparam);
}

void ThemeProgress(const StatusState* state)
{
    if (!state->progress)
    {
        return;
    }
    if (const StatusBarColors colors = ColorsOf(); colors.custom_paint)
    {
        // no visual style provides a dark progress track, classic mode uses bar colors
        SetWindowTheme(state->progress, L"", L"");
        SendMessageW(state->progress, PBM_SETBKCOLOR, 0, colors.background);
        SendMessageW(state->progress, PBM_SETBARCOLOR, 0, colors.accent);
        return;
    }
    SetWindowTheme(state->progress, nullptr, nullptr);
    // classic mode without visual styles would keep the edges
    SendMessageW(state->progress, PBM_SETBKCOLOR, 0, CLR_DEFAULT);
    SendMessageW(state->progress, PBM_SETBARCOLOR, 0, CLR_DEFAULT);
    ApplyFrame(state->hooks, state->progress);
}

std::wstring PartText(HWND bar, int part)
{
    const int length = LOWORD(SendMessageW(bar, SB_GETTEXTLENGTHW, part, 0));
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0)
    {
        SendMessageW(bar, SB_GETTEXTW, part, reinterpret_cast<LPARAM>(text.data()));
    }
    text.resize(static_cast<size_t>(length));
    return text;
}

std::vector<int> TextWidths(HWND bar, const std::vector<std::wstring_view>& texts)
{
    std::vector<int> widths;
    HDC dc = GetDC(bar);
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(bar, WM_GETFONT, 0, 0));
    const HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    for (const std::wstring_view text : texts)
    {
        SIZE size = {};
        GetTextExtentPoint32W(dc, text.data(), static_cast<int>(text.size()), &size);
        widths.push_back(size.cx);
    }
    if (old)
    {
        SelectObject(dc, old);
    }
    ReleaseDC(bar, dc);
    return widths;
}

int TextRoom(HWND bar, const RECT& part)
{
    int borders[3] = {};
    SendMessageW(bar, SB_GETBORDERS, 0, reinterpret_cast<LPARAM>(borders));
    return static_cast<int>(part.right - part.left) - 2 * (borders[2] + kTextInset);
}

// match native bars, hide size grip while parent is maximized
bool GripShown(HWND bar)
{
    return (GetWindowLongPtrW(bar, GWL_STYLE) & SBARS_SIZEGRIP) && !IsZoomed(GetParent(bar));
}

void PlaceTips(HWND bar, const StatusState* state)
{
    const int parts = static_cast<int>(SendMessageW(bar, SB_GETPARTS, 0, 0));
    const int tools = static_cast<int>(SendMessageW(state->tip, TTM_GETTOOLCOUNT, 0, 0));
    for (int part = 0; part < std::max(parts, tools); ++part)
    {
        TTTOOLINFOW tool = {sizeof(tool)};
        tool.hwnd = bar;
        tool.uId = static_cast<UINT_PTR>(part);
        if (part >= parts)
        {
            SendMessageW(state->tip, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
            continue;
        }
        SendMessageW(bar, SB_GETRECT, part, reinterpret_cast<LPARAM>(&tool.rect));
        if (part < tools)
        {
            SendMessageW(state->tip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
        }
        else
        {
            tool.uFlags = TTF_SUBCLASS;
            tool.lpszText = LPSTR_TEXTCALLBACKW;
            SendMessageW(state->tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }
    }
}

void PlaceProgress(HWND bar, StatusState* state)
{
    if (!state->progress || state->progress_part < 0)
    {
        return;
    }
    RECT part = {};
    if (!SendMessageW(bar, SB_GETRECT, state->progress_part, reinterpret_cast<LPARAM>(&part)))
    {
        // removed progress part stays hidden until the app shows it again
        state->progress_part = -1;
        SendMessageW(state->progress, PBM_SETMARQUEE, FALSE, 0);
        ShowWindow(state->progress, SW_HIDE);
        return;
    }
    int borders[3] = {};
    SendMessageW(bar, SB_GETBORDERS, 0, reinterpret_cast<LPARAM>(borders));
    InflateRect(&part, -(borders[2] + kTextInset), -borders[1] - 2);
    SetWindowPos(state->progress, nullptr, part.left, part.top, std::max<int>(0, part.right - part.left), std::max<int>(0, part.bottom - part.top), SWP_NOZORDER | SWP_NOACTIVATE);
}

void LayoutParts(HWND bar, StatusState* state)
{
    if (!state->parts.empty())
    {
        RECT client = {};
        GetClientRect(bar, &client);
        const UINT dpi = WindowDpi(bar);
        std::vector<std::wstring_view> samples;
        for (const StatusPart& part : state->parts)
        {
            samples.push_back(part.fill ? std::wstring_view() : std::wstring_view(part.sample));
        }
        std::vector<int> widths = TextWidths(bar, samples);
        for (size_t index = 0; index < widths.size(); ++index)
        {
            const StatusPart& part = state->parts[index];
            widths[index] = part.fill ? 0 : std::max(widths[index] + Scale(kPartMarginDip, dpi), Scale(part.min_dip, dpi));
        }
        // parts before the fill start at the left, parts after it end at the size grip
        const size_t fill = static_cast<size_t>(std::ranges::find(state->parts, true, &StatusPart::fill) - state->parts.begin());
        std::vector<int> edges(widths.size());
        int edge = 0;
        for (size_t index = 0; index < std::min(fill, widths.size()); ++index)
        {
            edge += widths[index];
            edges[index] = edge;
        }
        if (fill < widths.size())
        {
            int end = std::max(0, static_cast<int>(client.right) - (GripShown(bar) ? SystemMetricForDpi(SM_CXVSCROLL, dpi) : 0));
            for (size_t index = widths.size() - 1; index > fill; --index)
            {
                edges[index] = end;
                end -= widths[index];
            }
            edges[fill] = end;
            // shrink the fill part first, then neighboring parts when space runs out
            for (size_t index = fill; index < widths.size(); ++index)
            {
                edges[index] = std::max(edges[index], index > 0 ? edges[index - 1] : 0);
            }
        }
        edges.back() = -1;
        SendMessageW(bar, SB_SETPARTS, edges.size(), reinterpret_cast<LPARAM>(edges.data()));
    }
    if (state->tip)
    {
        PlaceTips(bar, state);
    }
    PlaceProgress(bar, state);
}

// line height plus margin, native bars would keep their initial height
void SetMinHeight(HWND bar)
{
    SendMessageW(bar, SB_SETMINHEIGHT, WindowFontMetrics(bar).tmHeight + Scale(kHeightMarginDip, WindowDpi(bar)), 0);
    SendMessageW(bar, WM_SIZE, 0, 0);
}

void Paint(HWND bar, HDC hdc, const StatusBarColors& colors)
{
    RECT client = {};
    GetClientRect(bar, &client);
    FillRect(hdc, &client, CachedBrush(colors.background));
    int borders[3] = {};
    SendMessageW(bar, SB_GETBORDERS, 0, reinterpret_cast<LPARAM>(borders));
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(bar, WM_GETFONT, 0, 0));
    const HGDIOBJ old_font = font ? SelectObject(hdc, font) : nullptr;
    const HGDIOBJ old_pen = SelectObject(hdc, CachedPen(colors.border));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, colors.text);
    const int parts = std::max(1, static_cast<int>(SendMessageW(bar, SB_GETPARTS, 0, 0)));
    for (int index = 0; index < parts; ++index)
    {
        RECT part = {};
        if (!SendMessageW(bar, SB_GETRECT, index, reinterpret_cast<LPARAM>(&part)))
        {
            part = client;
        }
        if (index < parts - 1)
        {
            MoveToEx(hdc, part.right - 1, part.top + 2, nullptr);
            LineTo(hdc, part.right - 1, part.bottom - 2);
        }
        RECT text = part;
        InflateRect(&text, -(borders[2] + kTextInset), 0);
        const std::wstring content = PartText(bar, index);
        DrawTextW(hdc, content.c_str(), static_cast<int>(content.size()), &text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    if (GripShown(bar))
    {
        if (HTHEME theme = OpenThemeData(bar, VSCLASS_STATUS))
        {
            SIZE grip = {};
            GetThemePartSize(theme, hdc, SP_GRIPPER, 0, &client, TS_DRAW, &grip);
            const RECT grip_rect = {client.right - grip.cx, client.bottom - grip.cy, client.right, client.bottom};
            DrawThemeBackground(theme, hdc, SP_GRIPPER, 0, &grip_rect, nullptr);
            CloseThemeData(theme);
        }
    }
    MoveToEx(hdc, client.left, client.top, nullptr);
    LineTo(hdc, client.right, client.top);
    SelectObject(hdc, old_pen);
    if (old_font)
    {
        SelectObject(hdc, old_font);
    }
}

LRESULT CALLBACK StatusProc(HWND bar, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data)
{
    auto* state = reinterpret_cast<StatusState*>(data);
    switch (message)
    {
    case WM_ERASEBKGND:
        if (ColorsOf().custom_paint)
        {
            return 1;
        }
        break;
    case WM_PAINT:
        if (ColorsOf().custom_paint)
        {
            PaintBuffered(bar);
            return 0;
        }
        break;
    case WM_PRINTCLIENT:
        if (const StatusBarColors colors = ColorsOf(); colors.custom_paint)
        {
            Paint(bar, reinterpret_cast<HDC>(wparam), colors);
            return 0;
        }
        break;
    case WM_SIZE:
        {
            const LRESULT result = DefSubclassProc(bar, message, wparam, lparam);
            LayoutParts(bar, state);
            return result;
        }
    case WM_SETFONT:
        {
            const LRESULT result = DefSubclassProc(bar, message, wparam, lparam);
            SetMinHeight(bar);
            return result;
        }
    case WM_TIMER:
        if (wparam == kMessageTimer)
        {
            KillTimer(bar, kMessageTimer);
            state->message.clear();
            if (state->hooks && state->hooks->message_ended)
            {
                state->hooks->message_ended(state->hooks->context);
            }
            return 0;
        }
        break;
    case WM_NOTIFY:
        if (const auto* header = reinterpret_cast<NMHDR*>(lparam); header->hwndFrom == state->tip && header->code == TTN_GETDISPINFOW)
        {
            const int part = static_cast<int>(header->idFrom);
            state->tip_text = state->hooks && state->hooks->part_tip ? state->hooks->part_tip(state->hooks->context, part) : std::wstring();
            if (state->tip_text.empty())
            {
                // show full text only for truncated parts
                state->tip_text = PartText(bar, part);
                RECT rect = {};
                SendMessageW(bar, SB_GETRECT, part, reinterpret_cast<LPARAM>(&rect));
                if (state->tip_text.empty() || TextWidths(bar, {state->tip_text}).front() <= TextRoom(bar, rect))
                {
                    state->tip_text.clear();
                }
            }
            reinterpret_cast<NMTTDISPINFOW*>(lparam)->lpszText = state->tip_text.data();
            return 0;
        }
        break;
    case WM_NCDESTROY:
        // top level tooltip would otherwise outlive the bar
        if (state->tip && IsWindow(state->tip))
        {
            DestroyWindow(state->tip);
        }
        RemoveWindowSubclass(bar, StatusProc, kStatusSubclassId);
        delete state;
        break;
    default:
        break;
    }
    return DefSubclassProc(bar, message, wparam, lparam);
}

} // namespace

HWND CreateStatusBar(HWND parent, int id, const wchar_t* name, const StatusBarHooks* hooks)
{
    HWND bar = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    if (bar && !AttachStatusBar(bar, name, hooks))
    {
        DestroyWindow(bar);
        return nullptr;
    }
    return bar;
}

bool AttachStatusBar(HWND bar, const wchar_t* name, const StatusBarHooks* hooks)
{
    if (!bar)
    {
        return false;
    }
    StatusState* state = StateOf(bar);
    if (!state)
    {
        auto created = std::make_unique<StatusState>();
        if (!SetWindowSubclass(bar, StatusProc, kStatusSubclassId, reinterpret_cast<DWORD_PTR>(created.get())))
        {
            return false;
        }
        state = created.release();
        // top level owner, destroyed by the bar
        state->tip = CreateTooltip(bar);
    }
    state->hooks = hooks;
    SetAccessibleName(bar, name ? name : L"");
    RefreshStatusBar(bar);
    return true;
}

void SetStatusParts(HWND bar, const std::vector<StatusPart>& parts)
{
    if (StatusState* state = StateOf(bar); state && state->parts != parts)
    {
        state->parts = parts;
        LayoutParts(bar, state);
    }
}

void SetStatusText(HWND bar, int part, std::wstring_view text)
{
    if (PartText(bar, part) != text)
    {
        const std::wstring copy(text);
        SendMessageW(bar, SB_SETTEXTW, part, reinterpret_cast<LPARAM>(copy.c_str()));
    }
}

void ShowStatusMessage(HWND bar, std::wstring_view text, UINT ms)
{
    if (StatusState* state = StateOf(bar))
    {
        state->message = text;
        if (text.empty() || ms == 0)
        {
            KillTimer(bar, kMessageTimer);
        }
        else
        {
            SetTimer(bar, kMessageTimer, ms, nullptr);
        }
    }
}

std::wstring StatusMessage(HWND bar)
{
    const StatusState* state = StateOf(bar);
    return state ? state->message : std::wstring();
}

void ShowStatusProgress(HWND bar, int part, bool shown)
{
    StatusState* state = StateOf(bar);
    if (!state || (state->progress && state->progress_part == (shown ? part : -1)))
    {
        return;
    }
    if (shown && !state->progress)
    {
        state->progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_MARQUEE, 0, 0, 0, 0, bar, nullptr, nullptr, nullptr);
        if (state->progress)
        {
            SetWindowSubclass(state->progress, ProgressProc, kProgressSubclassId, 0);
        }
        // native bar painting would cover the progress bar
        SetWindowLongPtrW(bar, GWL_STYLE, GetWindowLongPtrW(bar, GWL_STYLE) | WS_CLIPCHILDREN);
        ThemeProgress(state);
    }
    if (!state->progress)
    {
        return;
    }
    state->progress_part = shown ? part : -1;
    PlaceProgress(bar, state);
    // missing parts keep progress hidden
    shown = state->progress_part >= 0;
    SendMessageW(state->progress, PBM_SETMARQUEE, shown ? TRUE : FALSE, 30);
    ShowWindow(state->progress, shown ? SW_SHOWNA : SW_HIDE);
}

void RefreshStatusBar(HWND bar)
{
    StatusState* state = StateOf(bar);
    if (!state)
    {
        return;
    }
    ApplyFrame(state->hooks, bar);
    ApplyFrame(state->hooks, state->tip);
    ThemeProgress(state);
    SendMessageW(state->tip, TTM_SETMAXTIPWIDTH, 0, Scale(kTipMaxWidthDip, WindowDpi(bar)));
    // WM_SIZE lays out the parts
    SetMinHeight(bar);
    InvalidateRect(bar, nullptr, TRUE);
}

} // namespace win32ui
