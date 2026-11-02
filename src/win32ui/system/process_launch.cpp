// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/process_launch.h"

#include "win32ui/handle_owner.h"
#include "win32ui/system/date_time.h"
#include "win32ui/system/process_token.h"
#include "win32ui/system/process_token_internal.h"
#include "win32ui/system/service_control.h"
#include "win32ui/system/strings.h"

#include <shellapi.h>
#include <userenv.h>
#include <wtsapi32.h>

#include <new>

namespace win32ui
{

namespace
{

struct DestroyEnvironment
{
    void operator()(LPVOID block) const noexcept
    {
        DestroyEnvironmentBlock(block);
    }
};
using UniqueEnvironment = UniqueNullable<LPVOID, DestroyEnvironment>;

struct FreeWts
{
    void operator()(PWTS_PROCESS_INFOW processes) const noexcept
    {
        WTSFreeMemory(processes);
    }
};

std::wstring CommandLine(const std::wstring& exe, const std::wstring& arguments)
{
    std::wstring command = QuoteArgument(exe);
    if (!arguments.empty())
    {
        command.append(L" ").append(arguments);
    }
    return command;
}

ULONGLONG CreationTime(HANDLE process)
{
    FILETIME created = {};
    FILETIME exited = {};
    FILETIME kernel = {};
    FILETIME user = {};
    return GetProcessTimes(process, &created, &exited, &kernel, &user) ? FileTimeTicks(created) : 0;
}

// SYSTEM token of lsass (session 0), else of the winlogon of this session
bool OpenSystemToken(DWORD session_id, HANDLE* token)
{
    DWORD lsass_pid = 0;
    DWORD winlogon_pid = 0;
    DWORD process_count = 0;
    PWTS_PROCESS_INFOW processes = nullptr;
    if (!WTSEnumerateProcessesW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &processes, &process_count))
    {
        return false;
    }
    const UniqueNullable<PWTS_PROCESS_INFOW, FreeWts> owned(processes);
    for (DWORD i = 0; i < process_count; ++i)
    {
        const auto& process = processes[i];
        if (!process.pProcessName || !process.pUserSid || !IsWellKnownSid(process.pUserSid, WELL_KNOWN_SID_TYPE::WinLocalSystemSid))
        {
            continue;
        }
        if (lsass_pid == 0 && process.SessionId == 0 && EqualsInsensitive(process.pProcessName, L"lsass.exe"))
        {
            lsass_pid = process.ProcessId;
        }
        else if (winlogon_pid == 0 && process.SessionId == session_id && EqualsInsensitive(process.pProcessName, L"winlogon.exe"))
        {
            winlogon_pid = process.ProcessId;
        }
    }
    // protected lsass cant be opened, winlogon is used then
    UniqueHandle system_process(lsass_pid != 0 ? OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, lsass_pid) : nullptr);
    if (!system_process && winlogon_pid != 0)
    {
        system_process.reset(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, winlogon_pid));
    }
    if (!system_process)
    {
        SetLastError(ERROR_NOT_FOUND);
        return false;
    }
    return OpenProcessToken(system_process.get(), TOKEN_DUPLICATE, token) != FALSE;
}

// last error as the result of a failed call, never 0
DWORD LastError()
{
    const DWORD error = GetLastError();
    return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
}

bool EnableAllPrivileges(HANDLE token)
{
    std::vector<BYTE> buffer = TokenInformation(token, TokenPrivileges);
    if (buffer.empty())
    {
        return false;
    }
    auto* privileges = reinterpret_cast<TOKEN_PRIVILEGES*>(buffer.data());
    for (DWORD i = 0; i < privileges->PrivilegeCount; ++i)
    {
        privileges->Privileges[i].Attributes = SE_PRIVILEGE_ENABLED;
    }
    AdjustTokenPrivileges(token, FALSE, privileges, static_cast<DWORD>(buffer.size()), nullptr, nullptr);
    return GetLastError() == ERROR_SUCCESS;
}

