// Copyright (C) 2026 nohuto
// SPDX License Identifier: AGPL-3.0-or-later

#include "win32ui/process_token.h"

#include "win32ui/handle_owner.h"

#include <windows.h>

#include <string>
#include <vector>

namespace win32ui
{

namespace
{

std::vector<BYTE> TokenInformation(HANDLE token, TOKEN_INFORMATION_CLASS type)
{
    DWORD size = 0;
    GetTokenInformation(token, type, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    if (size == 0 || !GetTokenInformation(token, type, buffer.data(), size, &size))
    {
        buffer.clear();
    }
    return buffer;
}

std::vector<BYTE> CurrentTokenInformation(TOKEN_INFORMATION_CLASS type)
{
    UniqueHandle token;
    return OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()) ? TokenInformation(token.get(), type)
                                                                           : std::vector<BYTE>();
}

PSID CurrentUserSid(const std::vector<BYTE>& buffer)
{
    return buffer.empty() ? nullptr : reinterpret_cast<const TOKEN_USER*>(buffer.data())->User.Sid;
}

} // namespace

bool IsProcessElevated()
{
    const std::vector<BYTE> elevation = CurrentTokenInformation(TokenElevation);
    return elevation.size() >= sizeof(TOKEN_ELEVATION) &&
           reinterpret_cast<const TOKEN_ELEVATION*>(elevation.data())->TokenIsElevated != 0;
}

bool IsProcessSystem()
{
    const std::vector<BYTE> user = CurrentTokenInformation(TokenUser);
    return !user.empty() && IsWellKnownSid(CurrentUserSid(user), WELL_KNOWN_SID_TYPE::WinLocalSystemSid) != FALSE;
}

bool IsProcessTrustedInstaller()
{
    static const std::vector<BYTE> ti_sid = [] {
        const wchar_t* account = L"NT SERVICE\\TrustedInstaller";
        DWORD sid_size = 0;
        DWORD domain_size = 0;
        SID_NAME_USE use = SidTypeUnknown;
        LookupAccountNameW(nullptr, account, nullptr, &sid_size, nullptr, &domain_size, &use);
        std::vector<BYTE> sid_buffer(sid_size);
        std::wstring domain(domain_size, L'\0');
        if (sid_size == 0 ||
            !LookupAccountNameW(nullptr, account, sid_buffer.data(), &sid_size, domain.data(), &domain_size, &use))
        {
            sid_buffer.clear();
        }
        return sid_buffer;
    }();
    if (ti_sid.empty())
    {
        return false;
    }
    const PSID ti = const_cast<PSID>(static_cast<const void*>(ti_sid.data()));
    UniqueHandle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()))
    {
        return false;
    }
    const std::vector<BYTE> user = TokenInformation(token.get(), TokenUser);
    if (!user.empty() && EqualSid(CurrentUserSid(user), ti))
    {
        return true;
    }
    const std::vector<BYTE> group_buffer = TokenInformation(token.get(), TokenGroups);
    if (group_buffer.empty())
    {
        return false;
    }
    const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(group_buffer.data());
    for (DWORD index = 0; index < groups->GroupCount; ++index)
    {
        if (EqualSid(groups->Groups[index].Sid, ti))
        {
            return true;
        }
    }
    return false;
}

} // namespace win32ui
