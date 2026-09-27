// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/system.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>

namespace oa::platform {
namespace {
std::atomic<ErrorSink> error_sink{nullptr};

const std::chrono::steady_clock::time_point process_origin = std::chrono::steady_clock::now();
} // namespace

uint32_t tick_ms() noexcept {
    const auto elapsed = std::chrono::steady_clock::now() - process_origin;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    return static_cast<uint32_t>(static_cast<uint64_t>(ms));
}

void sleep_ms(uint32_t milliseconds) noexcept {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

bool start_thread(ThreadEntry entry, std::size_t, void* argument) noexcept {
    try {
        std::thread(entry, argument).detach();
        return true;
    } catch (...) {
        return false;
    }
}

void set_error_sink(ErrorSink sink) noexcept {
    error_sink.store(sink);
}

void show_error_message(const char* message) noexcept {
    if (const ErrorSink sink = error_sink.load()) {
        sink(message);
        return;
    }
    std::fprintf(stderr, "Error: %s\n", message ? message : "");
}

bool append_error_log(const char* directory, const char* text) noexcept {
    std::string path = directory != nullptr ? directory : "";
    path += error_log_file_name;
    std::FILE* log = std::fopen(path.c_str(), "ab");
    if (log == nullptr)
        return false;
    const std::size_t length = text != nullptr ? std::strlen(text) : 0;
    const bool written = std::fwrite(text, 1, length, log) == length;
    return std::fclose(log) == 0 && written;
}

std::string error_log_directory(const char* base_path) {
    // SDL names the Resources folder of a bundle as the executable's.
    constexpr std::string_view bundle_resources = "/Contents/Resources/";
    constexpr std::string_view bundle_extension = ".app";
    const std::string_view folder = base_path != nullptr ? base_path : "";
    if (folder.ends_with(bundle_resources)) {
        const std::string_view bundle = folder.substr(0, folder.size() - bundle_resources.size());
        const std::size_t slash = bundle.find_last_of('/');
        if (bundle.ends_with(bundle_extension) && slash != std::string_view::npos)
            return std::string(bundle.substr(0, slash + 1));
    }
    return std::string(folder);
}

} // namespace oa::platform
