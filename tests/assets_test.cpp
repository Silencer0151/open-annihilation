// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/hpi.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr uint8_t kHeaderKey = 0xBF;
constexpr uint8_t kXorKey = 0xFE;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <class Function>
void require_throws(Function&& function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

template <class Function>
void require_throws_containing(
    Function&& function, std::string_view expected, std::string_view message
) {
    try {
        function();
    } catch (const std::exception& error) {
        if (std::string_view(error.what()).find(expected) != std::string_view::npos) {
            return;
        }
        throw std::runtime_error(std::string(message) + ": got '" + error.what() + "'");
    }
    throw std::runtime_error(std::string(message));
}

void put16(std::vector<uint8_t>& bytes, std::size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
}

void put32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16U);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24U);
}

uint32_t get32(std::span<const uint8_t> bytes, std::size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24U);
}

uint32_t checksum(std::span<const uint8_t> bytes) {
    uint32_t result = 0;
    for (const auto byte : bytes) {
        result += byte;
    }
    return result;
}

std::vector<uint8_t> zlib_compress(std::span<const uint8_t> input) {
    uLongf output_size = compressBound(input.size());
    std::vector<uint8_t> output(output_size);
    require(
        compress2(output.data(), &output_size, input.data(), input.size(), Z_BEST_SPEED) == Z_OK,
        "test zlib compression failed"
    );
    output.resize(output_size);
    return output;
}

std::vector<uint8_t> sqsh(uint8_t method, std::span<const uint8_t> stored, uint32_t decoded_size) {
    std::vector<uint8_t> result(19 + stored.size());
    put32(result, 0, 0x48535153U);
    result[4] = 2;
    result[5] = method;
    put32(result, 7, static_cast<uint32_t>(stored.size()));
    put32(result, 11, decoded_size);
    put32(result, 15, checksum(stored));
    std::copy(stored.begin(), stored.end(), result.begin() + 19);
    return result;
}

std::vector<uint8_t>
chunk_stream(uint8_t method, std::span<const uint8_t> stored, uint32_t decoded_size) {
    const auto chunk = sqsh(method, stored, decoded_size);
    std::vector<uint8_t> result(4 + chunk.size());
    put32(result, 0, static_cast<uint32_t>(chunk.size()));
    std::copy(chunk.begin(), chunk.end(), result.begin() + 4);
    return result;
}

// The archive's 36-byte copyright trailer, here with the year 1998. Every
// archive the reader accepts ends with these bytes; their four year
// characters may be any year.
constexpr char archive_trailer_bytes[] = {0x43, 0x6f, 0x70, 0x79, 0x72, 0x69, 0x67, 0x68, 0x74,
                                          0x20, 0x31, 0x39, 0x39, 0x38, 0x20, 0x43, 0x61, 0x76,
                                          0x65, 0x64, 0x6f, 0x67, 0x20, 0x45, 0x6e, 0x74, 0x65,
                                          0x72, 0x74, 0x61, 0x69, 0x6e, 0x6d, 0x65, 0x6e, 0x74};
constexpr std::string_view archive_trailer{archive_trailer_bytes, sizeof archive_trailer_bytes};

void append_trailer(std::vector<uint8_t>& archive) {
    archive.insert(archive.end(), archive_trailer.begin(), archive_trailer.end());
}

struct TestFile {
    std::string name;
    std::vector<uint8_t> decoded;
    std::vector<uint8_t> stored;
    uint8_t directory_method{};
};

