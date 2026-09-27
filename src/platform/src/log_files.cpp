// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/log_files.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace oa::platform::log_files {
namespace {

namespace fs = std::filesystem;
using std::chrono::sys_seconds;

constexpr std::string_view kPrefix = "open-annihilation-";
constexpr std::string_view kExtension = ".log";

struct State {
    fs::path folder;
    Limits limits;
    fs::path current;
    sys_seconds begun{};
    std::chrono::steady_clock::time_point last_check{};
    int terminal = -1; // descriptor of the standard error the program began with, when a terminal
    bool active = false;
};

State& state() {
    static State instance;
    return instance;
}

sys_seconds now_utc() {
    return std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
}

std::chrono::sys_days utc_day(sys_seconds time) {
    return std::chrono::floor<std::chrono::days>(time);
}

// Reads the whole of `text` as a decimal number.
std::optional<int32_t> number(std::string_view text) {
    int32_t value = 0;
    const auto* end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != end)
        return std::nullopt;
    return value;
}

struct LogEntry {
    fs::path path;
    sys_seconds begun{};
};

// The folder's log files, oldest first.
std::vector<LogEntry> list_logs(const fs::path& folder) {
    std::vector<LogEntry> logs;
    std::error_code error;
    for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error))
            continue;
        if (const auto begun = begun_at(it->path().filename().string()))
            logs.push_back({it->path(), *begun});
    }
    std::sort(logs.begin(), logs.end(), [](const LogEntry& a, const LogEntry& b) {
        return a.begun != b.begun ? a.begun < b.begun : a.path.filename() < b.path.filename();
    });
    return logs;
}

// A new file name at `now` that no file in the folder has yet.
fs::path new_file(const fs::path& folder, sys_seconds now) {
    std::error_code error;
    for (int32_t sequence = 0;; ++sequence) {
        auto path = folder / file_name(now, sequence);
        if (!fs::exists(path, error))
            return path;
    }
}

std::FILE* reopen(const fs::path& path, std::FILE* stream) {
#if defined(_WIN32)
    return _wfreopen(path.c_str(), L"ab", stream);
#else
    return std::freopen(path.c_str(), "ab", stream);
#endif
}

bool can_append(const fs::path& path) {
#if defined(_WIN32)
    std::FILE* file = _wfopen(path.c_str(), L"ab");
#else
    std::FILE* file = std::fopen(path.c_str(), "ab");
#endif
    if (file == nullptr)
        return false;
    std::fclose(file);
    return true;
}

// Keeps a descriptor of standard error when it is a terminal, for
// write_to_terminal() once the stream goes to the log.
int keep_terminal() {
#if defined(_WIN32)
    const int descriptor = _fileno(stderr);
    return descriptor >= 0 && _isatty(descriptor) ? _dup(descriptor) : -1;
#else
    const int descriptor = fileno(stderr);
    return descriptor >= 0 && isatty(descriptor) ? dup(descriptor) : -1;
#endif
}

// Points both streams at `path`. Standard output is line buffered where the
// C library supports it, so a crash loses at most a partial line; standard
// error stays unbuffered.
bool redirect(const fs::path& path) {
    if (!can_append(path))
        return false;
    std::fflush(stdout);
    std::fflush(stderr);
    if (reopen(path, stdout) == nullptr || reopen(path, stderr) == nullptr)
        return false;
#if defined(_WIN32)
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#else
    std::setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);
#endif
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    return true;
}

void write_first_line(sys_seconds now) {
    const auto day = utc_day(now);
    const std::chrono::year_month_day date{day};
    const std::chrono::hh_mm_ss time{now - day};
    std::fprintf(
        stderr,
        "open-annihilation log, %04d-%02u-%02u %02d:%02d:%02d UTC\n",
        static_cast<int>(date.year()),
        static_cast<unsigned>(date.month()),
        static_cast<unsigned>(date.day()),
        static_cast<int>(time.hours().count()),
        static_cast<int>(time.minutes().count()),
        static_cast<int>(time.seconds().count())
    );
}

} // namespace

std::string file_name(sys_seconds begun, int32_t sequence) {
    const auto day = utc_day(begun);
    const std::chrono::year_month_day date{day};
    const std::chrono::hh_mm_ss time{begun - day};
    char text[64]{};
    const int length = std::snprintf(
        text,
        sizeof text,
        "%s%04d%02u%02u-%02d%02d%02d",
        std::string(kPrefix).c_str(),
        static_cast<int>(date.year()),
        static_cast<unsigned>(date.month()),
        static_cast<unsigned>(date.day()),
        static_cast<int>(time.hours().count()),
        static_cast<int>(time.minutes().count()),
        static_cast<int>(time.seconds().count())
    );
    std::string name(text, static_cast<std::size_t>(length > 0 ? length : 0));
    if (sequence > 0)
        name += '-' + std::to_string(sequence);
    return name + std::string(kExtension);
}

