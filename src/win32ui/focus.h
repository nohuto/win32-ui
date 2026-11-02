// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <span>

namespace win32ui
{

// edit superclasses only for windows of the calling thread
bool IsTextInput(HWND window) noexcept;
bool TextInputOwnsKey(const MSG& message) noexcept;
HWND CyclePane(HWND window, std::span<const HWND> panes, bool forward) noexcept;
// WM_ACTIVATE, a reused handle value is not told apart
HWND ActivationFocus(HWND window, WPARAM wparam, HWND* last) noexcept;

}
