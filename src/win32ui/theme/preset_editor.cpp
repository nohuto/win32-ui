// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/theme/preset_editor.h"

#include "win32ui/dialog/combo_box.h"
#include "win32ui/dialog/dialog_layout.h"
#include "win32ui/dialog/dialog_window.h"
#include "win32ui/dialog/message_dialog.h"
#include "win32ui/list_view/sort.h"
#include "win32ui/menu/popup_menu.h"
#include "win32ui/system/strings.h"
#include "win32ui/text/text.h"
#include "win32ui/theme/preset_editor_internal.h"
#include "win32ui/theme/theme.h"
#include "win32ui/theme/theme_presets_internal.h"

#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <iterator>

namespace win32ui
{

namespace
{

constexpr int kGroupPresetsId = 2114;
constexpr int kGroupColorsId = 2115;
constexpr int kGroupTemplatesId = 2116;
constexpr int kCopyCommand = 0x7F00;
constexpr int kSelectAllCommand = 0x7F01;
constexpr const wchar_t* kCopySuffix = L" Copy";

// name is one line of the preset file and the reader trims it
std::wstring Trimmed(std::wstring text)
{
    std::erase_if(text, [](wchar_t character) { return character < 0x20 || character == 0x7F; });
    return std::wstring(TrimView(text));
}

class PresetEditor final : public DialogWindow
{
  public:
    PresetEditor(const std::vector<ThemePreset>& presets, const std::wstring& active_name, const PresetEditorHooks* hooks)
        : presets_(presets), active_name_(active_name), hooks_(hooks), templates_(BuiltInThemePresets()),
          // row menu uses editor commands with app context
          list_hooks_(DialogListHooks([](void*, HWND list, int, POINT screen) {
              auto* editor = static_cast<PresetEditor*>(DialogWindow::FromWindow(GetParent(list)));
              return editor && editor->ShowMenu(list, screen);
          }))
    {
    }

  protected:
    HWND initial_focus() const override
    {
        return preset_list_;
    }

    void OnCreate() override
    {
        SetWindowTextW(hwnd(), Text(L"Theme Presets"));
        const std::pair<int, const wchar_t*> texts[] = {
            {kGroupPresetsId, Text(L"Presets")},
            {kGroupColorsId, Text(L"Colors")},
            {kGroupTemplatesId, Text(L"Templates")},
            {kPresetNewId, Text(L"New...")},
            {kPresetDuplicateId, Text(L"Duplicate")},
            {kPresetRenameId, Text(L"Rename...")},
            {kPresetDeleteId, Text(L"Delete")},
            {kPresetImportId, Text(L"Import...")},
            {kPresetExportId, Text(L"Export...")},
            {kPresetEditColorId, Text(L"Edit Color...")},
            {kPresetDarkId, Text(L"Treat as dark theme")},
            {kPresetApplyTemplateId, Text(L"Apply Template")},
            {kPresetApplyId, Text(L"Apply")},
            {IDOK, Text(L"OK")},
            {IDCANCEL, Text(L"Cancel")},
        };
        for (const auto& [id, text] : texts)
        {
            SetDlgItemTextW(hwnd(), id, text);
        }
        // template was already fitted with empty text
        FitDialogText(hwnd());

        preset_list_ = GetDlgItem(hwnd(), kPresetListId);
        color_list_ = GetDlgItem(hwnd(), kPresetColorsId);
        dark_check_ = GetDlgItem(hwnd(), kPresetDarkId);
        template_combo_ = GetDlgItem(hwnd(), kPresetTemplateId);
        // preset names use one headerless column, so no grid button appears
        AttachDialogListView(preset_list_, &list_hooks_);
        SetListColumns(preset_list_, {{L"", 120, 120}});
        AttachDialogListView(color_list_, &list_hooks_);
        SetListColumns(color_list_, {{Text(L"Color"), 150, 150}, {Text(L"Hex"), 90, 90}});
        ShowListGridButton(color_list_);
        std::vector<std::wstring> names;
        names.reserve(templates_.size());
        for (const ThemePreset& preset : templates_)
        {
            names.push_back(preset.name);
        }
        SetComboItems(template_combo_, names);
        SetComboIndex(template_combo_, 0);
        selected_ = Find(active_name_);
        RefreshPresets();
    }

    void OnLayout(int, int) override
    {
    }

