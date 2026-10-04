// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/team_panels.hpp"

#include "oa/ui/hud/share_panel.hpp"

#include "oa/core/player.h"
#include "oa/core/player_setup.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace oa::ui::hud {
namespace {

/// Game.session_flags bit 2, set once a game starts loading and kept while it
/// runs: the rows then leave out players no longer taking part.
constexpr uint8_t kSessionLoading = 0x04u;
/// PlayerSetupInfo.role bit of the player that hosts the game.
constexpr uint8_t kRoleHost = 0x01u;
/// PlayerSetupInfo.state of a player on another machine who was defeated.
constexpr uint8_t kSetupStateDefeated = 2;
/// PlayerSetupInfo.color of a player that has none yet.
constexpr uint8_t kNoColor = 0xffu;
/// TEAMICONSn value of a team without members.
constexpr int32_t kNoTeamIcon = 10;
/// Bytes of a formatted control name ("TEAMICONS9" and the like).
constexpr size_t kNameBytes = 32;
/// Characters of Player.name, which need not be terminated.
constexpr size_t kPlayerNameBytes = sizeof(Player::name);

/// Tells whether a player record is live: in use, local, computer or on
/// another machine, with a real index.
///
/// @param player player record
/// @return whether the player is live
bool slot_live(const Player& player) noexcept {
    return player.in_use != 0 &&
           (player.status == OA_PLAYER_STATUS_LOCAL || player.status == OA_PLAYER_STATUS_COMPUTER ||
            player.status == OA_PLAYER_STATUS_MIRRORED) &&
           player.index != kNoPlayer;
}

/// Tells whether a player in use is local or computer.
///
/// @param player player record
/// @return whether the player is simulated here
bool local_or_computer(const Player& player) noexcept {
    return player.in_use != 0 &&
           (player.status == OA_PLAYER_STATUS_LOCAL || player.status == OA_PLAYER_STATUS_COMPUTER);
}

/// Tells whether a player in use is a computer player.
///
/// @param player player record
/// @return whether the player is a computer player
bool computer(const Player& player) noexcept {
    return player.in_use != 0 && player.status == OA_PLAYER_STATUS_COMPUTER;
}

/// Tells whether a player is on another machine and was defeated there.
///
/// @param world setup blocks
/// @param player player record
/// @return whether the player is a defeated player on another machine
bool defeated_elsewhere(const World& world, const Player& player) noexcept {
    if (player.in_use == 0 || player.status != OA_PLAYER_STATUS_MIRRORED)
        return false;
    const auto* info = world_player_info(&world, &player);
    return info != nullptr && info->state == kSetupStateDefeated;
}

/// Tells whether a player's setup block marks it as a watcher.
///
/// @param world setup blocks
/// @param player player record
/// @return false without a setup block
bool watcher(const World& world, const Player& player) noexcept {
    const auto* info = world_player_info(&world, &player);
    return info != nullptr && (info->options & OA_SETUP_OPTION_WATCHER) != 0;
}

/// Returns a player's colour.
///
/// @param world setup blocks
/// @param player player record
/// @return PlayerSetupInfo.color, or kNoColor without a setup block
uint8_t color_of(const World& world, const Player& player) noexcept {
    const auto* info = world_player_info(&world, &player);
    return info != nullptr ? info->color : kNoColor;
}

/// Tells whether the game has started loading (Game.session_flags bit 2).
///
/// @param world the Game block
/// @return whether the bit is set
bool game_loading(const World& world) noexcept {
    return (world.game.session_flags & kSessionLoading) != 0;
}

/// Returns the local player.
///
/// @param world players and Game.local_player_index
/// @return the local player's record; player 0's when the index is out of range
const Player& local_player(const World& world) noexcept {
    return world.game.players[world.game.local_player_index % OA_PLAYER_COUNT];
}

/// Returns the local player, for changing.
///
/// @param world players and Game.local_player_index
/// @return the local player's record; player 0's when the index is out of range
Player& local_player(World& world) noexcept {
    return world.game.players[world.game.local_player_index % OA_PLAYER_COUNT];
}

/// Finds the control a pattern names for one number.
///
/// @param controls named controls of the panel
/// @param pattern printf pattern with one %d
/// @param number the number
/// @return the control's index, or -1
int32_t find_numbered(const PanelControls& controls, const char* pattern, int32_t number) {
    char name[kNameBytes];
    std::snprintf(name, sizeof name, pattern, static_cast<int>(number));
    return find_control(controls, name);
}

/// Shows or hides a control; nothing for -1 or without set_active.
///
/// @param controls named controls of the panel
/// @param index the control, or -1
/// @param shown whether it shows
void show(const PanelControls& controls, int32_t index, bool shown) {
    if (index != -1 && controls.set_active != nullptr)
        controls.set_active(controls.user, index, shown);
}

/// Greys a control out or makes it live; nothing for -1 or without set_grayed.
///
/// @param controls named controls of the panel
/// @param index the control, or -1
/// @param grayed whether it is greyed
void grey(const PanelControls& controls, int32_t index, bool grayed) {
    if (index != -1 && controls.set_grayed != nullptr)
        controls.set_grayed(controls.user, index, grayed);
}

/// Sets a control's value; nothing for -1 or without set_value.
///
/// @param controls named controls of the panel
/// @param index the control, or -1
/// @param value the value (a button's stage, an image's frame)
void set_control_value(const PanelControls& controls, int32_t index, int32_t value) {
    if (index != -1 && controls.set_value != nullptr)
        controls.set_value(controls.user, index, value);
}

/// Sets a control's text; nothing for -1 or without set_text.
///
/// @param controls named controls of the panel
/// @param index the control, or -1
/// @param text the text
void set_control_text(const PanelControls& controls, int32_t index, const char* text) {
    if (index != -1 && controls.set_text != nullptr)
        controls.set_text(controls.user, index, text);
}

/// Renames a control to a pattern and number; nothing for -1 or without set_name.
///
/// @param controls named controls of the panel
/// @param index the control, or -1
/// @param pattern printf pattern with one %d
/// @param number the number
void rename_numbered(
    const PanelControls& controls, int32_t index, const char* pattern, int32_t number
) {
    if (index == -1 || controls.set_name == nullptr)
        return;
    char name[kNameBytes];
    std::snprintf(name, sizeof name, pattern, static_cast<int>(number));
    controls.set_name(controls.user, index, name);
}

/// Numbers every control named `template_name` in panel order: the first
/// becomes pattern 0, the next pattern 1, and so on.
///
/// @param controls named controls of the panel
/// @param template_name the name the templates share
/// @param pattern printf pattern with one %d
void number_templates(
    const PanelControls& controls, const char* template_name, const char* pattern
) {
    if (controls.set_name == nullptr)
        return;
    for (int32_t number = 0; number < OA_PLAYER_COUNT; ++number) {
        const auto index = find_control(controls, template_name);
        if (index == -1)
            return;
        rename_numbered(controls, index, pattern, number);
    }
}

/// Reads a control's value.
///
/// @param controls named controls of the panel
/// @param name control name
/// @return its value; 0 when the panel has no such control or no value callback
int32_t control_value(const PanelControls& controls, const char* name) {
    const auto index = find_control(controls, name);
    return index != -1 && controls.value != nullptr ? controls.value(controls.user, index) : 0;
}

/// Tells whether a clicked name is a pattern with one number.
///
/// @param name clicked control name
/// @param pattern printf pattern with one %d
/// @param number the number
/// @return whether the name matches exactly
bool named(const char* name, const char* pattern, int32_t number) {
    char expected[kNameBytes];
    std::snprintf(expected, sizeof expected, pattern, static_cast<int>(number));
    return std::strcmp(name, expected) == 0;
}

/// Translates a text through the lookup.
///
/// @param translate UI text lookup; may be null
/// @param context context passed to `translate`
/// @param text the text
/// @return the translation, or `text` without one
const char* translated(TranslateText translate, void* context, const char* text) {
    const char* result = translate != nullptr ? translate(context, text) : nullptr;
    return result != nullptr ? result : text;
}

} // namespace

