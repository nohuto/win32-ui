// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

// export newer than the Windows build target, null if module or export is missing
template <typename Fn>
Fn ImportProc(HMODULE module, const char* name) noexcept
{
    return module ? reinterpret_cast<Fn>(GetProcAddress(module, name)) : nullptr;
}

// module must already be loaded
template <typename Fn>
Fn ImportProc(const wchar_t* module, const char* name) noexcept
{
    return ImportProc<Fn>(GetModuleHandleW(module), name);
}

// RtlGetVersion avoids GetVersionEx manifest dependency, zero means unknown
inline const RTL_OSVERSIONINFOW& OsVersion() noexcept
{
    static const RTL_OSVERSIONINFOW version = [] {
        RTL_OSVERSIONINFOW info = {};
        info.dwOSVersionInfoSize = sizeof(info);
        const auto get_version = ImportProc<LONG(WINAPI*)(RTL_OSVERSIONINFOW*)>(L"ntdll.dll", "RtlGetVersion");
        if (!get_version || get_version(&info) != 0)
        {
            info = {};
        }
        return info;
    }();
    return version;
}

} // namespace win32ui
