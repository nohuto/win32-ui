// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/dialog/file_dialog.h"

#include "win32ui/dialog/dialog_window.h"
#include "win32ui/dialog/file_dialog_internal.h"
#include "win32ui/handle_owner.h"
#include "win32ui/system/error_text.h"
#include "win32ui/system/process_token.h"
#include "win32ui/text/text.h"

#include <objsel.h>
#include <shlobj.h>

#include <algorithm>
#include <memory>
#include <utility>

namespace win32ui
{

namespace
{

constexpr DWORD kChoiceGroup = 1;
constexpr DWORD kChoiceCombo = 2;

bool (*g_missing_desktop_prompt)(HWND, const wchar_t*) = nullptr;

void SetFolder(IFileDialog* dialog, std::wstring_view folder, bool as_default)
{
    UniqueCom<IShellItem> item;
    if (folder.empty() || FAILED(SHCreateItemFromParsingName(std::wstring(folder).c_str(), nullptr, IID_PPV_ARGS(item.put()))))
    {
        return;
    }
    if (as_default)
    {
        dialog->SetDefaultFolder(item.get());
    }
    else
    {
        dialog->SetFolder(item.get());
    }
}

void PrepareForSystem(HWND owner, IFileDialog* dialog)
{
    if (!IsProcessSystem())
    {
        return;
    }
    UniqueCoTask<PWSTR> own_desktop;
    if (g_missing_desktop_prompt && SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DONT_VERIFY, nullptr, own_desktop.put())) &&
        GetFileAttributesW(own_desktop.get()) == INVALID_FILE_ATTRIBUTES && std::exchange(g_missing_desktop_prompt, nullptr)(owner, own_desktop.get()))
    {
        CreateDirectoryW(own_desktop.get(), nullptr);
    }
    SetFolder(dialog, ShellUserDocuments(), true);
}

// IFileDialog::Close leaves the dialog open unless called from its own event callbacks
void CloseFileDialog(void* dialog)
{
    UniqueCom<IOleWindow> window;
    HWND hwnd = nullptr;
    if (SUCCEEDED(static_cast<IFileDialog*>(dialog)->QueryInterface(IID_PPV_ARGS(window.put()))) && SUCCEEDED(window.get()->GetWindow(&hwnd)) && hwnd)
    {
        PostMessageW(hwnd, WM_COMMAND, IDCANCEL, 0);
    }
}

HRESULT AddChoices(IFileDialogCustomize* customize, const FileDialogSpec& spec)
{
    HRESULT hr = customize->StartVisualGroup(kChoiceGroup, Text(spec.choice_label ? spec.choice_label : L""));
    if (SUCCEEDED(hr))
    {
        hr = customize->AddComboBox(kChoiceCombo);
    }
    for (const FileDialogChoice& choice : spec.choices)
    {
        if (SUCCEEDED(hr))
        {
            hr = customize->AddControlItem(kChoiceCombo, choice.id, Text(choice.text));
        }
    }
    if (SUCCEEDED(hr))
    {
        hr = customize->EndVisualGroup();
    }
    return SUCCEEDED(hr) ? customize->SetSelectedControlItem(kChoiceCombo, *spec.choice) : hr;
}

