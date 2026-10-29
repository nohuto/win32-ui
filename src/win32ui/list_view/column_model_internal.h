// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <commctrl.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace win32ui
{

class ColumnModel
{
  public:
    void Load(HWND list);
    bool TakeNative(UINT message, WPARAM wparam, LPARAM lparam);
    bool Translate(HWND list, UINT message, WPARAM wparam, LPARAM lparam, LRESULT* result);
    int* NotifySubitem(NMHDR* header) const;
    bool IsRawNotify(const NMHDR* header) const;
    void Reconcile(HWND list, int first_width);

    int Count() const;
    bool AnyHidden() const;
    int Position(int subitem) const;
    int Subitem(int position) const;
    std::wstring Title(int subitem) const;
    void RecordSort(int subitem, int bits);
    // lists class procedure below every subclass, in native positions, SendMessage from another thread
    LRESULT Direct(HWND list, UINT message, WPARAM wparam, LPARAM lparam) const;
    // false on another thread, where Direct sends
    bool Local() const;

  private:
    struct Column
    {
        std::wstring title;
        int format = LVCFMT_LEFT;
        int width = 0;
        bool shown = true;
        int sort = 0;
        std::unordered_map<UINT, std::wstring> texts;
    };

    struct Pending
    {
        UINT message = 0;
        WPARAM wparam = 0;
        LPARAM lparam = 0;
    };

    LRESULT Native(HWND list, UINT message, WPARAM wparam, LPARAM lparam);
    void Rebuild();
    int InsertPosition(int subitem) const;
    void SyncOrder(HWND list);
    void ApplyOrder(HWND list);
    void Hide(HWND list, int subitem);
    void Show(HWND list, int subitem, int width);
    bool HiddenText(HWND list, UINT message, LVITEMW* item, LRESULT* result);

    std::vector<Column> columns_;
    std::vector<int> order_;
    std::vector<int> positions_;
    std::vector<int> subitems_;
    Pending pending_;
    const NMHDR* raw_notify_ = nullptr;
    WNDPROC native_ = nullptr;
    DWORD thread_ = 0;
};

} // namespace win32ui
