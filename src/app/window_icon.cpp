// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Decoding the window icon's PNG file with the engine's PNG reader.
#include "oa/app/window_icon.hpp"

#include "oa/formats/png.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace oa::app {
namespace {

// Bits per sample of the icon's pixels.
constexpr uint8_t icon_bit_depth = 8;

/// Keeps the first error the PNG reader reports; warnings are left out.
///
/// @param user the std::string that keeps it
/// @param text the error
void keep_first_error(void* user, const char* text) {
    auto& error = *static_cast<std::string*>(user);
    if (error.empty())
        error = text != nullptr && *text != '\0' ? text : "unreadable PNG";
}

/// Ignores a warning of the PNG reader.
void ignore_warning(void*, const char*) {
}

} // namespace

bool decode_window_icon(std::span<const uint8_t> png, WindowIcon& icon, std::string& error) {
    namespace png_format = oa::formats::png;
    icon = WindowIcon{};
    error.clear();
    if (!png_format::has_signature(png)) {
        error = "not a PNG file";
        return false;
    }
    const png_format::Messages messages{&error, ignore_warning, keep_first_error};
    png_format::Info info;
    if (!png_format::read_info(png, messages, &info))
        return false;
    const png_format::Header& header = info.header;
    if (header.color_type != png_format::ColorType::rgb_alpha ||
        header.bit_depth != icon_bit_depth) {
        error = "not an 8-bit RGBA image";
        return false;
    }
    if (header.width != window_icon_size || header.height != window_icon_size) {
        error = "not " + std::to_string(window_icon_size) + " by " +
                std::to_string(window_icon_size) + " pixels";
        return false;
    }
    const std::size_t row = png_format::row_bytes(header, {});
    std::vector<uint8_t> pixels(row * header.height);
    const png_format::Progress progress = png_format::read_image(png, info, {}, messages, pixels);
    if (progress == png_format::Progress::none || !error.empty()) {
        if (error.empty())
            error = "the image data ends early";
        return false;
    }
    icon.width = header.width;
    icon.height = header.height;
    icon.pixels = std::move(pixels);
    return true;
}

} // namespace oa::app
