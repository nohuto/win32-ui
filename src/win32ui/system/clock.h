// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <cstdint>

namespace win32ui
{

struct QpcClock
{
    static int64_t Frequency() noexcept
    {
        static const int64_t frequency = [] {
            LARGE_INTEGER value = {};
            QueryPerformanceFrequency(&value);
            return value.QuadPart > 0 ? value.QuadPart : 1;
        }();
        return frequency;
    }
    static int64_t Now() noexcept
    {
        LARGE_INTEGER value = {};
        QueryPerformanceCounter(&value);
        return value.QuadPart;
    }
    static int64_t TicksToMicroseconds(int64_t ticks) noexcept
    {
        const int64_t frequency = Frequency();
        const int64_t whole = ticks / frequency;
        const int64_t part = ticks % frequency;
        return whole * 1'000'000 + part * 1'000'000 / frequency;
    }
};

}
