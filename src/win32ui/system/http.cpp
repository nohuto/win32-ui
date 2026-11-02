// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/http.h"

#include "win32ui/handle_owner.h"

#include <winhttp.h>

#include <climits>
#include <memory>

namespace win32ui
{

namespace
{

constexpr DWORD kPieceBytes = 64 * 1024;

struct InternetCloser
{
    void operator()(HINTERNET handle) const noexcept
    {
        WinHttpCloseHandle(handle);
    }
};
using UniqueInternet = UniqueNullable<HINTERNET, InternetCloser>;

// asynchronous request, one operation at a time, its completion signals done, only the calling thread closes the handle
// (a close from another thread cannot end a synchronous call, and WinHTTP reuses closed handle values)
struct AsyncRequest
{
    HINTERNET handle = nullptr;
    UniqueHandle done{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    UniqueHandle closed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool callbacks = false;
    DWORD error = ERROR_SUCCESS;
    DWORD bytes = 0;
    // pending read writes here until the handle is closed
    char piece[kPieceBytes];

    AsyncRequest() = default;
    AsyncRequest(const AsyncRequest&) = delete;
    AsyncRequest& operator=(const AsyncRequest&) = delete;

    // result of the pending operation, ERROR_CANCELLED (checked every 100 ms) leaves it to the close
    DWORD Wait(const std::atomic_bool& cancel) const
    {
        for (;;)
        {
            if (cancel.load())
            {
                return ERROR_CANCELLED;
            }
            const DWORD wait = WaitForSingleObject(done.get(), 100);
            if (wait == WAIT_OBJECT_0)
            {
                return error;
            }
            if (wait != WAIT_TIMEOUT)
            {
                return GetLastError();
            }
        }
    }
};

// frees the state once WinHTTP reports the handle closed, a failed close leaves the handle open and its callbacks
// possible, so the state then stays allocated
struct RequestCloser
{
    void operator()(AsyncRequest* request) const noexcept
    {
        if (request->handle)
        {
            const bool closed = WinHttpCloseHandle(request->handle) != FALSE;
            if (request->callbacks && !closed)
            {
                return;
            }
            if (request->callbacks)
            {
                WaitForSingleObject(request->closed.get(), INFINITE);
            }
        }
        delete request;
    }
};
using RequestOwner = std::unique_ptr<AsyncRequest, RequestCloser>;

void CALLBACK RequestCallback(HINTERNET, DWORD_PTR context, DWORD status, void* info, DWORD length)
{
    auto* request = reinterpret_cast<AsyncRequest*>(context);
    if (!request)
    {
        return;
    }
    switch (status)
    {
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
        request->error = ERROR_SUCCESS;
        request->bytes = length;
        SetEvent(request->done.get());
        break;
    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
        request->error = static_cast<const WINHTTP_ASYNC_RESULT*>(info)->dwError;
        SetEvent(request->done.get());
        break;
    case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
        SetEvent(request->closed.get());
        break;
    default:
        break;
    }
}

struct UrlParts
{
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 0;
    bool secure = false;
};

DWORD CrackUrl(const wchar_t* url, UrlParts* parts)
{
    URL_COMPONENTS components = {sizeof(components)};
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url, 0, 0, &components))
    {
        return GetLastError();
    }
    // it also cracks ftp and others, which would go out as http to their ports
    if (components.nScheme != INTERNET_SCHEME_HTTP && components.nScheme != INTERNET_SCHEME_HTTPS)
    {
        return ERROR_WINHTTP_UNRECOGNIZED_SCHEME;
    }
    parts->host.assign(components.lpszHostName, components.dwHostNameLength);
    parts->path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    parts->path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    parts->port = components.nPort;
    parts->secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    return ERROR_SUCCESS;
}

bool Allowed(HttpHostCheck allow, const UrlParts& parts)
{
    return allow ? allow(parts.host, parts.secure) : parts.secure;
}

DWORD FinalUrl(HINTERNET request, UrlParts* parts)
{
    DWORD size = 0;
    WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &size);
    std::wstring url(size / sizeof(wchar_t) + 1, L'\0');
    size = static_cast<DWORD>(url.size() * sizeof(wchar_t));
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_URL, url.data(), &size))
    {
        return GetLastError();
    }
    return CrackUrl(url.c_str(), parts);
}

