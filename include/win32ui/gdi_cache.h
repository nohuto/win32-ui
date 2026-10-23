// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

// shared solid brushes and pens for painting, the cache owns them, so callers never delete them
// 16 of each, the oldest is replaced after that, so a handle is valid until 16 other colours were asked for
HBRUSH CachedBrush(COLORREF color);
HPEN CachedPen(COLORREF color, int width = 1);

} // namespace win32ui