std::optional<sys_seconds> begun_at(std::string_view name) {
    if (!name.starts_with(kPrefix) || !name.ends_with(kExtension))
        return std::nullopt;
    name.remove_prefix(kPrefix.size());
    name.remove_suffix(kExtension.size());
    // YYYYMMDD-HHMMSS, then an optional -N.
    if (name.size() < 15 || name[8] != '-')
        return std::nullopt;
    if (name.size() > 15 && (name[15] != '-' || !number(name.substr(16))))
        return std::nullopt;
    for (const std::size_t at : {0u, 4u, 6u, 9u, 11u, 13u}) {
        const std::size_t width = at == 0 ? 4 : 2;
        for (std::size_t i = at; i < at + width; ++i)
            if (name[i] < '0' || name[i] > '9')
                return std::nullopt;
    }
    const auto year = number(name.substr(0, 4));
    const auto month = number(name.substr(4, 2));
    const auto day = number(name.substr(6, 2));
    const auto hours = number(name.substr(9, 2));
    const auto minutes = number(name.substr(11, 2));
    const auto seconds = number(name.substr(13, 2));
    const std::chrono::year_month_day date{
        std::chrono::year{*year},
        std::chrono::month{static_cast<unsigned>(*month)},
        std::chrono::day{static_cast<unsigned>(*day)}
    };
    if (!date.ok() || *hours > 23 || *minutes > 59 || *seconds > 59)
        return std::nullopt;
    return std::chrono::sys_days{date} + std::chrono::hours{*hours} +
           std::chrono::minutes{*minutes} + std::chrono::seconds{*seconds};
}

bool must_roll(sys_seconds begun, uint64_t bytes, sys_seconds now, const Limits& limits) {
    return bytes >= limits.max_file_bytes || utc_day(begun) != utc_day(now);
}

fs::path choose_file(const fs::path& folder, sys_seconds now, const Limits& limits) {
    const auto logs = list_logs(folder);
    if (!logs.empty()) {
        std::error_code error;
        const auto& newest = logs.back();
        const auto bytes = fs::file_size(newest.path, error);
        if (!error && !must_roll(newest.begun, bytes, now, limits))
            return newest.path;
    }
    return new_file(folder, now);
}

void prune(const fs::path& folder, sys_seconds now, const Limits& limits, const fs::path& current) {
    auto logs = list_logs(folder);
    std::error_code error;
    const auto oldest_kept = now - std::chrono::days{limits.max_age_days};
    const auto is_current = [&](const LogEntry& entry) {
        return entry.path.filename() == current.filename();
    };
    std::vector<LogEntry> kept;
    for (const auto& entry : logs) {
        if (entry.begun < oldest_kept && !is_current(entry))
            fs::remove(entry.path, error);
        else
            kept.push_back(entry);
    }
    // The current file counts even before anything is written to it.
    std::size_t count = kept.size();
    if (std::none_of(kept.begin(), kept.end(), is_current))
        ++count;
    const auto limit = static_cast<std::size_t>(std::max<int32_t>(limits.max_files, 1));
    for (const auto& entry : kept) {
        if (count <= limit)
            break;
        if (is_current(entry))
            continue;
        if (fs::remove(entry.path, error))
            --count;
    }
}

bool begin(const fs::path& folder, const Limits& limits) {
    std::error_code error;
    fs::create_directories(folder, error);
    if (!fs::is_directory(folder, error))
        return false;
    const auto now = now_utc();
    const auto path = choose_file(folder, now, limits);
    const int terminal = keep_terminal();
    if (!redirect(path)) {
        if (terminal >= 0) {
#if defined(_WIN32)
            _close(terminal);
#else
            close(terminal);
#endif
        }
        return false;
    }
    auto& current = state();
    current.terminal = terminal;
    current.folder = folder;
    current.limits = limits;
    current.current = path;
    current.begun = begun_at(path.filename().string()).value_or(now);
    current.last_check = std::chrono::steady_clock::now();
    current.active = true;
    prune(folder, now, limits, path);
    write_first_line(now);
    return true;
}

void maintain(sys_seconds now) {
    auto& current = state();
    if (!current.active)
        return;
    std::fflush(stdout);
    std::error_code error;
    const auto bytes = fs::file_size(current.current, error);
    if (!must_roll(current.begun, error ? 0 : bytes, now, current.limits))
        return;
    const auto path = new_file(current.folder, now);
    if (!redirect(path))
        return;
    current.current = path;
    current.begun = now;
    prune(current.folder, now, current.limits, path);
    write_first_line(now);
}

void maintain() {
    auto& current = state();
    if (!current.active)
        return;
    const auto steady = std::chrono::steady_clock::now();
    if (steady - current.last_check < std::chrono::seconds{1})
        return;
    current.last_check = steady;
    maintain(now_utc());
}

bool output_captured() {
#if defined(_WIN32)
    for (const DWORD stream : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
        const HANDLE handle = GetStdHandle(stream);
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
            continue;
        const DWORD type = GetFileType(handle);
        if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK)
            return true;
    }
    return false;
#else
    for (std::FILE* stream : {stdout, stderr}) {
        struct stat status{};
        if (fstat(fileno(stream), &status) == 0 &&
            (S_ISFIFO(status.st_mode) || S_ISREG(status.st_mode)))
            return true;
    }
    return false;
#endif
}

bool write_to_terminal(std::string_view text) {
    const auto& current = state();
    if (!current.active) {
        std::fwrite(text.data(), 1, text.size(), stderr);
        std::fflush(stderr);
        return true;
    }
    if (current.terminal < 0)
        return false;
#if defined(_WIN32)
    return _write(current.terminal, text.data(), static_cast<unsigned>(text.size())) >= 0;
#else
    return write(current.terminal, text.data(), text.size()) >= 0;
#endif
}

fs::path current_file() {
    return state().active ? state().current : fs::path{};
}

} // namespace oa::platform::log_files