bool ContentLength(HINTERNET request, unsigned long long* length)
{
    wchar_t text[24] = {};
    DWORD size = sizeof(text);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, text, &size, WINHTTP_NO_HEADER_INDEX) || size == 0)
    {
        return false;
    }
    unsigned long long value = 0;
    for (const wchar_t c : std::wstring_view(text, size / sizeof(wchar_t)))
    {
        if (c < L'0' || c > L'9')
        {
            return false;
        }
        value = value > (ULLONG_MAX - 9) / 10 ? ULLONG_MAX : value * 10 + static_cast<unsigned>(c - L'0');
    }
    *length = value;
    return true;
}

}

DWORD HttpGet(const std::wstring& url, const wchar_t* user_agent, HttpHostCheck allow, size_t max_bytes, const std::atomic_bool& cancel, std::string* body)
{
    body->clear();
    UrlParts parts;
    if (const DWORD error = CrackUrl(url.c_str(), &parts))
    {
        return error;
    }
    if (!Allowed(allow, parts))
    {
        return ERROR_ACCESS_DENIED;
    }
    if (cancel.load())
    {
        return ERROR_CANCELLED;
    }
    UniqueInternet session(WinHttpOpen(user_agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    if (!session)
    {
        // before Windows 8.1
        session.reset(WinHttpOpen(user_agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    }
    const UniqueInternet connect(session ? WinHttpConnect(session.get(), parts.host.c_str(), parts.port, 0) : nullptr);
    if (!connect)
    {
        return GetLastError();
    }
    // pending read writes here until the request is closed
    const RequestOwner owner(new AsyncRequest);
    AsyncRequest& request = *owner;
    if (!request.done || !request.closed)
    {
        return GetLastError();
    }
    request.handle = WinHttpOpenRequest(connect.get(), L"GET", parts.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request.handle)
    {
        return GetLastError();
    }
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(&request);
    if (!WinHttpSetOption(request.handle, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) ||
        WinHttpSetStatusCallback(request.handle, RequestCallback, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
    {
        return GetLastError();
    }
    request.callbacks = true;
    // one started operation to its completion, a failure after cancel was set counts as the cancel, a reset connection
    // can wake the wait before its next check
    const auto run = [&](BOOL started) {
        const DWORD error = started ? request.Wait(cancel) : GetLastError();
        return error && cancel.load() ? static_cast<DWORD>(ERROR_CANCELLED) : error;
    };

    if (const DWORD error = run(WinHttpSetTimeouts(request.handle, 5000, 10000, 10000, 30000) && WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, context)))
    {
        return error;
    }
    if (const DWORD error = run(WinHttpReceiveResponse(request.handle, nullptr)))
    {
        return error;
    }
    if (const DWORD error = FinalUrl(request.handle, &parts))
    {
        return error;
    }
    if (!Allowed(allow, parts))
    {
        return ERROR_ACCESS_DENIED;
    }
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
    {
        return GetLastError();
    }
    if (status != HTTP_STATUS_OK)
    {
        return HttpStatusError(status);
    }
    unsigned long long length = 0;
    const bool announced = ContentLength(request.handle, &length);
    if (announced && length > max_bytes)
    {
        return ERROR_FILE_TOO_LARGE;
    }
    if (announced)
    {
        body->reserve(static_cast<size_t>(length));
    }
    for (;;)
    {
        if (const DWORD error = run(WinHttpReadData(request.handle, request.piece, kPieceBytes, nullptr)))
        {
            return error;
        }
        if (request.bytes == 0)
        {
            break;
        }
        if (request.bytes > max_bytes - body->size())
        {
            return ERROR_FILE_TOO_LARGE;
        }
        body->append(request.piece, request.bytes);
    }
    return announced && body->size() < length ? ERROR_HANDLE_EOF : ERROR_SUCCESS;
}

}
