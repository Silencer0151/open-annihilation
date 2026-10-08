// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Opening a web address for an extension: only http and https are handed to
// an opener, and a Steam Deck in Game Mode has no browser to hand one to.
#include "web_address.hpp"

#include "oa/app/extension.hpp"
#include "oa/platform/machine.hpp"

#include <SDL3/SDL.h>

#include <string_view>

namespace oa::app {
namespace {

/// The https scheme and its separator, in lower case.
constexpr std::string_view https_scheme = "https://";
/// The http scheme and its separator, in lower case.
constexpr std::string_view http_scheme = "http://";
/// The first byte that is not an ASCII control.
constexpr unsigned char first_printable = 0x20;
/// The ASCII delete control.
constexpr unsigned char delete_control = 0x7f;

/// Tells whether `text` begins with `scheme`, ignoring the case of letters.
///
/// @param text the address
/// @param scheme the scheme and its separator, in lower case
/// @return true when `text` begins with `scheme`
bool starts_with_scheme(std::string_view text, std::string_view scheme) noexcept {
    if (text.size() < scheme.size())
        return false;
    for (std::size_t index = 0; index < scheme.size(); ++index) {
        unsigned char byte = static_cast<unsigned char>(text[index]);
        if (byte >= 'A' && byte <= 'Z')
            byte = static_cast<unsigned char>(byte - 'A' + 'a');
        if (byte != static_cast<unsigned char>(scheme[index]))
            return false;
    }
    return true;
}

/// Tells whether `address` is an http or https address the opener may see.
///
/// The scheme's letters may be either case. The body after "://" must be
/// non-empty and hold no ASCII control and no space.
///
/// @param address the address; null is refused
/// @return true when it may be handed to the opener
bool acceptable_web_address(const char* address) noexcept {
    if (address == nullptr)
        return false;
    const std::string_view text(address);
    std::string_view body;
    if (starts_with_scheme(text, https_scheme))
        body = text.substr(https_scheme.size());
    else if (starts_with_scheme(text, http_scheme))
        body = text.substr(http_scheme.size());
    else
        return false;
    if (body.empty())
        return false;
    for (const char byte : body) {
        const auto value = static_cast<unsigned char>(byte);
        if (value < first_printable || value == delete_control || value == ' ')
            return false;
    }
    return true;
}

/// Opens `address` in the system's browser.
///
/// @param address the address
/// @return false when the browser could not be opened
bool open_with_system_browser(void*, const char* address) {
    return SDL_OpenURL(address);
}

/// Returns the opener that hands an address to the system's browser.
///
/// @return the opener; it needs no context
WebAddressOpener system_web_address_opener() noexcept {
    WebAddressOpener opener{};
    opener.open = open_with_system_browser;
    return opener;
}

} // namespace

bool web_address_available() noexcept {
    return oa::platform::web_address_available();
}

bool open_web_address_with(const char* address, const WebAddressOpener& opener) {
    if (!acceptable_web_address(address) || opener.open == nullptr)
        return false;
    return opener.open(opener.context, address);
}

bool open_web_address(const char* address) {
    if (!web_address_available())
        return false;
    return open_web_address_with(address, system_web_address_opener());
}

} // namespace oa::app
