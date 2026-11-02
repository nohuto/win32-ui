// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/single_instance.h"

#include "win32ui/handle_owner.h"
#include "win32ui/system/process_launch.h"
#include "win32ui/system/process_token.h"
#include "win32ui/system/strings.h"

#include <cwchar>

namespace win32ui
{

namespace
{

struct DestroyWindowFn
{
    void operator()(HWND window) const noexcept
    {
        DestroyWindow(window);
    }
};

// both sides read the image path the same way (a subst drive or short name in the launch path doesnt matter)
bool RunsThisExe(DWORD process_id)
{
    static const std::wstring own_image = ProcessImagePath(GetCurrentProcessId());
    DWORD own_session = 0;
    DWORD session = 0;
    if (process_id == 0 || process_id == GetCurrentProcessId() || own_image.empty() ||
        !ProcessIdToSessionId(GetCurrentProcessId(), &own_session) || !ProcessIdToSessionId(process_id, &session) || session != own_session)
    {
        return false;
    }
    return EqualsInsensitive(ProcessImagePath(process_id), own_image);
}

struct InstanceSearch
{
    const wchar_t* property;
    DWORD skip_id;
    ULONGLONG skip_created;
    HWND found;
};

BOOL CALLBACK FindInstanceProc(HWND window, LPARAM lparam)
{
    auto* search = reinterpret_cast<InstanceSearch*>(lparam);
    DWORD process_id = 0;
    if (!GetPropW(window, search->property) || !GetWindowThreadProcessId(window, &process_id) ||
        (process_id == search->skip_id && (search->skip_created == 0 || ProcessCreationTime(process_id) == search->skip_created)) ||
        !RunsThisExe(process_id))
    {
        return TRUE;
    }
    search->found = window;
    return FALSE;
}

}

HWND FindInstanceWindow(const wchar_t* property, DWORD wait_ms, DWORD skip_id, ULONGLONG skip_created)
{
    const ULONGLONG end = GetTickCount64() + wait_ms;
    for (;;)
    {
        InstanceSearch search{property, skip_id, skip_created, nullptr};
        EnumWindows(FindInstanceProc, reinterpret_cast<LPARAM>(&search));
        if (search.found || GetTickCount64() >= end)
        {
            return search.found;
        }
        Sleep(50);
    }
}

DWORD SendInstanceText(HWND window, ULONG_PTR id, std::wstring_view text, DWORD timeout_ms)
{
    DWORD process_id = 0;
    if (text.empty() || !GetWindowThreadProcessId(window, &process_id))
    {
        return ERROR_INVALID_PARAMETER;
    }
    const std::wstring data(text);
    const UniqueNullable<HWND, DestroyWindowFn> sender(CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr));
    if (!sender)
    {
        return GetLastError();
    }
    AllowSetForegroundWindow(process_id);
    COPYDATASTRUCT copy = {};
    copy.dwData = id;
    copy.cbData = static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t));
    copy.lpData = const_cast<wchar_t*>(data.c_str());
    DWORD_PTR accepted = 0;
    if (!SendMessageTimeoutW(window, WM_COPYDATA, reinterpret_cast<WPARAM>(sender.get()), reinterpret_cast<LPARAM>(&copy), SMTO_ABORTIFHUNG, timeout_ms, &accepted))
    {
        const DWORD error = GetLastError();
        return error == ERROR_SUCCESS ? ERROR_TIMEOUT : error;
    }
    return accepted ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
}

bool ReadInstanceText(WPARAM wparam, LPARAM lparam, size_t max_chars, ULONG_PTR* id, std::wstring* text)
{
    const auto* copy = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
    DWORD sender_id = 0;
    if (!copy || !copy->lpData || copy->cbData < 2 * sizeof(wchar_t) || copy->cbData % sizeof(wchar_t) != 0 ||
        copy->cbData / sizeof(wchar_t) - 1 > max_chars)
    {
        return false;
    }
    const auto* chars = static_cast<const wchar_t*>(copy->lpData);
    const size_t length = copy->cbData / sizeof(wchar_t) - 1;
    if (chars[length] != L'\0' || std::wmemchr(chars, L'\0', length) ||
        !GetWindowThreadProcessId(reinterpret_cast<HWND>(wparam), &sender_id) || !RunsThisExe(sender_id))
    {
        return false;
    }
    *id = copy->dwData;
    text->assign(chars, length);
    return true;
}

}
