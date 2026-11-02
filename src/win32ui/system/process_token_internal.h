// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <vector>

namespace win32ui
{

std::vector<BYTE> TokenInformation(HANDLE token, TOKEN_INFORMATION_CLASS type);
bool EnablePrivilege(HANDLE token, const wchar_t* name, TOKEN_PRIVILEGES* previous = nullptr);

}
