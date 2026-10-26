// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

namespace win32ui
{

// system icon such as IDI_WARNING, caller must destroy it
HICON LoadSystemIcon(PCWSTR id, int size, UINT dpi);

} // namespace win32ui
