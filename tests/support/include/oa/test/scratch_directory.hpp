// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A test's scratch directory: a new, empty directory under the system's
// temporary directory that no other run shares, so that two test runs at
// once, from one build tree or several, never write to or remove each
// other's files.
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace oa::test {

/// Creates a new, empty directory under the temporary directory, named
/// `name` followed by a suffix that makes it the caller's alone.
///
/// The directory is created before its path is returned, and creating a
/// directory fails when one of that name exists, so two runs never receive
/// the same one, whatever their clocks or random numbers. The caller
/// removes it when done.
///
/// @param name what the directory is for, the start of its name
/// @return the directory's path
/// @throws std::runtime_error when no directory can be created
inline std::filesystem::path make_scratch_directory(std::string_view name) {
    constexpr int attempts = 1000;
    const auto base = std::filesystem::temp_directory_path();
    std::random_device device;
    const auto stamp =
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::mt19937_64 numbers(stamp ^ (static_cast<uint64_t>(device()) << 32U) ^ device());
    for (int attempt = 0; attempt < attempts; ++attempt) {
        const auto candidate =
            base / (std::string(name) + "-" + std::to_string(numbers() % 1000000000U));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error))
            return candidate;
        if (error && !std::filesystem::exists(candidate))
            throw std::runtime_error(
                "cannot create the scratch directory " + candidate.string() + ": " + error.message()
            );
    }
    throw std::runtime_error("cannot create a scratch directory named " + std::string(name) + "-*");
}

} // namespace oa::test
