// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <vector>

namespace win32ui
{

enum AnchorFlags : unsigned
{
    kAnchorLeft = 1u,
    kAnchorTop = 2u,
    kAnchorRight = 4u,
    kAnchorBottom = 8u,
};

struct AnchorRule
{
    int id = 0;
    unsigned anchors = kAnchorLeft | kAnchorTop;
};

// anchors controls during resize, positions use pixels at the Attach DPI until Rescale
class DialogResizer
{
  public:
    void Attach(HWND dialog, const std::vector<AnchorRule>& rules);
    void Apply(HWND dialog) const;
    // call after scaling the dialog and controls to the new DPI
    void Rescale(UINT from_dpi, UINT to_dpi);

  private:
    struct Item
    {
        int id = 0;
        unsigned anchors = 0;
        RECT rect = {};
    };

    std::vector<Item> items_;
    SIZE client_ = {};
};

} // namespace win32ui
