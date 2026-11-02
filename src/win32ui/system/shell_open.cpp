// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/shell_open.h"

#include "win32ui/handle_owner.h"
#include "win32ui/system/error_text.h"

#include <shellapi.h>
#include <shlobj.h>

namespace win32ui
{

DWORD ShellOpen(HWND owner, const wchar_t* target, const wchar_t* file_class)
{
    if (!target || !*target)
    {
        return ERROR_INVALID_PARAMETER;
    }
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI | (file_class ? SEE_MASK_CLASSNAME : 0);
    info.hwnd = owner;
    info.lpVerb = L"open";
    info.lpFile = target;
    info.lpClass = file_class;
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info))
    {
        return ERROR_SUCCESS;
    }
    // failure without a last error must not read as success
    const DWORD error = GetLastError();
    return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
}

DWORD RevealInExplorer(const wchar_t* path)
{
    if (!path || !*path)
    {
        return ERROR_INVALID_PARAMETER;
    }
    UniqueCoTask<PIDLIST_ABSOLUTE> item;
    HRESULT hr = SHParseDisplayName(path, nullptr, item.put(), 0, nullptr);
    if (SUCCEEDED(hr))
    {
        hr = SHOpenFolderAndSelectItems(item.get(), 0, nullptr, 0);
    }
    return Win32FromHResult(hr);
}

}
