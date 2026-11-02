// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>

namespace win32ui
{

bool ZoneTimeToLocal(const DYNAMIC_TIME_ZONE_INFORMATION& zone, const SYSTEMTIME& utc, SYSTEMTIME* local) noexcept;
bool ZoneLocalToUtc(const DYNAMIC_TIME_ZONE_INFORMATION& zone, const SYSTEMTIME& local, SYSTEMTIME* utc) noexcept;
bool CachedZoneTimeToLocal(const DYNAMIC_TIME_ZONE_INFORMATION& zone, const FILETIME& utc, SYSTEMTIME* local) noexcept;
std::wstring FormatDateTimeWith(const SYSTEMTIME& time, const wchar_t* date_pattern, const wchar_t* time_pattern);

}
