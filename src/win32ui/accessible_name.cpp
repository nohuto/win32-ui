// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#include "win32ui/accessible_name.h"

#include <commctrl.h>
#include <initguid.h>
#include <oleacc.h>

#include <iterator>
#include <string>

namespace win32ui
{

namespace
{

constexpr UINT_PTR kNameSubclassId = 1;
const MSAAPROPID kNameProperties[] = {PROPID_ACC_NAME};

IAccPropServices* g_services = nullptr;
bool g_services_tried = false;

IAccPropServices* Services()
{
    if (!g_services_tried)
    {
        g_services_tried = true;
        if (FAILED(CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_services))))
        {
            g_services = nullptr;
        }
    }
    return g_services;
}

LRESULT CALLBACK NameProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR)
{
    if (message == WM_NCDESTROY)
    {
        if (g_services)
        {
            g_services->ClearHwndProps(window, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, kNameProperties, static_cast<int>(std::size(kNameProperties)));
        }
        RemoveWindowSubclass(window, NameProc, id);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

} // namespace

void SetAccessibleName(HWND window, std::wstring_view name)
{
    IAccPropServices* services = Services();
    if (!services || !window || name.empty())
    {
        return;
    }
    const std::wstring copy(name);
    if (SUCCEEDED(services->SetHwndPropStr(window, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, PROPID_ACC_NAME, copy.c_str())) &&
        !GetWindowSubclass(window, NameProc, kNameSubclassId, nullptr))
    {
        SetWindowSubclass(window, NameProc, kNameSubclassId, 0);
    }
}

void ReleaseAccessibility()
{
    if (g_services)
    {
        g_services->Release();
        g_services = nullptr;
    }
    // next call on a thread with COM recreates the services
    g_services_tried = false;
}

} // namespace win32ui
