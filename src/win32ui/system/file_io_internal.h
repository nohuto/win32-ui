// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <vector>

namespace win32ui
{

DWORD ReadExactly(HANDLE file, size_t size, std::vector<unsigned char>* bytes);

}