bool hosts_multiplayer_game(const World& world) noexcept {
    for (uint8_t index = 0; index < OA_PLAYER_COUNT; ++index) {
        const Player& player = world.game.players[index];
        if (player.status == OA_PLAYER_STATUS_FREE)
            continue;
        const auto* info = world_player_info(&world, &player);
        if (info == nullptr || (info->role & kRoleHost) == 0)
            continue;
        return local_or_computer(player);
    }
    return false;
}

bool control_offered(const World& world, const TeamPanelHost& host) {
    if (!hosts_multiplayer_game(world))
        return false;
    return host.tournament_game == nullptr || !host.tournament_game(host.context);
}

int32_t team_member_count(const World& world, uint8_t team) noexcept {
    if (team == OA_PLAYER_NO_TEAM)
        return 0;
    int32_t count = 0;
    for (const Player& player : world.game.players) {
        if (player.team != team || !slot_live(player))
            continue;
        if (game_loading(world) && !player_participating(player))
            continue;
        ++count;
    }
    return count;
}

void set_alliance(
    World& world,
    uint8_t from,
    uint8_t to,
    uint8_t allied,
    bool both_sides,
    const TeamPanelHost& host
) {
    if (from >= OA_PLAYER_COUNT || to >= OA_PLAYER_COUNT)
        return;
    Player& giver = world.game.players[from];
    Player& other = world.game.players[to];
    if (giver.index >= OA_PLAYER_COUNT || other.index >= OA_PLAYER_COUNT)
        return;
    if (local_or_computer(giver)) {
        giver.alliance[other.index] = allied;
        if (computer(other) || defeated_elsewhere(world, other) || both_sides)
            giver.allied_by[other.index] = allied;
    }
    if (local_or_computer(other)) {
        other.allied_by[giver.index] = allied;
        if (computer(other) || both_sides)
            other.alliance[giver.index] = allied;
    } else if (
        other.in_use != 0 && other.status == OA_PLAYER_STATUS_MIRRORED &&
        host.alliance_changed != nullptr
    ) {
        host.alliance_changed(host.context, from, to, allied);
    }
}

