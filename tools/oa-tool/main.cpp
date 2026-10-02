// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/hpi.hpp"
#include "oa/formats/png.hpp"
#include "oa/ui/decoded.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (size > 256 * 1024 * 1024)
        throw std::runtime_error("input exceeds 256 MiB limit");
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
        throw std::runtime_error("cannot read input: " + path.string());
    return bytes;
}

void write_file(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out.write(
            reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
        ))
        throw std::runtime_error("cannot write output: " + path.string());
    out.close();
    if (!out)
        throw std::runtime_error("cannot finish output: " + path.string());
}

void usage() {
    std::cerr << "open-annihilation compatibility workbench\n"
                 "  oa-tool list ARCHIVE\n"
                 "  oa-tool extract ARCHIVE ENTRY OUTPUT\n"
                 "  oa-tool asset-extract ROOT ENTRY OUTPUT [ARCHIVE...]\n"
                 "  oa-tool preview ARCHIVE PCX_ENTRY OUTPUT.ppm|OUTPUT.png\n"
                 "  oa-tool decode-pcx INPUT.pcx OUTPUT.ppm|OUTPUT.png\n";
}

// Writes a PNG when the output name ends in .png, otherwise a binary PPM.
void write_image(const std::filesystem::path& output, const oa::Image& image) {
    if (output.extension() == ".png") {
        std::vector<uint8_t> png;
        const oa::formats::png::Header header{
            image.width, image.height, 8, oa::formats::png::ColorType::rgb
        };
        if (!oa::formats::png::write(oa::formats::png::Image{header, {}, image.rgb}, &png))
            throw std::runtime_error("cannot encode image: " + output.string());
        write_file(output, png);
    } else {
        std::ofstream out(output, std::ios::binary);
        out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
        out.write(
            reinterpret_cast<const char*>(image.rgb.data()),
            static_cast<std::streamsize>(image.rgb.size())
        );
        out.close();
        if (!out)
            throw std::runtime_error("cannot write image: " + output.string());
    }
    std::cout << image.width << 'x' << image.height << " RGB -> " << output.string() << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            usage();
            return 0;
        }
        if (argc < 3) {
            usage();
            return 2;
        }
        const std::string command = argv[1];
        if (command == "list" && argc == 3) {
            for (const auto& entry : oa::HpiArchive(argv[2]).entries())
                std::cout << entry.path << '\t' << entry.size << '\n';
        } else if (command == "asset-extract" && argc >= 5) {
            oa::AssetStore store(argv[2]);
            for (int index = 5; index < argc; ++index)
                store.mount(argv[index]);
            const auto asset = store.read(argv[3]);
            write_file(argv[4], asset.bytes);
            std::cout << (asset.archived ? "archive: " : "loose: ") << asset.source.string() << " ("
                      << asset.bytes.size() << " bytes)\n";
        } else if ((command == "extract" || command == "preview") && argc == 5) {
            const auto data =
                oa::ui::decoded::require(oa::HpiArchive(argv[2]).read(argv[3]), argv[3]);
            if (command == "extract")
                write_file(argv[4], data);
            else
                write_image(argv[4], oa::ui::decoded::require(oa::decode_pcx(data), argv[3]));
        } else if (command == "decode-pcx" && argc == 4) {
            write_image(
                argv[3], oa::ui::decoded::require(oa::decode_pcx(read_file(argv[2])), argv[2])
            );
        } else {
            usage();
            return 2;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "oa-tool: " << e.what() << '\n';
        return 1;
    }
}
