// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/app_runtime.h"

#include "win32ui/accessible_name.h"
#include "win32ui/system_api.h"

#include <commctrl.h>
#include <uxtheme.h>

namespace win32ui
{

void HardenProcess() noexcept
{
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
    // older systems without KB2533623 lack it
    if (const auto set_directories = ImportProc<BOOL(WINAPI*)(DWORD)>(L"kernel32.dll", "SetDefaultDllDirectories"))
    {
        set_directories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS);
    }
    SetDllDirectoryW(L"");
}

GuiRuntime::GuiRuntime() noexcept
{
    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_DATE_CLASSES | ICC_COOL_CLASSES | ICC_LINK_CLASS;
    InitCommonControlsEx(&controls);
    BufferedPaintInit();
}

GuiRuntime::~GuiRuntime()
{
    // before COM ends with the member
    ReleaseAccessibility();
    BufferedPaintUnInit();
}

}
