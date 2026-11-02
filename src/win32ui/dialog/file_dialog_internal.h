// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "win32ui/dialog/file_dialog.h"

#include <shobjidl.h>

#include <string>
#include <vector>

namespace win32ui
{

enum class FileDialogKind
{
    open,
    save,
    folder,
};

std::vector<COMDLG_FILTERSPEC> ParseFileFilter(const wchar_t* filter);
std::wstring DefaultExtension(const wchar_t* pattern);
// filters must outlive the dialog
HRESULT ConfigureFileDialog(IFileDialog* dialog, FileDialogKind kind, const FileDialogSpec& spec, const std::vector<COMDLG_FILTERSPEC>& filters);

}
