// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The log folder's names, rolling and pruning, and the streams sent to it.

#include "oa/platform/log_files.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;
namespace log_files = oa::platform::log_files;
using std::chrono::sys_seconds;

int failures = 0;
std::vector<std::string> messages;

void check(bool condition, const std::string& what) {
    if (!condition) {
        messages.push_back("FAIL: " + what + "\n");
        ++failures;
    }
}

sys_seconds at(int year, unsigned month, unsigned day, int hours, int minutes, int seconds) {
    return std::chrono::sys_days{std::chrono::year_month_day{
               std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}
           }} +
           std::chrono::hours{hours} + std::chrono::minutes{minutes} +
           std::chrono::seconds{seconds};
}

void write_file(const fs::path& path, std::size_t bytes) {
    std::ofstream out(path, std::ios::binary);
    out << std::string(bytes, 'x');
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<std::string> names_in(const fs::path& folder) {
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(folder))
        names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

fs::path fresh_folder(const std::string& name) {
    const auto folder = fs::temp_directory_path() / ("oa-log-files-test-" + name);
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

void test_names() {
    const auto begun = at(2026, 9, 27, 22, 15, 30);
    check(log_files::file_name(begun, 0) == "open-annihilation-20260927-221530.log", "file name");
    check(log_files::file_name(begun, 2) == "open-annihilation-20260927-221530-2.log", "sequence");
    check(log_files::begun_at("open-annihilation-20260927-221530.log") == begun, "name read back");
    check(log_files::begun_at("open-annihilation-20260927-221530-2.log") == begun, "sequence read");
    check(!log_files::begun_at("open-annihilation-20260231-221530.log"), "no 31 February");
    check(!log_files::begun_at("open-annihilation-20260927-241530.log"), "no hour 24");
    check(!log_files::begun_at("open-annihilation-2026092-221530.log"), "short date");
    check(!log_files::begun_at("open-annihilation-20260927-221530-x.log"), "bad sequence");
    check(!log_files::begun_at("notes.txt"), "other files");
    check(!log_files::begun_at("open-annihilation-20260927-221530.txt"), "other extension");
}

void test_rolling() {
    const log_files::Limits limits{};
    const auto begun = at(2026, 9, 27, 23, 59, 0);
    check(!log_files::must_roll(begun, 100, begun + std::chrono::seconds{30}, limits), "grows");
    check(log_files::must_roll(begun, limits.max_file_bytes, begun, limits), "full at 10 MB");
    check(!log_files::must_roll(begun, limits.max_file_bytes - 1, begun, limits), "not yet full");
    check(log_files::must_roll(begun, 0, begun + std::chrono::minutes{1}, limits), "UTC midnight");
}

void test_choose_file() {
    const auto folder = fresh_folder("choose");
    const log_files::Limits limits{};
    const auto morning = at(2026, 9, 27, 8, 0, 0);
    const auto fresh = log_files::choose_file(folder, morning, limits);
    check(fresh.filename() == "open-annihilation-20260927-080000.log", "first file");
    write_file(fresh, 100);
    const auto later = at(2026, 9, 27, 9, 30, 0);
    check(log_files::choose_file(folder, later, limits) == fresh, "same day keeps the file");
    const auto next_day = at(2026, 9, 28, 0, 0, 5);
    check(
        log_files::choose_file(folder, next_day, limits).filename() ==
            "open-annihilation-20260928-000005.log",
        "a new day begins a file"
    );
    write_file(fresh, static_cast<std::size_t>(limits.max_file_bytes));
    check(
        log_files::choose_file(folder, later, limits).filename() ==
            "open-annihilation-20260927-093000.log",
        "a full file gives way"
    );
    write_file(
        folder / "open-annihilation-20260927-093000.log",
        static_cast<std::size_t>(limits.max_file_bytes)
    );
    check(
        log_files::choose_file(folder, later, limits).filename() ==
            "open-annihilation-20260927-093000-1.log",
        "a second file in the same second"
    );
    fs::remove_all(folder);
}

void test_prune() {
    const auto folder = fresh_folder("prune");
    const log_files::Limits limits{};
    const auto now = at(2026, 9, 27, 12, 0, 0);
    // Ten days, three files a day, and a file that is not a log.
    for (int day = 0; day < 10; ++day)
        for (int hour = 0; hour < 3; ++hour)
            write_file(
                folder / log_files::file_name(
                             now - std::chrono::days{day} - std::chrono::hours{hour}, 0
                         ),
                10
            );
    write_file(folder / "notes.txt", 10);
    const auto current = folder / log_files::file_name(now, 1);
    log_files::prune(folder, now, limits, current);
    auto names = names_in(folder);
    std::size_t logs = 0;
    for (const auto& name : names) {
        if (const auto begun = log_files::begun_at(name)) {
            ++logs;
            check(*begun >= now - std::chrono::days{7}, "nothing older than seven days: " + name);
        }
    }
    // Seven days hold 22 files (the hours before noon of the seventh day are
    // older than seven days); the current file, not yet written, brings the
    // count to 23, within 25.
    check(logs == 22, "files within seven days kept: " + std::to_string(logs));
    check(std::find(names.begin(), names.end(), "notes.txt") != names.end(), "other files kept");

    // With a tighter limit the oldest go first, never the current file.
    write_file(current, 10);
    log_files::Limits tight = limits;
    tight.max_files = 5;
    log_files::prune(folder, now, tight, current);
    names = names_in(folder);
    logs = 0;
    for (const auto& name : names)
        logs += log_files::begun_at(name) ? 1 : 0;
    check(logs == 5, "at most max_files: " + std::to_string(logs));
    check(fs::exists(current), "the current file stays");
    check(fs::exists(folder / log_files::file_name(now, 0)), "the newest others stay");
    fs::remove_all(folder);
}

// Sends this process's streams to a folder, writes through every route, rolls
// to a new file and checks both files. Runs last: the streams stay redirected.
void test_streams(int report) {
    const auto folder = fresh_folder("streams");
    check(log_files::current_file().empty(), "no file before begin");
    check(log_files::begin(folder), "begin");
    const auto first = log_files::current_file();
    check(first.parent_path() == folder, "the file is in the folder");
    std::printf("from printf\n");
    std::fprintf(stderr, "from stderr\n");
    std::cout << "from cout" << std::endl;
    std::cerr << "from cerr\n";
    std::fflush(stdout);
    const auto text = read_file(first);
    for (const char* line :
         {"open-annihilation log, ", "from printf", "from stderr", "from cout", "from cerr"})
        check(text.find(line) != std::string::npos, std::string("the file holds: ") + line);
    // The next UTC day moves both streams to a new file.
    log_files::maintain(
        std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) +
        std::chrono::days{1}
    );
    const auto second = log_files::current_file();
    check(second != first && fs::exists(second), "a new file after the day changes");
    std::printf("after the roll\n");
    std::fflush(stdout);
    check(read_file(second).find("after the roll") != std::string::npos, "the new file is written");
    check(read_file(first).find("after the roll") == std::string::npos, "the old file is left");
    for (const auto& message : messages) {
#if defined(_WIN32)
        (void)_write(report, message.data(), static_cast<unsigned>(message.size()));
#else
        (void)write(report, message.data(), message.size());
#endif
    }
    messages.clear();
    // Windows keeps a file open for writing from being deleted, and both
    // streams still write to the second file, so the folder may stay behind.
    std::error_code error;
    fs::remove_all(folder, error);
}

} // namespace

int main() {
    test_names();
    test_rolling();
    test_choose_file();
    test_prune();
    for (const auto& message : messages)
        std::fputs(message.c_str(), stderr);
    messages.clear();
    // The failures of the last test are written to the error stream this test
    // began with, since the test moves the stream to the log folder.
#if defined(_WIN32)
    const int report = _dup(_fileno(stderr));
#else
    const int report = dup(fileno(stderr));
#endif
    test_streams(report);
    return failures == 0 ? 0 : 1;
}
