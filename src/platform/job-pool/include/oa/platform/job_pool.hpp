// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// A small fixed pool of worker threads that runs a job's bands, the calling
// thread taking part, and returns once every band has run. The caller splits
// its work into bands by its data, never by the number of threads, and each
// band writes only its own part of the output, so a job gives the same
// result with any number of threads. A pool of one thread starts none and
// runs the bands in order on the calling thread.

#include "oa/platform/lock.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace oa::platform::job_pool {

/// Most threads the default policy gives a pool, the calling thread included.
inline constexpr uint32_t default_thread_limit = 4;

/// Most threads a pool runs a job on, the calling thread included.
inline constexpr uint32_t max_threads = 32;

/// Stack each worker thread asks for, in bytes.
inline constexpr std::size_t worker_stack_bytes = 64 * 1024;

/// Function a band of a job runs.
using BandEntry = void (*)(void* context, uint32_t band);

/// Returns the threads a pool uses by default on a machine.
///
/// One on a machine of one or two logical processors, which keeps the
/// drawing on the calling thread; otherwise one fewer than the processors,
/// leaving one to the rest of the system, and at most default_thread_limit.
///
/// @param processors logical processors the machine runs at once
/// @return threads, the calling thread included, from 1 to default_thread_limit
[[nodiscard]] uint32_t default_threads(uint32_t processors) noexcept;

/// Returns how many bands of `band_rows` rows cover `rows` rows; the last may be shorter.
///
/// @param rows rows of the output
/// @param band_rows rows of each band, at least 1
/// @return the number of bands
[[nodiscard]] constexpr uint32_t bands_of_rows(uint32_t rows, uint32_t band_rows) noexcept {
    return rows / band_rows + (rows % band_rows != 0 ? 1U : 0U);
}

/// A fixed set of worker threads that runs jobs' bands with the calling thread.
///
/// One thread at a time runs a job on a pool; a job started while another
/// runs, from a band or from another thread, runs its bands in order on the
/// thread that started it.
class Pool {
  public:

    /// Starts the workers: one fewer than `threads`, which counts the
    /// calling thread.
    ///
    /// A pool of one thread starts none. A worker that cannot be started is
    /// left out; threads() says how many run.
    ///
    /// @param threads threads a job runs on, the calling thread included;
    ///        0 counts as 1 and more than max_threads as max_threads
    explicit Pool(uint32_t threads);

    /// Stops the workers and waits until each has ended.
    ///
    /// No job may be running on the pool.
    ~Pool();

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    /// Returns the threads a job runs on, the calling thread included.
    ///
    /// @return 1 plus the workers that started
    [[nodiscard]] uint32_t threads() const noexcept { return worker_count_ + 1; }

    /// Runs entry(context, band) for every band in [0, bands) and returns once all have run.
    ///
    /// Bands run in any order and at the same time; the calling thread runs
    /// bands too. Everything the bands wrote is visible to the caller on
    /// return. A band must not throw.
    ///
    /// @param bands number of bands
    /// @param entry function each band runs
    /// @param context value passed to entry
    void run(uint32_t bands, BandEntry entry, void* context) noexcept;

  private:

    struct Job;
    struct Worker;

    /// Runs a worker: waits for each job, runs bands of it while any are left.
    ///
    /// @param argument the Worker
    static void serve(void* argument) noexcept;

    /// Runs the job's bands one at a time until none is left to take.
    ///
    /// @param[in,out] job the job whose bands are taken
    static void run_bands_of(Job& job) noexcept;

    std::unique_ptr<Worker[]> workers_;
    uint32_t worker_count_{};            ///< workers that started
    std::atomic<Job*> current_{nullptr}; ///< the job workers join; null between jobs
    std::atomic<uint32_t> inside_{0};    ///< workers that joined a job and have not left it
    std::atomic<uint32_t> running_{0};   ///< workers started and not yet ended
    std::atomic<bool> busy_{false};      ///< a thread is running a job on the workers
    std::atomic<bool> stopping_{false};  ///< the workers are to end
    WakeEvent left_;                     ///< signalled by the worker that leaves a job last
};

/// Runs band(index) for every band in [0, bands) on a pool, or in order on
/// the calling thread when there is no pool.
///
/// @param pool pool to run the bands on; null runs them in order on the calling thread
/// @param bands number of bands
/// @param band callable taking the band's index, which must not throw
template <typename Band>
void run_bands(Pool* pool, uint32_t bands, Band&& band) {
    if (pool == nullptr) {
        for (uint32_t index = 0; index < bands; ++index)
            band(index);
        return;
    }
    using Callable = std::remove_reference_t<Band>;
    pool->run(
        bands,
        [](void* context, uint32_t index) { (*static_cast<Callable*>(context))(index); },
        const_cast<void*>(static_cast<const void*>(std::addressof(band)))
    );
}

} // namespace oa::platform::job_pool
