// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#define _CRT_RAND_S

#include "win32ui/theme/theme_presets.h"

#include "win32ui/handle_owner.h"
#include "win32ui/hex_digit_internal.h"
#include "win32ui/theme/theme_presets_internal.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <limits>

namespace win32ui
{

namespace
{

constexpr LONGLONG kMaxFileBytes = 4 * 1024 * 1024;
constexpr std::wstring_view kPresetSection = L"[preset]";

struct BuiltInPreset
{
    const wchar_t* name;
    bool dark;
    ThemeColors colors;
};

constexpr BuiltInPreset kBuiltInPresets[] = {
    {L"Ayu Dark", true, {RGB(15, 20, 25), RGB(21, 26, 33), RGB(27, 34, 43), RGB(9, 14, 19), RGB(27, 34, 43), RGB(37, 51, 64), RGB(230, 225, 207), RGB(166, 179, 191), RGB(255, 180, 84), RGB(21, 26, 33), RGB(230, 225, 207), RGB(27, 34, 43), RGB(255, 180, 84)}},
    {L"Ayu Light", false, {RGB(250, 250, 250), RGB(243, 243, 243), RGB(232, 232, 232), RGB(240, 240, 240), RGB(232, 232, 232), RGB(214, 214, 214), RGB(92, 103, 115), RGB(138, 145, 153), RGB(242, 151, 24), RGB(243, 243, 243), RGB(92, 103, 115), RGB(232, 232, 232), RGB(242, 151, 24)}},
    {L"Catppuccin Frappe", true, {RGB(48, 52, 70), RGB(65, 69, 89), RGB(81, 87, 109), RGB(42, 46, 64), RGB(81, 87, 109), RGB(98, 104, 128), RGB(198, 208, 245), RGB(181, 191, 226), RGB(140, 170, 238), RGB(65, 69, 89), RGB(198, 208, 245), RGB(81, 87, 109), RGB(140, 170, 238)}},
    {L"Catppuccin Latte", false, {RGB(239, 241, 245), RGB(230, 233, 239), RGB(220, 224, 232), RGB(229, 231, 235), RGB(220, 224, 232), RGB(204, 208, 218), RGB(76, 79, 105), RGB(108, 111, 133), RGB(30, 102, 245), RGB(230, 233, 239), RGB(76, 79, 105), RGB(220, 224, 232), RGB(30, 102, 245)}},
    {L"Catppuccin Macchiato", true, {RGB(36, 39, 58), RGB(48, 52, 70), RGB(54, 58, 79), RGB(30, 33, 52), RGB(54, 58, 79), RGB(73, 77, 100), RGB(202, 211, 245), RGB(165, 173, 203), RGB(138, 173, 244), RGB(48, 52, 70), RGB(202, 211, 245), RGB(54, 58, 79), RGB(138, 173, 244)}},
    {L"Catppuccin Mocha", true, {RGB(30, 30, 46), RGB(42, 43, 60), RGB(49, 50, 68), RGB(24, 24, 40), RGB(49, 50, 68), RGB(69, 71, 90), RGB(205, 214, 244), RGB(166, 173, 200), RGB(137, 180, 250), RGB(42, 43, 60), RGB(205, 214, 244), RGB(49, 50, 68), RGB(137, 180, 250)}},
    {L"Dracula", true, {RGB(40, 42, 54), RGB(52, 55, 70), RGB(59, 63, 82), RGB(34, 36, 48), RGB(59, 63, 82), RGB(68, 71, 90), RGB(248, 248, 242), RGB(191, 191, 191), RGB(189, 147, 249), RGB(52, 55, 70), RGB(248, 248, 242), RGB(59, 63, 82), RGB(189, 147, 249)}},
    {L"Everforest Dark", true, {RGB(43, 51, 57), RGB(52, 63, 68), RGB(60, 71, 77), RGB(37, 45, 51), RGB(60, 71, 77), RGB(61, 72, 77), RGB(211, 198, 170), RGB(157, 169, 160), RGB(167, 192, 128), RGB(52, 63, 68), RGB(211, 198, 170), RGB(60, 71, 77), RGB(167, 192, 128)}},
    {L"Everforest Light", false, {RGB(243, 234, 211), RGB(232, 223, 198), RGB(223, 212, 181), RGB(233, 224, 201), RGB(223, 212, 181), RGB(211, 198, 170), RGB(92, 106, 114), RGB(127, 140, 141), RGB(141, 161, 1), RGB(232, 223, 198), RGB(92, 106, 114), RGB(223, 212, 181), RGB(141, 161, 1)}},
    {L"Gruvbox Dark", true, {RGB(23, 24, 25), RGB(32, 32, 32), RGB(38, 37, 36), RGB(15, 16, 16), RGB(38, 37, 36), RGB(55, 51, 49), RGB(217, 214, 204), RGB(183, 177, 170), RGB(215, 153, 33), RGB(32, 32, 32), RGB(217, 214, 204), RGB(38, 37, 36), RGB(215, 153, 33)}},
    {L"Gruvbox Light", false, {RGB(251, 241, 199), RGB(242, 229, 188), RGB(235, 219, 178), RGB(242, 229, 188), RGB(235, 219, 178), RGB(213, 196, 161), RGB(54, 54, 52), RGB(94, 92, 88), RGB(215, 153, 33), RGB(242, 229, 188), RGB(54, 54, 52), RGB(235, 219, 178), RGB(215, 153, 33)}},
    {L"Horizon", true, {RGB(28, 30, 38), RGB(35, 37, 48), RGB(45, 47, 58), RGB(22, 24, 32), RGB(45, 47, 58), RGB(46, 48, 62), RGB(224, 224, 224), RGB(157, 160, 162), RGB(233, 86, 120), RGB(35, 37, 48), RGB(224, 224, 224), RGB(45, 47, 58), RGB(233, 86, 120)}},
    {L"Kanagawa Dragon", true, {RGB(24, 22, 22), RGB(31, 31, 31), RGB(38, 38, 38), RGB(18, 16, 16), RGB(38, 38, 38), RGB(45, 42, 46), RGB(197, 201, 197), RGB(166, 166, 156), RGB(127, 180, 202), RGB(31, 31, 31), RGB(197, 201, 197), RGB(38, 38, 38), RGB(127, 180, 202)}},
    {L"Kanagawa Lotus", false, {RGB(242, 236, 188), RGB(231, 221, 176), RGB(223, 212, 164), RGB(232, 226, 178), RGB(223, 212, 164), RGB(200, 192, 160), RGB(77, 74, 65), RGB(116, 108, 93), RGB(196, 109, 137), RGB(231, 221, 176), RGB(77, 74, 65), RGB(223, 212, 164), RGB(196, 109, 137)}},
    {L"Kanagawa Wave", true, {RGB(31, 31, 40), RGB(42, 42, 55), RGB(54, 54, 70), RGB(25, 25, 34), RGB(54, 54, 70), RGB(59, 59, 79), RGB(220, 215, 186), RGB(166, 166, 156), RGB(126, 156, 216), RGB(42, 42, 55), RGB(220, 215, 186), RGB(54, 54, 70), RGB(126, 156, 216)}},
    {L"Material", true, {RGB(38, 50, 56), RGB(47, 59, 67), RGB(54, 69, 79), RGB(32, 44, 50), RGB(54, 69, 79), RGB(55, 71, 79), RGB(207, 216, 220), RGB(176, 190, 197), RGB(128, 203, 196), RGB(47, 59, 67), RGB(207, 216, 220), RGB(54, 69, 79), RGB(128, 203, 196)}},
    {L"Monokai", true, {RGB(39, 40, 34), RGB(45, 46, 39), RGB(58, 59, 51), RGB(33, 34, 28), RGB(58, 59, 51), RGB(62, 61, 50), RGB(248, 248, 242), RGB(197, 197, 190), RGB(166, 226, 46), RGB(45, 46, 39), RGB(248, 248, 242), RGB(58, 59, 51), RGB(166, 226, 46)}},
    {L"Night Owl", true, {RGB(1, 22, 39), RGB(11, 37, 58), RGB(17, 50, 77), RGB(0, 16, 33), RGB(17, 50, 77), RGB(18, 48, 71), RGB(214, 222, 235), RGB(159, 179, 200), RGB(130, 170, 255), RGB(11, 37, 58), RGB(214, 222, 235), RGB(17, 50, 77), RGB(130, 170, 255)}},
    {L"Nord", true, {RGB(46, 52, 64), RGB(59, 66, 82), RGB(67, 76, 94), RGB(40, 46, 58), RGB(67, 76, 94), RGB(76, 86, 106), RGB(229, 233, 240), RGB(167, 177, 194), RGB(136, 192, 208), RGB(59, 66, 82), RGB(229, 233, 240), RGB(67, 76, 94), RGB(136, 192, 208)}},
    {L"One Dark", true, {RGB(40, 44, 52), RGB(47, 52, 63), RGB(59, 64, 74), RGB(34, 38, 46), RGB(59, 64, 74), RGB(62, 68, 81), RGB(171, 178, 191), RGB(139, 147, 165), RGB(97, 175, 239), RGB(47, 52, 63), RGB(171, 178, 191), RGB(59, 64, 74), RGB(97, 175, 239)}},
    {L"One Light", false, {RGB(250, 250, 250), RGB(242, 242, 242), RGB(231, 231, 231), RGB(240, 240, 240), RGB(231, 231, 231), RGB(208, 208, 208), RGB(56, 58, 66), RGB(107, 111, 119), RGB(64, 120, 242), RGB(242, 242, 242), RGB(56, 58, 66), RGB(231, 231, 231), RGB(64, 120, 242)}},
    {L"Rose Pine", true, {RGB(25, 23, 36), RGB(31, 29, 46), RGB(38, 35, 58), RGB(19, 17, 30), RGB(38, 35, 58), RGB(64, 61, 82), RGB(224, 222, 244), RGB(156, 154, 179), RGB(235, 111, 146), RGB(31, 29, 46), RGB(224, 222, 244), RGB(38, 35, 58), RGB(235, 111, 146)}},
    {L"Rose Pine Moon", true, {RGB(35, 33, 54), RGB(42, 39, 63), RGB(49, 48, 74), RGB(29, 27, 48), RGB(49, 48, 74), RGB(68, 65, 90), RGB(224, 222, 244), RGB(179, 176, 214), RGB(234, 154, 151), RGB(42, 39, 63), RGB(224, 222, 244), RGB(49, 48, 74), RGB(234, 154, 151)}},
    {L"Solarized Dark", true, {RGB(0, 43, 54), RGB(7, 54, 66), RGB(10, 60, 71), RGB(0, 37, 48), RGB(10, 60, 71), RGB(15, 59, 70), RGB(147, 161, 161), RGB(131, 148, 150), RGB(181, 137, 0), RGB(7, 54, 66), RGB(147, 161, 161), RGB(10, 60, 71), RGB(181, 137, 0)}},
    {L"Solarized Light", false, {RGB(253, 246, 227), RGB(238, 232, 213), RGB(228, 221, 200), RGB(243, 236, 217), RGB(228, 221, 200), RGB(214, 207, 181), RGB(88, 110, 117), RGB(101, 123, 131), RGB(181, 137, 0), RGB(238, 232, 213), RGB(88, 110, 117), RGB(228, 221, 200), RGB(181, 137, 0)}},
    {L"Tokyo Night", true, {RGB(26, 27, 38), RGB(36, 40, 59), RGB(47, 51, 77), RGB(20, 21, 32), RGB(47, 51, 77), RGB(65, 72, 104), RGB(192, 202, 245), RGB(169, 177, 214), RGB(122, 162, 247), RGB(36, 40, 59), RGB(192, 202, 245), RGB(47, 51, 77), RGB(122, 162, 247)}},
};

std::wstring_view Trim(std::wstring_view text)
{
    while (!text.empty() && iswspace(text.front()))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && iswspace(text.back()))
    {
        text.remove_suffix(1);
    }
    return text;
}

bool ApplyField(ThemePreset* preset, std::wstring_view key, std::wstring_view value)
{
    if (EqualName(key, L"name"))
    {
        preset->name = value;
        return true;
    }
    if (EqualName(key, L"dark"))
    {
        preset->colors.dark = value == L"1" || EqualName(value, L"true");
        return preset->colors.dark || value == L"0" || EqualName(value, L"false");
    }
    const auto field = std::ranges::find_if(kColorFields, [&](const ColorField& entry) { return EqualName(key, entry.key); });
    return field == std::end(kColorFields) || ParseThemeColor(value, &(preset->colors.*field->member));
}

ThemePresetResult ReadText(const std::wstring& path, std::wstring* content)
{
    const UniqueFile file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER size = {};
    if (!file || !GetFileSizeEx(file.get(), &size) || size.QuadPart <= 0)
    {
        return {ThemePresetError::read_failed};
    }
    if (size.QuadPart > kMaxFileBytes)
    {
        return {ThemePresetError::too_large};
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size())
    {
        return {ThemePresetError::read_failed};
    }
    if (bytes.starts_with("\xFF\xFE"))
    {
        if (bytes.size() <= 2 || (bytes.size() - 2) % sizeof(wchar_t) != 0)
        {
            return {ThemePresetError::bad_encoding};
        }
        content->resize((bytes.size() - 2) / sizeof(wchar_t));
        std::memcpy(content->data(), bytes.data() + 2, bytes.size() - 2);
        return {};
    }
    const size_t offset = bytes.starts_with("\xEF\xBB\xBF") ? 3 : 0;
    const char* data = bytes.data() + offset;
    const int length = static_cast<int>(bytes.size() - offset);
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int count = MultiByteToWideChar(code_page, flags, data, length, nullptr, 0);
    if (count <= 0 && offset == 0)
    {
        code_page = CP_ACP;
        flags = 0;
        count = MultiByteToWideChar(code_page, flags, data, length, nullptr, 0);
    }
    if (count <= 0)
    {
        return {ThemePresetError::bad_encoding};
    }
    content->resize(static_cast<size_t>(count));
    return MultiByteToWideChar(code_page, flags, data, length, content->data(), count) == count
               ? ThemePresetResult{}
               : ThemePresetResult{ThemePresetError::bad_encoding};
}

std::wstring TempSuffix()
{
    unsigned high = 0;
    unsigned low = 0;
    if (rand_s(&high) != 0 || rand_s(&low) != 0)
    {
        high = GetTickCount();
        low = GetCurrentProcessId();
    }
    wchar_t suffix[32] = {};
    swprintf_s(suffix, L".%08x%08x.tmp", high, low);
    return suffix;
}

// dont overwrite existing files with content the reader would reject
ThemePresetError WriteText(const std::wstring& path, const std::wstring& content)
{
    if (content.size() > static_cast<size_t>(kMaxFileBytes))
    {
        return ThemePresetError::too_large;
    }
    const int length = static_cast<int>(content.size());
    const int count = WideCharToMultiByte(CP_UTF8, 0, content.data(), length, nullptr, 0, nullptr, nullptr);
    if (count <= 0)
    {
        return ThemePresetError::write_failed;
    }
    if (count > kMaxFileBytes)
    {
        return ThemePresetError::too_large;
    }
    std::string bytes(static_cast<size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, content.data(), length, bytes.data(), count, nullptr, nullptr) != count)
    {
        return ThemePresetError::write_failed;
    }
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const std::wstring temp_path = path + TempSuffix();
        UniqueFile file(CreateFileW(temp_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file)
        {
            if (GetLastError() == ERROR_FILE_EXISTS)
            {
                continue;
            }
            return ThemePresetError::write_failed;
        }
        DWORD written = 0;
        const bool ok = WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                        written == bytes.size() && FlushFileBuffers(file.get());
        file.reset();
        if (ok)
        {
            for (int retry = 0; retry < 20; ++retry)
            {
                if (MoveFileExW(temp_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                {
                    return ThemePresetError::none;
                }
                const DWORD error = GetLastError();
                if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION)
                {
                    break;
                }
                Sleep(25);
            }
        }
        DeleteFileW(temp_path.c_str());
        return ThemePresetError::write_failed;
    }
    return ThemePresetError::write_failed;
}

// persist thirteen colors and dark flag, rebuild error color from the flag
bool SameSavedColors(const ThemeColors& colors, bool dark, const ThemeColors& saved)
{
    return dark == saved.dark && std::ranges::all_of(kColorFields, [&](const ColorField& field) { return colors.*field.member == saved.*field.member; });
}

} // namespace

