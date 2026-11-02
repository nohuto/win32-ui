// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string_view>

namespace win32ui
{

enum class ConsoleStream
{
    output,
    error
};

DWORD WriteConsoleText(ConsoleStream stream, std::wstring_view text);
bool AttachTerminal();

}
