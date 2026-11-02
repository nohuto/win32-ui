// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/handle_owner.h"

#include <windows.h>

#include <initializer_list>
#include <string>
#include <vector>

namespace win32ui
{

bool IsProcessElevated();
bool IsProcessSystem();
bool IsProcessTrustedInstaller();
bool IsProcessPrivileged();
bool IsUacEnabled();

// scopes on any thread share the token, the last scope of a privilege restores its state from before the first
class PrivilegeScope
{
  public:
    explicit PrivilegeScope(std::initializer_list<const wchar_t*> names);
    ~PrivilegeScope();
    PrivilegeScope(const PrivilegeScope&) = delete;
    PrivilegeScope& operator=(const PrivilegeScope&) = delete;

    bool held() const noexcept
    {
        return held_;
    }

  private:
    UniqueHandle token_;
    std::vector<LUID> enabled_;
    bool held_ = false;
};

std::wstring TokenUserSid(HANDLE token);
const std::wstring& ProcessUserSid();
std::wstring AccountName(const std::wstring& sid);
std::wstring ProcessImagePath(DWORD process_id);
// folder above it counts too
bool IsWritableByNonAdmins(const std::wstring& path);
UniqueHandle OpenShellToken(DWORD access);
bool ShellUserMatchesProcess();
std::wstring ShellUserDocuments();

}
