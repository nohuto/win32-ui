// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#pragma once

namespace win32ui
{

bool IsProcessElevated();

bool IsProcessSystem();

// matches token user or any group regardless of group attributes
bool IsProcessTrustedInstaller();

} // namespace win32ui
