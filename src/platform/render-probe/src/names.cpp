// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The probe's pure tables: the names of software and virtual rasterisers,
// the drivers that report a fixed texture limit and the video drivers whose
// windows have a framebuffer of their own. Built on every system.
#include "oa/platform/render_probe.hpp"

#include <algorithm>
#include <array>

namespace oa::platform::render_probe {
namespace {

/// The Microsoft Basic Render Driver's PCI vendor identifier.
constexpr uint32_t kBasicRenderVendor = 0x1414;
/// The Microsoft Basic Render Driver's PCI device identifier.
constexpr uint32_t kBasicRenderDevice = 0x008C;

/// Names, in lower case, of rasterisers that draw on the processor. Mesa's
/// drivers layered over one (zink, virgl, its Direct3D 12 driver over WARP)
/// carry the rasteriser's name in their own.
constexpr std::array<std::string_view, 9> kProcessorRasterisers{
    "llvmpipe",
    "softpipe",
    "lavapipe",
    "swrast",
    "software rasterizer",
    "swiftshader",
    "gdi generic",
    "apple software renderer",
    "microsoft basic render driver",
};

/// Names, in lower case, of virtual machines' adapters. chromium is
/// VirtualBox's older OpenGL pass-through, up to VirtualBox 6.0.
constexpr std::array<std::string_view, 4> kVirtualAdapters{
    "virtualbox",
    "vmware",
    "svga3d",
    "chromium",
};

/// The OpenGL vendor string, in lower case, of VirtualBox's older OpenGL
/// pass-through.
constexpr std::string_view kVirtualVendor = "humper";

/// SDL's names, in lower case, of the video drivers whose windows have a
/// framebuffer of their own.
constexpr std::array<std::string_view, 4> window_framebuffer_video_drivers{
    "windows",
    "x11",
    "dummy",
    "offscreen",
};

/// The top two bits of a UTF-8 byte, which tell a continuation byte.
constexpr unsigned kUtf8LeadMask = 0xC0;
/// The top two bits of a UTF-8 continuation byte.
constexpr unsigned kUtf8Continuation = 0x80;
/// The delete character, cleaned like a control character.
constexpr char kDeleteCharacter = '\x7f';

/// Returns an ASCII letter in lower case; other characters as they are.
///
/// @param character the character
/// @return its lower case
char lower_ascii(char character) noexcept {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a')
                                                : character;
}

/// Says whether a text holds a lower-case word in any letter case.
///
/// @param text the text searched
/// @param word the word, in lower case
/// @return true where the text holds it
bool contains_word(std::string_view text, std::string_view word) noexcept {
    if (word.empty())
        return true;
    if (text.size() < word.size())
        return false;
    for (std::size_t start = 0; start + word.size() <= text.size(); ++start) {
        std::size_t matched = 0;
        while (matched < word.size() && lower_ascii(text[start + matched]) == word[matched])
            ++matched;
        if (matched == word.size())
            return true;
    }
    return false;
}

/// Says whether a text holds any of a list's words in any letter case.
///
/// @param text the text searched
/// @param words the words, in lower case
/// @return true where the text holds one
template <std::size_t Count>
bool contains_any(
    std::string_view text, const std::array<std::string_view, Count>& words
) noexcept {
    return std::any_of(words.begin(), words.end(), [text](std::string_view word) {
        return contains_word(text, word);
    });
}

/// Says whether an adapter's name or identifiers mark a rasteriser that
/// draws on the processor.
///
/// @param name the adapter's name
/// @param vendor its PCI vendor identifier
/// @param device its PCI device identifier
/// @return true for such a rasteriser
bool names_processor_rasteriser(std::string_view name, uint32_t vendor, uint32_t device) noexcept {
    return contains_any(name, kProcessorRasterisers) ||
           (vendor == kBasicRenderVendor && device == kBasicRenderDevice);
}

/// Says whether a name equals a lower-case name in any letter case.
///
/// @param name the name
/// @param lower the other name, in lower case
/// @return true where they are the same name
bool same_name(std::string_view name, std::string_view lower) noexcept {
    return name.size() == lower.size() &&
           std::equal(name.begin(), name.end(), lower.begin(), [](char left, char right) {
               return lower_ascii(left) == right;
           });
}

} // namespace

bool names_software_rasteriser(std::string_view name, uint32_t vendor, uint32_t device) noexcept {
    return names_processor_rasteriser(name, vendor, device) || names_virtual_adapter(name);
}

bool names_virtual_adapter(std::string_view name) noexcept {
    return contains_any(name, kVirtualAdapters);
}

bool vendor_names_virtual_adapter(std::string_view vendor) noexcept {
    return contains_word(vendor, kVirtualVendor);
}

bool reports_fixed_texture_limit(std::string_view renderer) noexcept {
    return renderer == vulkan_renderer || renderer == gpu_renderer;
}

void classify(AdapterFacts& facts) noexcept {
    facts.software_rasteriser =
        facts.renderer == software_renderer || facts.software_flag ||
        names_processor_rasteriser(facts.adapter, facts.vendor_id, facts.device_id);
    facts.virtual_adapter =
        names_virtual_adapter(facts.adapter) || vendor_names_virtual_adapter(facts.vendor);
}

bool software_or_virtual(const AdapterFacts& facts) noexcept {
    return facts.software_rasteriser || facts.virtual_adapter || facts.wine;
}

std::string clean_name(std::string_view name) {
    // A cut never splits a UTF-8 sequence: it backs off to the start of the
    // character the limit falls in.
    std::size_t length = std::min(name.size(), adapter_name_limit);
    while (length > 0 && length < name.size() &&
           (static_cast<unsigned char>(name[length]) & kUtf8LeadMask) == kUtf8Continuation)
        --length;
    std::string cleaned(name.substr(0, length));
    for (char& character : cleaned)
        if (static_cast<unsigned char>(character) < ' ' || character == kDeleteCharacter)
            character = ' ';
    const auto first = cleaned.find_first_not_of(' ');
    if (first == std::string::npos)
        return {};
    const auto last = cleaned.find_last_not_of(' ');
    return cleaned.substr(first, last - first + 1);
}

bool native_window_framebuffer(std::string_view video_driver) noexcept {
    return std::any_of(
        window_framebuffer_video_drivers.begin(),
        window_framebuffer_video_drivers.end(),
        [video_driver](std::string_view driver) { return same_name(video_driver, driver); }
    );
}

DeviceState device_state_from_result(int32_t result) noexcept {
    if (result == 0)
        return DeviceState::ok;
    if (result == device_lost_result)
        return DeviceState::lost;
    if (result == device_not_reset_result)
        return DeviceState::not_reset;
    return DeviceState::unknown;
}

} // namespace oa::platform::render_probe
