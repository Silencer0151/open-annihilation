// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The browser and recording implementations of the web link seam.
#include "oa/app/web_link.hpp"

#include <SDL3/SDL.h>

namespace oa::app {

std::string web_link_caption(std::string_view address) {
    for (const std::string_view scheme : {"https://", "http://"})
        if (address.starts_with(scheme)) {
            address.remove_prefix(scheme.size());
            break;
        }
    if (address.ends_with('/'))
        address.remove_suffix(1);
    return "Go to " + std::string(address);
}

WebLinkHooks browser_web_links() noexcept {
    WebLinkHooks hooks{};
    hooks.open = [](void*, const char* address) { return SDL_OpenURL(address); };
    return hooks;
}

WebLinkHooks recorded_web_links(std::vector<std::string>& requests) noexcept {
    WebLinkHooks hooks{};
    hooks.context = &requests;
    hooks.open = [](void* context, const char* address) {
        static_cast<std::vector<std::string>*>(context)->emplace_back(address);
        return true;
    };
    return hooks;
}

} // namespace oa::app
