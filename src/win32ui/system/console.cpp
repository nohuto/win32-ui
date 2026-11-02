// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/console.h"

#include "win32ui/system/strings.h"

#include <algorithm>
#include <string>

namespace win32ui
{

namespace
{

// Windows 7 passes the pseudo handles of the parent console to GUI children, invalid until attached
bool Usable(HANDLE handle)
{
    return handle && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

struct Streams
{
    HANDLE output = nullptr;
    HANDLE error = nullptr;
};

const Streams& ConsoleStreams()
{
    static const Streams streams = [] {
        Streams result{GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE)};
        if (!Usable(result.output))
        {
            if (!AttachConsole(ATTACH_PARENT_PROCESS))
            {
                AllocConsole();
            }
            result.output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        }
        if (!Usable(result.error))
        {
            result.error = result.output;
        }
        return result;
    }();
    return streams;
}

// both write calls may take less than asked
template <typename Write>
DWORD WriteAll(size_t count, Write&& write)
{
    for (size_t done = 0; done < count;)
    {
        DWORD written = 0;
        if (!write(done, static_cast<DWORD>((std::min<size_t>)(count - done, 16384)), &written) || written == 0)
        {
            const DWORD error = GetLastError();
            return error != ERROR_SUCCESS ? error : ERROR_WRITE_FAULT;
        }
        done += written;
    }
    return ERROR_SUCCESS;
}

}

DWORD WriteConsoleText(ConsoleStream stream, std::wstring_view text)
{
    const HANDLE handle = stream == ConsoleStream::output ? ConsoleStreams().output : ConsoleStreams().error;
    if (!Usable(handle))
    {
        return ERROR_INVALID_HANDLE;
    }
    if (GetFileType(handle) == FILE_TYPE_CHAR)
    {
        return WriteAll(text.size(), [&](size_t offset, DWORD count, DWORD* written) {
            // surrogate pair split over two writes shows as two replacement characters
            if (offset + count < text.size() && count > 1 && IS_HIGH_SURROGATE(text[offset + count - 1]))
            {
                --count;
            }
            return WriteConsoleW(handle, text.data() + offset, count, written, nullptr);
        });
    }
    const UINT code_page = GetConsoleOutputCP();
    const std::string narrow = WideToMultiByte(text, code_page ? code_page : CP_OEMCP);
    return WriteAll(narrow.size(), [&](size_t offset, DWORD count, DWORD* written) { return WriteFile(handle, narrow.data() + offset, count, written, nullptr); });
}

bool AttachTerminal()
{
    return Usable(GetStdHandle(STD_ERROR_HANDLE)) || AttachConsole(ATTACH_PARENT_PROCESS);
}

}