    bool OnCommand(int id, int, HWND) override
    {
        switch (id)
        {
        case kPresetNewId:
            NewPreset();
            return true;
        case kPresetDuplicateId:
            if (const ThemePreset* preset = Selected())
            {
                ThemePreset copy = *preset;
                copy.name = Unique(preset->name + kCopySuffix);
                Add(std::move(copy));
            }
            return true;
        case kPresetRenameId:
            Rename();
            return true;
        case kPresetDeleteId:
            Delete();
            return true;
        case kPresetImportId:
            Import();
            return true;
        case kPresetExportId:
            if (hooks_->export_presets)
            {
                hooks_->export_presets(hwnd(), presets_);
            }
            return true;
        case kPresetEditColorId:
            EditColor();
            return true;
        case kPresetDarkId:
            {
                const bool dark = Button_GetCheck(dark_check_) == BST_CHECKED;
                Change([dark](ThemePreset& preset) {
                    preset.colors.dark = dark;
                    // file has no error color, choose the built in color for the preset dark flag
                    preset.colors.error = BuiltInThemeColors(dark).error;
                });
                return true;
            }
        case kPresetApplyTemplateId:
            {
                const int chosen = static_cast<int>(SendMessageW(template_combo_, CB_GETCURSEL, 0, 0));
                const ThemeColors colors = templates_[chosen >= 0 && static_cast<size_t>(chosen) < templates_.size() ? static_cast<size_t>(chosen) : 0].colors;
                Change([&colors](ThemePreset& preset) { preset.colors = colors; });
                return true;
            }
        case kPresetApplyId:
            Apply();
            return true;
        case IDOK:
            Apply();
            End(IDOK);
            return true;
        case kCopyCommand:
            // leave clipboard unchanged when nothing can be copied
            if (const std::wstring text = SelectedRowsText(GetFocus() == color_list_ ? color_list_ : preset_list_); !text.empty())
            {
                CopyTextToClipboard(hwnd(), text);
            }
            return true;
        case kSelectAllCommand:
            SelectAllListItems(GetFocus() == color_list_ ? color_list_ : preset_list_);
            return true;
        default:
            return false;
        }
    }

    LRESULT OnNotify(NMHDR* header, bool* handled) override
    {
        *handled = false;
        if (header->hwndFrom == preset_list_ && header->code == LVN_ITEMCHANGED)
        {
            const auto* change = reinterpret_cast<NMLISTVIEW*>(header);
            LPARAM data = -1;
            if ((change->uNewState & LVIS_SELECTED) && ListViewItemData(preset_list_, change->iItem, &data))
            {
                selected_ = static_cast<int>(data);
                SyncSelection();
            }
            // commands use list selection, none if the user cleared it
            else if ((change->uOldState & LVIS_SELECTED) && !(change->uNewState & LVIS_SELECTED) && ListView_GetSelectedCount(preset_list_) == 0)
            {
                selected_ = -1;
                SyncSelection();
            }
        }
        else if (header->hwndFrom == color_list_ && header->code == NM_DBLCLK)
        {
            EditColor();
        }
        else if (header->hwndFrom == color_list_ && header->code == LVN_COLUMNCLICK)
        {
            SortListViewItems(color_list_, reinterpret_cast<NMLISTVIEW*>(header)->iSubItem, true, &sort_column_, &sort_ascending_, &sort_earlier_, CompareColors, this);
        }
        return 0;
    }

  private:
    ThemePreset* Selected()
    {
        return selected_ >= 0 && static_cast<size_t>(selected_) < presets_.size() ? &presets_[static_cast<size_t>(selected_)] : nullptr;
    }

    // custom theme needs a preset, the last one stays
    bool CanDelete()
    {
        return Selected() && presets_.size() > 1;
    }

    int Find(std::wstring_view name) const
    {
        for (size_t index = 0; index < presets_.size(); ++index)
        {
            if (EqualsInsensitive(presets_[index].name, name))
            {
                return static_cast<int>(index);
            }
        }
        return presets_.empty() ? -1 : 0;
    }

    std::wstring Unique(const std::wstring& base, const ThemePreset* ignored = nullptr) const
    {
        const auto taken = [&](const std::wstring& name) {
            return std::any_of(presets_.begin(), presets_.end(), [&](const ThemePreset& preset) { return &preset != ignored && EqualsInsensitive(preset.name, name); });
        };
        if (!taken(base))
        {
            return base;
        }
        for (int number = 2;; ++number)
        {
            std::wstring candidate = base + L" " + std::to_wstring(number);
            if (!taken(candidate))
            {
                return candidate;
            }
        }
    }

    bool Prompt(const wchar_t* title, const std::wstring& initial, std::wstring* name)
    {
        std::wstring answer;
        if (!hooks_->prompt_name || !hooks_->prompt_name(hwnd(), title, initial, &answer))
        {
            return false;
        }
        *name = Trimmed(std::move(answer));
        return !name->empty();
    }

    void Add(ThemePreset preset)
    {
        presets_.push_back(std::move(preset));
        selected_ = static_cast<int>(presets_.size()) - 1;
        RefreshPresets();
    }

