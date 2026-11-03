// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/service_control.h"

#include <algorithm>

namespace win32ui
{

bool QueryServiceProcess(SC_HANDLE service, SERVICE_STATUS_PROCESS* status)
{
    DWORD bytes = 0;
    return QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(status), sizeof(SERVICE_STATUS_PROCESS), &bytes) != FALSE;
}

bool WaitWhileServicePending(SC_HANDLE service, DWORD pending_state, SERVICE_STATUS_PROCESS* status)
{
    constexpr ULONGLONG kMaxWaitMs = 60000;
    const ULONGLONG start = GetTickCount64();
    DWORD checkpoint = status->dwCheckPoint;
    ULONGLONG progress = start;
    while (status->dwCurrentState == pending_state)
    {
        Sleep(std::clamp<DWORD>(status->dwWaitHint / 10, 100, 2000));
        if (!QueryServiceProcess(service, status))
        {
            return false;
        }
        if (status->dwCurrentState != pending_state)
        {
            break;
        }
        const ULONGLONG now = GetTickCount64();
        if (status->dwCheckPoint > checkpoint)
        {
            checkpoint = status->dwCheckPoint;
            progress = now;
        }
        if (now - progress > std::max<ULONGLONG>(status->dwWaitHint, 5000) || now - start > kMaxWaitMs)
        {
            SetLastError(ERROR_SERVICE_REQUEST_TIMEOUT);
            return false;
        }
    }
    return true;
}

bool StartServiceAndWait(SC_HANDLE service, SERVICE_STATUS_PROCESS* status)
{
    if ((!StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) || !QueryServiceProcess(service, status) ||
        !WaitWhileServicePending(service, SERVICE_START_PENDING, status))
    {
        return false;
    }
    if (status->dwCurrentState != SERVICE_RUNNING)
    {
        const DWORD error = status->dwWin32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR ? status->dwServiceSpecificExitCode : status->dwWin32ExitCode;
        SetLastError(error != ERROR_SUCCESS ? error : ERROR_SERVICE_NOT_ACTIVE);
        return false;
    }
    return true;
}

}
