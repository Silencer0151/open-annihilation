// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Locks, condition variables, events and threads that run on every system the
// engine supports, Windows XP included. On Windows they rest on the calls
// every release since Windows XP has (critical sections, semaphores, events
// and threads started through the C library); elsewhere on the C++
// standard library and POSIX threads. Nothing here throws.

#include <atomic>
#include <cstddef>
#include <cstdint>

#if !defined(_WIN32)
#include <condition_variable>
#include <mutex>
#include <pthread.h>
#endif

namespace oa::base::threads {

#if defined(_WIN32)
/// Bytes that hold one Windows critical section: 24 on 32-bit Windows, 40 on
/// 64-bit Windows.
inline constexpr size_t critical_section_bytes = sizeof(void*) == 8 ? 40 : 24;
#endif

/// A lock that one thread holds at a time.
///
/// It is not recursive: a thread that holds it does not take it again. A
/// Mutex is ready as soon as it exists, without running code, so a Mutex at
/// namespace scope is usable while other files' statics are still being
/// initialised. It meets the standard's Lockable requirements.
class Mutex {
  public:

    /// Makes an unlocked lock.
    constexpr Mutex() noexcept = default;
    /// Releases the lock's system resources; no thread may hold it.
    ~Mutex();
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    /// Waits until the lock is free and takes it.
    void lock() noexcept;
    /// Takes the lock when it is free.
    ///
    /// @return true when the calling thread now holds the lock
    [[nodiscard]] bool try_lock() noexcept;
    /// Releases the lock the calling thread holds.
    void unlock() noexcept;

  private:

    friend class ConditionVariable;
#if defined(_WIN32)
    /// Makes the critical section on first use.
    void make_ready() noexcept;

    /// 0 before first use, 1 while one thread makes the critical section,
    /// 2 once it is ready.
    std::atomic<int32_t> state_{0};
    alignas(void*) unsigned char section_[critical_section_bytes]{};
#else
    std::mutex mutex_{};
#endif
};

/// Holds a Mutex for as long as the guard lives.
class LockGuard {
  public:

    /// Takes the lock.
    ///
    /// @param[in,out] mutex lock to hold until the guard is destroyed
    explicit LockGuard(Mutex& mutex) noexcept : mutex_(&mutex) { mutex_->lock(); }

    /// Releases the lock.
    ~LockGuard() { mutex_->unlock(); }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

  private:

    Mutex* mutex_{};
};

/// Lets threads wait, while holding a Mutex, until another thread signals.
///
/// As with the standard's condition variables, a wait may also end without a
/// signal, so a waiter checks its condition again; the wait that takes a
/// predicate does.
class ConditionVariable {
  public:

    /// Makes a condition variable no thread waits on.
    ConditionVariable() noexcept;
    /// Releases its system resources; no thread may wait on it.
    ~ConditionVariable();
    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

    /// Releases the lock, waits for a signal and takes the lock again.
    ///
    /// @param[in,out] mutex lock the calling thread holds
    void wait(Mutex& mutex) noexcept;

    /// Waits until a condition holds.
    ///
    /// @param[in,out] mutex lock the calling thread holds, which guards the condition
    /// @param ready callable returning true once the thread may go on; called with the lock held
    template <typename Predicate>
    void wait(Mutex& mutex, Predicate ready) {
        while (!ready())
            wait(mutex);
    }

    /// Wakes one waiting thread, if any waits.
    void notify_one() noexcept;
    /// Wakes every waiting thread.
    void notify_all() noexcept;

  private:

#if defined(_WIN32)
    /// Wakes up to `count` of the threads that wait and no signal has woken.
    ///
    /// @param count most threads to wake
    void wake(int32_t count) noexcept;

    Mutex counts_lock_{};           ///< guards waiting_ and signals_
    void* wake_semaphore_{};        ///< released once per thread woken
    void* acknowledge_semaphore_{}; ///< released by each thread as it wakes
    int32_t waiting_{};             ///< threads waiting
    int32_t signals_{};             ///< of those, threads woken that have not yet woken up
#else
    std::condition_variable condition_{};
#endif
};

/// A signal that wakes one waiting thread and then clears itself.
///
/// A signal that comes while no thread waits is kept until the next wait.
class Event {
  public:

    /// Makes an event that is not signalled.
    Event() noexcept;
    /// Releases its system resources; no thread may wait on it.
    ~Event();
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    /// Signals the event, waking one waiting thread.
    void signal() noexcept;
    /// Waits until the event is signalled, and clears it.
    void wait() noexcept;
    /// Clears a signal no thread has taken yet.
    void reset() noexcept;

  private:

#if defined(_WIN32)
    void* handle_{}; ///< an automatically resetting event; null when it could not be made
#else
    std::mutex mutex_{};
    std::condition_variable condition_{};
    bool signalled_{}; ///< guarded by mutex_
#endif
};

/// Function a thread runs.
using ThreadEntry = void (*)(void* argument);

/// A thread that another thread joins.
struct Thread {
#if defined(_WIN32)
    void* handle{}; ///< the thread's handle; null when no thread runs
#else
    pthread_t handle{}; ///< the thread, while started is true
    bool started{};
#endif
};

/// Starts a thread that a later join_thread waits for.
///
/// @param[out] thread the started thread; unchanged when none could start
/// @param entry function the thread runs
/// @param argument value passed to entry
/// @return false when the thread could not be started
[[nodiscard]] bool start_thread(Thread& thread, ThreadEntry entry, void* argument) noexcept;

/// Waits for a thread to finish and releases it.
///
/// Does nothing for a thread that was never started or is already joined.
///
/// @param[in,out] thread thread to wait for; empty afterwards
void join_thread(Thread& thread) noexcept;

/// Starts a thread no other thread waits for.
///
/// @param entry function the thread runs
/// @param argument value passed to entry
/// @return false when the thread could not be started
[[nodiscard]] bool start_detached_thread(ThreadEntry entry, void* argument) noexcept;

/// Returns how many threads the machine runs at once.
///
/// @return the number of logical processors the system reports, at least 1
[[nodiscard]] uint32_t processor_count() noexcept;

/// Suspends the calling thread.
///
/// @param milliseconds time to sleep; 0 gives up the rest of the time slice
void sleep_ms(uint32_t milliseconds) noexcept;

} // namespace oa::base::threads
