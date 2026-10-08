// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// read_game_file over a fixture store: a loose file wins over the same path
// in an archive, a file that only the archive holds is read, and a missing
// path returns false.
#include "game_file.hpp"

#include "oa/formats/hpi.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

/// Writes bytes to a file, creating its folder.
///
/// @param path the file
/// @param bytes its contents
void write_bytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
    );
}

/// The bytes of a text.
///
/// @param text the text
/// @return its bytes
std::vector<uint8_t> text_bytes(std::string_view text) {
    return {text.begin(), text.end()};
}

/// An archive of the named files, each holding its text.
///
/// @param files path and text of each entry
/// @return the archive bytes
std::vector<uint8_t> archive(std::vector<std::pair<std::string, std::string>> files) {
    std::vector<oa::HpiWriteFile> written;
    for (auto& file : files)
        written.push_back(oa::HpiWriteFile{std::move(file.first), text_bytes(file.second)});
    return oa::write_hpi(written);
}

/// The text `bytes` hold.
///
/// @param bytes the bytes
/// @return the text
std::string text_of(const std::vector<uint8_t>& bytes) {
    return {bytes.begin(), bytes.end()};
}

/// A loose file wins over the archive, the archive's own file is read, and
/// a missing path returns false with empty bytes.
void reads_loose_archive_and_missing() {
    const fs::path root = oa::test::make_scratch_directory("read-game-file");
    write_bytes(root / "notes" / "loose.txt", text_bytes("from-loose"));
    write_bytes(
        root / "pack.hpi",
        archive({{"notes/loose.txt", "from-archive"}, {"notes/archived.txt", "archived-only"}})
    );

    {
        oa::AssetStore store(root);
        store.mount(root / "pack.hpi");

        // read_game_file reads the runtime's store through this.
        std::vector<uint8_t> bytes{1, 2, 3};
        OA_CHECK(oa::app::read_stored_game_file(store, "notes/loose.txt", bytes));
        OA_CHECK(text_of(bytes) == "from-loose");
        OA_CHECK(oa::app::read_stored_game_file(store, "notes\\archived.txt", bytes));
        OA_CHECK(text_of(bytes) == "archived-only");
        bytes = {9};
        OA_CHECK(!oa::app::read_stored_game_file(store, "notes/missing.txt", bytes));
        OA_CHECK(bytes.empty());
        bytes = {9};
        OA_CHECK(!oa::app::read_stored_game_file(store, nullptr, bytes));
        OA_CHECK(bytes.empty());
    }

    // The store holds the archive open until it is gone, and Windows does not
    // remove an open file.
    std::error_code error;
    fs::remove_all(root, error);
}

} // namespace

int main() {
    reads_loose_archive_and_missing();
    return oa::test::check_exit_status();
}