void update_player_rows(const World& world, const PanelControls& controls, bool skip_local) {
    for (int32_t slot = 0; slot < OA_PLAYER_COUNT; ++slot)
        for (const char* pattern : {"PLAYER%d", "LOGO%d", "ALLY%d", "TEAMICONS%d"})
            show(controls, find_numbered(controls, pattern, slot), false);
    const Player& me = local_player(world);
    const auto local = world.game.local_player_index;
    const bool loading = game_loading(world);
    int32_t row = 0;
    for (uint8_t slot = 0; slot < OA_PLAYER_COUNT; ++slot) {
        const Player& player = world.game.players[slot];
        if ((player.in_use != 0 && watcher(world, player)) || !slot_live(player) ||
            (slot == local && skip_local) || (loading && !player_participating(player)))
            continue;
        const auto color = color_of(world, player);
        if (color == kNoColor)
            continue;
        const auto line = find_numbered(controls, "PLAYER%d", row);
        char name[kPlayerNameBytes + 1]{};
        std::memcpy(name, player.name, kPlayerNameBytes);
        set_control_text(controls, line, name);
        show(controls, line, true);
        rename_numbered(controls, line, "LIVEPLYR%d", slot);
        const auto ally = find_numbered(controls, "ALLY%d", row);
        if (player_participating(player) && !local_or_computer(player) &&
            !defeated_elsewhere(world, player) && player_participating(me))
            show(controls, ally, true);
        rename_numbered(controls, ally, "LIVEALLY%d", slot);
        if (player.team == me.team && player.team != OA_PLAYER_NO_TEAM)
            grey(controls, ally, true);
        const auto icon = find_numbered(controls, "TEAMICONS%d", row);
        show(controls, icon, true);
        grey(controls, icon, !(local_or_computer(player) && !loading));
        const auto logo = find_numbered(controls, "LOGO%d", row);
        show(controls, logo, true);
        set_control_value(controls, logo, color);
        ++row;
    }
}

void update_ally_indicators(const World& world, const PanelControls& controls) {
    const Player& me = local_player(world);
    const auto local = world.game.local_player_index;
    for (uint8_t slot = 0; slot < OA_PLAYER_COUNT; ++slot) {
        const Player& player = world.game.players[slot];
        if ((player.in_use != 0 && watcher(world, player)) || !slot_live(player) ||
            !player_participating(player) || slot == local || color_of(world, player) == kNoColor)
            continue;
        const int32_t value = (me.allied_by[slot] != 0 ? 2 : 0) | (me.alliance[slot] != 0 ? 1 : 0);
        set_control_value(controls, find_numbered(controls, "LIVEALLY%d", slot), value);
    }
}

