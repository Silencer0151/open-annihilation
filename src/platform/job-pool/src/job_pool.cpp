// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/job_pool.hpp"

#include "oa/platform/system.hpp"

#include <algorithm>

namespace oa::platform::job_pool {
namespace {

/// Times the calling thread checks whether the workers have left a job
/// before it waits for the last to signal: long enough to cover a band's
/// last few microseconds, short enough not to hold a processor.
constexpr uint32_t finish_checks_before_waiting = 4096;

/// Milliseconds the destructor sleeps between checks for workers still running.
constexpr uint32_t stop_check_ms = 1;

} // namespace

/// One job's bands and the next one to take.
struct Pool::Job {
    BandEntry entry{};
    void* context{};
    uint32_t bands{};
    std::atomic<uint32_t> next{0}; ///< the next band to take
};

/// A worker thread's pool and the event that wakes it for a job.
struct Pool::Worker {
    Pool* pool{};
    WakeEvent wake;
};

uint32_t default_threads(uint32_t processors) noexcept {
    if (processors <= 2)
        return 1;
    return std::min(processors - 1, default_thread_limit);
}

Pool::Pool(uint32_t threads) {
    const uint32_t wanted = std::clamp<uint32_t>(threads, 1, max_threads) - 1;
    if (wanted == 0)
        return;
    workers_ = std::make_unique<Worker[]>(wanted);
    for (uint32_t index = 0; index < wanted; ++index) {
        Worker& worker = workers_[index];
        worker.pool = this;
        running_.fetch_add(1);
        if (!start_thread(&Pool::serve, worker_stack_bytes, &worker)) {
            running_.fetch_sub(1);
            break;
        }
        ++worker_count_;
    }
}

Pool::~Pool() {
    stopping_.store(true);
    for (uint32_t index = 0; index < worker_count_; ++index)
        wake_event_signal(&workers_[index].wake);
    // A worker's last touch of the pool is its count going down, so the
    // workers and their events outlive every worker's use of them.
    while (running_.load() != 0)
        sleep_ms(stop_check_ms);
}

void Pool::run(uint32_t bands, BandEntry entry, void* context) noexcept {
    if (bands == 0)
        return;
    if (worker_count_ == 0 || bands == 1 || busy_.exchange(true)) {
        for (uint32_t band = 0; band < bands; ++band)
            entry(context, band);
        return;
    }
    Job job;
    job.entry = entry;
    job.context = context;
    job.bands = bands;
    current_.store(&job);
    // A worker for every band beyond the one the calling thread starts with.
    const uint32_t woken = std::min(worker_count_, bands - 1);
    for (uint32_t index = 0; index < woken; ++index)
        wake_event_signal(&workers_[index].wake);
    run_bands_of(job);
    // From here no worker joins the job. A worker that joined it counted
    // itself in inside_ before it read current_, so once inside_ is 0 every
    // band it took has run and none reads the job again.
    current_.store(nullptr);
    for (uint32_t checks = 0; inside_.load() != 0; ++checks)
        if (checks >= finish_checks_before_waiting)
            wake_event_wait(&left_);
    busy_.store(false);
}

void Pool::run_bands_of(Job& job) noexcept {
    for (;;) {
        const uint32_t band = job.next.fetch_add(1, std::memory_order_relaxed);
        if (band >= job.bands)
            return;
        job.entry(job.context, band);
    }
}

void Pool::serve(void* argument) noexcept {
    Worker& worker = *static_cast<Worker*>(argument);
    Pool& pool = *worker.pool;
    for (;;) {
        wake_event_wait(&worker.wake);
        if (pool.stopping_.load())
            break;
        // A wake-up left over from a job already finished finds no job, or
        // the next one, which it joins as any other worker would.
        pool.inside_.fetch_add(1);
        if (Job* job = pool.current_.load(); job != nullptr)
            run_bands_of(*job);
        if (pool.inside_.fetch_sub(1) == 1)
            wake_event_signal(&pool.left_);
    }
    pool.running_.fetch_sub(1);
}

} // namespace oa::platform::job_pool
