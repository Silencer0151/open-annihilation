// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Console command handlers and the static command lists.
#include "oa/ui/console/console.hpp"
#include "oa/core/map_plot.h"
#include "oa/ui/console/game_fields.hpp"

#include "oa/sim/ai.hpp"
#include "oa/data/mission_types.hpp"
#include "oa/sim/simulation_state.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace oa::ui::console {
namespace {

namespace services = oa::ui::services;
using services::TokenLine;

// Handlers take only the token line; the console being dispatched is held
// here for the duration of the call.
Console* g_active = nullptr;

constexpr const char* kEmpty = "";
constexpr const char* kOn = "ON";
constexpr const char* kOff = "OFF";
constexpr float kAtmAmount = 1000.0f;
constexpr double kGammaStep = 0.1;
// Game.session_flags bit set while a multiplayer game is live.
constexpr uint8_t kSessionLiveGame = 0x01u;
constexpr double kContourScale = 256.0;
constexpr double kSearchWeightScale = 65536.0;
constexpr float kContourSpacingDefault = 0.75f;
constexpr int32_t kPosterDefaultWidth = 0xc80;
constexpr int32_t kPosterDefaultHeight = 0x960;
constexpr int32_t kEdgeDefaultX = 0x20;
constexpr int32_t kEdgeDefaultZ = 0x80;
constexpr uint16_t kNoCursorFeature = 0xfffb;
constexpr int32_t kCursorStep = 0x100000;     // 16 pixels in 16.16
constexpr int32_t kSpawnColumnGap = 0x200000; // 32 pixels
constexpr int32_t kSpawnRowStart = 0xa00000;  // 160 pixels
constexpr int32_t kCommandLineGameId = 0x29a;
constexpr size_t kPathBytes = 0x100;

const ConsoleHost kNoHost{};

/// Returns the console being dispatched.
///
/// @return console_active(); only valid inside a dispatch.
Console& active() noexcept {
    return *g_active;
}

/// Returns the Game block of the console being dispatched.
///
/// @return The dispatching console's World::game.
Game& game() noexcept {
    return g_active->world->game;
}

/// Returns the dispatching console's host, or an empty host when it has none.
///
/// @return The ConsoleHost; every callback of the empty one is null.
const ConsoleHost& host() noexcept {
    return g_active->host != nullptr ? *g_active->host : kNoHost;
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

/// Lowers an ASCII capital letter; other characters are returned unchanged.
///
/// @param c Character.
/// @return The lower-case character.
char lower_ascii(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

/// Compares two strings for equality, ignoring ASCII case.
///
/// @param a First string.
/// @param b Second string.
/// @return Whether the strings are equal.
bool equal_nocase(const char* a, const char* b) noexcept {
    for (;; ++a, ++b) {
        if (lower_ascii(*a) != lower_ascii(*b))
            return false;
        if (*a == '\0')
            return true;
    }
}

/// Tests whether a player index names an active player slot of the game.
///
/// @param index Player index.
/// @return False for an index past the player table or a slot not in play.
bool slot_active(uint8_t index) noexcept {
    return index < OA_PLAYER_COUNT &&
           sim::simulation_state::player_slot_active(index, game().players[index]);
}

/// Returns the local player's record.
///
/// @return The Player at Game.local_player_index, or null when that index is past
///         the player table.
Player* local_player() noexcept {
    const auto index = game().local_player_index;
    return index < OA_PLAYER_COUNT ? &game().players[index] : nullptr;
}

/// Returns the local player's info record.
///
/// @return The record, or null when there is no local player or it has none.
PlayerSetupInfo* local_info() noexcept {
    Player* player = local_player();
    return player != nullptr ? world_player_info(active().world, player) : nullptr;
}

/// Tests whether a multiplayer game is live (kSessionLiveGame in the game's session flags).
///
/// @return Whether the flag is set.
bool live_game() noexcept {
    return (game().session_flags & kSessionLiveGame) != 0;
}

/// Has the host save the game options; nothing without a host callback.
void save_options() noexcept {
    if (host().save_game_options != nullptr)
        host().save_game_options(host().context);
}

/// Has the host compact the render cache; nothing without a host callback.
void compact_cache() noexcept {
    if (host().compact_render_cache != nullptr)
        host().compact_render_cache(host().context);
}

/// Posts a message through the host; nothing without a host callback.
///
/// @param text Message text.
/// @param kind kMessageNotice or kMessageService.
void post(const char* text, uint8_t kind) noexcept {
    if (host().post_message != nullptr)
        host().post_message(host().context, text, kind, kMessageNoSender);
}

/// Has the host reset the sight buffers; nothing without a host callback.
///
/// @param refill_grid Whether the sight grid is refilled as well.
void reset_sight(bool refill_grid) noexcept {
    if (host().reset_sight_buffers != nullptr)
        host().reset_sight_buffers(host().context, refill_grid);
}

/// Flips bits of the console option word (Game.console_flags).
///
/// @param bit console_flag bits to flip.
void toggle_console_flag(uint16_t bit) noexcept {
    set_console_flags(game(), static_cast<uint16_t>(console_flags(game()) ^ bit));
}

/// Flips bits of Game.graphics_flags.
///
/// @param bit graphics_flag bits to flip.
void toggle_graphics_flag(uint16_t bit) noexcept {
    game().graphics_flags = static_cast<uint16_t>(game().graphics_flags ^ bit);
}

/// Flips bit 0 of a 32-bit Game option word.
///
/// @param offset Byte offset of the word into oa::Game (a game_offset value).
void toggle_game_dword(size_t offset) noexcept {
    game_store<uint32_t>(game(), offset, game_load<uint32_t>(game(), offset) ^ 1u);
}

/// Reads the map position under the cursor.
///
/// @return Position in 16.16 world coordinates (Game.cursor_position).
FixedVec3 cursor_position() noexcept {
    return game_load<FixedVec3>(game(), game_offset::cursor_position);
}

/// Writes the map position under the cursor.
///
/// @param position Position in 16.16 world coordinates (Game.cursor_position).
void set_cursor_position(const FixedVec3& position) noexcept {
    game_store(game(), game_offset::cursor_position, position);
}

/// Reads the map cell column under the cursor.
///
/// @return Cell x (Game.cursor_cell_x).
int16_t cursor_cell_x() noexcept {
    return game_load<int16_t>(game(), game_offset::cursor_cell_x);
}

/// Reads the map cell row under the cursor.
///
/// @return Cell z (Game.cursor_cell_z).
int16_t cursor_cell_z() noexcept {
    return game_load<int16_t>(game(), game_offset::cursor_cell_z);
}

// --- Option commands --------------------------------------------------------

/// Toggles console_flag::no_shake, which stops blasts from shaking the screen ("NoShake").
///
/// @param line Command tokens (unused).
void toggle_no_shake(TokenLine* /*line*/) {
    toggle_console_flag(console_flag::no_shake);
}

/// Sets the contour display's spacing and phase ("Contour <spacing> [phase]").
///
/// Both values are scaled by 256 and truncated into Console::contour_values;
/// a spacing of 0 turns the contour lines off.
///
/// @param line Command tokens: the spacing (token 1, default 0) and the phase
///             (token 2, default 0.75), in heights.
void set_contour(TokenLine* line) {
    active().contour_values[0] = static_cast<int32_t>(
        static_cast<int64_t>(services::token_line_get_double(line, 1, 0.0f) * kContourScale)
    );
    active().contour_values[1] = static_cast<int32_t>(static_cast<int64_t>(
        services::token_line_get_double(line, 2, kContourSpacingDefault) * kContourScale
    ));
}

/// Sets Game.scroll_speed and saves the options ("ScrollSpeed <speed>").
///
/// @param line Command tokens; token 1 (default 0) is stored as its low byte.
void set_scroll_speed(TokenLine* line) {
    game().scroll_speed = static_cast<uint8_t>(token_int(line, 1, 0));
    save_options();
}

/// Sets the interface mode and saves the options ("IFace <mode>").
///
/// @param line Command tokens; token 1 (default 0) is stored whole in
///             Game.interface_type.
void set_interface_mode(TokenLine* line) {
    game_store<int32_t>(game(), game_offset::interface_mode, token_int(line, 1, 0));
    save_options();
}

/// Gives metal or energy from the local player to another player ("Give <player> <amount> metal|energy").
///
/// Nothing happens unless token 1 names an active player. The resource word is
/// compared ignoring case, and the host's transfer_metal or transfer_energy
/// performs the gift.
///
/// @param line Command tokens: the receiving player, the whole amount and the
///             resource.
void give_resource(TokenLine* line) {
    if (!slot_active(static_cast<uint8_t>(token_int(line, 1, 0))))
        return;
    const auto from = game().local_player_index;
    if (equal_nocase(token(line, 3), "metal") && host().transfer_metal != nullptr) {
        const auto amount = static_cast<float>(token_int(line, 2, 0));
        host().transfer_metal(
            host().context, from, static_cast<uint8_t>(token_int(line, 1, 0)), amount
        );
    }
    if (equal_nocase(token(line, 3), "energy") && host().transfer_energy != nullptr) {
        const auto amount = static_cast<float>(token_int(line, 2, 0));
        host().transfer_energy(
            host().context, from, static_cast<uint8_t>(token_int(line, 1, 0)), amount
        );
    }
}

/// Plays a CD audio track through the host ("CDPlay <track>"); the action is the music session's.
///
/// @param line Command tokens; token 1 (default 0) is the track number.
void play_cd_track(TokenLine* line) {
    if (host().play_cd_track != nullptr)
        host().play_cd_track(host().context, token_int(line, 1, 0));
}

/// Stops CD audio through the host ("CDStop"); the action is the music session's.
///
/// @param line Command tokens (unused).
void stop_cd(TokenLine* /*line*/) {
    if (host().stop_cd != nullptr)
        host().stop_cd(host().context);
}

/// Toggles 3D sound through the host, then saves the options ("Sound3D").
///
/// @param line Command tokens (unused).
void toggle_sound_3d(TokenLine* /*line*/) {
    if (host().toggle_sound_3d != nullptr)
        host().toggle_sound_3d(host().context);
    save_options();
}

/// Toggles graphics_flag::shading, compacts the render cache and saves the options ("Shading").
///
/// @param line Command tokens (unused).
void toggle_shading(TokenLine* /*line*/) {
    toggle_graphics_flag(graphics_flag::shading);
    compact_cache();
    save_options();
}

/// Toggles graphics_flag::anti_alias, compacts the render cache and saves the options ("AntiAlias").
///
/// @param line Command tokens (unused).
void toggle_anti_alias(TokenLine* /*line*/) {
    toggle_graphics_flag(graphics_flag::anti_alias);
    compact_cache();
    save_options();
}

/// Toggles graphics_flag::shadow, compacts the render cache and saves the options ("Shadow").
///
/// @param line Command tokens (unused).
void toggle_shadow(TokenLine* /*line*/) {
    toggle_graphics_flag(graphics_flag::shadow);
    compact_cache();
    save_options();
}

/// Toggles graphics_flag::dither and saves the options ("Dither").
///
/// @param line Command tokens (unused).
void toggle_dither(TokenLine* /*line*/) {
    toggle_graphics_flag(graphics_flag::dither);
    save_options();
}

/// Toggles or sets graphics_flag::switch_alt, under which digits select squads without Alt ("SwitchAlt [n]").
///
/// Without an argument the bit toggles and the options are saved; with one the
/// bit is set from the argument's low bit and nothing is saved.
///
/// @param line Command tokens; token 1 is the optional value.
void switch_alt(TokenLine* line) {
    if (line->count < 2) {
        toggle_graphics_flag(graphics_flag::switch_alt);
        save_options();
        return;
    }
    const bool on = (token_int(line, 1, 0) & 1) != 0;
    game().graphics_flags = static_cast<uint16_t>(
        (game().graphics_flags & ~graphics_flag::switch_alt) | (on ? graphics_flag::switch_alt : 0)
    );
}

/// Toggles graphics_flag::vehicle_shadow, the VehicleShadows option ("TShadow").
///
/// @param line Command tokens (unused).
void toggle_vehicle_shadow(TokenLine* /*line*/) {
    toggle_graphics_flag(graphics_flag::vehicle_shadow);
}

/// Toggles graphics_flag::feature_shadow ("FShadow").
///
/// @param line Command tokens (unused).
void toggle_feature_shadow(TokenLine* /*line*/) {
    toggle_graphics_flag(graphics_flag::feature_shadow);
}

/// Toggles visibility_flag::los_type and resets the sight buffers without refilling the grid ("LOSType").
///
/// @param line Command tokens (unused).
void toggle_los_type(TokenLine* /*line*/) {
    game().visibility_flags =
        static_cast<uint8_t>(game().visibility_flags ^ visibility_flag::los_type);
    reset_sight(false);
}

/// Passes three lighting values to the host and compacts the render cache ("Light <a> <b> <c>").
///
/// @param line Command tokens; tokens 1 to 3 (default 0) are the values, in order.
void set_lighting(TokenLine* line) {
    const int32_t c = token_int(line, 3, 0);
    const int32_t b = token_int(line, 2, 0);
    const int32_t a = token_int(line, 1, 0);
    if (host().set_lighting != nullptr)
        host().set_lighting(host().context, a, b, c);
    compact_cache();
}

/// Compacts the render cache ("RCache").
///
/// @param line Command tokens (unused).
void compact_render_cache(TokenLine* /*line*/) {
    compact_cache();
}

/// Marks every live unit selectable ("Selectable").
///
/// Sets kUnitFlagSelectable on every live unit from slot 1 up.
///
/// @param line Command tokens (unused).
void make_all_selectable(TokenLine* /*line*/) {
    World* world = active().world;
    for (uint32_t slot = 1; slot < world->unit_slot_count; ++slot) {
        Unit& unit = world->units[slot];
        if ((unit.flags & OA_UNIT_FLAG_LIVE) != 0)
            unit.flags |= kUnitFlagSelectable;
    }
}

/// Sets the music mode through the host ("MusicMode <kind>"); the action is the music session's.
///
/// @param line Command tokens; token 1 (default 0) is the mode.
void set_music_mode(TokenLine* line) {
    if (host().set_music_mode != nullptr)
        host().set_music_mode(host().context, token_int(line, 1, 0));
}

/// Sets an active player's logo ("Logo <logo> <player>").
///
/// The logo must be below the host's logo count; it is stored in the player
/// info record's colour byte and the render cache is compacted. Otherwise the
/// notice "Invalid logo setting" is posted.
///
/// @param line Command tokens: the logo index and the player index.
void set_player_logo(TokenLine* line) {
    const int32_t count = host().logo_count != nullptr ? host().logo_count(host().context) : 0;
    const int32_t logo = token_int(line, 1, 0);
    if (logo >= 0 && logo < count) {
        const auto index = static_cast<uint8_t>(token_int(line, 2, 0));
        if (slot_active(index)) {
            PlayerSetupInfo* info = world_player_info(active().world, &game().players[index]);
            if (info != nullptr)
                info->color = static_cast<uint8_t>(logo);
            compact_cache();
            return;
        }
    }
    post("Invalid logo setting", kMessageNotice);
}

/// Toggles bit 0 of the screen-chat option word and saves the options ("ScreenChat").
///
/// @param line Command tokens (unused).
void toggle_screen_chat(TokenLine* /*line*/) {
    toggle_game_dword(game_offset::screen_chat);
    save_options();
}

/// Sets the palette gamma and saves the options ("Gamma <tenths>").
///
/// The host's palette takes the value in tenths (token 1 times 0.1); the option
/// word Game.gamma keeps the integer.
///
/// @param line Command tokens; token 1 (default 0) is the gamma in tenths.
void set_gamma(TokenLine* line) {
    const int32_t tenths = token_int(line, 1, 0);
    if (host().set_gamma != nullptr)
        host().set_gamma(
            host().context, static_cast<float>(static_cast<double>(tenths) * kGammaStep)
        );
    game_store<int32_t>(game(), game_offset::gamma, token_int(line, 1, 0));
    save_options();
}

/// Toggles console_flag::clock and saves the options ("Clock").
///
/// @param line Command tokens (unused).
void toggle_clock(TokenLine* /*line*/) {
    toggle_console_flag(console_flag::clock);
    save_options();
}

/// Toggles the sound system's novelty-voice flag through the host ("Sing").
///
/// @param line Command tokens (unused).
void toggle_novelty_voice(TokenLine* /*line*/) {
    if (host().toggle_novelty_voice != nullptr)
        host().toggle_novelty_voice(host().context);
}

/// Returns the player a command names in token 1, or the local player for a bare command.
///
/// @param line Command tokens.
/// @return Player index; not checked against the player table.
uint8_t player_argument(const TokenLine* line) noexcept {
    return line->count == 1 ? game().local_player_index
                            : static_cast<uint8_t>(token_int(line, 1, 0));
}

/// Sets an active player's metal ("NoMetal [player] [amount]").
///
/// @param line Command tokens: the player (default the local player when the
///             command stands alone, else 0) and the whole amount (default 0).
void set_player_metal(TokenLine* line) {
    const auto index = player_argument(line);
    if (slot_active(index))
        game().players[index].metal = static_cast<float>(token_int(line, 2, 0));
}

/// Sets an active player's energy ("NoEnergy [player] [amount]").
///
/// @param line Command tokens: the player (default the local player when the
///             command stands alone, else 0) and the whole amount (default 0).
void set_player_energy(TokenLine* line) {
    const auto index = player_argument(line);
    if (slot_active(index))
        game().players[index].energy = static_cast<float>(token_int(line, 2, 0));
}

/// Toggles the observer camera ("BigBrother").
///
/// Turning it on sets kPeriodicFlagObserver and restarts Game.periodic_countdown
/// at 1; turning it off clears the flag and drops any camera follow
/// (follow_point_ticks, follow_unit, follow_target).
///
/// @param line Command tokens (unused).
void toggle_observer_camera(TokenLine* /*line*/) {
    if ((game().periodic_flags & kPeriodicFlagObserver) == 0) {
        game().periodic_flags = static_cast<uint8_t>(game().periodic_flags | kPeriodicFlagObserver);
        game().periodic_countdown = 1;
        return;
    }
    game().periodic_flags = static_cast<uint8_t>(game().periodic_flags & ~kPeriodicFlagObserver);
    game().follow_point_ticks = 0;
    game().follow_unit = 0;
    game().follow_target = 0;
}

/// Sets console_flag::developer for the passphrase, clears it for anything else ("Now ...").
///
/// The passphrase is "Now Film Chris Include Reload Assert": exactly six
/// tokens, the five after the command word compared case-sensitively.
///
/// @param line Command tokens.
void check_passphrase(TokenLine* line) {
    static constexpr const char* kPhrase[] = {"Film", "Chris", "Include", "Reload", "Assert"};
    constexpr int32_t kPhraseTokens = 6;
    bool accepted = line->count == kPhraseTokens;
    for (int32_t i = 0; accepted && i < kPhraseTokens - 1; ++i)
        accepted = std::strcmp(token(line, i + 1), kPhrase[i]) == 0;
    const uint16_t flags = console_flags(game());
    set_console_flags(
        game(),
        accepted ? static_cast<uint16_t>(flags | console_flag::developer)
                 : static_cast<uint16_t>(flags & ~console_flag::developer)
    );
}

/// Toggles console_flag::shoot_all ("ShootAll").
///
/// @param line Command tokens (unused).
void toggle_shoot_all(TokenLine* /*line*/) {
    toggle_console_flag(console_flag::shoot_all);
}

/// Flips one of the local player's share settings in a live multiplayer game and posts the new state.
///
/// Outside a live game, or without a local player info record, nothing happens.
/// After the flip the notice reads `format` with "ON" or "OFF", and the host's
/// player_info_changed announces the changed record.
///
/// @param bit share_flag bit to flip.
/// @param format Notice format with one %s for the new state.
void toggle_share(uint16_t bit, const char* format) noexcept {
    if (!live_game())
        return;
    PlayerSetupInfo* info = local_info();
    if (info == nullptr)
        return;
    set_share_flags(*info, static_cast<uint16_t>(share_flags(*info) ^ bit));
    char text[kChatLineBytes];
    std::snprintf(text, sizeof text, format, (share_flags(*info) & bit) != 0 ? kOn : kOff);
    post(text, kMessageNotice);
    if (host().player_info_changed != nullptr)
        host().player_info_changed(host().context);
}

/// Toggles sharing the local player's metal in a live multiplayer game ("ShareMetal").
///
/// Posts "Toggled ShareMetal to: ON|OFF".
///
/// @param line Command tokens (unused).
void toggle_share_metal(TokenLine* /*line*/) {
    toggle_share(share_flag::metal, "Toggled ShareMetal to: %s");
}

/// Toggles sharing the local player's energy in a live multiplayer game ("ShareEnergy").
///
/// Posts "Toggled ShareEnergy to: ON|OFF".
///
/// @param line Command tokens (unused).
void toggle_share_energy(TokenLine* /*line*/) {
    toggle_share(share_flag::energy, "Toggled ShareEnergy to: %s");
}

/// Toggles sharing the local player's mapping in a live multiplayer game ("ShareMapping").
///
/// Posts "Toggled ShareMapping to: ON|OFF".
///
/// @param line Command tokens (unused).
void toggle_share_mapping(TokenLine* /*line*/) {
    toggle_share(share_flag::mapping, "Toggled ShareMapping to: %s");
}

/// Toggles sharing the local player's radar in a live multiplayer game ("ShareRadar").
///
/// Posts "Toggled ShareRadar to: ON|OFF".
///
/// @param line Command tokens (unused).
void toggle_share_radar(TokenLine* /*line*/) {
    toggle_share(share_flag::radar, "Toggled ShareRadar to: %s");
}

/// Toggles all four share settings, each with its notice, in a live multiplayer game ("ShareAll").
///
/// @param line Command tokens, handed to each share command.
void toggle_share_all(TokenLine* line) {
    if (!live_game())
        return;
    toggle_share_metal(line);
    toggle_share_energy(line);
    toggle_share_mapping(line);
    toggle_share_radar(line);
}

/// Toggles bit 0 of the range display word Game.show_ranges ("ShowRanges").
///
/// @param line Command tokens (unused).
void toggle_show_ranges(TokenLine* /*line*/) {
    toggle_game_dword(game_offset::show_ranges);
}

/// Stores the local player's share threshold for one resource in a live multiplayer game and posts the notice.
///
/// A threshold above the player's storage is replaced by the storage; the
/// notice repeats the requested value.
///
/// @param line Command tokens; token 1 (default 0) is the whole threshold.
/// @param offset Byte offset of the float threshold in oa::Player (a player_offset value).
/// @param storage The player's storage of that resource.
/// @param format Notice format with one %d for the requested value.
void set_share_threshold(
    const TokenLine* line, size_t offset, float storage, const char* format
) noexcept {
    if (!live_game())
        return;
    Player* player = local_player();
    if (player == nullptr)
        return;
    const int32_t requested = token_int(line, 1, 0);
    const float threshold = static_cast<float>(requested) <= storage
                                ? static_cast<float>(token_int(line, 1, 0))
                                : storage;
    player_store(*player, offset, threshold);
    char text[kChatLineBytes];
    std::snprintf(text, sizeof text, format, token_int(line, 1, 0));
    post(text, kMessageNotice);
}

/// Sets the metal level above which the local player shares metal ("SetShareMetal <amount>").
///
/// Posts "OK. Will share metal if above <amount>"; see set_share_threshold.
///
/// @param line Command tokens; token 1 is the amount.
void set_metal_share_threshold(TokenLine* line) {
    const Player* player = local_player();
    if (player != nullptr)
        set_share_threshold(
            line,
            player_offset::metal_share_threshold,
            player->metal_storage,
            "OK.  Will share metal if above %d"
        );
}

/// Sets the energy level above which the local player shares energy ("SetShareEnergy <amount>").
///
/// Posts "OK. Will share energy if above <amount>"; see set_share_threshold.
///
/// @param line Command tokens; token 1 is the amount.
void set_energy_share_threshold(TokenLine* line) {
    const Player* player = local_player();
    if (player != nullptr)
        set_share_threshold(
            line,
            player_offset::energy_share_threshold,
            player->energy_storage,
            "OK.  Will share energy if above %d"
        );
}

/// Toggles Console::sfx_flag, under which the emitter pool refuses new particle emitters ("SFX").
///
/// @param line Command tokens (unused).
void toggle_sfx_flag(TokenLine* /*line*/) {
    active().sfx_flag = !active().sfx_flag;
}

// --- Cheats -----------------------------------------------------------------

/// Toggles console_flag::full_radar ("Radar").
///
/// @param line Command tokens (unused).
void toggle_full_radar(TokenLine* /*line*/) {
    toggle_console_flag(console_flag::full_radar);
}

/// Adds 1000 energy and 1000 metal to the viewed player ("ATM").
///
/// @param line Command tokens (unused).
void add_atm_resources(TokenLine* /*line*/) {
    const auto index = game().viewpoint_player;
    if (index >= OA_PLAYER_COUNT)
        return;
    game().players[index].energy += kAtmAmount;
    game().players[index].metal += kAtmAmount;
}

/// Views the game as another active player ("View <player>").
///
/// @param line Command tokens; token 1 is the player stored in Game.viewpoint_player.
void set_viewpoint(TokenLine* line) {
    if (slot_active(static_cast<uint8_t>(token_int(line, 1, 0))))
        game().viewpoint_player = static_cast<uint8_t>(token_int(line, 1, 0));
}

/// Toggles visibility_flag::line_of_sight, saves the options and resets the sight buffers ("LOS").
///
/// The sight grid is not refilled.
///
/// @param line Command tokens (unused).
void toggle_line_of_sight(TokenLine* /*line*/) {
    game().visibility_flags =
        static_cast<uint8_t>(game().visibility_flags ^ visibility_flag::line_of_sight);
    save_options();
    reset_sight(false);
}

/// Toggles visibility_flag::mapping, saves the options and resets the sight buffers, refilling the grid ("Mapping").
///
/// @param line Command tokens (unused).
void toggle_mapping(TokenLine* /*line*/) {
    game().visibility_flags =
        static_cast<uint8_t>(game().visibility_flags ^ visibility_flag::mapping);
    save_options();
    reset_sight(true);
}

/// Toggles console_flag::double_shot ("DoubleShot").
///
/// @param line Command tokens (unused).
void toggle_double_shot(TokenLine* /*line*/) {
    toggle_console_flag(console_flag::double_shot);
}

/// Toggles console_flag::half_shot ("HalfShot").
///
/// @param line Command tokens (unused).
void toggle_half_shot(TokenLine* /*line*/) {
    toggle_console_flag(console_flag::half_shot);
}

/// Reveals the whole map by turning mapping and line of sight off ("NowISee").
///
/// The sight buffers are reset and the grid refilled.
///
/// @param line Command tokens (unused).
void reveal_map(TokenLine* /*line*/) {
    game().visibility_flags = static_cast<uint8_t>(
        game().visibility_flags & ~(visibility_flag::mapping | visibility_flag::line_of_sight)
    );
    reset_sight(true);
}

/// Starts a meteor storm ("Meteor"), or enables or disables storms ("Meteor <n>").
///
/// @param line Command tokens; a nonzero token 1 enables storms, 0 disables them.
void meteor_command(TokenLine* line) {
    if (line->count < 2) {
        if (host().start_meteor_storm != nullptr)
            host().start_meteor_storm(host().context);
        return;
    }
    if (host().set_meteor_enabled != nullptr)
        host().set_meteor_enabled(host().context, token_int(line, 1, 0) != 0);
}

/// Renders a poster of the map around the view into the output directory's screenshots folder ("MakePoster [width|all] [height]").
///
/// The size defaults to 3200 x 2400 pixels; "all" takes the whole scrollable
/// map. Each side is at least the view's and at most the map's, and the
/// rectangle is centred on the view and kept inside the map. The host creates
/// "<output directory>\screenshots" and renders the rectangle there under the
/// prefix "BIGSHOT"; Game.last_frame_time then takes the host's current time.
///
/// @param line Command tokens: the width or "all" (token 1) and the height
///             (token 2), in pixels.
void make_poster(TokenLine* line) {
    int32_t width = kPosterDefaultWidth;
    int32_t height = kPosterDefaultHeight;
    if (line->count > 1)
        width = token_int(line, 1, 0);
    if (line->count > 2)
        height = token_int(line, 2, 0);
    Game& g = game();
    if (equal_nocase(token(line, 1), "all")) {
        width = g.map_pixel_width;
        height = g.map_pixel_height;
    }
    const int32_t view_width = g.view_cells_width * OA_MAP_CELL_PIXELS;
    if (width <= view_width)
        width = view_width;
    if (width >= g.map_pixel_width)
        width = g.map_pixel_width;
    const int32_t view_height = g.view_cells_height * OA_MAP_CELL_PIXELS;
    if (height <= view_height)
        height = view_height;
    if (height >= g.map_pixel_height)
        height = g.map_pixel_height;
    int32_t x = static_cast<int32_t>(g.camera_x) + g.view_cells_width * (OA_MAP_CELL_PIXELS / 2) -
                width / 2;
    int32_t y = static_cast<int32_t>(g.camera_y) + g.view_cells_height * (OA_MAP_CELL_PIXELS / 2) -
                height / 2;
    if (x <= 0)
        x = 0;
    if (x >= g.map_pixel_width - width)
        x = g.map_pixel_width - width;
    if (y <= 0)
        y = 0;
    if (y >= g.map_pixel_height - height)
        y = g.map_pixel_height - height;
    char directory[kPathBytes];
    char output[kOutputDirectoryBytes + 1] = {};
    std::memcpy(output, game_bytes(g, game_offset::output_directory), kOutputDirectoryBytes);
    // The path is cut to the buffer; one that cannot be formatted is left empty.
    if (std::snprintf(directory, sizeof directory, "%s\\screenshots", output) < 0)
        directory[0] = '\0';
    if (host().create_directories != nullptr)
        host().create_directories(host().context, directory);
    if (host().render_poster != nullptr)
        host().render_poster(host().context, directory, "BIGSHOT", x, y, width, height);
    if (host().now_ms != nullptr)
        g.last_frame_time = host().now_ms(host().context);
}

// --- Developer commands -----------------------------------------------------

/// Swaps an active player between local and computer control ("AI [player]").
///
/// A local player in use becomes a computer player; anything else becomes
/// local. The player's info record mirrors the new status.
///
/// @param line Command tokens; token 1 (default the local player) is the player.
void toggle_computer_control(TokenLine* line) {
    const auto index = static_cast<uint8_t>(token_int(line, 1, game().local_player_index));
    if (!slot_active(index))
        return;
    Player& player = game().players[index];
    const uint8_t status = player.in_use != 0 && player.status == OA_PLAYER_STATUS_LOCAL
                               ? OA_PLAYER_STATUS_COMPUTER
                               : OA_PLAYER_STATUS_LOCAL;
    player.status = status;
    PlayerSetupInfo* info = world_player_info(active().world, &player);
    if (info != nullptr)
        info->state = status;
}

/// Makes an active player the local and viewed player ("Control <player>").
///
/// @param line Command tokens; token 1 is the player.
void take_control(TokenLine* line) {
    if (!slot_active(static_cast<uint8_t>(token_int(line, 1, 0))))
        return;
    game().local_player_index = static_cast<uint8_t>(token_int(line, 1, 0));
    game().viewpoint_player = static_cast<uint8_t>(token_int(line, 1, 0));
}

/// Destroys every unit ("Kill") or one player's units ("Kill <player>"), then disables the mission conditions.
///
/// @param line Command tokens; token 1 is the player.
void kill_command(TokenLine* line) {
    if (line->count == 1) {
        if (host().kill_all_units != nullptr)
            host().kill_all_units(host().context);
    } else if (host().kill_player_units != nullptr) {
        host().kill_player_units(host().context, static_cast<uint8_t>(token_int(line, 1, 0)));
    }
    if (host().disable_mission_conditions != nullptr)
        host().disable_mission_conditions(host().context);
}

/// Returns the local player's side from its info record.
///
/// @return The side, or 0 without a record.
uint8_t local_side() noexcept {
    const PlayerSetupInfo* info = local_info();
    return info != nullptr ? info->side : 0;
}

/// Kills an opponent's units and flags a won game ("IWin").
///
/// The units of player 1 are killed, or of player 0 when the local player's
/// side is 1. Game.outcome_flags gains won, victory_transition and finished.
///
/// @param line Command tokens (unused).
/// @quirk The player killed is chosen from the local player's side number, not
///        from the player slots in play.
void force_win(TokenLine* /*line*/) {
    if (host().kill_player_units != nullptr)
        host().kill_player_units(host().context, local_side() != 1 ? 1 : 0);
    game().outcome_flags = static_cast<uint16_t>(
        game().outcome_flags | outcome_flag::won | outcome_flag::victory_transition |
        outcome_flag::finished
    );
}

/// Kills the units of the player whose index equals the local player's side and flags a lost game ("ILose").
///
/// Game.outcome_flags loses won and gains defeat_transition and finished.
///
/// @param line Command tokens (unused).
/// @quirk The player killed is chosen from the local player's side number, not
///        from the player slots in play.
void force_loss(TokenLine* /*line*/) {
    if (host().kill_player_units != nullptr)
        host().kill_player_units(host().context, local_side());
    game().outcome_flags = static_cast<uint16_t>(
        (game().outcome_flags & ~outcome_flag::won) | outcome_flag::defeat_transition |
        outcome_flag::finished
    );
}

/// Sets the output directory for posters and movie captures ("Film <dir>").
///
/// One trailing '\' or '/' is dropped. The change is marked
/// (Game.output_directory_changed) and the options are saved.
///
/// @param line Command tokens; token 1 is the directory. A bare command does nothing.
void set_output_directory(TokenLine* line) {
    if (line->count <= 1)
        return;
    char* directory = game_bytes(game(), game_offset::output_directory);
    std::snprintf(directory, kOutputDirectoryBytes, "%s", token(line, 1));
    const size_t length = std::strlen(directory);
    if (length > 0 && (directory[length - 1] == '\\' || directory[length - 1] == '/'))
        directory[length - 1] = '\0';
    game_store<uint32_t>(game(), game_offset::output_directory_changed, 1u);
    save_options();
}

/// Sets Game.capture_rate, marks the change and saves the options ("FilmSpeed <n>").
///
/// @param line Command tokens; token 1 must be above 0, else nothing changes.
void set_capture_rate(TokenLine* line) {
    if (line->count <= 1 || token_int(line, 1, 0) <= 0)
        return;
    game().capture_rate = token_int(line, 1, 0);
    game_store<uint32_t>(game(), game_offset::capture_rate_changed, 1u);
    save_options();
}

/// Does nothing; registered for "Assert", "DPrint", "Mem" and "ZBuffer".
///
/// @param line Command tokens (unused).
void ignore_command(TokenLine* /*line*/) {
}

/// Gives the selected units the named mission ("Assign <mission> <n>").
///
/// The mission is looked up in the mission table; an unknown name does
/// nothing. Its two parameter words are tokens 1 and 2 read as numbers, so the
/// name itself supplies the first (0 for a plain name).
///
/// @param line Command tokens: the mission name and the second mission word.
void assign_mission(TokenLine* line) {
    const uint8_t mission = oa::data::mission_types::index_for_name(token(line, 1));
    if (mission == oa::data::mission_types::unknown_mission ||
        host().issue_group_mission == nullptr)
        return;
    host().issue_group_mission(
        host().context, mission, token_int(line, 1, 0), token_int(line, 2, 0)
    );
}

/// Removes every feature from the map ("BurnAll").
///
/// @param line Command tokens (unused).
void burn_all_features(TokenLine* /*line*/) {
    if (host().clear_all_features != nullptr)
        host().clear_all_features(host().context);
}

/// Removes the feature under the cursor ("BurnOne").
///
/// Nothing happens when the cursor is over no feature (Game.cursor_feature at
/// or above 0xfffb).
///
/// @param line Command tokens (unused).
void burn_cursor_feature(TokenLine* /*line*/) {
    if (game_load<uint16_t>(game(), game_offset::cursor_feature) >= kNoCursorFeature)
        return;
    if (host().clear_feature_at != nullptr)
        host().clear_feature_at(host().context, cursor_cell_x(), cursor_cell_z());
}

/// Runs a crash test in debug-key mode after the passphrase ("DebugBreak [1|2|3]").
///
/// 1 and 2 exhaust the heap and the tagged heap, 3 divides by zero, and no
/// argument or 0 breaks into the debugger; other values do nothing.
///
/// @param line Command tokens; token 1 is the mode.
void debug_break(TokenLine* line) {
    if ((console_flags(game()) & console_flag::developer) == 0 ||
        (game().outcome_flags & outcome_flag::debug_keys) == 0)
        return;
    const auto crash = [](CrashTest test) {
        if (host().crash_test != nullptr)
            host().crash_test(host().context, test);
    };
    if (line->count > 0) {
        const int32_t mode = token_int(line, 1, 0);
        if (mode == 1)
            return crash(CrashTest::exhaust_heap);
        if (mode == 2)
            return crash(CrashTest::exhaust_tagged_heap);
        if (mode == 3)
            return crash(CrashTest::divide);
    }
    if (line->count != 0 && token_int(line, 1, 0) != 0)
        return;
    crash(CrashTest::break_into_debugger);
}

/// Sets the scrollable map size to the world size less the margins ("Edge [x] [z]").
///
/// @param line Command tokens: the x margin (token 1, default 32) and the z
///             margin (token 2, default 128), in pixels.
void set_map_edge(TokenLine* line) {
    game().map_pixel_width = game().map_width_world - token_int(line, 1, kEdgeDefaultX);
    game().map_pixel_height = game().map_height_world - token_int(line, 2, kEdgeDefaultZ);
}

/// Runs debugdat\<token 0>.txt with the line's tokens as its %N arguments.
///
/// As registered for "Include" that is always debugdat\Include.txt.
///
/// @param line Command tokens.
void include_script(TokenLine* line) {
    console_run_debug_script(&active(), token(line, 0), line);
}

/// Has the host touch memdump.txt ("MemDump").
///
/// @param line Command tokens (unused).
void touch_memory_dump(TokenLine* /*line*/) {
    if (host().touch_file != nullptr)
        host().touch_file(host().context, "memdump.txt");
}

/// Shifts the cursor position in 16-pixel steps ("Move <dx> <dz>").
///
/// Only a line whose token 0 is "move" (ignoring case) moves the cursor.
///
/// @param line Command tokens: the x and z steps (default 0).
void move_cursor(TokenLine* line) {
    if (!equal_nocase(token(line, 0), "move"))
        return;
    FixedVec3 position = cursor_position();
    position.x = static_cast<int32_t>(
        static_cast<uint32_t>(position.x) +
        static_cast<uint32_t>(token_int(line, 1, 0)) * static_cast<uint32_t>(kCursorStep)
    );
    set_cursor_position(position);
    position.z = static_cast<int32_t>(
        static_cast<uint32_t>(position.z) +
        static_cast<uint32_t>(token_int(line, 2, 0)) * static_cast<uint32_t>(kCursorStep)
    );
    set_cursor_position(position);
}

/// Writes an active player's AI weight report to a file ("PrintWeights <player> <file>").
///
/// Any other token count does nothing.
///
/// @param line Command tokens: the player and the file path.
void print_ai_weights(TokenLine* line) {
    if (line->count != 3)
        return;
    const auto player = static_cast<uint8_t>(token_int(line, 1, 0));
    if (!slot_active(player) || host().write_ai_weights == nullptr)
        return;
    host().write_ai_weights(host().context, player, token(line, 2));
}

/// Toggles Game.profiling, which shows the profiling bar graph ("Profile").
///
/// @param line Command tokens (unused).
void toggle_profiling(TokenLine* /*line*/) {
    game().profiling = game().profiling == 0;
}

/// Kills a unit type's units and reloads its definition ("Reload <unit>").
///
/// An unknown unit name does nothing.
///
/// @param line Command tokens; token 1 is the unit name.
void reload_unit_type(TokenLine* line) {
    if (line->count <= 1 || host().find_unit_type == nullptr)
        return;
    const uint16_t type = host().find_unit_type(host().context, token(line, 1));
    if (type == 0)
        return;
    if (host().kill_units_of_type != nullptr)
        host().kill_units_of_type(host().context, type);
    if (host().reload_unit_type != nullptr)
        host().reload_unit_type(host().context, type);
}

/// Reloads the AI profiles through the host ("ReloadAIProfiles").
///
/// @param line Command tokens (unused).
void reload_ai_profiles(TokenLine* /*line*/) {
    if (host().reload_ai_profiles != nullptr)
        host().reload_ai_profiles(host().context);
}

/// Saves the game as savegame\<name>.sav ("Save <name>"); the writer is the save system's.
///
/// The savegame directory is created first; the save's description is
/// "Generic Game Description".
///
/// @param line Command tokens; token 1 is the name. A bare command does nothing.
void save_game(TokenLine* line) {
    if (line->count <= 1)
        return;
    char path[kPathBytes];
    std::snprintf(path, sizeof path, "savegame\\%s.sav", token(line, 1));
    if (host().create_directories != nullptr)
        host().create_directories(host().context, "savegame");
    if (host().save_game != nullptr)
        host().save_game(host().context, path, "Generic Game Description", kCommandLineGameId);
}

/// Sets Game.sea_level ("SeaLevel <height>").
///
/// @param line Command tokens; token 1 (default 0) is stored as its low byte.
void set_sea_level(TokenLine* line) {
    game().sea_level = static_cast<uint8_t>(token_int(line, 1, 0));
}

/// Sets the path search's per-tick node credit and base heuristic weight ("Search <nodes> [weight]").
///
/// A nonzero node count becomes the per-tick credit. With exactly three tokens
/// the weight, scaled to 16.16 and truncated, becomes the base heuristic weight.
///
/// @param line Command tokens: the node count and the weight.
void set_search_limits(TokenLine* line) {
    if (token_int(line, 1, 0) != 0 && host().set_search_node_credit != nullptr)
        host().set_search_node_credit(host().context, token_int(line, 1, 0));
    if (line->count != 3)
        return;
    const double weight = services::parse_double(token(line, 2)) * kSearchWeightScale;
    if (host().set_search_heuristic != nullptr)
        host().set_search_heuristic(
            host().context, static_cast<int32_t>(static_cast<int64_t>(weight))
        );
}

/// Toggles console_flag::selection_boxes, which draws units' selection boxes ("SelBoxes").
///
/// @param line Command tokens (unused).
void toggle_selection_boxes(TokenLine* /*line*/) {
    toggle_console_flag(console_flag::selection_boxes);
}

/// Toggles console_flag::tree_death, without which weapons leave features undamaged ("TreeDeath").
///
/// @param line Command tokens (unused).
void toggle_tree_death(TokenLine* /*line*/) {
    toggle_console_flag(console_flag::tree_death);
}

/// Places a feature at the cursor cell ("Feature <name>").
///
/// @param line Command tokens; token 1 is the feature name.
void place_feature(TokenLine* line) {
    if (host().place_feature_at != nullptr)
        host().place_feature_at(host().context, token(line, 1), cursor_cell_x(), cursor_cell_z());
}

// --- AI profile directives --------------------------------------------------

/// Starts an AI-profile plan section ("plan <level>...").
///
/// The plan matches when a token after the directive names the current
/// difficulty (easy, medium or hard, ignoring case) or token 1 is "any";
/// token 1 is re-tested for "any" on every pass, so "any" in a later position
/// does not count.
///
/// @param line Directive tokens.
void ai_plan(TokenLine* line) {
    Console& console = active();
    console.ai_plan_matches = false;
    for (int32_t i = 1; i < line->count; ++i) {
        if (equal_nocase(token(line, 1), "any"))
            console.ai_plan_matches = true;
        const int32_t difficulty = game().difficulty;
        if (difficulty == OA_DIFFICULTY_EASY && equal_nocase(token(line, i), "easy"))
            console.ai_plan_matches = true;
        if (difficulty == OA_DIFFICULTY_MEDIUM && equal_nocase(token(line, i), "medium"))
            console.ai_plan_matches = true;
        if (difficulty == OA_DIFFICULTY_HARD && equal_nocase(token(line, i), "hard"))
            console.ai_plan_matches = true;
    }
}

/// Weights a unit type for every player with a controller ("weight <unit> <percent>").
///
/// Only after a matching "plan". Every player but a mirrored one has a
/// controller (Player.controller).
///
/// @param line Directive tokens: the unit name and the percentage.
void ai_weight(TokenLine* line) {
    if (!active().ai_plan_matches || host().apply_ai_weight == nullptr)
        return;
    const char* unit_type = token(line, 1);
    const auto percent = static_cast<float>(services::token_line_get_double(line, 2, 0.0f));
    for (uint8_t i = 0; i < OA_PLAYER_COUNT; ++i) {
        if (oa::sim::ai::player_has_controller(game().players[i]))
            host().apply_ai_weight(host().context, i, unit_type, percent);
    }
}

/// Limits a unit type for every computer player ("limit <unit> <count>").
///
/// Only after a matching "plan".
///
/// @param line Directive tokens: the unit name and the limit.
void ai_limit(TokenLine* line) {
    if (!active().ai_plan_matches || host().apply_ai_limit == nullptr)
        return;
    const char* unit_type = token(line, 1);
    const int32_t limit = token_int(line, 2, 0);
    for (uint8_t i = 0; i < OA_PLAYER_COUNT; ++i) {
        const Player& player = game().players[i];
        if (player.in_use != 0 && player.status == OA_PLAYER_STATUS_COMPUTER)
            host().apply_ai_limit(host().context, i, unit_type, limit);
    }
}

/// Runs console_spawn_by_pattern on the dispatching console; the developer fallback for unknown command names.
///
/// @param line Command tokens; token 0 is the pattern.
void spawn_fallback(TokenLine* line) {
    console_spawn_by_pattern(&active(), line);
}

constexpr uint32_t kOptionList = command_class::option | command_class::private_echo;

// Option commands.
const services::CommandRegistration kOptionCommands[] = {
    {"NoShake", toggle_no_shake, kOptionList},
    {"Contour", set_contour, kOptionList},
    {"ScrollSpeed", set_scroll_speed, kOptionList},
    {"IFace", set_interface_mode, kOptionList},
    {"Give", give_resource, kOptionList},
    {"CDPlay", play_cd_track, kOptionList},
    {"CDStop", stop_cd, kOptionList},
    {"Sound3D", toggle_sound_3d, kOptionList},
    {"Shading", toggle_shading, kOptionList},
    {"AntiAlias", toggle_anti_alias, kOptionList},
    {"Shadow", toggle_shadow, kOptionList},
    {"Dither", toggle_dither, kOptionList},
    {"SwitchAlt", switch_alt, kOptionList},
    {"TShadow", toggle_vehicle_shadow, kOptionList},
    {"FShadow", toggle_feature_shadow, kOptionList},
    {"LOSType", toggle_los_type, kOptionList},
    {"Light", set_lighting, kOptionList},
    {"RCache", compact_render_cache, kOptionList},
    {"Selectable", make_all_selectable, kOptionList},
    {"MusicMode", set_music_mode, kOptionList},
    {"Logo", set_player_logo, kOptionList},
    {"ScreenChat", toggle_screen_chat, kOptionList},
    {"Gamma", set_gamma, kOptionList},
    {"Clock", toggle_clock, kOptionList},
    {"Sing", toggle_novelty_voice, kOptionList},
    {"NoMetal", set_player_metal, kOptionList},
    {"NoEnergy", set_player_energy, kOptionList},
    {"BigBrother", toggle_observer_camera, kOptionList},
    {"Now", check_passphrase, kOptionList},
    {"ShootAll", toggle_shoot_all, kOptionList},
    {"ShareMetal", toggle_share_metal, kOptionList},
    {"ShareEnergy", toggle_share_energy, kOptionList},
    {"ShareMapping", toggle_share_mapping, kOptionList},
    {"ShareRadar", toggle_share_radar, kOptionList},
    {"ShareAll", toggle_share_all, kOptionList},
    {"ShowRanges", toggle_show_ranges, kOptionList},
    {"SetShareMetal", set_metal_share_threshold, kOptionList},
    {"SetShareEnergy", set_energy_share_threshold, kOptionList},
    {"SFX", toggle_sfx_flag, kOptionList},
    {nullptr, nullptr, 0},
};

const services::CommandRegistration kCheatCommands[] = {
    {"Radar", toggle_full_radar, command_class::cheat},
    {"ATM", add_atm_resources, command_class::cheat},
    {"View", set_viewpoint, command_class::cheat},
    {"LOS", toggle_line_of_sight, command_class::cheat},
    {"Mapping", toggle_mapping, command_class::cheat},
    {"DoubleShot", toggle_double_shot, command_class::cheat},
    {"HalfShot", toggle_half_shot, command_class::cheat},
    {"NowISee", reveal_map, command_class::cheat},
    {"Meteor", meteor_command, command_class::cheat},
    {"MakePoster", make_poster, command_class::cheat},
    {nullptr, nullptr, 0},
};

const services::CommandRegistration kDeveloperCommands[] = {
    {"AI", toggle_computer_control, command_class::developer},
    {"Control", take_control, command_class::developer},
    {"Kill", kill_command, command_class::developer},
    {"IWin", force_win, command_class::developer},
    {"ILose", force_loss, command_class::developer},
    {"Film", set_output_directory, command_class::developer},
    {"FilmSpeed", set_capture_rate, command_class::developer},
    {"Assert", ignore_command, command_class::developer},
    {"Assign", assign_mission, command_class::developer},
    {"BurnAll", burn_all_features, command_class::developer},
    {"BurnOne", burn_cursor_feature, command_class::developer},
    {"DebugBreak", debug_break, command_class::developer},
    {"DPrint", ignore_command, command_class::developer},
    {"Edge", set_map_edge, command_class::developer},
    {"Include", include_script, command_class::developer},
    {"Mem", ignore_command, command_class::developer},
    {"MemDump", touch_memory_dump, command_class::developer},
    {"Move", move_cursor, command_class::developer},
    {"PrintWeights", print_ai_weights, command_class::developer},
    {"Profile", toggle_profiling, command_class::developer},
    {"Reload", reload_unit_type, command_class::developer},
    {"ReloadAIProfiles", reload_ai_profiles, command_class::developer},
    {"Save", save_game, command_class::developer},
    {"SeaLevel", set_sea_level, command_class::developer},
    {"Search", set_search_limits, command_class::developer},
    {"SelBoxes", toggle_selection_boxes, command_class::developer},
    {"TreeDeath", toggle_tree_death, command_class::developer},
    {"Feature", place_feature, command_class::developer},
    {"ZBuffer", ignore_command, command_class::developer},
    {nullptr, nullptr, 0},
};

/// Registers the AI-profile directives "plan", "weight" and "limit" under command_class::ai_profile.
///
/// @param[in,out] table Command table the directives join.
/// @return False if the table refused one of them.
bool register_ai_profile_directives(services::CommandTable* table) noexcept {
    bool ok = services::command_table_set(table, "plan", ai_plan, command_class::ai_profile);
    ok = services::command_table_set(table, "weight", ai_weight, command_class::ai_profile) && ok;
    ok = services::command_table_set(table, "limit", ai_limit, command_class::ai_profile) && ok;
    return ok;
}

// Makes a console the dispatch target for the duration of a scope.
struct ActiveScope {
    Console* previous;

    /// Makes a console the dispatch target, keeping the previous one.
    ///
    /// @param console Console that console_active() returns until the scope ends.
    explicit ActiveScope(Console* console) noexcept : previous(g_active) { g_active = console; }

    /// Restores the previous dispatch target.
    ~ActiveScope() { g_active = previous; }

    ActiveScope(const ActiveScope&) = delete;
    ActiveScope& operator=(const ActiveScope&) = delete;
};

/// Raises an ASCII lower-case letter; other characters are returned unchanged.
///
/// @param c Character.
/// @return The upper-case character.
char upper_ascii(char c) noexcept {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

} // namespace

bool console_init(Console* console, World* world, const ConsoleHost* host) noexcept {
    std::memset(static_cast<void*>(console), 0, sizeof *console);
    console->world = world;
    console->host = host;
    bool ok = services::command_table_register(&console->commands, kOptionCommands);
    ok = services::command_table_register(&console->commands, kCheatCommands) && ok;
    ok = services::command_table_register(&console->commands, kDeveloperCommands) && ok;
    ok = register_ai_profile_directives(&console->commands) && ok;
    services::command_table_set_fallback(
        &console->commands, spawn_fallback, command_class::developer
    );
    if (host != nullptr && host->extend != nullptr)
        host->extend(host->extension_context, console);
    return ok;
}

uint32_t console_execute(Console* console, const char* text, uint32_t mask) noexcept {
    if (text == nullptr) {
        text = console->last_command;
    } else if (text != console->last_command) {
        std::snprintf(console->last_command, sizeof console->last_command, "%s", text);
        text = console->last_command;
    }
    services::TokenLine line;
    services::token_line_clear(&line);
    services::token_line_parse(&line, text, nullptr);
    ActiveScope scope(console);
    return services::command_dispatch(&console->commands, &line, mask);
}

uint32_t console_chat_mask(const Console* console) noexcept {
    uint32_t mask = kOptionList;
    if ((console_flags(console->world->game) & console_flag::developer) != 0)
        mask |= command_class::cheat | command_class::developer;
    if (console->cheats_enabled)
        mask |= command_class::cheat;
    return mask;
}

uint8_t console_submit_chat_line(
    Console* console, const char* line, uint8_t current_mode, const char** echo
) noexcept {
    while (*line == ' ')
        ++line;
    if (echo != nullptr)
        *echo = line;
    if (*line != '+')
        return current_mode;
    const uint32_t result = console_execute(console, line + 1, console_chat_mask(console));
    if ((result & command_class::cheat) != 0)
        return kChatModeEveryone;
    if ((result & command_class::private_echo) != 0)
        return kChatModeLocalOnly;
    return current_mode;
}

Console* console_active() noexcept {
    return g_active;
}

void console_post(Console* console, const char* text, uint8_t kind) noexcept {
    const ConsoleHost* host = console->host;
    if (host != nullptr && host->post_message != nullptr)
        host->post_message(host->context, text, kind, kMessageNoSender);
}

bool console_run_debug_script(
    Console* console, const char* name, services::TokenLine* arguments
) noexcept {
    const ConsoleHost* host = console->host;
    if (host == nullptr || host->read_text_file == nullptr)
        return false;
    char path[kPathBytes];
    std::snprintf(path, sizeof path, "debugdat\\%s.txt", name);
    int32_t length = 0;
    char* text = host->read_text_file(host->context, path, &length);
    if (text == nullptr)
        return false;
    Game& g = console->world->game;
    const FixedVec3 saved = game_load<FixedVec3>(g, game_offset::cursor_position);
    {
        ActiveScope scope(console);
        services::command_run_script(
            &console->commands, text, length, arguments, command_class::all
        );
    }
    if (host->free_text_file != nullptr)
        host->free_text_file(host->context, text);
    game_store(g, game_offset::cursor_position, saved);
    return true;
}

void console_spawn_by_pattern(Console* console, services::TokenLine* line) noexcept {
    World* world = console->world;
    Game& g = world->game;
    const ConsoleHost* host = console->host;
    FixedVec3 position = game_load<FixedVec3>(g, game_offset::cursor_position);
    const char* pattern = services::token_line_get(line, 0, kEmpty);
    const int32_t type_count = g.unit_def_count < static_cast<int32_t>(world->unit_def_count)
                                   ? g.unit_def_count
                                   : static_cast<int32_t>(world->unit_def_count);
    int32_t placed = 0;
    for (int32_t type = 1; type < type_count; type = static_cast<uint16_t>(type + 1)) {
        const UnitDef& def = world->unit_defs[type];
        if (!console_name_matches(def.unit_name, pattern))
            continue;
        if (placed != 0)
            position.x -= def.bounds_min_x;
        const auto type_id = static_cast<uint16_t>(type);
        if (host != nullptr && host->snap_build_position != nullptr)
            host->snap_build_position(host->context, type_id, &position);
        if (host != nullptr && host->create_unit != nullptr)
            host->create_unit(
                host->context,
                static_cast<uint8_t>(services::token_line_get_int(line, 1, 0)),
                type_id,
                &position
            );
        position.x += def.bounds_max_x + kSpawnColumnGap;
        if (static_cast<int32_t>(static_cast<uint32_t>(g.map_pixel_width) << 16) <= position.x) {
            position.z += kSpawnRowStart;
            position.x = kSpawnRowStart;
        }
        ++placed;
    }
    if (placed != 0)
        return;
    console_run_debug_script(console, pattern, line);
}

bool console_name_matches(const char* name, const char* pattern) noexcept {
    // Active pattern positions, as in the TDF loader's matcher: '?' and a
    // literal advance, '*' forks a copy one past it, a mismatch drops the path.
    constexpr int32_t kMaxPaths = 100;
    int32_t paths[kMaxPaths] = {0};
    int32_t count = 1;
    for (; *name != '\0'; ++name) {
        const char c = upper_ascii(*name);
        int32_t seen = 0;
        int32_t read = 0;
        int32_t write = count;
        while (seen < count) {
            const int32_t at = paths[read];
            const char p = upper_ascii(pattern[at]);
            if (p == '?' || p == c) {
                paths[read] = at + 1;
            } else if (p == '*') {
                if (count < kMaxPaths) {
                    ++count;
                    paths[write++] = at + 1;
                }
            } else {
                --count;
                --write;
                if (count == 0)
                    return false;
                --seen;
                paths[read] = paths[write];
                --read;
            }
            ++seen;
            ++read;
        }
    }
    for (int32_t i = 0; i < count; ++i) {
        const char* rest = pattern + paths[i];
        if (rest[0] == '\0' || (rest[0] == '*' && rest[1] == '\0'))
            return true;
    }
    return false;
}

void console_set_ai_plan_match(Console* console) noexcept {
    console->ai_plan_matches = true;
}

} // namespace oa::ui::console
