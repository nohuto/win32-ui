// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#include "win32ui/gdi/icon_loader.h"

#include "win32ui/gdi/icon_loader_internal.h"
#include "win32ui/system_api.h"
#include "win32ui/window_metrics.h"

namespace win32ui
{

namespace
{

HICON LoadScaledDown(HINSTANCE instance, PCWSTR name, int size)
{
    static const auto load = ImportProc<HRESULT(WINAPI*)(HINSTANCE, PCWSTR, int, int, HICON*)>(L"comctl32.dll", "LoadIconWithScaleDown");
    HICON icon = nullptr;
    return load && SUCCEEDED(load(instance, name, size, size, &icon)) ? icon : nullptr;
}

} // namespace

int ScaleForDpi(int size, UINT dpi)
{
    if (size <= 0)
    {
        return size;
    }
    dpi = dpi != 0 ? dpi : DpiForWindow(nullptr);
    if (dpi <= 96)
    {
        return size;
    }
    const int scaled = MulDiv(size, static_cast<int>(dpi), 96);
    return scaled > 0 ? scaled : size;
}

HICON LoadIconResource(int resource_id, UINT dpi)
{
    if (resource_id == 0)
    {
        return nullptr;
    }
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const int scaled = ScaleForDpi(kIconDip, dpi);
    HICON icon = LoadScaledDown(instance, MAKEINTRESOURCEW(resource_id), scaled);
    if (!icon)
    {
        icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(resource_id), IMAGE_ICON, scaled, scaled, LR_DEFAULTCOLOR));
    }
    return icon;
}

HICON LoadSystemIcon(PCWSTR id, int size, UINT dpi)
{
    if (!id || size <= 0)
    {
        return nullptr;
    }
    HICON icon = LoadScaledDown(nullptr, id, ScaleForDpi(size, dpi));
    if (!icon)
    {
        // copy shared icons so callers always own the result
        if (HICON shared = LoadIconW(nullptr, id))
        {
            icon = CopyIcon(shared);
        }
    }
    return icon;
}

HICON LoadIconFromFile(const std::wstring& path, UINT dpi)
{
    if (path.empty())
    {
        return nullptr;
    }
    const int scaled = ScaleForDpi(kIconDip, dpi);
    HICON icon = LoadScaledDown(nullptr, path.c_str(), scaled);
    if (!icon)
    {
        icon = static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, scaled, scaled, LR_LOADFROMFILE | LR_DEFAULTCOLOR));
    }
    return icon;
}

void ImageListAddOrBlank(HIMAGELIST list, HICON icon, int size)
{
    if (!list)
    {
        return;
    }
    if (icon)
    {
        ImageList_AddIcon(list, icon);
        return;
    }
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    if (HBITMAP blank = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0))
    {
        ImageList_Add(list, blank, nullptr);
        DeleteObject(blank);
    }
}

} // namespace win32ui
