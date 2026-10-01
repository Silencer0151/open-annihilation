// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Telling a Raspberry Pi from its board's model, given as text and as the
// model file Linux keeps it in.

#include "oa/platform/machine.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

namespace fs = std::filesystem;
namespace platform = oa::platform;

int failures = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (condition)
        return;
    std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
    ++failures;
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

using namespace std::string_literals;

/// Writes a model file as Linux gives it: the text, then a NUL.
///
/// @param path the file
/// @param contents the file's bytes
void write_file(const fs::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

void every_raspberry_pi_model_is_one() {
    CHECK(platform::raspberry_pi_model("Raspberry Pi 5 Model B Rev 1.0"));
    CHECK(platform::raspberry_pi_model("Raspberry Pi 4 Model B Rev 1.4"));
    CHECK(platform::raspberry_pi_model("Raspberry Pi 400 Rev 1.0"));
    CHECK(platform::raspberry_pi_model("Raspberry Pi 3 Model B Plus Rev 1.3"));
    CHECK(platform::raspberry_pi_model("Raspberry Pi 2 Model B Rev 1.1"));
    CHECK(platform::raspberry_pi_model("Raspberry Pi Compute Module 4 Rev 1.0"));
    CHECK(platform::raspberry_pi_model("Raspberry Pi Zero 2 W Rev 1.0"));
    CHECK(platform::raspberry_pi_model("Raspberry Pi 4 Model B Rev 1.4\0"s));
    CHECK(platform::raspberry_pi_model(platform::raspberry_pi_model_prefix));
}

void other_boards_are_not() {
    CHECK(!platform::raspberry_pi_model(""));
    CHECK(!platform::raspberry_pi_model("linux,dummy-virt"));
    CHECK(!platform::raspberry_pi_model("Pine64 RockPro64 v2.1"));
    CHECK(!platform::raspberry_pi_model("Radxa ROCK 5B"));
    CHECK(!platform::raspberry_pi_model("raspberry pi 4 model b"));
    CHECK(!platform::raspberry_pi_model(" Raspberry Pi 4 Model B"));
    CHECK(!platform::raspberry_pi_model("Raspberry"));
    CHECK(!platform::raspberry_pi_model("\0Raspberry Pi 4 Model B"s));
}

void model_files_are_read_up_to_their_limit() {
    std::error_code error;
    const fs::path folder = fs::temp_directory_path(error) / "oa-platform-machine-test";
    fs::create_directories(folder, error);
    const fs::path model = folder / "model";

    write_file(model, "Raspberry Pi 4 Model B Rev 1.4\0"s);
    CHECK(platform::model_file_names_raspberry_pi(model));
    write_file(model, "Raspberry Pi 5 Model B Rev 1.0");
    CHECK(platform::model_file_names_raspberry_pi(model));
    write_file(model, "linux,dummy-virt\0"s);
    CHECK(!platform::model_file_names_raspberry_pi(model));
    write_file(model, "");
    CHECK(!platform::model_file_names_raspberry_pi(model));
    // The name past the read limit is not seen.
    write_file(model, std::string(platform::model_file_limit, ' ') + "Raspberry Pi 4");
    CHECK(!platform::model_file_names_raspberry_pi(model));
    write_file(model, "Raspberry Pi 4" + std::string(platform::model_file_limit * 4, 'x'));
    CHECK(platform::model_file_names_raspberry_pi(model));

    CHECK(!platform::model_file_names_raspberry_pi(folder / "missing"));
    CHECK(!platform::model_file_names_raspberry_pi(folder));
    fs::remove_all(folder, error);
}

void only_linux_reads_the_device_tree() {
#if !defined(__linux__)
    CHECK(!platform::running_on_raspberry_pi());
#else
    CHECK(
        platform::running_on_raspberry_pi() ==
        platform::model_file_names_raspberry_pi(platform::device_tree_model_path)
    );
#endif
}

} // namespace

int main() {
    every_raspberry_pi_model_is_one();
    other_boards_are_not();
    model_files_are_read_up_to_their_limit();
    only_linux_reads_the_device_tree();
    return failures == 0 ? 0 : 1;
}
