// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <commctrl.h>

#include <string>

namespace win32ui
{

// app icon size in DIPs
inline constexpr int kIconDip = 16;

// dpi 0 uses system DPI, never scales below original size
int ScaleForDpi(int size, UINT dpi);
HICON LoadIconResource(int resource_id, UINT dpi);
HICON LoadIconFromFile(const std::wstring& path, UINT dpi);
void ImageListAddOrBlank(HIMAGELIST list, HICON icon, int size);

} // namespace win32ui
