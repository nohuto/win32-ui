// Copyright (C) 2026 nohuto
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

namespace win32ui
{

// SetThreadDescription (Windows 10 1607 and later), success can be 0x10000000, test with SUCCEEDED
HRESULT NameThread(HANDLE thread, const wchar_t* name) noexcept;

// base of results posted to a window in lparam, so queued messages dropped with the window can delete their payload
class PostedPayload
{
  public:
    PostedPayload() = default;
    virtual ~PostedPayload() = default;
    PostedPayload(PostedPayload&&) noexcept = default;
    PostedPayload& operator=(PostedPayload&&) noexcept = default;
    PostedPayload(const PostedPayload&) = delete;
    PostedPayload& operator=(const PostedPayload&) = delete;
};

// window owns the payload once this returns true
template <typename T>
bool PostPayload(HWND hwnd, UINT message, WPARAM wparam, std::unique_ptr<T>& payload)
{
    static_assert(std::is_base_of_v<PostedPayload, T>);
    if (!PostMessageW(hwnd, message, wparam, reinterpret_cast<LPARAM>(static_cast<PostedPayload*>(payload.get()))))
    {
        return false;
    }
    payload.release();
    return true;
}

template <typename T>
std::unique_ptr<T> TakePayload(LPARAM lparam)
{
    return std::unique_ptr<T>(static_cast<T*>(reinterpret_cast<PostedPayload*>(lparam)));
}

// Start, Cancel and Join come from one thread (not from the task itself)
class Session
{
  public:
    using Task = std::function<void(uint64_t, std::atomic_bool&)>;

    Session() = default;
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    uint64_t Start(const wchar_t* name, Task task);
    // runs the task on the calling thread when no thread can be started
    uint64_t StartOrRun(const wchar_t* name, Task task);
    bool StartIfIdle(const wchar_t* name, Task task, uint64_t* generation = nullptr);
    void Cancel() noexcept;
    void CancelAndJoin() noexcept;
    void Join() noexcept;

    bool IsCurrent(uint64_t generation) const noexcept;
    bool running() const noexcept;

  private:
    uint64_t StartPrepared(const wchar_t* name, Task task, bool run_here);
    void Run(std::unique_ptr<Task> task, uint64_t generation) noexcept;

    std::thread thread_;
    std::atomic_bool cancel_{false};
    std::atomic_bool running_{false};
    std::atomic<uint64_t> generation_{0};
};

// Start and Stop come from one thread (not from the processor), Submit from any
template <typename Task>
class LatestTask
{
  public:
    using Processor = std::function<void(std::unique_ptr<Task>, const std::atomic_bool&)>;

    LatestTask() = default;
    ~LatestTask()
    {
        Stop();
    }
    LatestTask(const LatestTask&) = delete;
    LatestTask& operator=(const LatestTask&) = delete;

    void Start(const wchar_t* name, Processor processor)
    {
        Stop();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            processor_ = std::move(processor);
            stopping_.store(false);
            superseded_.store(false);
        }
        try
        {
            thread_ = std::thread([this]() { Run(); });
        }
        catch (const std::exception&)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_.store(true);
            processor_ = {};
            return;
        }
        NameThread(thread_.native_handle(), name);
    }

    // replaced waiting task, or the task itself when it was not accepted
    std::unique_ptr<Task> Submit(std::unique_ptr<Task> task, bool* accepted = nullptr)
    {
        if (accepted)
        {
            *accepted = false;
        }
        if (!task)
        {
            return nullptr;
        }
        std::unique_ptr<Task> displaced;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_.load())
            {
                return task;
            }
            displaced = std::move(pending_);
            pending_ = std::move(task);
            superseded_.store(true);
        }
        if (accepted)
        {
            *accepted = true;
        }
        ready_.notify_one();
        return displaced;
    }

    void Stop() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_.store(true);
            superseded_.store(true);
        }
        ready_.notify_one();
        if (thread_.joinable())
        {
            thread_.join();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.reset();
        processor_ = {};
    }

    bool running() const noexcept
    {
        return thread_.joinable() && !stopping_.load();
    }

  private:
    void Run()
    {
        for (;;)
        {
            std::unique_ptr<Task> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this]() { return stopping_.load() || pending_ != nullptr; });
                if (stopping_.load())
                {
                    return;
                }
                task = std::move(pending_);
                superseded_.store(false);
            }
            if (processor_)
            {
                try
                {
                    processor_(std::move(task), superseded_);
                }
                catch (...)
                {
                }
            }
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::thread thread_;
    std::atomic_bool stopping_{true};
    std::atomic_bool superseded_{false};
    std::unique_ptr<Task> pending_;
    Processor processor_;
};

// Start and Stop come from one thread (not from the handler), Submit from any
template <typename Task>
class DebouncedTask
{
    // take pending task outside the worker catch
    static_assert(std::is_nothrow_move_constructible_v<Task>);

  public:
    using Handler = std::function<void(Task)>;

    DebouncedTask() = default;
    ~DebouncedTask()
    {
        Stop();
    }
    DebouncedTask(const DebouncedTask&) = delete;
    DebouncedTask& operator=(const DebouncedTask&) = delete;

    void Start(const wchar_t* name, std::chrono::milliseconds delay, Handler handler)
    {
        Stop();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            delay_ = delay;
            handler_ = std::move(handler);
            stopping_ = false;
            revision_ = 0;
        }
        try
        {
            thread_ = std::thread([this]() { Run(); });
        }
        catch (const std::exception&)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            handler_ = {};
            return;
        }
        NameThread(thread_.native_handle(), name);
    }

    void Submit(Task task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
            {
                return;
            }
            pending_ = std::move(task);
            ++revision_;
        }
        changed_.notify_one();
    }

    void Stop() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        changed_.notify_one();
        if (thread_.joinable())
        {
            thread_.join();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.reset();
        handler_ = {};
    }

    bool running() const noexcept
    {
        return thread_.joinable();
    }

  private:
    void Run()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;)
        {
            changed_.wait(lock, [this]() { return stopping_ || pending_.has_value(); });
            if (stopping_)
            {
                return;
            }

            uint64_t observed = revision_;
            auto deadline = std::chrono::steady_clock::now() + delay_;
            while (!stopping_ && changed_.wait_until(lock, deadline, [this, observed]() { return stopping_ || revision_ != observed; }))
            {
                if (stopping_)
                {
                    return;
                }
                observed = revision_;
                deadline = std::chrono::steady_clock::now() + delay_;
            }
            if (stopping_)
            {
                return;
            }

            Task task = std::move(*pending_);
            pending_.reset();
            lock.unlock();
            try
            {
                if (handler_)
                {
                    handler_(std::move(task));
                }
            }
            catch (...)
            {
            }
            lock.lock();
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::thread thread_;
    bool stopping_ = true;
    uint64_t revision_ = 0;
    std::chrono::milliseconds delay_{0};
    std::optional<Task> pending_;
    Handler handler_;
};

}
