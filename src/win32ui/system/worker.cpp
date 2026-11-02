// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "win32ui/system/worker.h"

#include "win32ui/system_api.h"

namespace win32ui
{

namespace
{

// shared generation prevents recreated sessions from accepting old completions for the same source
std::atomic<uint64_t> g_generation{0};

}

HRESULT NameThread(HANDLE thread, const wchar_t* name) noexcept
{
    static const auto set_description = ImportProc<HRESULT(WINAPI*)(HANDLE, PCWSTR)>(L"kernel32.dll", "SetThreadDescription");
    return set_description ? set_description(thread, name) : E_NOTIMPL;
}

Session::~Session()
{
    CancelAndJoin();
}

uint64_t Session::Start(const wchar_t* name, Task task)
{
    CancelAndJoin();
    return StartPrepared(name, std::move(task), false);
}

uint64_t Session::StartOrRun(const wchar_t* name, Task task)
{
    CancelAndJoin();
    return StartPrepared(name, std::move(task), true);
}

bool Session::StartIfIdle(const wchar_t* name, Task task, uint64_t* generation)
{
    if (running_.load())
    {
        return false;
    }
    Join();
    const uint64_t started = StartPrepared(name, std::move(task), false);
    if (generation)
    {
        *generation = started;
    }
    return started != 0;
}

void Session::Cancel() noexcept
{
    cancel_.store(true);
    generation_.store(g_generation.fetch_add(1) + 1);
}

void Session::CancelAndJoin() noexcept
{
    Cancel();
    Join();
}

void Session::Join() noexcept
{
    if (thread_.joinable())
    {
        thread_.join();
    }
    running_.store(false);
}

bool Session::IsCurrent(uint64_t generation) const noexcept
{
    return generation != 0 && generation_.load() == generation && !cancel_.load();
}

bool Session::running() const noexcept
{
    return running_.load();
}

uint64_t Session::StartPrepared(const wchar_t* name, Task task, bool run_here)
{
    cancel_.store(false);
    const uint64_t generation = g_generation.fetch_add(1) + 1;
    generation_.store(generation);
    running_.store(true);
    // thread owns the task once it exists, a failed start leaves it here
    std::unique_ptr<Task> owned;
    try
    {
        owned = std::make_unique<Task>(std::move(task));
        thread_ = std::thread([this, generation, work = owned.get()]() { Run(std::unique_ptr<Task>(work), generation); });
        owned.release();
    }
    // std::system_error from the thread, std::bad_alloc from its state
    catch (const std::exception&)
    {
        if (run_here && owned)
        {
            Run(std::move(owned), generation);
            return generation;
        }
        running_.store(false);
        cancel_.store(true);
        return 0;
    }
    NameThread(thread_.native_handle(), name);
    return generation;
}

void Session::Run(std::unique_ptr<Task> task, uint64_t generation) noexcept
{
    try
    {
        if (*task)
        {
            (*task)(generation, cancel_);
        }
    }
    catch (...)
    {
    }
    running_.store(false);
}

}