bool OpenServiceProcessToken(const wchar_t* service_name, HANDLE* token)
{
    const UniqueService scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    const UniqueService service(scm ? OpenServiceW(scm.get(), service_name, SERVICE_QUERY_STATUS | SERVICE_START) : nullptr);
    SERVICE_STATUS_PROCESS status = {};
    if (!service || !QueryServiceProcess(service.get(), &status) || !WaitWhileServicePending(service.get(), SERVICE_STOP_PENDING, &status))
    {
        return false;
    }
    if (status.dwCurrentState == SERVICE_STOPPED)
    {
        if ((!StartServiceW(service.get(), 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) || !QueryServiceProcess(service.get(), &status))
        {
            return false;
        }
    }
    if (!WaitWhileServicePending(service.get(), SERVICE_START_PENDING, &status))
    {
        return false;
    }
    if (status.dwCurrentState != SERVICE_RUNNING || status.dwProcessId == 0)
    {
        const DWORD error = status.dwWin32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR ? status.dwServiceSpecificExitCode : status.dwWin32ExitCode;
        SetLastError(error != ERROR_SUCCESS ? error : ERROR_SERVICE_NOT_ACTIVE);
        return false;
    }
    const UniqueHandle process(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, status.dwProcessId));
    return process && OpenProcessToken(process.get(), MAXIMUM_ALLOWED, token);
}

