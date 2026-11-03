// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/handle_owner.h"

#include <windows.h>

namespace win32ui
{

struct CloseServiceHandleFn
{
    void operator()(SC_HANDLE service) const noexcept
    {
        CloseServiceHandle(service);
    }
};
using UniqueService = UniqueNullable<SC_HANDLE, CloseServiceHandleFn>;

bool QueryServiceProcess(SC_HANDLE service, SERVICE_STATUS_PROCESS* status);
bool WaitWhileServicePending(SC_HANDLE service, DWORD pending_state, SERVICE_STATUS_PROCESS* status);
// a service that stops instead of running gives its exit code
bool StartServiceAndWait(SC_HANDLE service, SERVICE_STATUS_PROCESS* status);

}
