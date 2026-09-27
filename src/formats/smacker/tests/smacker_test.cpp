// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/media/intro_player.hpp"
#include "oa/formats/smacker.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void put32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset + shift / 8] = static_cast<uint8_t>(value >> shift);
}

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::filesystem::path write_fixture() {
    std::vector<uint8_t> bytes(125, 0);
    put32(bytes, 0, oa::formats::smacker::kSmk2);
    put32(bytes, 4, 4);
    put32(bytes, 8, 2);
    put32(bytes, 12, 2);
    put32(bytes, 16, static_cast<uint32_t>(-3333));
    put32(bytes, 24, 9);
    put32(bytes, 52, 3);
    put32(bytes, 72, 0xD0005622U);
    put32(bytes, 104, 5);
    put32(bytes, 108, 3);
    bytes[112] = 2;
    bytes[113] = 0;
    bytes[114] = 0xAA;
    bytes[115] = 0xBB;
    bytes[116] = 0xCC;
    bytes[117] = 1;
    bytes[118] = 2;
    bytes[119] = 3;
    bytes[120] = 0;
    const auto path = std::filesystem::temp_directory_path() / "oa-intro-smacker-test.zrb";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
    );
    return path;
}

void test_fixture() {
    const auto path = write_fixture();
    const auto opened = oa::formats::smacker::SmackerReader::open(path);
    require(static_cast<bool>(opened), "fixture did not open");
    const auto& reader = *opened.reader;
    require(reader.header().width == 4 && reader.header().height == 2, "fixture geometry");
    require(
        reader.header().frame_count == 2 && reader.header().frame_rate_hz() > 29.9 &&
            reader.header().frame_rate_hz() < 30.1,
        "fixture timing"
    );
    require(
        reader.header().has_audio() && reader.header().audio[0].sample_rate() == 22050,
        "fixture audio metadata"
    );
    require(
        reader.frame_data_offset() == 117 && reader.frame_data_bytes() == 8, "fixture frame bounds"
    );
    const auto frame = reader.frame(1);
    require(
        frame && frame->file_offset == 122 && frame->compressed_size == 3 && frame->type == 0,
        "fixture frame index"
    );
    std::vector<uint8_t> payload;
    std::string error;
    require(
        reader.read_frame(0, payload, error) && payload == std::vector<uint8_t>({1, 2, 3, 0, 0}),
        "fixture frame payload"
    );
    std::vector<uint8_t> trees;
    require(
        reader.read_huffman_trees(trees, error) && trees.size() == 3 && trees[0] == 0xAA,
        "fixture trees"
    );
    std::filesystem::remove(path);
}

void test_letterbox() {
    const auto exact = oa::media::letterbox_dest(640, 480, 1280, 960);
    require(exact.w == 1280 && exact.h == 960 && exact.x == 0 && exact.y == 0, "exact 2x window");
    const auto hd = oa::media::letterbox_dest(640, 480, 1920, 1080);
    require(
        hd.w == 1440 && hd.h == 1080 && hd.x == 240 && hd.y == 0,
        "1080p fills height at 4:3, not an integer-shrunk crop"
    );
    const auto four_k = oa::media::letterbox_dest(640, 480, 3840, 2160);
    require(
        four_k.w == 2880 && four_k.h == 2160 && four_k.x == 480 && four_k.y == 0,
        "4K fills height at 4:3"
    );
    const auto tiny = oa::media::letterbox_dest(640, 480, 320, 240);
    require(
        tiny.w == 320 && tiny.h == 240 && tiny.x == 0 && tiny.y == 0,
        "window smaller than canvas still fills"
    );
}
} // namespace

int main() {
    try {
        test_fixture();
        test_letterbox();
        std::cout << "Smacker container tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