bool EqualName(std::wstring_view left, std::wstring_view right)
{
    return left.size() == right.size() && (left.empty() ||
                                           (left.size() <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
                                            CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL));
}

const std::vector<ThemePreset>& BuiltInThemePresets()
{
    static const std::vector<ThemePreset> presets = [] {
        std::vector<ThemePreset> result = {
            {L"Default Dark", BuiltInThemeColors(true)},
            {L"Default Light", BuiltInThemeColors(false)},
        };
        result.reserve(2 + std::size(kBuiltInPresets));
        for (const BuiltInPreset& preset : kBuiltInPresets)
        {
            ThemeColors colors = preset.colors;
            colors.dark = preset.dark;
            colors.error = BuiltInThemeColors(preset.dark).error;
            result.push_back({preset.name, colors});
        }
        return result;
    }();
    return presets;
}

ThemePresetResult ParseThemePresets(std::wstring_view content, std::vector<ThemePreset>* presets)
{
    if (!presets)
    {
        return {ThemePresetError::bad_line};
    }
    presets->clear();
    ThemePreset current;
    current.colors.dark = true;
    int section_line = 0;
    const auto fail = [&](ThemePresetError error, int line) {
        presets->clear();
        return ThemePresetResult{error, line};
    };
    const auto finish = [&] {
        if (section_line == 0)
        {
            return true;
        }
        if (current.name.empty())
        {
            return false;
        }
        current.colors.error = BuiltInThemeColors(current.colors.dark).error;
        presets->push_back(current);
        return true;
    };
    int line_number = 1;
    while (!content.empty())
    {
        const size_t end = content.find_first_of(L"\r\n");
        const std::wstring_view line = Trim(content.substr(0, end));
        if (line == kPresetSection)
        {
            if (!finish())
            {
                return fail(ThemePresetError::missing_name, section_line);
            }
            current = {};
            current.colors.dark = true;
            section_line = line_number;
        }
        else if (section_line != 0 && !line.empty())
        {
            const size_t separator = line.find(L'=');
            if (separator == std::wstring_view::npos ||
                !ApplyField(&current, Trim(line.substr(0, separator)), Trim(line.substr(separator + 1))))
            {
                return fail(ThemePresetError::bad_line, line_number);
            }
        }
        if (end == std::wstring_view::npos)
        {
            break;
        }
        const size_t newline = content[end] == L'\r' && end + 1 < content.size() && content[end + 1] == L'\n' ? 2 : 1;
        content.remove_prefix(end + newline);
        if (line_number < std::numeric_limits<int>::max())
        {
            ++line_number;
        }
    }
    return finish() ? ThemePresetResult{} : fail(ThemePresetError::missing_name, section_line);
}

