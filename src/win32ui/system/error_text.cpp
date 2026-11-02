// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/error_text.h"

#include "win32ui/handle_owner.h"
#include "win32ui/system_api.h"
#include "win32ui/text/text.h"

#include <winhttp.h>

#include <cwctype>
#include <string_view>

namespace win32ui
{
namespace
{

using RtlNtStatusToDosErrorFn = ULONG(WINAPI*)(LONG);

// system allocates the buffer, so long messages arent cut off
std::wstring Format(DWORD flags, HMODULE module, DWORD code)
{
    UniqueLocal<wchar_t*> buffer;
    const DWORD length = FormatMessageW(flags | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS, module, code, 0, reinterpret_cast<wchar_t*>(buffer.put()), 0, nullptr);
    std::wstring_view text(buffer.get(), buffer ? length : 0);
    while (!text.empty() && iswspace(text.back()))
    {
        text.remove_suffix(1);
    }
    return std::wstring(text);
}

}

std::wstring FormatWin32Error(DWORD code)
{
    if (code == ERROR_SUCCESS)
    {
        return {};
    }
    // WinHTTP keeps its texts in its own message table
    std::wstring text = code >= WINHTTP_ERROR_BASE && code <= WINHTTP_ERROR_LAST ? FormatModuleError(code, L"winhttp.dll") : Format(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, code);
    return text.empty() ? Text(L"Unknown error.") : text;
}

std::wstring FormatModuleError(DWORD code, const wchar_t* module)
{
    HMODULE handle = module ? GetModuleHandleW(module) : nullptr;
    return Format(handle ? FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM : FORMAT_MESSAGE_FROM_SYSTEM, handle, code);
}

DWORD Win32FromNtStatus(LONG status) noexcept
{
    static const auto convert = ImportProc<RtlNtStatusToDosErrorFn>(L"ntdll.dll", "RtlNtStatusToDosError");
    if (status >= 0)
    {
        return ERROR_SUCCESS;
    }
    return convert ? convert(status) : ERROR_GEN_FAILURE;
}

DWORD Win32FromHResult(HRESULT hr) noexcept
{
    if (SUCCEEDED(hr))
    {
        return ERROR_SUCCESS;
    }
    return HRESULT_FACILITY(hr) == FACILITY_WIN32 ? HRESULT_CODE(hr) : static_cast<DWORD>(hr);
}

}
