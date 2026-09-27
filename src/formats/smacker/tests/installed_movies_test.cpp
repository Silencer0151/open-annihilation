// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The installed game's movies, Data/1.zrb to Data/5.zrb: 640 wide, 240 tall
// but for the 304-tall fifth, at a frame-rate field of -3333 (30 frames a
// second), each with audio and a readable first frame. The movies are
// optional content, as they are to the game: an installation without them
// skips, and one without some of them checks the rest.
#include "oa/formats/smacker.hpp"
#include "oa/test/game_data.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr int kInstalledMovieCount = 5;
constexpr uint32_t kInstalledMovieWidth = 640;
constexpr uint32_t kInstalledMovieHeight = 240;
// The one movie of the five that is taller.
constexpr int kTallMovie = 5;
constexpr uint32_t kTallMovieHeight = 304;
// Negative: 1/100000 s per frame.
constexpr int32_t kInstalledMovieFrameRate = -3333;

int failures = 0;

/// Reports a failed check and counts it.
///
/// @param condition the check
/// @param what the failure, printed after "FAIL: " when `condition` is false
/// @return `condition`
bool check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
    return condition;
}

/// Finds a directory entry by name ignoring ASCII case.
///
/// Installs spell the Data folder and the movie names either way.
///
/// @param directory the folder searched
/// @param name the entry's name in any case
/// @return the entry, or nullopt when the folder holds none of that name
std::optional<std::filesystem::path>
entry_ignoring_case(const std::filesystem::path& directory, const std::string& name) {
    const auto upper = [](char c) {
        return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    };
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        const auto candidate = entry.path().filename().string();
        if (std::equal(
                candidate.begin(), candidate.end(), name.begin(), name.end(), [&](char a, char b) {
                    return upper(a) == upper(b);
                }
            ))
            return entry.path();
    }
    return std::nullopt;
}

/// Opens each installed movie and checks its header and first frame.
///
/// Ends the test as skipped when the installation has no Data folder or no
/// movie in it; a missing movie is noted and the others are checked.
///
/// @param root the installation
void check_installed_movies(const std::filesystem::path& root) {
    const auto data = entry_ignoring_case(root, "Data");
    if (!data)
        oa::test::skip_test("the installed movies", "the install has no Data folder");
    int present = 0;
    for (int movie = 1; movie <= kInstalledMovieCount; ++movie) {
        const std::string name = std::to_string(movie) + ".zrb";
        const auto path = entry_ignoring_case(*data, name);
        if (!path) {
            std::cout << "note: the install holds no Data/" << name << '\n';
            continue;
        }
        ++present;
        const auto opened = oa::formats::smacker::SmackerReader::open(*path);
        if (!check(static_cast<bool>(opened), "Data/" + name + " does not open: " + opened.error))
            continue;
        const auto& header = opened.reader->header();
        const auto expected_height = movie == kTallMovie ? kTallMovieHeight : kInstalledMovieHeight;
        check(
            header.signature == oa::formats::smacker::kSmk2 &&
                header.width == kInstalledMovieWidth && header.height == expected_height &&
                header.frame_rate == kInstalledMovieFrameRate && header.has_audio(),
            "Data/" + name + ": SMK2 header"
        );
        const auto frame = opened.reader->frame(0);
        if (!check(frame && frame->compressed_size != 0, "Data/" + name + ": first frame"))
            continue;
        std::vector<uint8_t> payload;
        std::string error;
        check(
            opened.reader->read_frame(0, payload, error) &&
                payload.size() == frame->compressed_size,
            "Data/" + name + ": first frame payload " + error
        );
    }
    if (present == 0)
        oa::test::skip_test("the installed movies", "the install's Data folder holds no movie");
}

} // namespace

int main() {
    check_installed_movies(oa::test::require_game_directory("the installed movies"));
    if (failures != 0)
        return 1;
    std::cout << "installed Smacker movies passed\n";
    return 0;
}
