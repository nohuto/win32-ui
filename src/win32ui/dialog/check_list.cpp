// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/dialog/check_list.h"

#include "win32ui/dialog/dialog_layout.h"
#include "win32ui/dialog/dialog_metrics.h"
#include "win32ui/dialog/dialog_window.h"
#include "win32ui/text/text.h"

#include <windowsx.h>

#include <algorithm>

namespace win32ui
{

namespace
{

constexpr int kSelectAllId = 2401;
constexpr int kClearAllId = 2402;

class CheckListDialog final : public DialogWindow
{
  public:
    CheckListDialog(const CheckListSpec& spec, std::vector<size_t> checked)
        : spec_(spec), checked_(std::move(checked)), columns_(std::max(1, spec.columns))
    {
    }

    std::vector<size_t>& checked() noexcept
    {
        return checked_;
    }

  protected:
    HWND initial_focus() const override
    {
        return checks_.empty() ? nullptr : checks_.front();
    }

    void OnCreate() override
    {
        SetWindowTextW(hwnd(), std::wstring(spec_.title).c_str());
        select_all_ = GetDlgItem(hwnd(), kSelectAllId);
        clear_all_ = GetDlgItem(hwnd(), kClearAllId);
        ok_button_ = GetDlgItem(hwnd(), IDOK);
        cancel_button_ = GetDlgItem(hwnd(), IDCANCEL);
        SetWindowTextW(select_all_, Text(L"Select All"));
        SetWindowTextW(clear_all_, Text(L"Clear All"));
        SetWindowTextW(ok_button_, Text(L"OK"));
        SetWindowTextW(cancel_button_, Text(L"Cancel"));
        // check boxes precede template buttons in tab order
        HWND previous = HWND_TOP;
        for (size_t index = 0; index < spec_.labels.size(); ++index)
        {
            HWND check = CreateWindowExW(0, L"BUTTON", spec_.labels[index].c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd(), nullptr, nullptr, nullptr);
            SetWindowPos(check, previous, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            previous = check;
            const bool checked = std::find(checked_.begin(), checked_.end(), index) != checked_.end();
            Button_SetCheck(check, checked ? BST_CHECKED : BST_UNCHECKED);
            // set font for measurement, dialog reapplies it after OnCreate
            SendMessageW(check, WM_SETFONT, reinterpret_cast<WPARAM>(fonts().ui()), FALSE);
            checks_.push_back(check);
        }
        MONITORINFO monitor = {sizeof(monitor)};
        RECT window = {};
        RECT client = {};
        if (GetMonitorInfoW(MonitorFromWindow(hwnd(), MONITOR_DEFAULTTONEAREST), &monitor) && GetWindowRect(hwnd(), &window) && GetClientRect(hwnd(), &client))
        {
            const int frame = window.right - window.left - client.right;
            const int room = monitor.rcWork.right - monitor.rcWork.left - frame;
            while (columns_ > 1 && ContentWidth(columns_) > room)
            {
                --columns_;
            }
            // template scales with font, beyond the work area use the width needed by columns
            if (client.right > room)
            {
                SetWindowPos(hwnd(), nullptr, 0, 0, ContentWidth(columns_) + frame, window.bottom - window.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        rows_per_column_ = std::max(1, (static_cast<int>(checks_.size()) + columns_ - 1) / columns_);
        FitDialogHeight(hwnd(), Px(metrics::kDialogContentMargin) + rows_per_column_ * TextPx(metrics::kRowPitch) + Px(metrics::kButtonGap) + TextPx(metrics::kButtonHeight) + Px(metrics::kDialogButtonBottomMargin));
    }

    void OnLayout(int width, int height) override
    {
        const int padding = Px(metrics::kDialogContentMargin);
        const int column_gap = Px(metrics::kBlockGap);
        const int row_step = TextPx(metrics::kRowPitch);
        const int button_height = TextPx(metrics::kButtonHeight);
        const int button_gap = Px(metrics::kButtonGap);
        const int select_all_width = std::max(Px(100), TextFitWidth(select_all_));
        const int clear_all_width = std::max(Px(90), TextFitWidth(clear_all_));
        const int button_y = height - Px(metrics::kDialogButtonBottomMargin) - button_height;
        PlaceButtonRow({ok_button_, cancel_button_}, width - Px(metrics::kDialogButtonRightMargin), button_y, Px(metrics::kButtonMinWidth), button_height, button_gap);
        if (GrowDialogWidth(hwnd(), ContentWidth(columns_)))
        {
            return;
        }
        int x = padding;
        for (int column = 0; column < columns_; ++column)
        {
            const int column_width = ColumnWidth(column, columns_);
            for (int row = 0; row < rows_per_column_ && column * rows_per_column_ + row < static_cast<int>(checks_.size()); ++row)
            {
                Place(checks_[static_cast<size_t>(column * rows_per_column_ + row)], x, padding + row * row_step, column_width, TextPx(metrics::kCheckHeight));
            }
            x += column_width + column_gap;
        }
        Place(select_all_, padding, button_y, select_all_width, button_height);
        Place(clear_all_, padding + select_all_width + button_gap, button_y, clear_all_width, button_height);
    }

    bool OnCommand(int id, int, HWND) override
    {
        switch (id)
        {
        case kSelectAllId:
        case kClearAllId:
            for (HWND check : checks_)
            {
                Button_SetCheck(check, id == kSelectAllId ? BST_CHECKED : BST_UNCHECKED);
            }
            return true;
        case IDOK:
            {
                std::vector<size_t> checked;
                for (size_t index = 0; index < checks_.size(); ++index)
                {
                    if (Button_GetCheck(checks_[index]) == BST_CHECKED)
                    {
                        checked.push_back(index);
                    }
                }
                if (spec_.accept && !spec_.accept(spec_.context, hwnd(), checked))
                {
                    return true;
                }
                checked_ = std::move(checked);
                End(IDOK);
                return true;
            }
        default:
            return false;
        }
    }

  private:
    int ColumnWidth(int column, int columns) const
    {
        const int count = static_cast<int>(checks_.size());
        const int rows = std::max(1, (count + columns - 1) / columns);
        int width = Px(spec_.column_width_dip);
        for (int index = column * rows; index < std::min(count, (column + 1) * rows); ++index)
        {
            width = std::max(width, TextFitWidth(checks_[static_cast<size_t>(index)]));
        }
        return width;
    }

    int ContentWidth(int columns) const
    {
        const int gap = Px(metrics::kBlockGap);
        const int button_gap = Px(metrics::kButtonGap);
        int checks_width = 0;
        for (int column = 0; column < columns; ++column)
        {
            checks_width += ColumnWidth(column, columns) + (column ? gap : 0);
        }
        const int buttons_width = std::max(Px(100), TextFitWidth(select_all_)) + std::max(Px(90), TextFitWidth(clear_all_)) +
                                  2 * std::max(Px(metrics::kButtonMinWidth), std::max(TextFitWidth(ok_button_), TextFitWidth(cancel_button_))) + 3 * button_gap;
        return Px(metrics::kDialogContentMargin) + std::max(checks_width, buttons_width) + Px(metrics::kDialogButtonRightMargin);
    }

    const CheckListSpec& spec_;
    std::vector<size_t> checked_;
    std::vector<HWND> checks_;
    HWND select_all_ = nullptr;
    HWND clear_all_ = nullptr;
    HWND ok_button_ = nullptr;
    HWND cancel_button_ = nullptr;
    int columns_ = 1;
    int rows_per_column_ = 1;
};

}

DWORD PickChecks(HWND owner, const CheckListSpec& spec, std::vector<size_t>* checked)
{
    if (!checked)
    {
        return ERROR_INVALID_PARAMETER;
    }
    CheckListDialog dialog(spec, *checked);
    if (dialog.RunModal(owner, L"WIN32UI_CHECK_LIST") != IDOK)
    {
        return ERROR_CANCELLED;
    }
    *checked = std::move(dialog.checked());
    return 0;
}

}
