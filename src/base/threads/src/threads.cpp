// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/base/threads.hpp"

#include <new>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <process.h>
#else
#include <cerrno>
#include <ctime>
#include <sched.h>
#include <unistd.h>
#endif

namespace oa::base::threads {
namespace {

/// What a new thread runs, handed from start_thread to the thread itself.
struct ThreadStart {
    ThreadEntry entry{};
    void* argument{};
};

#if defined(_WIN32)
static_assert(sizeof(CRITICAL_SECTION) <= critical_section_bytes);
static_assert(alignof(CRITICAL_SECTION) <= alignof(void*));

constexpr int32_t mutex_uninitialised = 0;
constexpr int32_t mutex_initialising = 1;
constexpr int32_t mutex_ready = 2;
/// The most signals a semaphore of a condition variable counts.
constexpr LONG semaphore_limit = 0x7fffffff;
/// How long a waiter whose semaphores could not be made sleeps between checks.
constexpr DWORD fallback_wait_ms = 1;

CRITICAL_SECTION* critical_section(unsigned char* storage) {
    return reinterpret_cast<CRITICAL_SECTION*>(storage);
}

/// Runs a started thread's entry and frees what start_thread allocated.
unsigned __stdcall run_thread(void* context) {
    const ThreadStart start = *static_cast<ThreadStart*>(context);
    delete static_cast<ThreadStart*>(context);
    start.entry(start.argument);
    return 0;
}

/// Starts a thread through the C library, which sets up its own state for
/// the thread.
///
/// @param entry function the thread runs
/// @param argument value passed to entry
/// @return the thread's handle, or null when it could not be started
HANDLE begin_thread(ThreadEntry entry, void* argument) noexcept {
    auto* start = new (std::nothrow) ThreadStart{entry, argument};
    if (start == nullptr)
        return nullptr;
    const uintptr_t handle = _beginthreadex(nullptr, 0, run_thread, start, 0, nullptr);
    if (handle == 0) {
        delete start;
        return nullptr;
    }
    return reinterpret_cast<HANDLE>(handle);
}
#else
/// Runs a started thread's entry and frees what start_thread allocated.
void* run_thread(void* context) {
    const ThreadStart start = *static_cast<ThreadStart*>(context);
    delete static_cast<ThreadStart*>(context);
    start.entry(start.argument);
    return nullptr;
}

/// Starts a POSIX thread.
///
/// @param[out] handle the started thread
/// @param entry function the thread runs
/// @param argument value passed to entry
/// @return false when the thread could not be started
bool begin_thread(pthread_t& handle, ThreadEntry entry, void* argument) noexcept {
    auto* start = new (std::nothrow) ThreadStart{entry, argument};
    if (start == nullptr)
        return false;
    if (pthread_create(&handle, nullptr, run_thread, start) != 0) {
        delete start;
        return false;
    }
    return true;
}
#endif

} // namespace

#if defined(_WIN32)

Mutex::~Mutex() {
    if (state_.load(std::memory_order_acquire) == mutex_ready)
        DeleteCriticalSection(critical_section(section_));
}

void Mutex::make_ready() noexcept {
    if (state_.load(std::memory_order_acquire) == mutex_ready)
        return;
    int32_t expected = mutex_uninitialised;
    if (state_.compare_exchange_strong(expected, mutex_initialising, std::memory_order_acq_rel)) {
        InitializeCriticalSection(critical_section(section_));
        state_.store(mutex_ready, std::memory_order_release);
        return;
    }
    while (state_.load(std::memory_order_acquire) != mutex_ready)
        Sleep(0);
}

void Mutex::lock() noexcept {
    make_ready();
    EnterCriticalSection(critical_section(section_));
}

bool Mutex::try_lock() noexcept {
    make_ready();
    return TryEnterCriticalSection(critical_section(section_)) != FALSE;
}

void Mutex::unlock() noexcept {
    LeaveCriticalSection(critical_section(section_));
}

// A waiter counts itself, releases the caller's lock and waits on the wake
// semaphore; a signal releases that semaphore once per thread it wakes and
// waits until each has acknowledged on the second semaphore, so that a
// thread that starts waiting after the signal never takes a wake-up meant
// for one that waited before it.
ConditionVariable::ConditionVariable() noexcept
    : wake_semaphore_(CreateSemaphoreW(nullptr, 0, semaphore_limit, nullptr)),
      acknowledge_semaphore_(CreateSemaphoreW(nullptr, 0, semaphore_limit, nullptr)) {
}

ConditionVariable::~ConditionVariable() {
    if (wake_semaphore_ != nullptr)
        CloseHandle(wake_semaphore_);
    if (acknowledge_semaphore_ != nullptr)
        CloseHandle(acknowledge_semaphore_);
}

void ConditionVariable::wait(Mutex& mutex) noexcept {
    if (wake_semaphore_ == nullptr || acknowledge_semaphore_ == nullptr) {
        // Without its semaphores every wait ends after a short sleep, which a
        // waiter takes as a wake-up that came without a signal.
        mutex.unlock();
        Sleep(fallback_wait_ms);
        mutex.lock();
        return;
    }
    counts_lock_.lock();
    ++waiting_;
    counts_lock_.unlock();
    mutex.unlock();
    WaitForSingleObject(wake_semaphore_, INFINITE);
    counts_lock_.lock();
    if (signals_ > 0) {
        ReleaseSemaphore(acknowledge_semaphore_, 1, nullptr);
        --signals_;
    }
    --waiting_;
    counts_lock_.unlock();
    mutex.lock();
}

void ConditionVariable::wake(int32_t count) noexcept {
    if (wake_semaphore_ == nullptr || acknowledge_semaphore_ == nullptr)
        return;
    counts_lock_.lock();
    const int32_t unwoken = waiting_ - signals_;
    const int32_t woken = unwoken < count ? unwoken : count;
    if (woken <= 0) {
        counts_lock_.unlock();
        return;
    }
    signals_ += woken;
    ReleaseSemaphore(wake_semaphore_, woken, nullptr);
    counts_lock_.unlock();
    for (int32_t index = 0; index < woken; ++index)
        WaitForSingleObject(acknowledge_semaphore_, INFINITE);
}

void ConditionVariable::notify_one() noexcept {
    wake(1);
}

void ConditionVariable::notify_all() noexcept {
    wake(semaphore_limit);
}

Event::Event() noexcept : handle_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {
}

Event::~Event() {
    if (handle_ != nullptr)
        CloseHandle(handle_);
}

void Event::signal() noexcept {
    if (handle_ != nullptr)
        SetEvent(handle_);
}

void Event::wait() noexcept {
    if (handle_ == nullptr) {
        // Without an event a wait ends after a short sleep; the waiters in
        // the engine check their condition again.
        Sleep(fallback_wait_ms);
        return;
    }
    WaitForSingleObject(handle_, INFINITE);
}

void Event::reset() noexcept {
    if (handle_ != nullptr)
        ResetEvent(handle_);
}

bool start_thread(Thread& thread, ThreadEntry entry, void* argument) noexcept {
    const HANDLE handle = begin_thread(entry, argument);
    if (handle == nullptr)
        return false;
    thread.handle = handle;
    return true;
}

void join_thread(Thread& thread) noexcept {
    if (thread.handle == nullptr)
        return;
    WaitForSingleObject(thread.handle, INFINITE);
    CloseHandle(thread.handle);
    thread.handle = nullptr;
}

bool start_detached_thread(ThreadEntry entry, void* argument) noexcept {
    const HANDLE handle = begin_thread(entry, argument);
    if (handle == nullptr)
        return false;
    CloseHandle(handle);
    return true;
}

uint32_t processor_count() noexcept {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? static_cast<uint32_t>(info.dwNumberOfProcessors) : 1;
}

void sleep_ms(uint32_t milliseconds) noexcept {
    Sleep(milliseconds);
}

#else

Mutex::~Mutex() = default;

void Mutex::lock() noexcept {
    mutex_.lock();
}

bool Mutex::try_lock() noexcept {
    return mutex_.try_lock();
}

void Mutex::unlock() noexcept {
    mutex_.unlock();
}

ConditionVariable::ConditionVariable() noexcept = default;

ConditionVariable::~ConditionVariable() = default;

void ConditionVariable::wait(Mutex& mutex) noexcept {
    std::unique_lock<std::mutex> held(mutex.mutex_, std::adopt_lock);
    condition_.wait(held);
    held.release();
}

void ConditionVariable::notify_one() noexcept {
    condition_.notify_one();
}

void ConditionVariable::notify_all() noexcept {
    condition_.notify_all();
}

Event::Event() noexcept = default;

Event::~Event() = default;

void Event::signal() noexcept {
    {
        const std::lock_guard held(mutex_);
        signalled_ = true;
    }
    condition_.notify_one();
}

void Event::wait() noexcept {
    std::unique_lock held(mutex_);
    condition_.wait(held, [this] { return signalled_; });
    signalled_ = false;
}

void Event::reset() noexcept {
    const std::lock_guard held(mutex_);
    signalled_ = false;
}

bool start_thread(Thread& thread, ThreadEntry entry, void* argument) noexcept {
    pthread_t handle{};
    if (!begin_thread(handle, entry, argument))
        return false;
    thread.handle = handle;
    thread.started = true;
    return true;
}

void join_thread(Thread& thread) noexcept {
    if (!thread.started)
        return;
    pthread_join(thread.handle, nullptr);
    thread.handle = pthread_t{};
    thread.started = false;
}

bool start_detached_thread(ThreadEntry entry, void* argument) noexcept {
    pthread_t handle{};
    if (!begin_thread(handle, entry, argument))
        return false;
    pthread_detach(handle);
    return true;
}

uint32_t processor_count() noexcept {
    const long online = sysconf(_SC_NPROCESSORS_ONLN);
    return online > 0 ? static_cast<uint32_t>(online) : 1;
}

void sleep_ms(uint32_t milliseconds) noexcept {
    if (milliseconds == 0) {
        sched_yield();
        return;
    }
    constexpr long milliseconds_per_second = 1000;
    constexpr long nanoseconds_per_millisecond = 1000000;
    timespec remaining{};
    remaining.tv_sec = static_cast<time_t>(milliseconds / milliseconds_per_second);
    remaining.tv_nsec =
        static_cast<long>(milliseconds % milliseconds_per_second) * nanoseconds_per_millisecond;
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
}

#endif

} // namespace oa::base::threads
