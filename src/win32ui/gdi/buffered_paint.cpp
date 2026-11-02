// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/gdi/buffered_paint.h"

#include <uxtheme.h>

namespace win32ui
{

void PaintBuffered(HWND window)
{
    PAINTSTRUCT paint = {};
    if (const HDC target = BeginPaint(window, &paint))
    {
        HDC hdc = nullptr;
        const HPAINTBUFFER buffer = BeginBufferedPaint(target, &paint.rcPaint, BPBF_COMPATIBLEBITMAP, nullptr, &hdc);
        SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(buffer ? hdc : target), PRF_CLIENT);
        if (buffer)
        {
            EndBufferedPaint(buffer, TRUE);
        }
    }
    EndPaint(window, &paint);
}

LRESULT HandleFillPaint(HWND window, UINT message, WPARAM wparam, HBRUSH brush)
{
    switch (message)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintBuffered(window);
        return 0;
    case WM_PRINTCLIENT:
        {
            RECT client = {};
            GetClientRect(window, &client);
            FillRect(reinterpret_cast<HDC>(wparam), &client, brush);
            return 0;
        }
    default:
        return 0;
    }
}

}
