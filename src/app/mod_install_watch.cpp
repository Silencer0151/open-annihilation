// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The watch on SDL's events that copies each .oamod file the system opens in
// the game into the mod packages' inbox.

#include "mod_install_watch.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/app/mod_install.hpp"
#include "oa/app/mod_install/inbox.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <exception>
#include <iostream>

namespace oa::app {

namespace {

/// The queued drop events taken at once when the watch starts.
constexpr int kQueuedDrops = 16;

/// Copies a dropped .oamod file into the inbox; any other file is logged
/// and left.
///
/// @param event the event
void take_drop(const SDL_Event& event) {
    if (event.type != SDL_EVENT_DROP_FILE || event.drop.data == nullptr)
        return;
    try {
        const fs::path file = path_from_utf8(event.drop.data);
        if (mod_install::names_mod_package(file)) {
            mod_install::post_mod_file(file);
            return;
        }
        std::cerr << "open-annihilation: a file opened in the game is no mod package and is left: "
                  << event.drop.data << '\n';
    } catch (const std::exception& failure) {
        std::cerr << "open-annihilation: a file opened in the game is lost: " << failure.what()
                  << '\n';
    }
}

/// SDL's event watch: copies each dropped package as it is queued.
///
/// @param event the event
/// @return true, which SDL ignores for a watch
bool SDLCALL take_dropped_file(void*, SDL_Event* event) {
    if (event != nullptr)
        take_drop(*event);
    return true;
}

} // namespace

void watch_opened_files() noexcept {
    SDL_RemoveEventWatch(take_dropped_file, nullptr);
    if (!SDL_AddEventWatch(take_dropped_file, nullptr))
        std::cerr << "open-annihilation: files opened in the game cannot be watched: "
                  << SDL_GetError() << '\n';
    // The drops already queued, which the watch did not see.
    std::array<SDL_Event, kQueuedDrops> queued{};
    const int count = SDL_PeepEvents(
        queued.data(), kQueuedDrops, SDL_PEEKEVENT, SDL_EVENT_DROP_FILE, SDL_EVENT_DROP_FILE
    );
    for (int index = 0; index < count; ++index)
        take_drop(queued[static_cast<std::size_t>(index)]);
}

} // namespace oa::app