std::wstring FormatThemePresets(const std::vector<ThemePreset>& presets)
{
    std::wstring content;
    for (const ThemePreset& preset : presets)
    {
        // a name is one line and the reader trims it, one that trims to nothing is skipped like an empty one
        std::wstring line = preset.name;
        std::replace_if(line.begin(), line.end(), [](wchar_t character) { return character < 0x20 || character == 0x7F; }, L' ');
        const std::wstring_view name = Trim(line);
        if (name.empty())
        {
            continue;
        }
        content.append(kPresetSection).append(L"\r\nname=").append(name);
        content.append(preset.colors.dark ? L"\r\ndark=1\r\n" : L"\r\ndark=0\r\n");
        for (const ColorField& field : kColorFields)
        {
            content.append(field.key).append(L"=").append(FormatThemeColor(preset.colors.*field.member)).append(L"\r\n");
        }
    }
    return content.empty() ? L"\r\n" : content;
}

ThemePresetResult ReadThemePresets(const std::wstring& path, std::vector<ThemePreset>* presets)
{
    if (!presets)
    {
        return {ThemePresetError::read_failed};
    }
    std::wstring content;
    const ThemePresetResult result = ReadText(path, &content);
    return result.error == ThemePresetError::none ? ParseThemePresets(content, presets) : result;
}

