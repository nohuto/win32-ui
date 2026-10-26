// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/theme/preset_editor.h"

#include "win32ui/dialog/dialog_layout.h"
#include "win32ui/dialog/dialog_window.h"
#include "win32ui/dialog/message_dialog.h"
#include "win32ui/list_view/sort.h"
#include "win32ui/menu/popup_menu.h"
#include "win32ui/theme/preset_editor_internal.h"
#include "win32ui/theme/theme.h"
#include "win32ui/theme/theme_presets_internal.h"

#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <cwctype>
#include <functional>
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

bool IsBuiltInThemePreset(std::wstring_view name)
{
    const std::vector<ThemePreset>& builtins = BuiltInThemePresets();
    return std::any_of(builtins.begin(), builtins.end(), [&](const ThemePreset& preset) { return EqualName(preset.name, name); });
}

// preset reader trims names
std::wstring Trimmed(std::wstring text)
{
    const auto space = [](wchar_t character) { return iswspace(character) != 0; };
    text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), space));
    text.erase(std::find_if_not(text.rbegin(), text.rend(), space).base(), text.end());
    return text;
}

class PresetEditor final : public DialogWindow
{
  public:
    PresetEditor(const std::vector<ThemePreset>& presets, const std::wstring& active_name, const PresetEditorHooks* hooks)
        : presets_(presets), active_name_(active_name), hooks_(hooks), templates_(BuiltInThemePresets())
    {
        if (hooks->list_hooks)
        {
            list_hooks_ = *hooks->list_hooks;
        }
        // row menu uses editor commands with app context
        list_hooks_.row_menu = [](void*, HWND list, int, POINT screen) {
            auto* editor = static_cast<PresetEditor*>(DialogWindow::FromWindow(GetParent(list)));
            return editor && editor->ShowMenu(list, screen);
        };
    }

  protected:
    HWND initial_focus() const override
    {
        return preset_list_;
    }

