// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The instance lock and the hand-off folder a second start writes its
// packages into.

#include "files.hpp"

#include "oa/app/mod_install/handoff.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace oa::app::mod_install {

namespace fs = std::filesystem;

namespace {

/// The longest request file read, in bytes: a path.
constexpr uintmax_t longest_request = 64 * 1024;
/// The digits a request's time is written with, so that names sort by it.
constexpr int time_digits = 20;

/// Returns this process's id.
///
/// @return the id
unsigned long process_id() noexcept {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}

} // namespace

std::unique_ptr<FileLock> take_instance_lock(const fs::path& file) {
    try {
        std::error_code error;
        if (file.has_parent_path())
            fs::create_directories(file.parent_path(), error);
        return FileLock::take(file);
    } catch (const std::exception&) {
        return nullptr;
    }
}

std::vector<fs::path> hand_files_over(const fs::path& folder, const std::vector<fs::path>& files) {
    std::vector<fs::path> written;
    try {
        std::error_code error;
        fs::create_directories(folder, error);
        if (error)
            return {};
        static std::atomic<uint32_t> sequence{0};
        const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch()
        )
                             .count();
        for (const fs::path& file : files) {
            char stamp[64]{};
            std::snprintf(
                stamp,
                sizeof stamp,
                "%0*lld-%lu-%u",
                time_digits,
                static_cast<long long>(now),
                process_id(),
                static_cast<unsigned>(sequence++)
            );
            const fs::path request = folder / (std::string(stamp) + std::string(request_extension));
            const fs::path temporary = folder / (std::string(stamp) + ".tmp");
            {
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                std::error_code absolute_error;
                const fs::path whole = fs::absolute(file, absolute_error);
                out << detail::utf8_of(absolute_error ? file : whole);
                if (!out)
                    return {};
            }
            rename_exclusively(temporary, request, error);
            if (error) {
                fs::remove(temporary, error);
                return {};
            }
            written.push_back(request);
        }
    } catch (const std::exception&) {
        return {};
    }
    return written;
}

std::vector<fs::path> take_handed_files(const fs::path& folder) {
    std::vector<fs::path> taken;
    try {
        std::vector<fs::path> requests;
        std::error_code error;
        for (fs::directory_iterator entry{folder, error}, end; !error && entry != end;
             entry.increment(error))
            if (entry->path().extension() == std::string(request_extension))
                requests.push_back(entry->path());
        std::sort(requests.begin(), requests.end());
        for (const fs::path& request : requests) {
            std::string text;
            {
                std::ifstream in(request, std::ios::binary);
                for (std::istreambuf_iterator<char> at{in}, end;
                     at != end && text.size() < longest_request;
                     ++at)
                    text.push_back(*at);
            }
            std::error_code removal;
            fs::remove(request, removal);
            const fs::path file = detail::path_of(text);
            std::error_code status;
            if (text.empty() || !fs::is_regular_file(file, status)) {
                detail::log_line("a handed-over package is gone: " + text);
                continue;
            }
            taken.push_back(file);
        }
    } catch (const std::exception& failure) {
        detail::log_line(std::string("cannot take the handed-over packages: ") + failure.what());
    }
    return taken;
}

} // namespace oa::app::mod_install
