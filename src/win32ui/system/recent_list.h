// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace win32ui
{

class RecentList
{
  public:
    explicit RecentList(size_t maximum) noexcept
        : maximum_(maximum)
    {
    }

    void Add(std::wstring_view item);
    void Assign(std::vector<std::wstring> items);

    const std::vector<std::wstring>& items() const noexcept
    {
        return items_;
    }

  private:
    size_t maximum_;
    std::vector<std::wstring> items_;
};

}
