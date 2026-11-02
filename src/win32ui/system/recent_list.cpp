// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/recent_list.h"

#include "win32ui/system/strings.h"

#include <algorithm>

namespace win32ui
{

void RecentList::Add(std::wstring_view item)
{
    if (item.empty() || maximum_ == 0)
    {
        return;
    }
    const auto existing = std::ranges::find_if(items_, [&](const std::wstring& entry) { return EqualsInsensitive(entry, item); });
    if (existing != items_.end())
    {
        existing->assign(item);
        std::rotate(items_.begin(), existing, existing + 1);
        return;
    }
    // item can view the entry that drops out
    std::wstring text(item);
    if (items_.size() >= maximum_)
    {
        items_.resize(maximum_ - 1);
    }
    items_.insert(items_.begin(), std::move(text));
}

void RecentList::Assign(std::vector<std::wstring> items)
{
    size_t kept = 0;
    for (size_t index = 0; index < items.size() && kept < maximum_; ++index)
    {
        const std::wstring& item = items[index];
        if (item.empty() || std::any_of(items.begin(), items.begin() + kept, [&](const std::wstring& entry) { return EqualsInsensitive(entry, item); }))
        {
            continue;
        }
        if (kept != index)
        {
            items[kept] = std::move(items[index]);
        }
        ++kept;
    }
    items.resize(kept);
    items_ = std::move(items);
}

}
