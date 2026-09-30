// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/preferences.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
#include <stdexcept>
#include <string>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace {
/// Throws, naming the file and line of the check, when `value` is false.
void require(bool value, const std::source_location where = std::source_location::current()) {
    if (!value)
        throw std::runtime_error(
            std::string(where.file_name()) + ":" + std::to_string(where.line()) +
            ": preference check failed"
        );
}

template <class F>
void rejects(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("invalid preference accepted");
}
} // namespace

int main() {
    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("oa-preferences-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{temporary};

    try {
        const auto file = temporary / "nested" / "preferences.conf";
        require(oa::platform::preferences::load(file).empty());
        const oa::platform::preferences::Values values{
            {"Total Annihilation|Game Speed", "10"},
            {"Unicode", "café 日本"},
            {"empty", ""},
            {"delimiters", "= | \\\"\nsecond line"}
        };
        oa::platform::preferences::save(file, values);
        require(oa::platform::preferences::load(file) == values);
        auto changed = values;
        changed["Total Annihilation|Game Speed"] = "20";
        oa::platform::preferences::save(file, changed);
        require(oa::platform::preferences::load(file) == changed);
        auto excessive = changed;
        excessive["too big"] = std::string(70000, 'x');
        rejects([&] { oa::platform::preferences::save(file, excessive); });
        require(
            oa::platform::preferences::load(file) == changed
        ); // Failed save preserves last good file.
        oa::platform::preferences::Values aggregate;
        for (unsigned index = 0; index < 17; ++index)
            aggregate[std::to_string(index)] = std::string(65536, 'x');
        rejects([&] { oa::platform::preferences::save(file, aggregate); });
        require(oa::platform::preferences::load(file) == changed);
        aggregate.clear();
        for (unsigned index = 0; index < 8; ++index)
            aggregate[std::to_string(index)] = std::string(65536, '\\');
        rejects([&] { oa::platform::preferences::save(file, aggregate); });
        require(
            oa::platform::preferences::load(file) == changed
        ); // Escaping counts toward file cap.
        {
            // A file named without a folder, as `--preferences-file
            // prefs.conf` names one, is written in the current directory.
            const auto working = std::filesystem::current_path();
            const auto folder = temporary / "working";
            std::filesystem::create_directories(folder);
            std::filesystem::current_path(folder);
            const std::filesystem::path bare{"bare.conf"};
            bool saved = false;
            try {
                oa::platform::preferences::save(bare, values);
                saved = oa::platform::preferences::load(bare) == values;
            } catch (const std::exception&) {
                saved = false;
            }
            std::filesystem::current_path(working);
            require(saved);
            require(oa::platform::preferences::load(folder / bare) == values);
        }
        std::ofstream(
            file
        ) << "open-annihilation-preferences 1\n\"duplicate\" \"a\"\n\"duplicate\" \"b\"\n";
        rejects([&] { (void)oa::platform::preferences::load(file); });
        std::ofstream(file) << "open-annihilation-preferences 1\n\"unfinished";
        rejects([&] { (void)oa::platform::preferences::load(file); });
        std::ofstream(file) << std::string(1024U * 1024U + 1, 'x');
        rejects([&] { (void)oa::platform::preferences::load(file); });
        require(oa::platform::preferences::default_file().is_absolute());
        require(oa::platform::preferences::data_directory().is_absolute());
#if defined(__APPLE__) || defined(_WIN32)
        require(
            oa::platform::preferences::data_directory() ==
            oa::platform::preferences::default_file().parent_path()
        );
#else
        {
            // XDG_DATA_HOME names the data folder while it is absolute.
            const char* saved = std::getenv("XDG_DATA_HOME");
            const std::string previous = saved != nullptr ? saved : "";
            ::setenv("XDG_DATA_HOME", "/xdg/data", 1);
            require(
                oa::platform::preferences::data_directory() ==
                std::filesystem::path("/xdg/data/open-annihilation")
            );
            ::setenv("XDG_DATA_HOME", "relative/data", 1);
            require(
                oa::platform::preferences::data_directory() ==
                std::filesystem::path(std::getenv("HOME")) / ".local/share/open-annihilation"
            );
            if (saved != nullptr)
                ::setenv("XDG_DATA_HOME", previous.c_str(), 1);
            else
                ::unsetenv("XDG_DATA_HOME");
        }
#endif
        {
            // A stand-in for the user's Application Support folder.
            const auto support = temporary / "Application Support";
            const auto folder = support / "net.coreprime.open-annihilation";
            const auto earlier = support / "com.coreprime.open-annihilation";
            const auto write = [](const std::filesystem::path& file, const std::string& text) {
                std::filesystem::create_directories(file.parent_path());
                std::ofstream(file, std::ios::binary) << text;
            };
            const auto read = [](const std::filesystem::path& file) {
                std::ifstream input(file, std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(input), {});
            };
            // Neither folder: the new one, and nothing is created.
            require(oa::platform::preferences::apple_data_directory(support) == folder);
            require(!std::filesystem::exists(support));
            // Only the earlier folder: it moves to the new name with its files.
            write(earlier / "preferences.conf", "earlier preferences");
            write(earlier / "demo-1997" / "totala1.hpi", "earlier demo data");
            require(oa::platform::preferences::apple_data_directory(support) == folder);
            require(!std::filesystem::exists(earlier));
            require(read(folder / "preferences.conf") == "earlier preferences");
            require(read(folder / "demo-1997" / "totala1.hpi") == "earlier demo data");
            require(oa::platform::preferences::apple_data_directory(support) == folder);
            // Both folders: the new one is used and neither is touched.
            write(earlier / "preferences.conf", "stale preferences");
            require(oa::platform::preferences::apple_data_directory(support) == folder);
            require(read(earlier / "preferences.conf") == "stale preferences");
            require(read(folder / "preferences.conf") == "earlier preferences");
            require(read(folder / "demo-1997" / "totala1.hpi") == "earlier demo data");
#ifndef _WIN32
            // A rename that fails leaves the earlier folder where it is, and
            // it is used under that name. An unwritable Application Support
            // folder makes it fail, except for the superuser.
            std::filesystem::remove_all(folder);
            std::filesystem::permissions(
                support, std::filesystem::perms::owner_write, std::filesystem::perm_options::remove
            );
            const bool superuser = ::geteuid() == 0;
            const auto unwritable = oa::platform::preferences::apple_data_directory(support);
            std::filesystem::permissions(
                support, std::filesystem::perms::owner_write, std::filesystem::perm_options::add
            );
            if (!superuser) {
                require(unwritable == earlier);
                require(!std::filesystem::exists(folder));
                require(read(earlier / "preferences.conf") == "stale preferences");
            }
#endif
        }
        oa::platform::preferences::MusicOptions music;
        music.music_volume = 1;
        music.cd_mode = 1;
        music.music_flags = 0xab00;
        oa::platform::preferences::restore_music_defaults(music);
        require(music.music_volume == oa::platform::preferences::default_music_volume);
        require(music.cd_mode == oa::platform::preferences::default_cd_mode);
        require(music.music_flags == 0xab01);
        music.music_flags = 0xfffe;
        music.music_volume = 0;
        music.cd_mode = 0;
        oa::platform::preferences::restore_music_defaults(music);
        require(music.music_volume == 0x20 && music.cd_mode == 4 && music.music_flags == 0xffff);
        music.music_flags = 0x10f7;
        music.music_volume = 9;
        music.cd_mode = 2;
        oa::platform::preferences::restore_music_defaults(music);
        require(
            music.music_flags == 0x10f7 &&
            music.music_volume == oa::platform::preferences::default_music_volume &&
            music.cd_mode == oa::platform::preferences::default_cd_mode
        );
        std::cout << "preferences roundtrip and music restore checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