bool LaunchWithToken(HANDLE token, const std::wstring& exe, const std::wstring& arguments, bool as_user)
{
    UniqueEnvironment environment;
    if (!CreateEnvironmentBlock(environment.put(), token, FALSE))
    {
        return false;
    }
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
    PROCESS_INFORMATION process = {};
    std::wstring command = CommandLine(exe, arguments);
    const BOOL launched =
        as_user ? CreateProcessAsUserW(token, exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, environment.get(), nullptr, &startup, &process)
                : CreateProcessWithTokenW(token, 0, exe.c_str(), command.data(), CREATE_UNICODE_ENVIRONMENT, environment.get(), nullptr, &startup, &process);
    if (!launched)
    {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

// SeDebugPrivilege on a copy of the thread token, then SYSTEM impersonation for the service and the session id
bool LaunchElevatedToken(const std::wstring& exe, const std::wstring& arguments, const wchar_t* service_name)
{
    UniqueHandle current_token;
    UniqueHandle current_impersonation;
    UniqueHandle system_token;
    UniqueHandle system_impersonation;
    UniqueHandle source_token;
    UniqueHandle target_token;
    DWORD session_id = 0;
    if (!OpenProcessToken(GetCurrentProcess(), MAXIMUM_ALLOWED, current_token.put()) ||
        !DuplicateTokenEx(current_token.get(), MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenImpersonation, current_impersonation.put()) ||
        !EnablePrivilege(current_impersonation.get(), SE_DEBUG_NAME) || !SetThreadToken(nullptr, current_impersonation.get()) ||
        !ProcessIdToSessionId(GetCurrentProcessId(), &session_id))
    {
        return false;
    }
    if (!OpenSystemToken(session_id, system_token.put()) ||
        !DuplicateTokenEx(system_token.get(), MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenImpersonation, system_impersonation.put()) ||
        !EnableAllPrivileges(system_impersonation.get()) || !SetThreadToken(nullptr, system_impersonation.get()))
    {
        return false;
    }
    if (service_name && !OpenServiceProcessToken(service_name, source_token.put()))
    {
        return false;
    }
    // primary token joins this session before the UI starts
    return DuplicateTokenEx(service_name ? source_token.get() : system_token.get(), MAXIMUM_ALLOWED, nullptr, SecurityIdentification, TokenPrimary, target_token.put()) &&
           SetTokenInformation(target_token.get(), TokenSessionId, &session_id, sizeof(session_id)) && EnableAllPrivileges(target_token.get()) &&
           LaunchWithToken(target_token.get(), exe, arguments, true);
}

DWORD LaunchImpersonated(const std::wstring& exe, const std::wstring& arguments, const wchar_t* service_name, DWORD* restore_error)
{
    if (restore_error)
    {
        *restore_error = ERROR_SUCCESS;
    }
    if (exe.empty())
    {
        return ERROR_INVALID_PARAMETER;
    }
    UniqueHandle previous_thread_token;
    const bool had_thread_token = OpenThreadToken(GetCurrentThread(), TOKEN_IMPERSONATE | TOKEN_QUERY, TRUE, previous_thread_token.put()) != FALSE;
    if (!had_thread_token && GetLastError() != ERROR_NO_TOKEN)
    {
        return LastError();
    }
    DWORD error = ERROR_SUCCESS;
    try
    {
        error = LaunchElevatedToken(exe, arguments, service_name) ? ERROR_SUCCESS : LastError();
    }
    catch (const std::bad_alloc&)
    {
        error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if ((had_thread_token ? !SetThreadToken(nullptr, previous_thread_token.get()) : !RevertToSelf()) && restore_error)
    {
        *restore_error = LastError();
    }
    return error;
}

DWORD ShellLaunch(HWND owner, const wchar_t* verb, const std::wstring& exe, const std::wstring& arguments)
{
    if (exe.empty())
    {
        return ERROR_INVALID_PARAMETER;
    }
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.hwnd = owner;
    info.lpVerb = verb;
    info.lpFile = exe.c_str();
    info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    info.nShow = SW_SHOWNORMAL;
    // failure without a last error must not read as success
    return ShellExecuteExW(&info) ? ERROR_SUCCESS : LastError();
}

}

std::vector<std::wstring> CommandLineArgs(const wchar_t* command_line, DWORD* error)
{
    int count = 0;
    const UniqueLocal<LPWSTR*> argv(CommandLineToArgvW(command_line ? command_line : GetCommandLineW(), &count));
    if (error)
    {
        *error = argv ? ERROR_SUCCESS : LastError();
    }
    return argv ? std::vector<std::wstring>(argv.get() + (count > 0 ? 1 : 0), argv.get() + count) : std::vector<std::wstring>();
}

std::wstring QuoteArgument(std::wstring_view argument)
{
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
    {
        return std::wstring(argument);
    }
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : argument)
    {
        if (character == L'"')
        {
            quoted.append(backslashes + 1, L'\\');
        }
        backslashes = character == L'\\' ? backslashes + 1 : 0;
        quoted.push_back(character);
    }
    quoted.append(backslashes, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

void AppendArgument(std::wstring* arguments, std::wstring_view argument)
{
    if (!arguments->empty())
    {
        arguments->push_back(L' ');
    }
    arguments->append(QuoteArgument(argument));
}

DWORD LaunchProcess(HWND owner, const std::wstring& exe, const std::wstring& arguments)
{
    return ShellLaunch(owner, L"open", exe, arguments);
}

DWORD LaunchElevated(HWND owner, const std::wstring& exe, const std::wstring& arguments)
{
    return ShellLaunch(owner, L"runas", exe, arguments);
}

DWORD LaunchAsSystem(const std::wstring& exe, const std::wstring& arguments, DWORD* restore_error)
{
    return LaunchImpersonated(exe, arguments, nullptr, restore_error);
}

DWORD LaunchAsTrustedInstaller(const std::wstring& exe, const std::wstring& arguments, DWORD* restore_error)
{
    return LaunchImpersonated(exe, arguments, L"TrustedInstaller", restore_error);
}

DWORD LaunchAsShellUser(const std::wstring& exe, const std::wstring& arguments, DWORD* restore_error)
{
    if (restore_error)
    {
        *restore_error = ERROR_SUCCESS;
    }
    if (exe.empty())
    {
        return ERROR_INVALID_PARAMETER;
    }
    // shell token gives the restart of a privileged process back to the signed-in user
    const UniqueHandle shell_token = OpenShellToken(TOKEN_DUPLICATE);
    UniqueHandle target_token;
    return shell_token && DuplicateTokenEx(shell_token.get(), MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary, target_token.put()) &&
                   LaunchWithToken(target_token.get(), exe, arguments, false)
               ? ERROR_SUCCESS
               : LastError();
}

ULONGLONG ProcessCreationTime(DWORD process_id)
{
    const UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
    return process ? CreationTime(process.get()) : 0;
}

DWORD WaitForProcessExit(DWORD process_id, ULONGLONG creation_time, DWORD timeout_ms)
{
    if (process_id == 0 || process_id == GetCurrentProcessId())
    {
        return ERROR_SUCCESS;
    }
    const UniqueHandle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
    if (!process)
    {
        const DWORD error = GetLastError();
        // no process has the id any more
        return error == ERROR_INVALID_PARAMETER ? ERROR_SUCCESS : error;
    }
    if (CreationTime(process.get()) != creation_time)
    {
        return ERROR_SUCCESS;
    }
    return WaitForSingleObject(process.get(), timeout_ms) == WAIT_TIMEOUT ? WAIT_TIMEOUT : ERROR_SUCCESS;
}

}
