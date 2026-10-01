// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The locks, condition variables, events and threads: a lock keeps several
// threads' increments whole, a lock at namespace scope works before any code
// runs, a condition variable hands work from one thread to several and back,
// an event keeps a signal that came before the wait, and joined threads have
// finished.
#include "oa/base/threads.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

namespace threads = oa::base::threads;

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

constexpr int worker_count = 4;
constexpr int increments_per_worker = 5000;
constexpr int job_count = 200;

/// A lock no constructor code has run for: constant-initialised.
constinit threads::Mutex namespace_lock;
int namespace_counter = 0;

struct Counting {
    threads::Mutex lock;
    int counter{};
};

void count(void* argument) {
    auto& counting = *static_cast<Counting*>(argument);
    for (int step = 0; step < increments_per_worker; ++step) {
        const threads::LockGuard held(counting.lock);
        ++counting.counter;
    }
}

void count_namespace(void*) {
    for (int step = 0; step < increments_per_worker; ++step) {
        const threads::LockGuard held(namespace_lock);
        ++namespace_counter;
    }
}

void test_lock_keeps_increments_whole() {
    Counting counting;
    std::array<threads::Thread, worker_count> workers{};
    for (auto& worker : workers)
        CHECK(threads::start_thread(worker, count, &counting));
    for (auto& worker : workers)
        threads::join_thread(worker);
    CHECK(counting.counter == worker_count * increments_per_worker);

    for (auto& worker : workers)
        CHECK(threads::start_thread(worker, count_namespace, nullptr));
    for (auto& worker : workers)
        threads::join_thread(worker);
    CHECK(namespace_counter == worker_count * increments_per_worker);
}

void test_try_lock() {
    threads::Mutex lock;
    CHECK(lock.try_lock());
    lock.unlock();

    struct Attempt {
        threads::Mutex* lock{};
        std::atomic<bool> taken{true};
    } attempt;

    attempt.lock = &lock;
    const threads::LockGuard held(lock);
    threads::Thread other;
    CHECK(
        threads::start_thread(
            other,
            [](void* argument) {
                auto& attempt = *static_cast<Attempt*>(argument);
                const bool took = attempt.lock->try_lock();
                if (took)
                    attempt.lock->unlock();
                attempt.taken.store(took);
            },
            &attempt
        )
    );
    threads::join_thread(other);
    CHECK(!attempt.taken.load());
}

struct Queue {
    threads::Mutex lock;
    threads::ConditionVariable work;
    threads::ConditionVariable finished;
    int pending{}; ///< jobs not yet taken
    int done{};    ///< jobs finished
    bool stopping{};
};

void serve(void* argument) {
    auto& queue = *static_cast<Queue*>(argument);
    for (;;) {
        {
            threads::LockGuard held(queue.lock);
            queue.work.wait(queue.lock, [&queue] { return queue.stopping || queue.pending > 0; });
            if (queue.pending == 0)
                return;
            --queue.pending;
        }
        {
            const threads::LockGuard held(queue.lock);
            ++queue.done;
        }
        queue.finished.notify_all();
    }
}

void test_condition_variable_hands_work_over() {
    Queue queue;
    std::array<threads::Thread, worker_count> workers{};
    for (auto& worker : workers)
        CHECK(threads::start_thread(worker, serve, &queue));
    for (int job = 0; job < job_count; ++job) {
        {
            const threads::LockGuard held(queue.lock);
            ++queue.pending;
        }
        queue.work.notify_one();
    }
    {
        threads::LockGuard held(queue.lock);
        queue.finished.wait(queue.lock, [&queue] { return queue.done == job_count; });
        queue.stopping = true;
    }
    queue.work.notify_all();
    for (auto& worker : workers)
        threads::join_thread(worker);
    CHECK(queue.done == job_count);
    CHECK(queue.pending == 0);
}

void test_event_keeps_an_early_signal() {
    threads::Event event;
    event.signal();
    event.wait();
    event.signal();
    event.reset();

    struct Relay {
        threads::Event go;
        threads::Event back;
        std::atomic<int> rounds{};
    } relay;

    threads::Thread partner;
    CHECK(
        threads::start_thread(
            partner,
            [](void* argument) {
                auto& relay = *static_cast<Relay*>(argument);
                for (int round = 0; round < 3; ++round) {
                    relay.go.wait();
                    relay.rounds.fetch_add(1);
                    relay.back.signal();
                }
            },
            &relay
        )
    );
    for (int round = 0; round < 3; ++round) {
        relay.go.signal();
        relay.back.wait();
    }
    threads::join_thread(partner);
    CHECK(relay.rounds.load() == 3);
}

void test_detached_thread_and_join_of_nothing() {
    std::atomic<bool> ran{false};
    CHECK(
        threads::start_detached_thread(
            [](void* argument) { static_cast<std::atomic<bool>*>(argument)->store(true); }, &ran
        )
    );
    for (int wait = 0; wait < 5000 && !ran.load(); ++wait)
        threads::sleep_ms(1);
    CHECK(ran.load());
    threads::Thread never;
    threads::join_thread(never);
    threads::sleep_ms(0);
    CHECK(threads::processor_count() >= 1);
}

} // namespace

int main() {
    test_lock_keeps_increments_whole();
    test_try_lock();
    test_condition_variable_hands_work_over();
    test_event_keeps_an_early_signal();
    test_detached_thread_and_join_of_nothing();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("threads checks passed");
    return 0;
}
