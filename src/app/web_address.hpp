// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The engine's own way to open one web address: the scheme rules, then an
// opener the caller gives. An extension does not see this. It calls
// web_address_available and open_web_address(const char*) in extension.hpp,
// and those hand an accepted address to the system's browser.
#pragma once

namespace oa::app {

/// Opens one web address for the engine.
struct WebAddressOpener {
    void* context{};
    /// Opens `address` and returns false when it could not be opened.
    /// `context` is WebAddressOpener::context. Null opens nothing. It does
    /// not throw.
    bool (*open)(void* context, const char* address){};
};

/// Opens an http or https address through `opener`.
///
/// Accepts an address whose scheme is http or https, in either letter case,
/// with a body after "://", and refuses a null or empty address, any other
/// scheme, a missing scheme and a body that holds an ASCII control or a
/// space. A refused address is not handed to the opener. The address that
/// is handed over is the one given, unchanged. This does not ask whether
/// the machine can open one: a test passes a stub opener, and
/// open_web_address(const char*) calls it after web_address_available.
///
/// @param address the address, read for this call only; null is refused
/// @param opener the opener; a null open function refuses the address
/// @return true when the opener opened the address
[[nodiscard]] bool open_web_address_with(const char* address, const WebAddressOpener& opener);

} // namespace oa::app
