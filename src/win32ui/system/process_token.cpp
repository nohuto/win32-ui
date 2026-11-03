// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/process_token.h"

#include "win32ui/system/paths.h"
#include "win32ui/system/process_token_internal.h"
#include "win32ui/system/registry_value.h"
#include "win32ui/system/strings.h"

#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>

#include <algorithm>
#include <mutex>

namespace win32ui
{

namespace
{

std::vector<BYTE> CurrentTokenInformation(TOKEN_INFORMATION_CLASS type)
{
    UniqueHandle token;
    return OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()) ? TokenInformation(token.get(), type)
                                                                           : std::vector<BYTE>();
}

PSID UserSid(const std::vector<BYTE>& buffer)
{
    return buffer.empty() ? nullptr : reinterpret_cast<const TOKEN_USER*>(buffer.data())->User.Sid;
}

std::wstring SidText(const std::vector<BYTE>& user)
{
    UniqueLocal<LPWSTR> sid;
    return !user.empty() && ConvertSidToStringSidW(UserSid(user), sid.put()) ? std::wstring(sid.get()) : std::wstring();
}

// scopes share the process token across threads, the first enables a privilege, the last restores it
struct PrivilegeHold
{
    LUID luid = {};
    int count = 0;
    TOKEN_PRIVILEGES previous = {};
};
std::mutex g_hold_mutex;
std::vector<PrivilegeHold> g_holds;

std::vector<PrivilegeHold>::iterator FindHold(const LUID& luid)
{
    return std::find_if(g_holds.begin(), g_holds.end(), [&luid](const PrivilegeHold& hold) {
        return hold.luid.LowPart == luid.LowPart && hold.luid.HighPart == luid.HighPart;
    });
}

constexpr ACCESS_MASK kWriteAccess = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
                                     FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
// drive root counts only with rights to replace what is in it, C:\ lets users add folders
constexpr ACCESS_MASK kReplaceChildAccess = FILE_DELETE_CHILD | WRITE_DAC | WRITE_OWNER | GENERIC_ALL;

bool GrantsWriteToNonAdmins(const std::wstring& path, const std::vector<std::vector<BYTE>>& trusted_sids, ACCESS_MASK write_access)
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PSID owner = nullptr;
    PACL dacl = nullptr;
    if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &dacl, nullptr, &descriptor) != ERROR_SUCCESS)
    {
        return true;
    }
    const UniqueLocal<PSECURITY_DESCRIPTOR> owned(descriptor);
    const auto trusted = [&](PSID sid) {
        return std::any_of(trusted_sids.begin(), trusted_sids.end(), [&](const std::vector<BYTE>& entry) {
            return EqualSid(const_cast<PSID>(static_cast<const void*>(entry.data())), sid) != FALSE;
        });
    };
    bool writable = !owner || !dacl || !trusted(owner);
    for (DWORD index = 0; dacl && !writable && index < dacl->AceCount; ++index)
    {
        void* entry = nullptr;
        if (!GetAce(dacl, index, &entry))
        {
            return true;
        }
        const auto* header = static_cast<const ACE_HEADER*>(entry);
        if ((header->AceFlags & INHERIT_ONLY_ACE) != 0 || header->AceType == ACCESS_DENIED_ACE_TYPE ||
            header->AceType == ACCESS_DENIED_CALLBACK_ACE_TYPE)
        {
            continue;
        }
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(entry);
        writable =
            (header->AceType != ACCESS_ALLOWED_ACE_TYPE && header->AceType != ACCESS_ALLOWED_CALLBACK_ACE_TYPE) ||
            ((ace->Mask & write_access) != 0 && !trusted(const_cast<PSID>(static_cast<const void*>(&ace->SidStart))));
    }
    return writable;
}

}

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

bool EnablePrivilege(HANDLE token, const wchar_t* name, TOKEN_PRIVILEGES* previous)
{
    TOKEN_PRIVILEGES privileges = {};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    DWORD previous_size = previous ? sizeof(TOKEN_PRIVILEGES) : 0;
    return LookupPrivilegeValueW(nullptr, name, &privileges.Privileges[0].Luid) &&
           AdjustTokenPrivileges(token, FALSE, &privileges, previous_size, previous, previous ? &previous_size : nullptr) &&
           GetLastError() == ERROR_SUCCESS;
}

bool IsProcessElevated()
{
    const std::vector<BYTE> elevation = CurrentTokenInformation(TokenElevation);
    return elevation.size() >= sizeof(TOKEN_ELEVATION) &&
           reinterpret_cast<const TOKEN_ELEVATION*>(elevation.data())->TokenIsElevated != 0;
}

bool IsProcessSystem()
{
    const std::vector<BYTE> user = CurrentTokenInformation(TokenUser);
    return !user.empty() && IsWellKnownSid(UserSid(user), WELL_KNOWN_SID_TYPE::WinLocalSystemSid) != FALSE;
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
    if (!user.empty() && EqualSid(UserSid(user), ti))
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

bool IsProcessPrivileged()
{
    return IsProcessElevated() || IsProcessSystem() || IsProcessTrustedInstaller();
}

bool IsUacEnabled()
{
    DWORD value = 1;
    return ReadRegistryDword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", L"EnableLUA", &value) != ERROR_SUCCESS || value != 0;
}

PrivilegeScope::PrivilegeScope(std::initializer_list<const wchar_t*> names)
{
    enabled_.reserve(names.size());
    held_ = OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, token_.put()) != FALSE;
    const std::lock_guard<std::mutex> lock(g_hold_mutex);
    // no allocation may fail once a privilege changed
    g_holds.reserve(g_holds.size() + names.size());
    for (const wchar_t* name : names)
    {
        LUID luid = {};
        held_ = held_ && LookupPrivilegeValueW(nullptr, name, &luid);
        if (!held_)
        {
            break;
        }
        if (const auto hold = FindHold(luid); hold != g_holds.end())
        {
            ++hold->count;
            enabled_.push_back(luid);
            continue;
        }
        TOKEN_PRIVILEGES previous = {};
        held_ = EnablePrivilege(token_.get(), name, &previous);
        if (held_)
        {
            g_holds.push_back({luid, 1, previous});
            enabled_.push_back(luid);
        }
    }
}

