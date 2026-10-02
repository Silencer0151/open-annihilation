// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The lines posted to a battle a launch started (battle_lines.hpp), and
// network play's API entries that post them (oa/app/netgame/extension_api.hpp).
#include "battle_lines.hpp"

#include "oa/app/netgame/extension_api.hpp"
#include "oa/ui/frontend_multiplayer/screens.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace oa::app {

namespace {

namespace mp = oa::ui::frontend_multiplayer;

// A notice's parts: at most 63 characters, broken at a blank within the
// last 12 characters of a part when there is one.
constexpr std::size_t kNoticePartLength = 0x3f;
constexpr std::size_t kNoticeLookback = 0xc;
// Game.gui_flags bit a posted line raises.
constexpr uint8_t kGuiFlagMessage = 0x01;

// Where post_battle_line posts while a match runs (bind_battle_lines).
struct MatchPoster {
    void* context{};
    bool (*post_to_match)(void* context, const char* line){};
};

MatchPoster g_match_poster{};

/// Posts one part of a line into the frontend Game's chat ring (the battle room's chat).
///
/// @param context unused
/// @param part the part
void post_to_chat_ring(void*, const char* part) noexcept {
    mp::Lobby& lobby = mp::multiplayer_lobby();
    if (lobby.game != nullptr)
        mp::lobby_post_chat(lobby, part);
}

} // namespace

void post_wrapped_line(
    const char* line, void (*post)(void* context, const char* part), void* context, oa::Game& game
) noexcept {
    if (line == nullptr)
        return;
    if (post == nullptr)
        post = post_to_chat_ring;
    const char* text = line;
    while (*text == ' ')
        ++text;
    std::size_t remaining = std::strlen(text);
    char part[kNoticePartLength + 1];
    while (remaining > kNoticePartLength) {
        std::size_t length = kNoticePartLength;
        if (*text != ' ') {
            for (std::size_t step = 0; step < kNoticeLookback; ++step) {
                if (text[kNoticePartLength - 1 - step] == ' ')
                    break;
                --length;
            }
        }
        std::memcpy(part, text, length);
        part[length] = '\0';
        post(context, part);
        text += length;
        remaining -= length;
        while (remaining > 0 && *text == ' ') {
            ++text;
            --remaining;
        }
    }
    if (remaining > 0)
        post(context, text);
    game.gui_flags = static_cast<uint8_t>(game.gui_flags | kGuiFlagMessage);
}

void bind_battle_lines(
    void* context, bool (*post_to_match)(void* context, const char* line)
) noexcept {
    g_match_poster.context = context;
    g_match_poster.post_to_match = post_to_match;
}

void post_battle_line(const char* line) noexcept {
    if (g_match_poster.post_to_match != nullptr &&
        g_match_poster.post_to_match(g_match_poster.context, line))
        return;
    post_wrapped_line(line, post_to_chat_ring, nullptr, mp::multiplayer_game());
}

void empty_chat_ring() noexcept {
    oa::Game& game = mp::multiplayer_game();
    mp::lobby_chat_head(game) = 0;
    mp::lobby_chat_tail(game) = 0;
}

} // namespace oa::app

namespace oa::app::netgame::extension_api {

void post_battle_line(const char* text) noexcept {
    oa::app::post_battle_line(text);
}

void empty_chat_ring() noexcept {
    oa::app::empty_chat_ring();
}

} // namespace oa::app::netgame::extension_api
