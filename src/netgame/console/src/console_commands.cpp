// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The console's network and service commands. "Compression" and "Senderror"
// are read by the packet layer's sender, "Drop" by the stall check and "BPS"
// by the traffic overlay; "Page" goes to the host's send_page, which hands it
// to the page hook of the extension built on network play
// (oa::app::netgame::extension_api::Hooks::page).
#include "oa/netgame/console/console_commands.hpp"

#include "oa/ui/services/commands.hpp"
#include "oa/ui/console/game_fields.hpp"
#include "oa/ui/frontend_multiplayer/lobby.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace oa::netgame::console {
namespace {

namespace ui = oa::ui::console;
namespace services = oa::ui::services;
using services::TokenLine;

constexpr const char* kEmpty = "";
constexpr const char* kOn = "ON";
constexpr const char* kOff = "OFF";
constexpr uint32_t kOptionList = ui::command_class::option | ui::command_class::private_echo;
constexpr int32_t kSendErrorMax = 100;
constexpr size_t kPageUserMax = 0xf;
constexpr int32_t kPageTextBudget = 0xff;
constexpr size_t kPageMessageBytes = 0x1fe;

const CommandHost kNoHost{};

/// Returns the CommandHost a console's host carries as its extension context.
///
/// @param console Console whose ConsoleHost is read.
/// @return The CommandHost, or an empty one (every callback null) when the
///         console has no host or no extension context.
const CommandHost& host_of(const ui::Console* console) noexcept {
    const ui::ConsoleHost* host = console->host;
    return host != nullptr && host->extension_context != nullptr
               ? *static_cast<const CommandHost*>(host->extension_context)
               : kNoHost;
}

/// Returns the Game block of the console being dispatched.
///
/// @return The dispatching console's World::game; only valid inside a dispatch.
Game& game() noexcept {
    return ui::console_active()->world->game;
}

/// Returns a token of the command line, or "" when the line has fewer tokens.
///
/// @param line Command tokens.
/// @param index Token index; 0 is the command word.
/// @return The token text.
const char* token(const TokenLine* line, int32_t index) noexcept {
    return services::token_line_get(line, index, kEmpty);
}

/// Returns a token of the command line read as a decimal integer.
///
/// @param line Command tokens.
/// @param index Token index; 0 is the command word.
/// @param fallback Value when the line has no such token.
/// @return The token's value, or `fallback`.
int32_t token_int(const TokenLine* line, int32_t index, int32_t fallback) noexcept {
    return services::token_line_get_int(line, index, fallback);
}

/// Tests whether a network game is live (Game.session_flags bit 0).
///
/// @return Whether the live bit is set.
bool live_game() noexcept {
    return (game().session_flags & oa::ui::frontend_multiplayer::kNetFlagLive) != 0;
}

/// Resets the traffic statistics ("NetStats").
///
/// @param line Command tokens (unused).
void reset_network_stats(TokenLine* /*line*/) {
    const CommandHost& host = host_of(ui::console_active());
    if (host.reset_traffic_stats != nullptr)
        host.reset_traffic_stats(host.context);
}

/// Sets or clears the console's no-drop bit, which skips the stalled-player check ("Drop <n>").
///
/// @param line Command tokens; token 1 of 0 (or missing) sets console_flag::no_drop,
///             any other value clears it.
void set_drop(TokenLine* line) {
    const bool bit = token_int(line, 1, 0) == 0;
    const uint16_t flags = ui::console_flags(game());
    ui::set_console_flags(
        game(),
        static_cast<uint16_t>(
            (flags & ~ui::console_flag::no_drop) | (bit ? ui::console_flag::no_drop : 0)
        )
    );
}

/// Toggles compression of outgoing packets in a live network game ("Compression").
///
/// Flips Game.compression_off and posts "Ok.  Outgoing packet compression
/// turned ON|OFF" as a notice; outside a live game it does nothing.
///
/// @param line Command tokens (unused).
void toggle_packet_compression(TokenLine* /*line*/) {
    if (!live_game())
        return;
    game().compression_off = game().compression_off == 0 ? 1u : 0u;
    char text[ui::kChatLineBytes];
    const bool off = game().compression_off != 0;
    std::snprintf(
        text, sizeof text, "Ok.  Outgoing packet compression turned %s", off ? kOff : kOn
    );
    ui::console_post(ui::console_active(), text, ui::kMessageNotice);
}

/// Toggles the traffic overlay's bandwidth readout ("BPS").
///
/// Flips bit 0 of Game.show_bandwidth.
///
/// @param line Command tokens (unused).
void toggle_show_bandwidth(TokenLine* /*line*/) {
    game().show_bandwidth ^= 1u;
}

/// Validates a "Page <user> <text>" line, sends the page and posts the outcome as a service message.
///
/// Without a user the notice is the syntax line; a game without an active
/// launch, or a user name over 15 characters, sends nothing. The words after
/// the user are joined with single spaces into at most 255 bytes; a longer
/// message is cut at a word and the notice says "truncated".
///
/// @param console Console whose ConsoleHost leads to the CommandHost and receives the notice.
/// @param line Tokens: the command word, the user and the message words.
/// @quirk The too-long-name notice ends after its colon without the name.
void page(ui::Console* console, const TokenLine* line) noexcept {
    const CommandHost& host = host_of(console);
    char message[kPageMessageBytes] = {};
    if (line->count < 2) {
        std::snprintf(message, sizeof message, "Syntax: page <user> <text>");
    } else if (host.launch_active == nullptr || !host.launch_active(host.context)) {
        std::snprintf(
            message, sizeof message, "Page command requires game launch from the Boneyards."
        );
    } else {
        const char* user = token(line, 1);
        if (std::strlen(user) > kPageUserMax) {
            std::snprintf(message, sizeof message, "Invalid user name (too long): ");
        } else {
            char text[kPageTextBudget + OA_PLAYER_COUNT * 2 + 1] = {};
            char* out = text;
            bool truncated = false;
            int32_t budget = kPageTextBudget;
            for (int32_t i = 2; i < line->count && !truncated && budget > 1; ++i) {
                const char* word = token(line, i);
                const auto length = static_cast<int32_t>(std::strlen(word));
                if (i > 2) {
                    *out++ = ' ';
                    --budget;
                }
                if (budget < length) {
                    truncated = true;
                } else {
                    std::memcpy(out, word, static_cast<size_t>(length));
                    out += length;
                    budget -= length;
                }
            }
            *out = '\0';
            if (host.send_page != nullptr)
                host.send_page(host.context, user, text);
            std::snprintf(
                message,
                sizeof message,
                "A %spage request for %s has been sent.",
                truncated ? "truncated " : kEmpty,
                user
            );
        }
    }
    if (message[0] != '\0')
        ui::console_post(console, message, ui::kMessageService);
}

/// Runs page() on the dispatching console ("Page" and "P").
///
/// @param line Command tokens.
void page_user(TokenLine* line) {
    page(ui::console_active(), line);
}

/// Sets the simulated datagram loss of outgoing sends ("Senderror <percent>").
///
/// @param line Command tokens; only a line of exactly two tokens has an effect.
///             Token 1 outside 0..100 stores 0 in Game.send_error_percent.
void set_send_error(TokenLine* line) {
    if (line->count != 2)
        return;
    int32_t percent = token_int(line, 1, 0);
    if (percent < 0 || percent > kSendErrorMax)
        percent = 0;
    game().send_error_percent = percent;
}

const services::CommandRegistration kCommands[] = {
    {"NetStats", reset_network_stats, kOptionList},
    {"Drop", set_drop, kOptionList},
    {"Compression", toggle_packet_compression, kOptionList},
    {"BPS", toggle_show_bandwidth, kOptionList},
    {"Page", page_user, kOptionList},
    {"P", page_user, kOptionList},
    {"Senderror", set_send_error, ui::command_class::developer},
    {nullptr, nullptr, 0},
};

} // namespace

void register_console_commands(void* extension_context, ui::Console* console) noexcept {
    const auto* host = static_cast<const CommandHost*>(extension_context);
    if (host != nullptr && host->reset_traffic_stats != nullptr)
        host->reset_traffic_stats(host->context);
    services::command_table_register(&console->commands, kCommands);
}

void console_page_user(ui::Console* console, const char* text) noexcept {
    services::TokenLine line;
    services::token_line_clear(&line);
    services::token_line_parse(&line, text, nullptr);
    page(console, &line);
}

} // namespace oa::netgame::console