void update_team_icons(const World& world, const PanelControls& controls) {
    const bool loading = game_loading(world);
    int32_t shown = 0;
    for (int32_t slot = 0; slot < OA_PLAYER_COUNT; ++slot) {
        const Player& player = world.game.players[slot];
        if (!slot_live(player) || player.status == OA_PLAYER_STATUS_CLOSED)
            continue;
        if (loading && (!player_participating(player) || color_of(world, player) == kNoColor))
            continue;
        const auto number = loading ? shown++ : slot;
        const auto members = team_member_count(world, player.team);
        const int32_t icon = members == 0   ? kNoTeamIcon
                             : members == 1 ? player.team * 2 + 1
                                            : player.team * 2;
        set_control_value(controls, find_numbered(controls, "TEAMICONS%d", number), icon);
    }
}

void open_allies_panel(World& world, const PanelControls& controls) {
    world.game.frame_flags = static_cast<uint16_t>(world.game.frame_flags | kFrameAlliesPanelOpen);
    number_templates(controls, "ALLYx", "ALLY%d");
    number_templates(controls, "TEAMICONSx", "TEAMICONS%d");
    update_player_rows(world, controls, false);
    update_ally_indicators(world, controls);
    update_team_icons(world, controls);
    const Player& me = local_player(world);
    const auto* info = world_player_info(&world, &me);
    const bool allied_victory =
        info != nullptr && (info->status & OA_SETUP_STATUS_ALLIED_VICTORY) != 0;
    const auto victory = find_control(controls, "VICTORY");
    set_control_value(controls, victory, allied_victory ? 1 : 0);
    grey(controls, victory, team_member_count(world, me.team) > 1 || watcher(world, me));
}

TeamPanelResult allies_panel_click(
    World& world,
    const char* name,
    const PanelControls& controls,
    const HudEvents& events,
    const TeamPanelHost& host,
    TranslateText translate,
    void* translate_context
) {
    TeamPanelResult result;
    if (name == nullptr) {
        world.game.frame_flags =
            static_cast<uint16_t>(world.game.frame_flags & ~kFrameAlliesPanelOpen);
        result.click = TeamPanelClick::closed;
        return result;
    }
    Player& me = local_player(world);
    for (uint8_t slot = 0; slot < OA_PLAYER_COUNT; ++slot) {
        Player& player = world.game.players[slot];
        if (!named(name, "LIVEALLY%d", slot) || !slot_live(player))
            continue;
        play_sound(events, "Options");
        me.alliance[slot] = static_cast<uint8_t>(me.alliance[slot] ^ 1u);
        const auto allied = me.alliance[slot];
        set_alliance(world, world.game.local_player_index, slot, allied, false, host);
        char other[kPlayerNameBytes + 1]{};
        std::memcpy(other, player.name, kPlayerNameBytes);
        const char* phrase = translated(
            translate, translate_context, allied != 0 ? "allied with" : "broke alliance with"
        );
        std::snprintf(result.announcement, sizeof result.announcement, " %s %s", phrase, other);
        update_ally_indicators(world, controls);
        return result;
    }
    if (std::strcmp(name, "VICTORY") == 0) {
        play_sound(events, "Options");
        return result;
    }
    if (std::strcmp(name, "OK") == 0) {
        play_sound(events, "Options");
        if (auto* info = world_player_info(&world, &me); info != nullptr) {
            const bool before = (info->status & OA_SETUP_STATUS_ALLIED_VICTORY) != 0;
            const bool on = (control_value(controls, "VICTORY") & 1) != 0;
            info->status = static_cast<uint16_t>(
                (info->status & ~OA_SETUP_STATUS_ALLIED_VICTORY) |
                (on ? OA_SETUP_STATUS_ALLIED_VICTORY : 0u)
            );
            if (before != on && host.setup_changed != nullptr)
                host.setup_changed(host.context);
        }
        world.game.frame_flags =
            static_cast<uint16_t>(world.game.frame_flags & ~kFrameAlliesPanelOpen);
        result.click = TeamPanelClick::closed;
    }
    return result;
}