std::vector<uint8_t> make_archive(bool encrypt = true) {
    const std::vector<uint8_t> raw{'r', 'a', 'w'};
    const std::vector<uint8_t> plain{'z', 'l', 'i', 'b', '-', 'd', 'a', 't', 'a'};
    const auto compressed = zlib_compress(plain);
    const std::vector<uint8_t> lz_output{'L', 'Z', '7', '7'};
    // Four literal flags followed by the format's zero-offset terminator.
    const std::vector<uint8_t> lz_literals{0x10, 'L', 'Z', '7', '7', 0, 0};
    const std::vector<uint8_t> stored_output{'S', 'Q', 'S', 'H'};

    std::vector<TestFile> files{
        {"raw.bin", raw, raw, 0},
        {"z.bin", plain, chunk_stream(2, compressed, static_cast<uint32_t>(plain.size())), 2},
        {"lz.bin",
         lz_output,
         chunk_stream(1, lz_literals, static_cast<uint32_t>(lz_output.size())),
         1},
        {"stored.bin",
         stored_output,
         chunk_stream(0, stored_output, static_cast<uint32_t>(stored_output.size())),
         1},
    };

    const std::size_t count = files.size();
    std::size_t directory_end = 20 + 8 + count * 9;
    for (const auto& file : files) {
        directory_end += file.name.size() + 1 + 9;
    }
    std::size_t total_size = directory_end;
    for (const auto& file : files) {
        total_size += file.stored.size();
    }
    std::vector<uint8_t> archive(total_size);
    put32(archive, 0, 0x49504148U);
    put32(archive, 4, 0x00010000U);
    put32(archive, 8, static_cast<uint32_t>(directory_end));
    put32(archive, 12, encrypt ? kHeaderKey : 0);
    put32(archive, 16, 20);
    put32(archive, 20, static_cast<uint32_t>(count));
    put32(archive, 24, 28);

    std::size_t metadata = 28 + count * 9;
    std::size_t file_data = directory_end;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t entry = 28 + i * 9;
        put32(archive, entry, static_cast<uint32_t>(metadata));
        std::copy(
            files[i].name.begin(),
            files[i].name.end(),
            archive.begin() + static_cast<std::ptrdiff_t>(metadata)
        );
        metadata += files[i].name.size() + 1;
        put32(archive, entry + 4, static_cast<uint32_t>(metadata));
        put32(archive, metadata, static_cast<uint32_t>(file_data));
        put32(archive, metadata + 4, static_cast<uint32_t>(files[i].decoded.size()));
        archive[metadata + 8] = files[i].directory_method;
        metadata += 9;
        std::copy(
            files[i].stored.begin(),
            files[i].stored.end(),
            archive.begin() + static_cast<std::ptrdiff_t>(file_data)
        );
        file_data += files[i].stored.size();
    }

    if (encrypt) {
        for (std::size_t i = 20; i < archive.size(); ++i) {
            archive[i] ^= static_cast<uint8_t>(i) ^ kXorKey;
        }
    }
    append_trailer(archive);
    return archive;
}

class TemporaryFile {
  public:

    explicit TemporaryFile(std::span<const uint8_t> bytes) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("oa-assets-test-" + std::to_string(stamp) + ".hpi");
        std::ofstream stream(path_, std::ios::binary);
        require(static_cast<bool>(stream), "cannot create temporary archive");
        stream.write(
            reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
        );
        require(static_cast<bool>(stream), "cannot write temporary archive");
    }

    ~TemporaryFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

    void resize(uintmax_t size) {
        std::error_code error;
        std::filesystem::resize_file(path_, size, error);
        require(!error, "cannot resize sparse temporary archive");
    }

  private:

    std::filesystem::path path_;
};

void test_palette_mapping() {
    oa::PaletteBytes source{}, destination{};
    destination.fill(255);
    // Equal L1 distance; a Euclidean matcher would prefer candidate 1.
    destination[0] = 3;
    destination[1] = 0;
    destination[2] = 0;
    destination[4] = 1;
    destination[5] = 1;
    destination[6] = 1;
    require(
        oa::remap_palette(source, destination)[0] == 0,
        "palette must use L1 distance and first-index tie resolution"
    );
    destination[4] = 0;
    destination[5] = 0;
    destination[6] = 0;
    destination[7] = 255;
    require(oa::remap_palette(source, destination)[0] == 1, "palette must ignore fourth byte");
    for (std::size_t index = 0; index < oa::palette_color_count; ++index) {
        source[index * oa::palette_entry_bytes] = static_cast<uint8_t>(index);
    }
    const auto identity = oa::remap_palette(source, source);
    for (std::size_t index = 0; index < identity.size(); ++index)
        require(identity[index] == index, "exact palette color must map to itself");
}

std::vector<uint8_t> one_byte_archive(uint8_t header_key, uint8_t plain) {
    // Header, one directory entry named "a", and one uncompressed byte.
    std::vector<uint8_t> archive(49);
    put32(archive, 0, 0x49504148U);
    put32(archive, 4, 0x00010000U);
    put32(archive, 8, 48);
    put32(archive, 12, header_key);
    put32(archive, 16, 20);
    put32(archive, 20, 1);
    put32(archive, 24, 28);
    put32(archive, 28, 37);
    archive[37] = 'a';
    put32(archive, 32, 39);
    put32(archive, 39, 48);
    put32(archive, 43, 1);
    archive[48] = plain;
    return archive;
}

