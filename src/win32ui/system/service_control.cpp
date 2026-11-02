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

}