ThemePresetResult WriteThemePresets(const std::wstring& path, const std::vector<ThemePreset>& presets)
{
    return {WriteText(path, FormatThemePresets(presets))};
}

bool MergeBuiltInThemePresets(std::vector<ThemePreset>* presets, std::wstring& active_name)
{
    if (!presets)
    {
        return false;
    }
    const auto taken = [presets](std::wstring_view name) {
        return std::ranges::any_of(*presets, [&](const ThemePreset& preset) { return EqualName(preset.name, name); });
    };
    bool changed = false;
    for (const ThemePreset& builtin : BuiltInThemePresets())
    {
        const auto found = std::ranges::find_if(*presets, [&](const ThemePreset& preset) { return EqualName(preset.name, builtin.name); });
        if (found == presets->end())
        {
            presets->push_back(builtin);
            changed = true;
            continue;
        }
        const bool current = SameSavedColors(builtin.colors, builtin.colors.dark, found->colors);
        if (current && found->name == builtin.name)
        {
            continue;
        }
        if (current)
        {
            *found = builtin;
            changed = true;
            continue;
        }
        std::wstring kept;
        for (int number = 2; kept.empty() || taken(kept); ++number)
        {
            kept = builtin.name + L" " + std::to_wstring(number);
        }
        if (EqualName(active_name, found->name))
        {
            active_name = kept;
        }
        found->name = std::move(kept);
        presets->push_back(builtin);
        changed = true;
    }
    return changed;
}

