// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/text/language_pack.h"
#include "win32ui/text/language_pack_internal.h"

#include "win32ui/system/file_io.h"
#include "win32ui/system/paths.h"
#include "win32ui/system/strings.h"

#include <atomic>
#include <cwchar>
#include <cwctype>
#include <memory>

namespace win32ui
{
namespace
{

constexpr uint64_t kMaxPackBytes = 64ull * 1024 * 1024;
// LOCALE_SNATIVEDISPLAYNAME, Windows 7 and later (the code is the name before)
constexpr LCTYPE kNativeDisplayName = 0x00000073;

// never freed, so translations stay valid through static destruction
std::atomic<const LanguageCatalog*> g_catalog{nullptr};

std::wstring Unquote(std::wstring_view line)
{
    std::wstring text;
    const size_t first = line.find(L'"');
    const size_t last = line.rfind(L'"');
    if (first == std::wstring_view::npos || last <= first)
    {
        return text;
    }
    constexpr std::wstring_view escapes = L"a\ab\bf\fn\nr\rt\tv\v";
    for (size_t i = first + 1; i < last; ++i)
    {
        wchar_t ch = line[i];
        if (ch == L'\\' && i + 1 < last)
        {
            ch = line[++i];
            const size_t escape = escapes.find(ch);
            if (escape != std::wstring_view::npos && escape % 2 == 0)
            {
                ch = escapes[escape + 1];
            }
        }
        text.push_back(ch);
    }
    return text;
}

// argument signature of a format, "%-5ls %.*d" gives "ls*d"
std::wstring Directives(std::wstring_view text)
{
    std::wstring out;
    for (size_t i = text.find(L'%'); i != std::wstring_view::npos; i = text.find(L'%', i + 1))
    {
        if (i + 1 < text.size() && text[i + 1] == L'%')
        {
            ++i;
            continue;
        }
        while (++i < text.size() && !iswalpha(text[i]))
        {
            if (text[i] == L'*')
            {
                out.push_back(L'*');
            }
        }
        while (i < text.size() && wcschr(L"hlLjztI", text[i]))
        {
            out.push_back(text[i++]);
        }
        if (i < text.size())
        {
            out.push_back(text[i]);
        }
    }
    return out;
}

}

void ParseLanguageCatalog(std::wstring_view content, LanguageCatalog* catalog)
{
    std::wstring context;
    std::wstring id;
    std::wstring str;
    std::wstring* target = nullptr;
    bool has_context = false;
    bool fuzzy = false;
    // plural forms are skipped, but the entry still ends at the next boundary
    bool plural = false;
    auto flush = [&] {
        if (!plural && !id.empty() && !str.empty() && !fuzzy && Directives(id) == Directives(str))
        {
            (*catalog)[has_context ? context + L'\x04' + id : id] = str;
        }
        context.clear();
        id.clear();
        str.clear();
        target = nullptr;
        has_context = false;
        fuzzy = false;
        plural = false;
    };
    while (!content.empty())
    {
        const size_t end = content.find(L'\n');
        std::wstring_view line = content.substr(0, end);
        content = end == std::wstring_view::npos ? std::wstring_view{} : content.substr(end + 1);
        if (!line.empty() && line.back() == L'\r')
        {
            line.remove_suffix(1);
        }
        if ((target == &str || plural) && (line.empty() || line.front() == L'#' || line.starts_with(L"msgctxt ") || line.starts_with(L"msgid ")))
        {
            flush();
        }
        if (line.starts_with(L"#,") && line.find(L"fuzzy") != std::wstring_view::npos)
        {
            fuzzy = true;
        }
        else if (line.starts_with(L"msgctxt "))
        {
            context = Unquote(line);
            target = &context;
            has_context = true;
        }
        else if (line.starts_with(L"msgid "))
        {
            id = Unquote(line);
            target = &id;
        }
        else if (line.starts_with(L"msgstr "))
        {
            str = Unquote(line);
            target = &str;
        }
        else if (line.starts_with(L"msgid_plural") || line.starts_with(L"msgstr["))
        {
            plural = true;
            target = nullptr;
        }
        else if (!line.empty() && line.front() == L'"' && target)
        {
            *target += Unquote(line);
        }
    }
    flush();
}

DWORD LoadLanguage(const wchar_t* folder, std::wstring_view code)
{
    if (g_catalog.load(std::memory_order_acquire))
    {
        return ERROR_ALREADY_INITIALIZED;
    }
    // list of null-terminated names ending with an empty one
    std::wstring languages(code);
    languages.push_back(L'\0');
    if (code.empty())
    {
        ULONG count = 0;
        ULONG size = 0;
        if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size))
        {
            return GetLastError();
        }
        languages.resize(size);
        if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, languages.data(), &size))
        {
            return GetLastError();
        }
    }
    for (const wchar_t* language = languages.c_str(); *language; language += wcslen(language) + 1)
    {
        std::wstring candidate = language;
        while (!candidate.empty())
        {
            if (EqualsInsensitive(candidate, L"en"))
            {
                return ERROR_FILE_NOT_FOUND;
            }
            std::wstring content;
            if (ReadTextFile(JoinPath(folder, candidate + L".po").c_str(), &content, kMaxPackBytes) == ERROR_SUCCESS && !content.empty())
            {
                auto catalog = std::make_unique<LanguageCatalog>();
                ParseLanguageCatalog(content, catalog.get());
                const LanguageCatalog* expected = nullptr;
                if (!g_catalog.compare_exchange_strong(expected, catalog.get(), std::memory_order_acq_rel))
                {
                    return ERROR_ALREADY_INITIALIZED;
                }
                catalog.release();
                return ERROR_SUCCESS;
            }
            const size_t dash = candidate.find_last_of(L'-');
            candidate.resize(dash == std::wstring::npos ? 0 : dash);
        }
    }
    return ERROR_FILE_NOT_FOUND;
}

std::vector<LanguagePack> InstalledLanguages(const wchar_t* folder)
{
    std::vector<LanguagePack> packs;
    ForEachFile(JoinPath(folder, L"*.po").c_str(), [&](const WIN32_FIND_DATAW& data) {
        // "*.po" also matches short names, a bare ".po" has no code, empty means the user languages
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && HasFileExtension(data.cFileName, L".po") && wcslen(data.cFileName) > 3)
        {
            std::wstring code(data.cFileName, wcslen(data.cFileName) - 3);
            wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
            packs.push_back({code, GetLocaleInfoEx(code.c_str(), kNativeDisplayName, name, LOCALE_NAME_MAX_LENGTH) ? name : code});
            CharUpperBuffW(packs.back().name.data(), 1);
        }
        return true;
    });
    return packs;
}

const wchar_t* Translate(const wchar_t* english) noexcept
{
    const LanguageCatalog* catalog = g_catalog.load(std::memory_order_acquire);
    if (!catalog || !english)
    {
        return english;
    }
    const auto it = catalog->find(std::wstring_view(english));
    return it == catalog->end() ? english : it->second.c_str();
}

}
