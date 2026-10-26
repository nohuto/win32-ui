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

} // namespace win32ui