    template <typename Edit>
    void Change(const Edit& change)
    {
        if (ThemePreset* preset = Selected())
        {
            change(*preset);
        }
        SyncSelection();
    }

    void NewPreset()
    {
        std::wstring name;
        if (!Prompt(Text(L"New Preset"), L"", &name))
        {
            return;
        }
        const int chosen = static_cast<int>(SendMessageW(template_combo_, CB_GETCURSEL, 0, 0));
        ThemePreset preset = templates_[chosen >= 0 && static_cast<size_t>(chosen) < templates_.size() ? static_cast<size_t>(chosen) : 0];
        preset.name = Unique(name);
        Add(std::move(preset));
    }

    void Rename()
    {
        ThemePreset* preset = Selected();
        std::wstring name;
        if (!preset || !Prompt(Text(L"Rename Preset"), preset->name, &name))
        {
            return;
        }
        preset->name = Unique(name, preset);
        RefreshPresets();
    }

    void Delete()
    {
        const ThemePreset* preset = Selected();
        if (!CanDelete() || (hooks_->confirm_delete && !hooks_->confirm_delete(hwnd(), preset->name)))
        {
            return;
        }
        presets_.erase(presets_.begin() + selected_);
        selected_ = std::min(selected_, static_cast<int>(presets_.size()) - 1);
        RefreshPresets();
    }

    void Import()
    {
        std::vector<ThemePreset> imported;
        if (!hooks_->import_presets || !hooks_->import_presets(hwnd(), &imported) || imported.empty())
        {
            return;
        }
        for (ThemePreset& preset : imported)
        {
            const std::wstring name = Trimmed(preset.name);
            if (name.empty())
            {
                continue;
            }
            preset.name = Unique(name);
            presets_.push_back(std::move(preset));
        }
        selected_ = static_cast<int>(presets_.size()) - 1;
        RefreshPresets();
    }

    void EditColor()
    {
        ThemePreset* preset = Selected();
        LPARAM field = -1;
        if (!preset || !SelectedListViewData(color_list_, &field) || field < 0 || static_cast<size_t>(field) >= std::size(kColorFields))
        {
            return;
        }
        const auto member = kColorFields[static_cast<size_t>(field)].member;
        CHOOSECOLORW choose = {sizeof(choose)};
        choose.hwndOwner = hwnd();
        choose.rgbResult = preset->colors.*member;
        choose.lpCustColors = custom_colors_;
        choose.Flags = CC_FULLOPEN | CC_RGBINIT;
        ModalScope modal;
        if (ChooseColorW(&choose))
        {
            const COLORREF color = choose.rgbResult;
            Change([member, color](ThemePreset& changed) { changed.colors.*member = color; });
        }
    }

    void Apply()
    {
        if (const ThemePreset* preset = Selected())
        {
            active_name_ = preset->name;
        }
        if (hooks_->apply)
        {
            hooks_->apply(hooks_->context, presets_, active_name_);
        }
        InvalidateRect(hwnd(), nullptr, TRUE);
    }

    void RefreshPresets()
    {
        ListView_DeleteAllItems(preset_list_);
        for (size_t index = 0; index < presets_.size(); ++index)
        {
            LVITEMW item = {};
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = static_cast<int>(index);
            item.pszText = const_cast<wchar_t*>(presets_[index].name.c_str());
            item.lParam = static_cast<LPARAM>(index);
            ListView_InsertItem(preset_list_, &item);
        }
        if (selected_ < 0 || static_cast<size_t>(selected_) >= presets_.size())
        {
            selected_ = presets_.empty() ? -1 : 0;
        }
        SelectOnlyListRow(preset_list_, selected_);
        ListView_SetColumnWidth(preset_list_, 0, LVSCW_AUTOSIZE_USEHEADER);
        SyncSelection();
    }

    void SyncSelection()
    {
        const ThemePreset* preset = Selected();
        LPARAM field = -1;
        const bool had_field = SelectedListViewData(color_list_, &field);
        ListView_DeleteAllItems(color_list_);
        if (preset)
        {
            for (size_t index = 0; index < std::size(kColorFields); ++index)
            {
                const std::wstring hex = FormatThemeColor(preset->colors.*kColorFields[index].member);
                LVITEMW item = {};
                item.mask = LVIF_TEXT | LVIF_PARAM;
                item.iItem = static_cast<int>(index);
                item.pszText = const_cast<wchar_t*>(Text(kColorFields[index].label));
                item.lParam = static_cast<LPARAM>(index);
                const int row = ListView_InsertItem(color_list_, &item);
                ListView_SetItemText(color_list_, row, 1, const_cast<wchar_t*>(hex.c_str()));
            }
            if (sort_column_ >= 0)
            {
                SortListViewItems(color_list_, sort_column_, false, &sort_column_, &sort_ascending_, &sort_earlier_, CompareColors, this);
            }
            if (had_field)
            {
                SelectOnlyListRow(color_list_, FindListViewItemByData(color_list_, field));
            }
        }
        Button_SetCheck(dark_check_, preset && preset->colors.dark ? BST_CHECKED : BST_UNCHECKED);
        for (const int id : {kPresetDuplicateId, kPresetRenameId, kPresetEditColorId, kPresetDarkId, kPresetApplyTemplateId})
        {
            EnableWindow(GetDlgItem(hwnd(), id), preset != nullptr);
        }
        const HWND delete_button = GetDlgItem(hwnd(), kPresetDeleteId);
        if (!CanDelete() && GetFocus() == delete_button)
        {
            SendMessageW(hwnd(), WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(preset_list_), TRUE);
        }
        EnableWindow(delete_button, CanDelete());
    }