void update_control_panel(const World& world, const PanelControls& controls) {
    const Player& me = local_player(world);
    const auto* info = world_player_info(&world, &me);
    const uint16_t options = info != nullptr ? info->options : 0;
    set_control_value(
        controls,
        find_control(controls, "WATCHING"),
        (options & OA_SETUP_OPTION_WATCHING_ALLOWED) != 0 ? 1 : 0
    );
    set_control_value(
        controls,
        find_control(controls, "GAMEOPEN"),
        (options & OA_SETUP_OPTION_GAME_CLOSED) == 0 ? 1 : 0
    );
}

bool open_control_panel(const World& world, const PanelControls& controls) {
    if (watcher(world, local_player(world)))
        return false;
    update_player_rows(world, controls, true);
    update_control_panel(world, controls);
    return true;
}

TeamPanelResult control_panel_click(
    World& world,
    const char* name,
    const PanelControls& controls,
    const HudEvents& events,
    const TeamPanelHost& host
) {
    TeamPanelResult result;
    if (name == nullptr) {
        result.click = TeamPanelClick::closed;
        return result;
    }
    for (uint8_t slot = 0; slot < OA_PLAYER_COUNT; ++slot) {
        if (!named(name, "LIVEPLYR%d", slot))
            continue;
        result.click = TeamPanelClick::confirm_removal;
        result.player = slot;
        return result;
    }
    auto* info = world_player_info(&world, &local_player(world));
    if (std::strcmp(name, "WATCHING") == 0) {
        if (info != nullptr)
            info->options = static_cast<uint16_t>(info->options ^ OA_SETUP_OPTION_WATCHING_ALLOWED);
        play_sound(events, "Options");
        update_control_panel(world, controls);
        if (host.setup_changed != nullptr)
            host.setup_changed(host.context);
        return result;
    }
    if (std::strcmp(name, "OK") == 0) {
        if (host.game_changed != nullptr)
            host.game_changed(host.context);
        play_sound(events, "Options");
        if (info != nullptr && (info->options & OA_SETUP_OPTION_WATCHING_ALLOWED) == 0)
            for (uint8_t slot = 0; slot < OA_PLAYER_COUNT; ++slot) {
                const Player& player = world.game.players[slot];
                if (player.in_use != 0 && player.status == OA_PLAYER_STATUS_MIRRORED &&
                    watcher(world, player) && host.remove_player != nullptr)
                    host.remove_player(host.context, slot, kWatchingNotAllowed);
            }
        result.click = TeamPanelClick::closed;
    }
    return result;
}

void open_removal_question(
    const World& world,
    uint8_t player,
    const PanelControls& controls,
    TranslateText translate,
    void* translate_context
) {
    set_control_text(
        controls, find_control(controls, "CHOICE1"), translated(translate, translate_context, "Yes")
    );
    set_control_text(
        controls, find_control(controls, "CHOICE2"), translated(translate, translate_context, "No")
    );
    char name[kPlayerNameBytes + 1]{};
    if (player < OA_PLAYER_COUNT)
        std::memcpy(name, world.game.players[player].name, kPlayerNameBytes);
    char title[kNameBytes + kPlayerNameBytes + 1];
    std::snprintf(
        title, sizeof title, "%s %s?", translated(translate, translate_context, "Reject"), name
    );
    set_control_text(controls, find_control(controls, "TITLE"), title);
}

TeamPanelResult
removal_question_click(const char* name, uint8_t player, const TeamPanelHost& host) {
    TeamPanelResult result;
    if (name == nullptr || std::strcmp(name, "CHOICE2") == 0) {
        result.click = TeamPanelClick::closed;
        return result;
    }
    if (std::strcmp(name, "CHOICE1") == 0) {
        confirm_player_removal(host, player);
        result.click = TeamPanelClick::closed;
    }
    return result;
}

void confirm_player_removal(const TeamPanelHost& host, uint8_t player) {
    if (player < OA_PLAYER_COUNT && host.remove_player != nullptr)
        host.remove_player(host.context, player, kRemovedByHost);
}

} // namespace oa::ui::hud
