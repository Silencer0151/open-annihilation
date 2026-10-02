// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The handler a request to end the program runs (close_handlers.hpp).
#include "close_handlers.hpp"

#include "oa/ui/frontend_state/dispatcher.hpp"
#include "oa/ui/frontend_multiplayer/connect.hpp"

namespace oa::app {

const char* leave_reason(const oa::Game& game) noexcept {
    if (game.local_player_index >= OA_PLAYER_COUNT)
        return nullptr;
    const uint8_t reason = game.players[game.local_player_index].reject_reason;
    return reason != 0 ? oa::ui::frontend_multiplayer::reject_reason_text(reason) : nullptr;
}

CloseHandler app_mode_close_handler(int32_t mode) noexcept {
    return mode == oa::ui::frontend_state::mode_id::in_match ? CloseHandler::exit_confirm
                                                             : CloseHandler::leave;
}

void close_handlers_observe(CloseHandlers& handlers, const CloseObservation& now) noexcept {
    if (now.match_runs)
        handlers.in_match = true;
    const int32_t mode =
        handlers.in_match ? oa::ui::frontend_state::mode_id::in_match : now.frontend_mode;
    if (mode != handlers.mode) {
        handlers.mode = mode;
        handlers.installed = app_mode_close_handler(mode);
    }
}

void close_handlers_mode_set(
    CloseHandlers& handlers, int32_t mode, const CloseObservation& now
) noexcept {
    close_handlers_observe(handlers, now);
    if (!handlers.in_match)
        handlers.mode = mode;
    handlers.installed = app_mode_close_handler(mode);
}

void close_handlers_match_ended(CloseHandlers& handlers) noexcept {
    handlers.in_match = false;
}

} // namespace oa::app
