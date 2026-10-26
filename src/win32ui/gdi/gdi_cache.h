// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

HBRUSH CachedBrush(COLORREF color);
HPEN CachedPen(COLORREF color, int width = 1);

} // namespace win32ui