const ThemePreset* FindThemePreset(const std::vector<ThemePreset>& presets, std::wstring_view name)
{
    const auto found = std::ranges::find_if(presets, [&](const ThemePreset& preset) { return EqualName(preset.name, name); });
    return found != presets.end() ? &*found : presets.empty() ? nullptr
                                                              : &presets.front();
}

std::wstring FormatThemeColor(COLORREF color)
{
    constexpr wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring result = L"#000000";
    const BYTE values[] = {GetRValue(color), GetGValue(color), GetBValue(color)};
    for (size_t index = 0; index < std::size(values); ++index)
    {
        result[1 + index * 2] = digits[values[index] >> 4];
        result[2 + index * 2] = digits[values[index] & 15];
    }
    return result;
}

bool ParseThemeColor(std::wstring_view text, COLORREF* color)
{
    if (!color)
    {
        return false;
    }
    text = Trim(text);
    const size_t prefix = text.size() == 7 && text[0] == L'#' ? 1 : text.size() == 8 && text.starts_with(L"0x") ? 2
                                                                                                                : 0;
    if (prefix != 0)
    {
        unsigned rgb = 0;
        for (const wchar_t character : text.substr(prefix))
        {
            const int digit = HexDigitValue(character);
            if (digit < 0)
            {
                return false;
            }
            rgb = (rgb << 4) | static_cast<unsigned>(digit);
        }
        *color = RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
        return true;
    }
    if (text.find(L',') != std::wstring_view::npos)
    {
        unsigned values[3] = {};
        for (size_t index = 0; index < std::size(values); ++index)
        {
            // whitespace may come before a component, not before its comma
            while (!text.empty() && iswspace(text.front()))
            {
                text.remove_prefix(1);
            }
            size_t digits = 0;
            while (!text.empty() && text.front() >= L'0' && text.front() <= L'9')
            {
                if (++digits > 3)
                {
                    return false;
                }
                values[index] = values[index] * 10 + static_cast<unsigned>(text.front() - L'0');
                text.remove_prefix(1);
            }
            if (digits == 0 || values[index] > 255)
            {
                return false;
            }
            if (index + 1 < std::size(values))
            {
                if (text.empty() || text.front() != L',')
                {
                    return false;
                }
                text.remove_prefix(1);
            }
        }
        if (text.empty())
        {
            *color = RGB(values[0], values[1], values[2]);
            return true;
        }
    }
    return false;
}

} // namespace win32ui