std::vector<uint8_t> with_trailer(std::vector<uint8_t> archive) {
    append_trailer(archive);
    return archive;
}

void test_asset_store_precedence() {
    TemporaryFile first(make_archive());
    TemporaryFile second(make_archive());
    const auto root = first.path().string() + "-loose";

    struct RemoveDirectory {
        std::filesystem::path path;

        ~RemoveDirectory() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{root};

    std::filesystem::create_directory(root);
    oa::AssetStore store(root);
    store.mount(first.path());
    store.mount(second.path());
    store.mount(first.path());
    auto data = store.read("RAW.BIN");
    require(
        data.archived && data.source == std::filesystem::weakly_canonical(first.path()),
        "first mounted archive must win"
    );
    require(data.bytes == std::vector<uint8_t>({'r', 'a', 'w'}), "wrong archive data");
    require(
        store.providing_archive("raw.bin") == data.source, "providing archive must be the winner"
    );
    require(!store.providing_archive("missing.bin"), "absent resource has no providing archive");
    const auto below = store.read_without("RAW.BIN", data.source);
    require(
        below.archived && below.source == std::filesystem::weakly_canonical(second.path()) &&
            below.bytes == data.bytes,
        "without the first archive the next mount must provide the resource"
    );
    TemporaryFile only_here(with_trailer(one_byte_archive(0xFF, 'q')));
    store.mount(only_here.path());
    require(store.read("a").bytes == std::vector<uint8_t>{'q'}, "third mount's file");
    require_throws(
        [&] { (void)store.read_without("a", std::filesystem::weakly_canonical(only_here.path())); },
        "a resource only the skipped archive holds must be absent"
    );
    {
        std::ofstream stream(std::filesystem::path(root) / "RaW.Bin", std::ios::binary);
        stream << "loose override";
    }
    // The store lists each loose folder once; a rescan picks up the new file.
    store.mark_loose_shadows();
    data = store.read("raw.bin");
    require(
        !data.archived && std::string(data.bytes.begin(), data.bytes.end()) == "loose override",
        "case-insensitive loose file must override archive"
    );
    require(!store.providing_archive("raw.bin"), "a loose file has no providing archive");
    require(
        !store.read_without("raw.bin", std::filesystem::weakly_canonical(first.path())).archived,
        "a loose file keeps precedence over the remaining mounts"
    );
    std::filesystem::create_directory(std::filesystem::path(root) / "GuiS");
    {
        std::ofstream stream(std::filesystem::path(root) / "GuiS" / "MainMenu.GUI");
        stream << "nested";
    }
    store.mark_loose_shadows();
    require(store.read("GUIS\\mainmenu.gui").bytes.size() == 6, "Windows path lookup failed");
    require(
        store.list_effective("GUIS", ".GUI") == std::vector<std::string>{"guis/mainmenu.gui"},
        "effective listing must normalize directory and suffix"
    );
    const auto binaries = store.list_effective("", ".bin");
    require(
        binaries.size() == 4 && std::count(binaries.begin(), binaries.end(), "raw.bin") == 1,
        "effective listing must deduplicate loose and mounted resources"
    );
    require(
        store.list_effective_in_mount_order("", ".bin") ==
            std::vector<std::string>{"raw.bin", "z.bin", "lz.bin", "stored.bin"},
        "mount listing must preserve stored order while deduplicating loose overrides"
    );
    require(
        store.list_effective_recursive("", ".gui") == std::vector<std::string>{"guis/mainmenu.gui"},
        "recursive listing must include nested resources"
    );
    require(
        store.list_effective("", ".gui").empty(),
        "nonrecursive listing must exclude nested resources"
    );
    require(store.list_effective("missing", ".fbi").empty(), "absent directory listing");
    require_throws(
        [&] { (void)store.list_effective("../", ".fbi"); }, "listing traversal accepted"
    );
    require_throws([&] { (void)store.read("../raw.bin"); }, "traversal accepted");
    require_throws([&] { (void)store.read("/raw.bin"); }, "absolute path accepted");
    require_throws([&] { (void)store.read("missing.gui"); }, "missing asset accepted");
}

void encrypt_hpi_tail(std::vector<uint8_t>& archive, uint8_t key) {
    for (std::size_t i = 20; i < archive.size(); ++i) {
        const auto position = static_cast<uint8_t>(i);
        const auto inverted = static_cast<uint8_t>(~archive[i]);
        archive[i] = static_cast<uint8_t>(position ^ inverted ^ key);
    }
}

void test_hpi_archive_key() {
    require(oa::hpi_archive_key(0) == 0, "hpi_archive_key must keep a zero header byte");
    require(oa::hpi_archive_key(0x01) == 0xFB, "hpi_archive_key key 0x01");
    require(oa::hpi_archive_key(0x40) == 0xFE, "hpi_archive_key key 0x40");
    require(oa::hpi_archive_key(0xBF) == 0x01, "hpi_archive_key key 0xBF");
    require(oa::hpi_archive_key(0xFF) == 0, "hpi_archive_key header 0xFF transforms to a zero key");

    auto plaintext = with_trailer(one_byte_archive(0xFF, 'Q'));
    TemporaryFile plain_file(plaintext);
    oa::HpiArchive plain(plain_file.path());
    require(
        plain.read("a").value == std::vector<uint8_t>{'Q'},
        "header byte 0xFF must not decrypt the directory"
    );

    const auto key = oa::hpi_archive_key(0x01);
    auto encrypted = one_byte_archive(0x01, 'Q');
    encrypt_hpi_tail(encrypted, key);
    TemporaryFile encrypted_file(with_trailer(encrypted));
    oa::HpiArchive archive(encrypted_file.path());
    require(
        archive.read("a").value == std::vector<uint8_t>{'Q'},
        "hpi_archive_key key must undo position ^ ~byte ^ key"
    );
}

void test_hpi_all_compression_methods() {
    TemporaryFile file(make_archive());
    oa::HpiArchive archive(file.path());
    const auto entries = archive.entries();
    require(entries.size() == 4, "wrong HPI entry count");
    require(
        entries[0].path == "raw.bin" && entries[0].size == 3, "wrong first HPI metadata record"
    );
    require(
        archive.read("RAW.BIN").value == std::vector<uint8_t>({'r', 'a', 'w'}),
        "encrypted raw HPI extraction failed"
    );
    require(
        archive.read("z.bin").value ==
            std::vector<uint8_t>({'z', 'l', 'i', 'b', '-', 'd', 'a', 't', 'a'}),
        "zlib SQSH extraction failed"
    );
    require(
        archive.read("lz.bin").value == std::vector<uint8_t>({'L', 'Z', '7', '7'}),
        "LZ77 SQSH extraction failed"
    );
    // A stored (type 0) SQSH chunk never matches its unpacked size: fatal.
    const auto stored = archive.read("stored.bin");
    require(
        !stored.ok() && std::string_view(stored.error.message) == "SQUASHERR_BADUNPACKSIZE",
        "stored SQSH chunk was accepted"
    );
    require(
        archive.read("missing").error.code == oa::base::bytes::DecodeCode::not_found,
        "missing HPI path was accepted"
    );
}

void test_hpi_malformed_inputs() {
    auto wrong_version = make_archive(false);
    put32(wrong_version, 4, 0x00020000U);
    TemporaryFile version_file(wrong_version);
    require_throws(
        [&] { oa::HpiArchive archive(version_file.path()); }, "HPI v2 was accepted as v1"
    );

    std::vector<uint8_t> cycle(20 + 8 + 9 + 2);
    put32(cycle, 0, 0x49504148U);
    put32(cycle, 4, 0x00010000U);
    put32(cycle, 8, static_cast<uint32_t>(cycle.size()));
    put32(cycle, 16, 20);
    put32(cycle, 20, 1);
    put32(cycle, 24, 28);
    put32(cycle, 28, 37);
    put32(cycle, 32, 20);
    cycle[36] = 1;
    cycle[37] = 'd';
    TemporaryFile cycle_file(with_trailer(cycle));
    require_throws(
        [&] { oa::HpiArchive archive(cycle_file.path()); }, "cyclic HPI directory was accepted"
    );

    auto bad_checksum = make_archive(false);
    // Find the first SQSH signature and corrupt its checksum.
    const std::array<uint8_t, 4> marker{'S', 'Q', 'S', 'H'};
    const auto found =
        std::search(bad_checksum.begin(), bad_checksum.end(), marker.begin(), marker.end());
    require(found != bad_checksum.end(), "test SQSH marker missing");
    const std::size_t sqsh_offset = static_cast<std::size_t>(found - bad_checksum.begin());
    bad_checksum[sqsh_offset + 15] ^= 0x01;
    TemporaryFile checksum_file(bad_checksum);
    oa::HpiArchive corrupt(checksum_file.path());
    require(!corrupt.read("z.bin").ok(), "bad SQSH checksum was accepted");

    auto bad_zlib = make_archive(false);
    const auto zlib_marker =
        std::search(bad_zlib.begin(), bad_zlib.end(), marker.begin(), marker.end());
    require(zlib_marker != bad_zlib.end(), "test zlib SQSH marker missing");
    const std::size_t zlib_offset = static_cast<std::size_t>(zlib_marker - bad_zlib.begin());
    const std::size_t payload_offset = zlib_offset + 19;
    const std::size_t payload_size = get32(bad_zlib, zlib_offset + 7);
    bad_zlib[payload_offset] ^= 0xFF;
    put32(
        bad_zlib,
        zlib_offset + 15,
        checksum(std::span<const uint8_t>(bad_zlib).subspan(payload_offset, payload_size))
    );
    TemporaryFile zlib_file(bad_zlib);
    oa::HpiArchive invalid_zlib(zlib_file.path());
    // zlib 1.0.4 leaves the expected length in place on a failed inflate, so
    // the chunk passes its size check and decodes as zero bytes.
    require(
        invalid_zlib.read("z.bin").value == std::vector<uint8_t>(9, 0),
        "an undecodable zlib chunk with a valid checksum must read as its expected length"
    );

    auto oversized_chunk = make_archive(false);
    const auto oversized_marker =
        std::search(oversized_chunk.begin(), oversized_chunk.end(), marker.begin(), marker.end());
    require(oversized_marker != oversized_chunk.end(), "test oversized SQSH marker missing");
    const std::size_t oversized_offset =
        static_cast<std::size_t>(oversized_marker - oversized_chunk.begin());
    constexpr uint32_t oversized_stored_size = (1U << 20) + 1U;
    put32(oversized_chunk, oversized_offset - 4, oversized_stored_size);
    TemporaryFile sparse_file(oversized_chunk);
    sparse_file.resize(oversized_offset + oversized_stored_size);
    {
        std::ofstream tail(sparse_file.path(), std::ios::binary | std::ios::app);
        tail << archive_trailer;
    }
    oa::HpiArchive sparse(sparse_file.path());
    const auto oversized = sparse.read("z.bin");
    require(
        !oversized.ok() && std::string_view(oversized.error.message).find("1 MiB safety limit") !=
                               std::string_view::npos,
        "oversized stored chunk was not rejected before allocation"
    );
}

std::vector<uint8_t> make_indexed_pcx() {
    std::vector<uint8_t> pcx(128);
    pcx[0] = 0x0A;
    pcx[1] = 5;
    pcx[2] = 1;
    pcx[3] = 8;
    put16(pcx, 8, 2);  // width 3
    put16(pcx, 10, 1); // height 2
    pcx[65] = 1;
    put16(pcx, 66, 4); // one padding byte per row
    const std::array<uint8_t, 7> pixels{1, 2, 3, 0, 0xC3, 2, 0};
    pcx.insert(pcx.end(), pixels.begin(), pixels.end());
    pcx.push_back(0x0C);
    pcx.resize(pcx.size() + 768);
    const std::size_t palette = pcx.size() - 768;
    pcx[palette + 3] = 10;
    pcx[palette + 4] = 20;
    pcx[palette + 5] = 30;
    pcx[palette + 6] = 40;
    pcx[palette + 7] = 50;
    pcx[palette + 8] = 60;
    pcx[palette + 9] = 70;
    pcx[palette + 10] = 80;
    pcx[palette + 11] = 90;
    return pcx;
}

void test_pcx_indexed_and_malformed() {
    const auto pcx = make_indexed_pcx();
    const oa::Image image = oa::decode_pcx(pcx).value.value();
    require(image.width == 3 && image.height == 2, "wrong indexed PCX dimensions");
    require(
        image.rgb == std::vector<uint8_t>(
                         {10, 20, 30, 40, 50, 60, 70, 80, 90, 40, 50, 60, 40, 50, 60, 40, 50, 60}
                     ),
        "indexed PCX palette or RLE decode failed"
    );
    require(image.palette.has_value(), "indexed PCX must retain its palette");
    for (std::size_t color = 0; color < oa::palette_color_count; ++color) {
        for (std::size_t channel = 0; channel < 3; ++channel)
            require(
                (*image.palette)[color * oa::palette_entry_bytes + channel] ==
                    pcx[pcx.size() - 768 + color * 3 + channel],
                "PCX palette expansion mismatch"
            );
        require(
            (*image.palette)[color * oa::palette_entry_bytes + 3] == 0,
            "PCX palette reserved byte must be zero"
        );
    }

    auto underflow = pcx;
    put16(underflow, 4, 5);
    put16(underflow, 8, 2);
    require(!oa::decode_pcx(underflow).ok(), "PCX coordinate underflow was accepted");

    auto missing_palette = pcx;
    missing_palette[missing_palette.size() - 769] = 0;
    require(!oa::decode_pcx(missing_palette).ok(), "palette-less indexed PCX was accepted");

    auto crossing_run = pcx;
    crossing_run[128] = 0xC5;
    const auto crossed = oa::decode_pcx(crossing_run);
    require(
        !crossed.ok() && crossed.error.code == oa::base::bytes::DecodeCode::malformed &&
            crossed.error.offset == 128,
        "PCX RLE run crossing a scanline was accepted"
    );

    // Each way the data can end early or be wrong, with its offset.
    const auto fails_at =
        [](const std::vector<uint8_t>& bytes, oa::base::bytes::DecodeCode code, uint64_t offset) {
            const auto decoded = oa::decode_pcx(bytes);
            return !decoded.ok() && decoded.error.code == code && decoded.error.offset == offset;
        };
    require(
        fails_at(
            std::vector<uint8_t>(pcx.begin(), pcx.begin() + 127),
            oa::base::bytes::DecodeCode::truncated,
            127
        ),
        "a short PCX header was accepted"
    );
    auto manufacturer = pcx;
    manufacturer[0] = 0x0B;
    require(
        fails_at(manufacturer, oa::base::bytes::DecodeCode::bad_signature, 0),
        "a bad PCX manufacturer byte was accepted"
    );
    auto version = pcx;
    version[1] = 1;
    require(
        fails_at(version, oa::base::bytes::DecodeCode::unsupported_version, 1),
        "an unsupported PCX version was accepted"
    );
    auto cut = pcx;
    cut.erase(cut.begin() + 129, cut.end() - 769);
    require(
        fails_at(cut, oa::base::bytes::DecodeCode::truncated, 129) ||
            fails_at(cut, oa::base::bytes::DecodeCode::malformed, 128),
        "PCX pixel data that ends early was accepted"
    );
    auto huge = pcx;
    put16(huge, 8, 0xFFFE);
    put16(huge, 10, 0xFFFF);
    put16(huge, 66, 0xFFFF);
    require(
        fails_at(huge, oa::base::bytes::DecodeCode::limit_exceeded, 4),
        "a PCX over the pixel limit was accepted"
    );
}

void test_pcx_planar_rgb() {
    std::vector<uint8_t> pcx(128);
    pcx[0] = 0x0A;
    pcx[1] = 5;
    pcx[2] = 0;
    pcx[3] = 8;
    put16(pcx, 8, 1); // 2x1
    pcx[65] = 3;
    put16(pcx, 66, 2);
    pcx.insert(pcx.end(), {1, 2, 3, 4, 5, 6});
    const auto image = oa::decode_pcx(pcx).value.value();
    require(image.rgb == std::vector<uint8_t>({1, 3, 5, 2, 4, 6}), "planar RGB PCX decode failed");
    require(!image.palette.has_value(), "true-color PCX must not invent a palette");
}

} // namespace

int main() {
    try {
        test_hpi_archive_key();
        test_hpi_all_compression_methods();
        test_asset_store_precedence();
        test_palette_mapping();
        test_hpi_malformed_inputs();
        test_pcx_indexed_and_malformed();
        test_pcx_planar_rgb();
        std::cout << "asset tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "asset tests failed: " << error.what() << '\n';
        return 1;
    }
}
