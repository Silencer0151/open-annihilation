// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// This system's memory sampled as the memory guard samples it: every figure
// printed, those the system reports checked for sense, the guard given some
// sign of free memory, and private committed memory seen to grow by a block
// the test writes. Exits with code 77, which ctest reports as skipped, where
// the system reports no figure at all.

#include "oa/platform/memory_status.hpp"
#include "oa/test/check.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace {

using oa::platform::MemoryPressure;
using oa::platform::SystemMemorySample;

/// The exit code ctest reports as skipped.
constexpr int skipped_exit_code = 77;

/// Bytes in a mebibyte.
constexpr std::size_t mebibyte = std::size_t{1024} * 1024;

/// The block the test writes to see committed memory grow, in bytes.
constexpr std::size_t written_block_size = 32 * mebibyte;

/// The least growth of committed memory accepted after writing the block:
/// half of it, since the process may give back other memory meanwhile.
constexpr uint64_t least_committed_growth = written_block_size / 2;

/// Distance between the bytes written, in bytes: no wider than a page.
constexpr std::size_t write_stride = 4096;

/// Names a memory pressure level for the report.
///
/// @param pressure the level
/// @return its name
const char* pressure_name(MemoryPressure pressure) {
    switch (pressure) {
    case MemoryPressure::normal:
        return "normal";
    case MemoryPressure::warning:
        return "warning";
    case MemoryPressure::critical:
        return "critical";
    case MemoryPressure::unknown:
        break;
    }
    return "unknown";
}

/// Prints one figure of a sample, or "unknown".
///
/// @param name the figure's name
/// @param value the figure
/// @param known whether the system reported it
void print_figure(const char* name, uint64_t value, bool known) {
    if (known)
        std::printf("  %-12s %llu\n", name, static_cast<unsigned long long>(value));
    else
        std::printf("  %-12s unknown\n", name);
}

/// Prints every figure of a sample.
///
/// @param title what the sample was taken after
/// @param sample the sample
void print_sample(const char* title, const SystemMemorySample& sample) {
    std::printf("%s\n", title);
    print_figure("physical", sample.physical, sample.physical != 0);
    print_figure("available", sample.available, sample.available_known);
    print_figure("committed", sample.committed, sample.committed_known);
    print_figure("hard faults", sample.hard_faults, sample.hard_faults_known);
    std::printf("  %-12s %s\n", "pressure", pressure_name(sample.pressure));
}

/// Checks what any sample must hold, whatever the system.
///
/// @param sample the sample
void check_sense(const SystemMemorySample& sample) {
    if (sample.physical != 0 && sample.available_known)
        OA_CHECK(sample.available <= sample.physical);
    if (sample.committed_known)
        OA_CHECK(sample.committed > 0);
    // The guard always has a sign of free memory to watch.
    OA_CHECK(
        sample.available_known || sample.pressure != MemoryPressure::unknown ||
        sample.hard_faults_known
    );
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    OA_CHECK(sample.physical != 0);
    OA_CHECK(sample.committed_known);
    OA_CHECK(sample.available_known);
#endif
}

} // namespace

int main() {
    SystemMemorySample before{};
    if (!oa::platform::sample_system_memory(&before)) {
        std::printf("platform-system-memory: skipped: this system reports no memory figure\n");
        return skipped_exit_code;
    }
    print_sample("at start", before);
    check_sense(before);
    OA_CHECK(!oa::platform::sample_system_memory(nullptr));

    // Write one byte of every page of a fresh block, so that each page is
    // given to the process as its own.
    const std::unique_ptr<uint8_t[]> block(new uint8_t[written_block_size]);
    for (std::size_t offset = 0; offset < written_block_size; offset += write_stride)
        block[offset] = static_cast<uint8_t>(offset / write_stride) | 1U;

    SystemMemorySample after{};
    OA_CHECK(oa::platform::sample_system_memory(&after));
    print_sample("after writing a 32 MiB block", after);
    check_sense(after);
    if (before.hard_faults_known && after.hard_faults_known)
        OA_CHECK(after.hard_faults >= before.hard_faults);
#if defined(__APPLE__) || defined(__linux__)
    // Committed memory counts private pages as they are written.
    if (before.committed_known && after.committed_known)
        OA_CHECK(after.committed >= before.committed + least_committed_growth);
#endif
    // Read the block back so that its writes are not dropped as unused.
    unsigned checksum = 0;
    for (std::size_t offset = 0; offset < written_block_size; offset += write_stride)
        checksum += block[offset];
    OA_CHECK(checksum != 0);
    return oa::test::check_exit_status();
}