DWORD ShowFileDialog(HWND owner, FileDialogKind kind, const FileDialogSpec& spec, std::wstring* path)
{
    if (!path || (spec.choice && std::none_of(spec.choices.begin(), spec.choices.end(), [&](const FileDialogChoice& choice) { return choice.id == *spec.choice; })))
    {
        return ERROR_INVALID_PARAMETER;
    }
    UniqueCom<IFileDialog> dialog;
    HRESULT hr = CoCreateInstance(kind == FileDialogKind::save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));
    const std::vector<COMDLG_FILTERSPEC> filters = ParseFileFilter(spec.filter);
    if (SUCCEEDED(hr))
    {
        hr = ConfigureFileDialog(dialog.get(), kind, spec, filters);
    }
    if (FAILED(hr))
    {
        return Win32FromHResult(hr);
    }
    PrepareForSystem(owner, dialog.get());
    UniqueCom<IFileDialogCustomize> customize;
    if (spec.choice)
    {
        hr = dialog.get()->QueryInterface(IID_PPV_ARGS(customize.put()));
        if (SUCCEEDED(hr))
        {
            hr = AddChoices(customize.get(), spec);
        }
        if (FAILED(hr))
        {
            return Win32FromHResult(hr);
        }
    }
    {
        const ModalScope modal(CloseFileDialog, dialog.get());
        hr = dialog.get()->Show(owner);
    }
    DWORD chosen = 0;
    if (SUCCEEDED(hr) && customize)
    {
        hr = customize.get()->GetSelectedControlItem(kChoiceCombo, &chosen);
    }
    UniqueCom<IShellItem> item;
    UniqueCoTask<PWSTR> text;
    if (SUCCEEDED(hr))
    {
        hr = dialog.get()->GetResult(item.put());
    }
    if (SUCCEEDED(hr))
    {
        hr = item.get()->GetDisplayName(SIGDN_FILESYSPATH, text.put());
    }
    if (FAILED(hr))
    {
        return Win32FromHResult(hr);
    }
    path->assign(text.get());
    // outputs only change together, on success
    if (customize)
    {
        *spec.choice = chosen;
    }
    return ERROR_SUCCESS;
}

}

std::vector<COMDLG_FILTERSPEC> ParseFileFilter(const wchar_t* filter)
{
    std::vector<COMDLG_FILTERSPEC> filters;
    for (const wchar_t* cursor = filter; cursor && *cursor;)
    {
        const wchar_t* name = cursor;
        cursor += wcslen(cursor) + 1;
        if (!*cursor)
        {
            break;
        }
        filters.push_back({Text(name), cursor});
        cursor += wcslen(cursor) + 1;
    }
    return filters;
}

std::wstring DefaultExtension(const wchar_t* pattern)
{
    const std::wstring_view patterns = pattern ? pattern : L"";
    const std::wstring_view first = patterns.substr(0, patterns.find(L';'));
    if (!first.starts_with(L"*.") || first == L"*.*")
    {
        return {};
    }
    return std::wstring(first.substr(2));
}

HRESULT ConfigureFileDialog(IFileDialog* dialog, FileDialogKind kind, const FileDialogSpec& spec, const std::vector<COMDLG_FILTERSPEC>& filters)
{
    FILEOPENDIALOGOPTIONS options = 0;
    HRESULT hr = dialog->GetOptions(&options);
    if (FAILED(hr))
    {
        return hr;
    }
    options |= FOS_FORCEFILESYSTEM;
    options |= kind == FileDialogKind::save ? FOS_OVERWRITEPROMPT : kind == FileDialogKind::open ? FOS_FILEMUSTEXIST
                                                                                                 : FOS_PICKFOLDERS;
    hr = dialog->SetOptions(options);
    if (FAILED(hr))
    {
        return hr;
    }
    if (!filters.empty())
    {
        hr = dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());
        if (FAILED(hr))
        {
            return hr;
        }
        dialog->SetFileTypeIndex(1);
        if (const std::wstring extension = DefaultExtension(filters.front().pszSpec); !extension.empty())
        {
            dialog->SetDefaultExtension(extension.c_str());
        }
    }
    if (spec.title)
    {
        dialog->SetTitle(Text(spec.title));
    }
    const size_t slash = spec.initial_name.find_last_of(L"\\/");
    SetFolder(dialog, slash == std::wstring_view::npos ? spec.folder : spec.initial_name.substr(0, slash + 1), false);
    if (const std::wstring_view name = spec.initial_name.substr(slash == std::wstring_view::npos ? 0 : slash + 1); !name.empty())
    {
        dialog->SetFileName(std::wstring(name).c_str());
    }
    return S_OK;
}

DWORD ChooseFileToOpen(HWND owner, const FileDialogSpec& spec, std::wstring* path)
{
    return ShowFileDialog(owner, FileDialogKind::open, spec, path);
}

