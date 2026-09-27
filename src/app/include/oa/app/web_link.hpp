// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Opening a web address in the player's browser, behind a seam that runs
// nobody watches replace with a record of the requests.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace oa::app {

/// The address the DEMOMSG.GUI notice's website button opens: the project's site.
inline constexpr std::string_view project_website_address = "https://www.coreprime.net";

/// Returns the caption of a button that opens a web address: "Go to " and the address without
/// its scheme or a closing slash.
///
/// @param address the address the button opens, such as project_website_address
/// @return the caption
[[nodiscard]] std::string web_link_caption(std::string_view address);

/// Opens web addresses for the frontend.
struct WebLinkHooks {
    void* context{};
    /// Opens `address` in the player's browser; null opens nothing.
    ///
    /// Returns false when the address could not be opened.
    bool (*open)(void* context, const char* address){};
};

/// Returns hooks that open an address in the player's browser through SDL.
///
/// @return the hooks; they need no context
[[nodiscard]] WebLinkHooks browser_web_links() noexcept;

/// Returns hooks that open nothing and append each address to a list instead.
///
/// @param[in,out] requests receives every address asked for, in order; it must outlive the hooks
/// @return the hooks
[[nodiscard]] WebLinkHooks recorded_web_links(std::vector<std::string>& requests) noexcept;

} // namespace oa::app
