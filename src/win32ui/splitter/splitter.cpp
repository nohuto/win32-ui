// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/splitter/splitter.h"

#include "win32ui/accessible_name.h"
#include "win32ui/gdi/buffered_paint.h"
#include "win32ui/gdi/gdi_cache.h"
#include "win32ui/splitter/splitter_internal.h"
#include "win32ui/theme/palette.h"
#include "win32ui/window_metrics.h"

#include <windowsx.h>

#include <algorithm>
#include <memory>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace win32ui
{

namespace
{

constexpr wchar_t kSplitterClass[] = L"win32ui.Splitter";
constexpr int kGripInsetDip = 4;
constexpr int kKeyStepDip = 8;

struct SplitterState
{
    const SplitterHooks* hooks = nullptr;
    bool columns = false;
    bool resizes_after = false;
    bool dragging = false;
    // drag start screen coordinate and pane size, plus last size sent
    int start = 0;
    int start_size = 0;
    int size = 0;
};

// splitter colors
struct SplitterColors
{
    COLORREF background = CLR_NONE;
    COLORREF grip = CLR_NONE;
    COLORREF focus = CLR_NONE;
};

SplitterColors ColorsOf()
{
    const ThemeColors& theme = CurrentThemeColors();
    return SplitterColors{theme.panel, theme.border, theme.focus};
}

void Paint(HWND splitter, const SplitterState* state, HDC hdc)
{
    const SplitterColors colors = ColorsOf();
    RECT client = {};
    GetClientRect(splitter, &client);
    if (GetFocus() == splitter)
    {
        FillRect(hdc, &client, CachedBrush(colors.focus));
        return;
    }
    FillRect(hdc, &client, CachedBrush(colors.background));
    const UINT dpi = WindowDpi(splitter);
    const int inset = Scale(kGripInsetDip, dpi);
    const int middle = (state->columns ? client.right : client.bottom) / 2;
    const int end = (state->columns ? client.bottom : client.right) - inset;
    if (end <= inset)
    {
        return;
    }
    const HGDIOBJ old_pen = SelectObject(hdc, CachedPen(colors.grip, std::max(1, Scale(1, dpi))));
    MoveToEx(hdc, state->columns ? middle : inset, state->columns ? inset : middle, nullptr);
    LineTo(hdc, state->columns ? middle : end, state->columns ? end : middle);
    SelectObject(hdc, old_pen);
}

// use screen coordinates as layout moves the bar under the mouse
int Along(HWND splitter, const SplitterState* state, LPARAM lparam)
{
    POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    ClientToScreen(splitter, &point);
    return state->columns ? point.x : point.y;
}

int SizeAt(const SplitterState* state, int along)
{
    const int delta = along - state->start;
    return state->start_size + (state->resizes_after ? -delta : delta);
}

LRESULT CALLBACK SplitterProc(HWND splitter, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* state = reinterpret_cast<SplitterState*>(GetWindowLongPtrW(splitter, 0));
    if (!state)
    {
        return DefWindowProcW(splitter, message, wparam, lparam);
    }
    switch (message)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintBuffered(splitter);
        return 0;
    case WM_PRINTCLIENT:
        Paint(splitter, state, reinterpret_cast<HDC>(wparam));
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT)
        {
            SetCursor(LoadCursorW(nullptr, state->columns ? IDC_SIZEWE : IDC_SIZENS));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        if (!state->dragging && state->hooks && state->hooks->size && state->hooks->resize)
        {
            state->start = Along(splitter, state, lparam);
            state->start_size = state->hooks->size(state->hooks->context, splitter);
            state->size = state->start_size;
            state->dragging = true;
            SetCapture(splitter);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (state->dragging)
        {
            if (const int size = SizeAt(state, Along(splitter, state, lparam)); size != state->size)
            {
                state->size = size;
                // resize hook may destroy the bar, dont access it afterward
                state->hooks->resize(state->hooks->context, splitter, size, false);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (state->dragging)
        {
            const int size = SizeAt(state, Along(splitter, state, lparam));
            state->dragging = false;
            ReleaseCapture();
            state->hooks->resize(state->hooks->context, splitter, size, true);
        }
        return 0;
    case WM_CAPTURECHANGED:
        // capture moved to another window, keep the last size
        if (state->dragging)
        {
            state->dragging = false;
            state->hooks->resize(state->hooks->context, splitter, state->size, true);
        }
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(splitter, nullptr, FALSE);
        break;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_KEYDOWN:
        {
            const bool back = state->columns ? wparam == VK_LEFT : wparam == VK_UP;
            const bool forward = state->columns ? wparam == VK_RIGHT : wparam == VK_DOWN;
            if ((back || forward) && !state->dragging && state->hooks && state->hooks->size && state->hooks->resize)
            {
                // forward step grows the preceding pane
                const int step = Scale(kKeyStepDip, WindowDpi(splitter)) * (forward ? 1 : -1);
                const int size = state->hooks->size(state->hooks->context, splitter) + (state->resizes_after ? -step : step);
                state->hooks->resize(state->hooks->context, splitter, size, true);
                return 0;
            }
            break;
        }
    case WM_NCDESTROY:
        SetWindowLongPtrW(splitter, 0, 0);
        delete state;
        break;
    default:
        break;
    }
    return DefWindowProcW(splitter, message, wparam, lparam);
}

bool RegisterSplitterClass(HINSTANCE instance)
{
    WNDCLASSEXW window_class = {sizeof(window_class)};
    if (GetClassInfoExW(instance, kSplitterClass, &window_class))
    {
        return true;
    }
    window_class = {sizeof(window_class)};
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = SplitterProc;
    window_class.hInstance = instance;
    window_class.cbWndExtra = sizeof(SplitterState*);
    window_class.lpszClassName = kSplitterClass;
    // another UI thread may register it between the lookup and here
    return RegisterClassExW(&window_class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace

HWND CreateSplitter(HWND parent, int id, const wchar_t* name, const SplitterHooks* hooks, bool columns, bool resizes_after)
{
    const auto instance = reinterpret_cast<HINSTANCE>(&__ImageBase);
    if (!RegisterSplitterClass(instance))
    {
        return nullptr;
    }
    HWND splitter = CreateWindowExW(0, kSplitterClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    if (!splitter)
    {
        return nullptr;
    }
    auto state = std::make_unique<SplitterState>();
    state->hooks = hooks;
    state->columns = columns;
    state->resizes_after = resizes_after;
    SetWindowLongPtrW(splitter, 0, reinterpret_cast<LONG_PTR>(state.release()));
    SetAccessibleName(splitter, name ? name : L"");
    return splitter;
}

void RefreshSplitter(HWND splitter)
{
    InvalidateRect(splitter, nullptr, FALSE);
}

} // namespace win32ui
