// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The names a package may unpack on every system, the folder-safe form of a
// version, and the installer's text helpers.

#include "files.hpp"

#include "oa/app/mod_install.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>

namespace oa::app::mod_install {

namespace {

/// The longest part of a path most file systems keep, in bytes.
constexpr std::size_t longest_part_bytes = 255;
/// The longest folder-safe version, in bytes.
constexpr std::size_t longest_version_part = 32;
/// The characters Windows refuses in a name.
constexpr std::string_view refused_characters = "<>:\"|?*\\";
/// The names Windows keeps for devices, lowered, without the numbered ones.
constexpr std::array<std::string_view, 6> device_names{
    "con", "prn", "aux", "nul", "conin$", "conout$"
};
/// The superscript digits 1, 2 and 3 in UTF-8, which Windows takes as
/// device numbers after COM and LPT.
constexpr std::array<std::string_view, 3> superscript_digits{"\xc2\xb9", "\xc2\xb2", "\xc2\xb3"};

/// Tells whether bytes are well-formed UTF-8.
///
/// @param text the bytes
/// @return true when they are
bool valid_utf8(std::string_view text) noexcept {
    std::size_t at = 0;
    while (at < text.size()) {
        const auto lead = static_cast<unsigned char>(text[at]);
        std::size_t length = 1;
        uint32_t code = lead;
        uint32_t least = 0;
        if (lead >= 0x80U) {
            if ((lead & 0xE0U) == 0xC0U) {
                length = 2;
                code = lead & 0x1FU;
                least = 0x80;
            } else if ((lead & 0xF0U) == 0xE0U) {
                length = 3;
                code = lead & 0x0FU;
                least = 0x800;
            } else if ((lead & 0xF8U) == 0xF0U) {
                length = 4;
                code = lead & 0x07U;
                least = 0x10000;
            } else {
                return false;
            }
            if (text.size() - at < length)
                return false;
            for (std::size_t index = 1; index < length; ++index) {
                const auto next = static_cast<unsigned char>(text[at + index]);
                if ((next & 0xC0U) != 0x80U)
                    return false;
                code = (code << 6U) | (next & 0x3FU);
            }
            if (code < least || code > 0x10FFFFU || (code >= 0xD800U && code <= 0xDFFFU))
                return false;
        }
        at += length;
    }
    return true;
}

/// Says why one part of a path cannot be unpacked everywhere.
///
/// @param part the part
/// @return the reason; empty when it can
std::string part_problem(std::string_view part) {
    const std::string shown = "\"" + std::string(part) + "\"";
    if (part.empty())
        return "a path has an empty part";
    if (part.size() > longest_part_bytes)
        return shown + " is longer than 255 bytes";
    if (!valid_utf8(part))
        return shown + " is not UTF-8";
    for (const char character : part) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte == 0x7FU)
            return shown + " holds a control character";
        if (refused_characters.find(character) != std::string_view::npos)
            return shown + " holds a character Windows refuses in a name";
    }
    if (part.back() == '.' || part.back() == ' ')
        return shown + " ends in a dot or a space, which Windows drops";
    std::string_view stem = part.substr(0, part.find('.'));
    while (!stem.empty() && stem.back() == ' ')
        stem.remove_suffix(1);
    if (windows_device_name(stem))
        return shown + " is a name Windows keeps for a device";
    return {};
}

} // namespace

namespace detail {

std::string folded(std::string_view text) {
    std::string lowered(text);
    for (char& character : lowered)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    return lowered;
}

std::string utf8_of(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::filesystem::path path_of(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

void log_line(std::string_view line) {
    std::cerr << "open-annihilation: mod install: " << line << '\n';
}

} // namespace detail

bool windows_device_name(std::string_view name) noexcept {
    const std::string lowered = detail::folded(name);
    for (const std::string_view device : device_names)
        if (lowered == device)
            return true;
    if (lowered.size() < 4 || (!lowered.starts_with("com") && !lowered.starts_with("lpt")))
        return false;
    const std::string_view number = std::string_view(lowered).substr(3);
    if (number.size() == 1 && number[0] >= '0' && number[0] <= '9')
        return true;
    for (const std::string_view digit : superscript_digits)
        if (number == digit)
            return true;
    return false;
}

std::string portable_name_problem(std::string_view relative_name) {
    std::string_view rest = relative_name;
    if (!rest.empty() && rest.back() == '/')
        rest.remove_suffix(1);
    if (rest.empty())
        return "the path is empty";
    while (true) {
        const std::size_t separator = rest.find('/');
        const std::string problem = part_problem(rest.substr(0, separator));
        if (!problem.empty())
            return problem;
        if (separator == std::string_view::npos)
            return {};
        rest.remove_prefix(separator + 1);
    }
}

std::string version_folder_part(std::string_view version) {
    std::string part;
    for (const char character : version) {
        char kept = '-';
        if (character >= 'A' && character <= 'Z')
            kept = static_cast<char>(character - 'A' + 'a');
        else if (
            (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
            character == '.' || character == '_'
        )
            kept = character;
        if (kept == '-' && !part.empty() && part.back() == '-')
            continue;
        part += kept;
    }
    const auto trim = [](std::string& text) {
        while (!text.empty() && (text.front() == '.' || text.front() == '-'))
            text.erase(text.begin());
        while (!text.empty() && (text.back() == '.' || text.back() == '-'))
            text.pop_back();
    };
    trim(part);
    if (part.size() > longest_version_part) {
        part.resize(longest_version_part);
        trim(part);
    }
    return part.empty() ? std::string("version") : part;
}

bool names_mod_package(const std::filesystem::path& file) {
    const std::string name = detail::folded(detail::utf8_of(file.filename()));
    return name.size() > package_extension.size() && name.ends_with(package_extension);
}

} // namespace oa::app::mod_install