    bool ShowMenu(HWND list, POINT screen)
    {
        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return true;
        }
        const bool row = ListView_GetSelectedCount(list) > 0;
        const UINT row_flags = MF_STRING | (row ? 0 : MF_GRAYED);
        if (list == preset_list_)
        {
            AppendMenuItem(menu, MF_STRING, kPresetNewId, Text(L"New..."));
            AppendMenuItem(menu, row_flags, kPresetDuplicateId, Text(L"Duplicate"));
            AppendMenuItem(menu, row_flags, kPresetRenameId, Text(L"Rename..."));
            AppendMenuItem(menu, MF_STRING | (CanDelete() ? 0 : MF_GRAYED), kPresetDeleteId, Text(L"Delete"));
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuItem(menu, MF_STRING, kPresetImportId, Text(L"Import..."));
            AppendMenuItem(menu, MF_STRING, kPresetExportId, Text(L"Export..."));
        }
        else
        {
            AppendMenuItem(menu, row_flags, kPresetEditColorId, Text(L"Edit Color..."));
        }

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        const bool can_select_all = ListView_GetItemCount(list) > 0 && !(GetWindowLongPtrW(list, GWL_STYLE) & LVS_SINGLESEL);
        AppendMenuItem(menu, row_flags, kCopyCommand, Text(L"&Copy"), L"Ctrl+C");
        AppendMenuItem(menu, MF_STRING | (can_select_all ? 0 : MF_GRAYED), kSelectAllCommand, Text(L"Select &All"), L"Ctrl+A");
        SetFocus(list);
        const int chosen = TrackContextMenu(menu, screen, hwnd());
        if (chosen != 0)
        {
            SendMessageW(hwnd(), WM_COMMAND, MAKEWPARAM(chosen, 0), 0);
        }
        return true;
    }

    static int CALLBACK CompareColors(LPARAM left, LPARAM right, int column, void* context)
    {
        auto* editor = static_cast<PresetEditor*>(context);
        const ThemePreset* preset = editor->Selected();
        if (!preset || left < 0 || right < 0 || static_cast<size_t>(left) >= std::size(kColorFields) || static_cast<size_t>(right) >= std::size(kColorFields))
        {
            return 0;
        }
        if (column == 0)
        {
            return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, Text(kColorFields[static_cast<size_t>(left)].label), -1, Text(kColorFields[static_cast<size_t>(right)].label), -1, nullptr, nullptr, 0) - CSTR_EQUAL;
        }
        // COLORREF stores blue in the high byte, display red first
        const auto shown = [preset](LPARAM field) {
            const COLORREF color = preset->colors.*kColorFields[static_cast<size_t>(field)].member;
            return (static_cast<DWORD>(GetRValue(color)) << 16) | (static_cast<DWORD>(GetGValue(color)) << 8) | GetBValue(color);
        };
        const DWORD a = shown(left);
        const DWORD b = shown(right);
        return (a > b) - (a < b);
    }

    std::vector<ThemePreset> presets_;
    std::wstring active_name_;
    const PresetEditorHooks* hooks_;
    const std::vector<ThemePreset>& templates_;
    ListViewHooks list_hooks_;
    HWND preset_list_ = nullptr;
    HWND color_list_ = nullptr;
    HWND dark_check_ = nullptr;
    HWND template_combo_ = nullptr;
    int selected_ = -1;
    int sort_column_ = -1;
    bool sort_ascending_ = true;
    SortKeys sort_earlier_;
    COLORREF custom_colors_[16] = {};
};

}

void ShowThemePresetEditor(HWND owner, const std::vector<ThemePreset>& presets, const std::wstring& active_name, const PresetEditorHooks* hooks)
{
    if (!hooks)
    {
        return;
    }
    PresetEditor editor(presets, active_name, hooks);
    editor.RunModal(owner, L"WIN32UI_THEME_PRESETS");
}

}
