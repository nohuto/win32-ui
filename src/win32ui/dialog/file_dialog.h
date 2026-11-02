// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <span>
#include <string>
#include <string_view>

namespace win32ui
{

struct FileDialogChoice
{
    DWORD id = 0;
    const wchar_t* text = nullptr;
};

struct FileDialogSpec
{
    const wchar_t* filter = nullptr;
    const wchar_t* title = nullptr;
    std::wstring_view initial_name;
    std::wstring_view folder;
    const wchar_t* choice_label = nullptr;
    std::span<const FileDialogChoice> choices;
    DWORD* choice = nullptr;
};

// call on an STA thread, as SYSTEM the default folder is the Documents folder of the user whose shell runs
DWORD ChooseFileToOpen(HWND owner, const FileDialogSpec& spec, std::wstring* path);
DWORD ChooseFileToSave(HWND owner, const FileDialogSpec& spec, std::wstring* path);
DWORD ChooseFolder(HWND owner, std::wstring* path);
DWORD ChooseComputer(HWND owner, std::wstring* name);
// asked once per process as SYSTEM when its own Desktop folder is missing (the dialog would show "Location is not
// available")
void SetMissingDesktopPrompt(bool (*prompt)(HWND owner, const wchar_t* path));

}
