// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/handle_owner.h"

#include <windows.h>

namespace win32ui
{

// first call of the exe, static imports are resolved before it, link with /DEPENDENTLOADFLAG:0x800
void HardenProcess() noexcept;

// destruction releases the accessibility services first
class GuiRuntime
{
  public:
    GuiRuntime() noexcept;
    ~GuiRuntime();
    GuiRuntime(const GuiRuntime&) = delete;
    GuiRuntime& operator=(const GuiRuntime&) = delete;

    bool com_ok() const noexcept
    {
        return com_.ok();
    }

  private:
    ComInit com_;
};

}