    void OnCreate() override
    {
        SetWindowTextW(hwnd(), Text(PresetEditorText::title));
        const std::pair<int, PresetEditorText> texts[] = {
            {kGroupPresetsId, PresetEditorText::presets},
            {kGroupColorsId, PresetEditorText::colors},
            {kGroupTemplatesId, PresetEditorText::templates},
            {kPresetNewId, PresetEditorText::new_preset},
            {kPresetDuplicateId, PresetEditorText::duplicate},
            {kPresetRenameId, PresetEditorText::rename},
            {kPresetDeleteId, PresetEditorText::delete_preset},
            {kPresetImportId, PresetEditorText::import_presets},
            {kPresetExportId, PresetEditorText::export_presets},
            {kPresetEditColorId, PresetEditorText::edit_color},
            {kPresetDarkId, PresetEditorText::dark},
            {kPresetApplyTemplateId, PresetEditorText::apply_template},
            {kPresetApplyId, PresetEditorText::apply},
            {IDOK, PresetEditorText::ok},
            {IDCANCEL, PresetEditorText::cancel},
        };
        for (const auto& [id, text] : texts)
        {
            SetDlgItemTextW(hwnd(), id, Text(text));
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
        SetListColumns(color_list_, {{Text(PresetEditorText::color_column), 150, 150}, {Text(PresetEditorText::hex_column), 90, 90}});
        ShowListGridButton(color_list_);
        for (const ThemePreset& preset : templates_)
        {
            SendMessageW(template_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(preset.name.c_str()));
        }
        SendMessageW(template_combo_, CB_SETCURSEL, 0, 0);
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
                copy.name = Unique(preset->name + Text(PresetEditorText::copy_suffix));
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
                const bool changed = Change([dark](ThemePreset& preset) {
                    preset.colors.dark = dark;
                    // file has no error color, choose the built in color for the preset dark flag
                    preset.colors.error = BuiltInThemeColors(dark).error;
                });
                if (!changed)
                {
                    SyncSelection();
                }
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
    const wchar_t* Text(PresetEditorText text) const
    {
        const wchar_t* value = hooks_->text ? hooks_->text(text) : nullptr;
        return value ? value : L"";
    }

    ThemePreset* Selected()
    {
        return selected_ >= 0 && static_cast<size_t>(selected_) < presets_.size() ? &presets_[static_cast<size_t>(selected_)] : nullptr;
    }

    int Find(std::wstring_view name) const
    {
        for (size_t index = 0; index < presets_.size(); ++index)
        {
            if (EqualName(presets_[index].name, name))
            {
                return static_cast<int>(index);
            }
        }
        return presets_.empty() ? -1 : 0;
    }

    // base or base with the first unused number, ignored may keep its own name
    std::wstring Unique(const std::wstring& base, const ThemePreset* ignored = nullptr) const
    {
        const auto taken = [&](const std::wstring& name) {
            return std::any_of(presets_.begin(), presets_.end(), [&](const ThemePreset& preset) { return &preset != ignored && EqualName(preset.name, name); });
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

    bool Prompt(PresetEditorText title, const std::wstring& initial, std::wstring* name)
    {
        std::wstring answer;
        if (!hooks_->prompt_name || !hooks_->prompt_name(hwnd(), Text(title), initial, &answer))
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

    // built ins stay unchanged, edits prompt for a named copy, false if nothing changed
    bool Change(const std::function<void(ThemePreset&)>& change)
    {
        ThemePreset* preset = Selected();
        if (!preset)
        {
            return false;
        }
        if (!IsBuiltInThemePreset(preset->name))
        {
            change(*preset);
            SyncSelection();
            return true;
        }
        std::wstring name;
        if (!Prompt(PresetEditorText::copy_title, Unique(preset->name + Text(PresetEditorText::copy_suffix)), &name))
        {
            return false;
        }
        ThemePreset copy = *preset;
        copy.name = Unique(name);
        change(copy);
        Add(std::move(copy));
        return true;
    }

    void NewPreset()
    {
        std::wstring name;
        if (!Prompt(PresetEditorText::new_title, L"", &name))
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
        if (!preset || IsBuiltInThemePreset(preset->name) || !Prompt(PresetEditorText::rename_title, preset->name, &name))
        {
            return;
        }
        preset->name = Unique(name, preset);
        RefreshPresets();
    }

    void Delete()
    {
        const ThemePreset* preset = Selected();
        if (!preset || IsBuiltInThemePreset(preset->name) || (hooks_->confirm_delete && !hooks_->confirm_delete(hwnd(), preset->name)))
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
            preset.name = Unique(Trimmed(preset.name));
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
        if (selected_ >= 0)
        {
            ListView_SetItemState(preset_list_, selected_, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(preset_list_, selected_, FALSE);
        }
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
                item.pszText = const_cast<wchar_t*>(Text(kColorFields[index].text));
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
                const int row = FindListViewItemByData(color_list_, field);
                if (row >= 0)
                {
                    ListView_SetItemState(color_list_, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_EnsureVisible(color_list_, row, FALSE);
                }
            }
        }
        Button_SetCheck(dark_check_, preset && preset->colors.dark ? BST_CHECKED : BST_UNCHECKED);
        for (const int id : {kPresetDuplicateId, kPresetEditColorId, kPresetDarkId, kPresetApplyTemplateId})
        {
            EnableWindow(GetDlgItem(hwnd(), id), preset != nullptr);
        }
        const bool own = preset && !IsBuiltInThemePreset(preset->name);
        EnableWindow(GetDlgItem(hwnd(), kPresetRenameId), own);
        EnableWindow(GetDlgItem(hwnd(), kPresetDeleteId), own);
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
            const ThemePreset* preset = Selected();
            const UINT own_flags = MF_STRING | (preset && !IsBuiltInThemePreset(preset->name) ? 0 : MF_GRAYED);
            AppendMenuItem(menu, MF_STRING, kPresetNewId, Text(PresetEditorText::new_preset));
            AppendMenuItem(menu, row_flags, kPresetDuplicateId, Text(PresetEditorText::duplicate));
            AppendMenuItem(menu, own_flags, kPresetRenameId, Text(PresetEditorText::rename));
            AppendMenuItem(menu, own_flags, kPresetDeleteId, Text(PresetEditorText::delete_preset));
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuItem(menu, MF_STRING, kPresetImportId, Text(PresetEditorText::import_presets));
            AppendMenuItem(menu, MF_STRING, kPresetExportId, Text(PresetEditorText::export_presets));
        }
        else
        {
            AppendMenuItem(menu, row_flags, kPresetEditColorId, Text(PresetEditorText::edit_color));
        }
        // shared Copy and Select all commands, disable Select all for single selection lists
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        const wchar_t* copy = list_hooks_.label ? list_hooks_.label(ListLabel::copy) : nullptr;
        const wchar_t* select_all = list_hooks_.label ? list_hooks_.label(ListLabel::select_all) : nullptr;
        const bool can_select_all = ListView_GetItemCount(list) > 0 && !(GetWindowLongPtrW(list, GWL_STYLE) & LVS_SINGLESEL);
        AppendMenuItem(menu, row_flags, kCopyCommand, copy ? copy : L"", L"Ctrl+C");
        AppendMenuItem(menu, MF_STRING | (can_select_all ? 0 : MF_GRAYED), kSelectAllCommand, select_all ? select_all : L"", L"Ctrl+A");
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
            return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, editor->Text(kColorFields[static_cast<size_t>(left)].text), -1, editor->Text(kColorFields[static_cast<size_t>(right)].text), -1, nullptr, nullptr, 0) - CSTR_EQUAL;
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

} // namespace

void ShowThemePresetEditor(HWND owner, const std::vector<ThemePreset>& presets, const std::wstring& active_name, const PresetEditorHooks* hooks)
{
    if (!hooks)
    {
        return;
    }
    PresetEditor editor(presets, active_name, hooks);
    editor.RunModal(owner, L"WIN32UI_THEME_PRESETS");
}

} // namespace win32ui
