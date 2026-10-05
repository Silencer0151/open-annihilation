// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/system.hpp"

#include "oa/base/threads.hpp"
#include "oa/platform/files.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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
    base::threads::sleep_ms(milliseconds);
}

bool start_thread(ThreadEntry entry, std::size_t, void* argument) noexcept {
    return base::threads::start_detached_thread(entry, argument);
}

uint32_t processor_count() noexcept {
    return base::threads::processor_count();
}

std::optional<std::string> environment_value(const char* name) {
#if defined(_MSC_VER)
    // Visual Studio's C library marks getenv unsafe; its own copy is the
    // same value.
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
        return std::nullopt;

    struct Release {
        void operator()(char* copy) const noexcept { std::free(copy); }
    };

    const std::unique_ptr<char, Release> owned(value);
    return std::string(owned.get());
#else
    const char* value = std::getenv(name);
    if (value == nullptr)
        return std::nullopt;
    return std::string(value);
#endif
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
    std::FILE* log = nullptr;
    try {
        std::string path = directory != nullptr ? directory : "";
        path += error_log_file_name;
        log = open_file(std::filesystem::path(std::u8string(path.begin(), path.end())), "ab");
    } catch (const std::exception&) {
        return false;
    }
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