DWORD ChooseFileToSave(HWND owner, const FileDialogSpec& spec, std::wstring* path)
{
    return ShowFileDialog(owner, FileDialogKind::save, spec, path);
}

DWORD ChooseFolder(HWND owner, std::wstring* path)
{
    return ShowFileDialog(owner, FileDialogKind::folder, {}, path);
}

DWORD ChooseComputer(HWND owner, std::wstring* name)
{
    if (!name)
    {
        return ERROR_INVALID_PARAMETER;
    }
    name->clear();
    UniqueCom<IDsObjectPicker> picker;
    HRESULT hr = CoCreateInstance(CLSID_DsObjectPicker, nullptr, CLSCTX_INPROC_SERVER, IID_IDsObjectPicker, reinterpret_cast<void**>(picker.put()));
    if (FAILED(hr))
    {
        return Win32FromHResult(hr);
    }
    DSOP_SCOPE_INIT_INFO scope = {};
    scope.cbSize = sizeof(scope);
    scope.flType = DSOP_SCOPE_TYPE_UPLEVEL_JOINED_DOMAIN | DSOP_SCOPE_TYPE_DOWNLEVEL_JOINED_DOMAIN | DSOP_SCOPE_TYPE_ENTERPRISE_DOMAIN |
                   DSOP_SCOPE_TYPE_GLOBAL_CATALOG | DSOP_SCOPE_TYPE_EXTERNAL_UPLEVEL_DOMAIN | DSOP_SCOPE_TYPE_EXTERNAL_DOWNLEVEL_DOMAIN |
                   DSOP_SCOPE_TYPE_WORKGROUP | DSOP_SCOPE_TYPE_USER_ENTERED_UPLEVEL_SCOPE | DSOP_SCOPE_TYPE_USER_ENTERED_DOWNLEVEL_SCOPE;
    scope.flScope = DSOP_SCOPE_FLAG_STARTING_SCOPE | DSOP_SCOPE_FLAG_DEFAULT_FILTER_COMPUTERS;
    scope.FilterFlags.Uplevel.flBothModes = DSOP_FILTER_COMPUTERS;
    scope.FilterFlags.flDownlevel = DSOP_DOWNLEVEL_FILTER_COMPUTERS;
    DSOP_INIT_INFO init = {};
    init.cbSize = sizeof(init);
    init.cDsScopeInfos = 1;
    init.aDsScopeInfos = &scope;
    hr = picker.get()->Initialize(&init);
    if (FAILED(hr))
    {
        return Win32FromHResult(hr);
    }
    UniqueCom<IDataObject> selection;
    {
        // picker has no way to close it from outside
        const ModalScope modal;
        hr = picker.get()->InvokeDialog(owner, selection.put());
    }
    if (FAILED(hr))
    {
        return Win32FromHResult(hr);
    }
    if (hr == S_FALSE || !selection)
    {
        return ERROR_CANCELLED;
    }
    FORMATETC format = {};
    format.cfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_DSOP_DS_SELECTION_LIST));
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;
    STGMEDIUM medium = {};
    hr = selection.get()->GetData(&format, &medium);
    if (FAILED(hr))
    {
        return Win32FromHResult(hr);
    }
    const std::unique_ptr<STGMEDIUM, decltype(&ReleaseStgMedium)> release(&medium, ReleaseStgMedium);
    if (const auto* list = static_cast<const DS_SELECTION_LIST*>(GlobalLock(medium.hGlobal)))
    {
        if (list->cItems > 0 && list->aDsSelection[0].pwzName)
        {
            name->assign(list->aDsSelection[0].pwzName);
        }
        GlobalUnlock(medium.hGlobal);
    }
    if (!name->empty() && name->back() == L'$')
    {
        name->pop_back();
    }
    return name->empty() ? ERROR_CANCELLED : ERROR_SUCCESS;
}

void SetMissingDesktopPrompt(bool (*prompt)(HWND owner, const wchar_t* path))
{
    g_missing_desktop_prompt = prompt;
}

}
