// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/job_pool.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int failures = 0;

void check_at(bool condition, const char* expression, const char* file, int line) {
    if (condition)
        return;
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
    ++failures;
}

#define CHECK(condition) check_at((condition), #condition, __FILE__, __LINE__)

namespace job_pool = oa::platform::job_pool;

/// Rows of the test's output and the rows each of its bands covers.
constexpr uint32_t test_rows = 1000;
constexpr uint32_t test_band_rows = 32;
/// Jobs each pool runs back to back.
constexpr int jobs_per_pool = 200;

/// Fills each row with a value that depends on the row and the job alone.
///
/// @param pool pool to run on; null runs on the calling thread
/// @param job number of the job, which changes the values
/// @return the rows
std::vector<uint32_t> fill_rows(job_pool::Pool* pool, uint32_t job) {
    std::vector<uint32_t> rows(test_rows);
    job_pool::run_bands(
        pool, job_pool::bands_of_rows(test_rows, test_band_rows), [&](uint32_t band) {
            const uint32_t first = band * test_band_rows;
            const uint32_t end =
                first + test_band_rows < test_rows ? first + test_band_rows : test_rows;
            for (uint32_t row = first; row < end; ++row)
                rows[row] = row * 2654435761U + job;
        }
    );
    return rows;
}

void test_policy() {
    CHECK(job_pool::default_threads(0) == 1);
    CHECK(job_pool::default_threads(1) == 1);
    CHECK(job_pool::default_threads(2) == 1);
    CHECK(job_pool::default_threads(3) == 2);
    CHECK(job_pool::default_threads(4) == 3);
    CHECK(job_pool::default_threads(5) == 4);
    CHECK(job_pool::default_threads(24) == job_pool::default_thread_limit);
    CHECK(job_pool::bands_of_rows(0, 32) == 0);
    CHECK(job_pool::bands_of_rows(1, 32) == 1);
    CHECK(job_pool::bands_of_rows(32, 32) == 1);
    CHECK(job_pool::bands_of_rows(33, 32) == 2);
}

void test_one_thread_runs_inline() {
    job_pool::Pool pool(1);
    CHECK(pool.threads() == 1);
    std::vector<uint32_t> order;
    job_pool::run_bands(&pool, 5, [&](uint32_t band) { order.push_back(band); });
    CHECK((order == std::vector<uint32_t>{0, 1, 2, 3, 4}));
    job_pool::Pool none(0);
    CHECK(none.threads() == 1);
}

void test_same_rows_at_every_thread_count() {
    for (const uint32_t threads : {2U, 3U, 4U, 8U}) {
        job_pool::Pool pool(threads);
        CHECK(pool.threads() == threads);
        for (int job = 0; job < jobs_per_pool; ++job)
            CHECK(
                fill_rows(&pool, static_cast<uint32_t>(job)) ==
                fill_rows(nullptr, static_cast<uint32_t>(job))
            );
    }
}

void test_every_band_runs_once() {
    job_pool::Pool pool(4);
    for (const uint32_t bands : {0U, 1U, 2U, 3U, 7U, 64U, 513U}) {
        std::vector<std::atomic<uint32_t>> runs(bands);
        job_pool::run_bands(&pool, bands, [&](uint32_t band) { runs[band].fetch_add(1); });
        bool once = true;
        for (const auto& count : runs)
            once = once && count.load() == 1;
        CHECK(once);
    }
}

void test_nested_job_runs_inline() {
    job_pool::Pool pool(4);
    std::vector<uint32_t> inner(8 * 8);
    job_pool::run_bands(&pool, 8, [&](uint32_t outer) {
        job_pool::run_bands(&pool, 8, [&](uint32_t band) {
            inner[outer * 8 + band] = outer + band;
        });
    });
    bool all = true;
    for (uint32_t outer = 0; outer < 8; ++outer)
        for (uint32_t band = 0; band < 8; ++band)
            all = all && inner[outer * 8 + band] == outer + band;
    CHECK(all);
}

void test_pools_start_and_stop() {
    for (int round = 0; round < 20; ++round) {
        job_pool::Pool pool(3);
        CHECK(fill_rows(&pool, 7) == fill_rows(nullptr, 7));
    }
    // A pool that never ran a job stops as well.
    job_pool::Pool idle(8);
    CHECK(idle.threads() == 8);
    job_pool::Pool capped(job_pool::max_threads + 10);
    CHECK(capped.threads() == job_pool::max_threads);
}

} // namespace

int main() {
    test_policy();
    test_one_thread_runs_inline();
    test_same_rows_at_every_thread_count();
    test_every_band_runs_once();
    test_nested_job_runs_inline();
    test_pools_start_and_stop();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("job pool: every thread count fills the same rows");
    return EXIT_SUCCESS;
}