PrivilegeScope::~PrivilegeScope()
{
    const std::lock_guard<std::mutex> lock(g_hold_mutex);
    for (auto luid = enabled_.rbegin(); luid != enabled_.rend(); ++luid)
    {
        const auto hold = FindHold(*luid);
        if (hold == g_holds.end() || --hold->count > 0)
        {
            continue;
        }
        // restore the state from before the first scope
        if (hold->previous.PrivilegeCount != 0)
        {
            AdjustTokenPrivileges(token_.get(), FALSE, &hold->previous, sizeof(hold->previous), nullptr, nullptr);
        }
        g_holds.erase(hold);
    }
}

std::wstring TokenUserSid(HANDLE token)
{
    return SidText(TokenInformation(token, TokenUser));
}

const std::wstring& ProcessUserSid()
{
    static const std::wstring cached = SidText(CurrentTokenInformation(TokenUser));
    return cached;
}

std::wstring AccountName(const std::wstring& sid_text)
{
    UniqueLocal<PSID> sid;
    if (!ConvertStringSidToSidW(sid_text.c_str(), sid.put()))
    {
        return sid_text;
    }
    DWORD name_size = 0;
    DWORD domain_size = 0;
    SID_NAME_USE use = SidTypeUnknown;
    LookupAccountSidW(nullptr, sid.get(), nullptr, &name_size, nullptr, &domain_size, &use);
    std::wstring name(name_size, L'\0');
    std::wstring domain(domain_size, L'\0');
    if (name_size == 0 || !LookupAccountSidW(nullptr, sid.get(), name.data(), &name_size, domain.data(), &domain_size, &use))
    {
        return sid_text;
    }
    name.resize(wcslen(name.c_str()));
    domain.resize(wcslen(domain.c_str()));
    return domain.empty() ? name : domain + L"\\" + name;
}

const std::wstring& ComputerName()
{
    static const std::wstring name = [] {
        wchar_t buffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
        DWORD length = static_cast<DWORD>(std::size(buffer));
        return GetComputerNameW(buffer, &length) ? std::wstring(buffer, length) : std::wstring();
    }();
    return name;
}

std::wstring ProcessImagePath(DWORD process_id)
{
    const UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
    for (DWORD capacity = MAX_PATH; process; capacity = std::min<DWORD>(capacity * 2, 32768))
    {
        std::wstring path(capacity, L'\0');
        DWORD length = capacity;
        if (QueryFullProcessImageNameW(process.get(), 0, path.data(), &length))
        {
            path.resize(length);
            return path;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || capacity == 32768)
        {
            break;
        }
    }
    return {};
}

bool IsWritableByNonAdmins(const std::wstring& file_path)
{
    std::vector<std::vector<BYTE>> trusted_sids;
    const std::wstring user_sid = !IsUacEnabled() && IsProcessElevated() ? ProcessUserSid() : std::wstring();
    // SYSTEM, Administrators, owner rights, TrustedInstaller, an elevated user without UAC
    for (const wchar_t* text : {L"S-1-5-18", L"S-1-5-32-544", L"S-1-3-4", L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464", user_sid.c_str()})
    {
        UniqueLocal<PSID> sid;
        if (*text && ConvertStringSidToSidW(text, sid.put()))
        {
            trusted_sids.emplace_back(static_cast<BYTE*>(sid.get()), static_cast<BYTE*>(sid.get()) + GetLengthSid(sid.get()));
        }
    }
    for (std::wstring path = file_path; !path.empty();)
    {
        const bool root = path.size() == 3 && path[1] == L':' && path[2] == L'\\';
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            GrantsWriteToNonAdmins(path, trusted_sids, root ? kReplaceChildAccess : kWriteAccess))
        {
            return true;
        }
        const size_t slash = path.find_last_of(L'\\');
        if (root || slash == std::wstring::npos)
        {
            break;
        }
        path.resize(slash <= 2 ? slash + 1 : slash);
    }
    return file_path.empty();
}

UniqueHandle OpenShellToken(DWORD access)
{
    DWORD shell_pid = 0;
    UniqueHandle token;
    if (!GetWindowThreadProcessId(GetShellWindow(), &shell_pid) || shell_pid == 0)
    {
        SetLastError(ERROR_NOT_FOUND);
        return token;
    }
    const UniqueHandle shell_process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shell_pid));
    if (shell_process)
    {
        OpenProcessToken(shell_process.get(), access, token.put());
    }
    return token;
}

bool ShellUserMatchesProcess()
{
    const UniqueHandle token = OpenShellToken(TOKEN_QUERY);
    return token && EqualsInsensitive(TokenUserSid(token.get()), ProcessUserSid());
}

std::wstring ShellUserDocuments()
{
    return KnownFolderPath(FOLDERID_Documents, 0, OpenShellToken(TOKEN_QUERY | TOKEN_IMPERSONATE | TOKEN_DUPLICATE).get());
}

}
