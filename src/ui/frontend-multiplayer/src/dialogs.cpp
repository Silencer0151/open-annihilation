// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Panels stacked over the battleroom or the match.
#include "oa/ui/frontend_multiplayer/dialogs.hpp"

#include "oa/base/game_loop.hpp"
#include "oa/formats/tnt.hpp"
#include "oa/present/model/mesh_raster.hpp"
#include "oa/present/surface.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace oa::ui::frontend_multiplayer {

namespace {

constexpr uint8_t kRejectPlayer = 1;
// Game.gui_flags: a lobby program launched the game.
constexpr uint8_t kGuiFlagLobbyLaunch = 0x10;
constexpr uint8_t kRejectTimedOut = 6;
constexpr uint8_t kRejectWatching = 9;
constexpr uint32_t kTimeoutGraceSeconds = 0x78;
constexpr uint32_t kTimeoutRefreshTicks = 2;
constexpr uint32_t kTicksPerSecond = 0x1e;
// The minimap leaves out this many world pixels on a map's right and at its bottom.
constexpr int32_t kMinimapRightMargin = 32;
constexpr int32_t kMinimapBottomMargin = 128;
// A terrain cell is 16 world pixels a side.
constexpr int32_t kWorldPixelsPerCellShift = 4;
// Bytes from a texture's first pixel that the quad sampler may read.
constexpr size_t kTextureWindowBytes = 0x10000;

void play(Lobby& lobby, const char* name) noexcept {
    if (lobby.services.play_sound != nullptr)
        lobby.services.play_sound(lobby.services.context, name);
}

void message(Lobby& lobby, const char* text) noexcept {
    if (lobby.services.message != nullptr)
        lobby.services.message(lobby.services.context, text);
}

uint32_t now(Lobby& lobby) noexcept {
    return lobby.services.tick != nullptr ? lobby.services.tick(lobby.services.context) : 0;
}

void notify(Lobby& lobby, int32_t event) noexcept {
    if (lobby.services.notify != nullptr)
        lobby.services.notify(lobby.services.context, event);
}

std::string name_of(const Player& player) {
    return {player.name, ::strnlen(player.name, sizeof(player.name))};
}

PlayerSetupInfo& info(Lobby& lobby, int32_t slot) noexcept {
    auto* found = slot_info(lobby, slot);
    return found != nullptr ? *found : lobby.infos[0];
}

bool local_or_computer(const Player& player) noexcept {
    return player.in_use != 0 && (player.status == kSlotLocal || player.status == kSlotComputer);
}

/// Returns the player timeout in seconds (Game.player_timeout_seconds).
///
/// @param game Game whose timeout is read.
/// @return The timeout in seconds.
int32_t timeout_seconds(const Game& game) noexcept {
    return game.player_timeout_seconds;
}

void set_text(Panel& panel, const char* pattern, int32_t index, std::string_view text) {
    char name[32];
    std::snprintf(name, sizeof(name), pattern, static_cast<int>(index));
    panel_set_text(panel, name, text);
}

Control* control_at(Panel& panel, const char* pattern, int32_t index) noexcept {
    char name[32];
    std::snprintf(name, sizeof(name), pattern, static_cast<int>(index));
    return panel_control(panel, name);
}

/// Translates interface text through LobbyServices::translate.
///
/// @param lobby lobby whose services translate
/// @param text text to translate
/// @return its translation, or the text itself without one
const char* translated(const Lobby& lobby, const char* text) noexcept {
    const char* translation = lobby.services.translate != nullptr
                                  ? lobby.services.translate(lobby.services.context, text)
                                  : nullptr;
    return translation != nullptr ? translation : text;
}

} // namespace

// ---------------------------------------------------------------------------
// YESORNO

void confirm_open(Lobby& lobby, Panel& panel, uint8_t slot) noexcept {
    lobby.confirm_slot = slot;
    panel_set_text(panel, "CHOICE1", "Yes");
    panel_set_text(panel, "CHOICE2", "No");
    char title[100];
    std::snprintf(
        title, sizeof(title), "%s %s?", "Reject", name_of(slot_player(lobby, slot)).c_str()
    );
    panel_set_text(panel, "TITLE", title);
}

bool confirm_handle_event(Lobby& lobby, Panel& panel) noexcept {
    if (panel.selected == kNoControl)
        return false;
    if (panel_selected_is(panel, "CHOICE1")) {
        panel.selected = kNoControl;
        if (lobby.confirm_slot < kSlotCount)
            lobby_reject(lobby, slot_player(lobby, lobby.confirm_slot).player_id, kRejectPlayer);
        return true;
    }
    if (panel_selected_is(panel, "CHOICE2")) {
        panel.selected = kNoControl;
        return true;
    }
    panel.selected = kNoControl;
    return false;
}

void exit_confirm_open(Lobby& lobby, Panel& panel) noexcept {
    panel_set_text(panel, "CHOICE1", "Yes");
    panel_set_text(panel, "CHOICE2", "No");
    const bool lobby_launch =
        lobby.game != nullptr && (lobby.game->gui_flags & kGuiFlagLobbyLaunch) != 0;
    panel_set_text(
        panel,
        "TITLE",
        lobby_launch ? "Exit the Battle" : "Surrender this battle and exit to the system?"
    );
    panel.focus = panel_find(panel, "CHOICE2");
    panel.selected = kNoControl;
    panel.dirty = true;
}

ExitConfirmAnswer exit_confirm_handle_event(Lobby& lobby, Panel& panel) noexcept {
    if (panel.selected == kNoControl)
        return ExitConfirmAnswer::none;
    play(lobby, "Exit");
    const bool leave = panel_selected_is(panel, "CHOICE1");
    const bool closed = panel_selected_is(panel, "CHOICE2");
    panel.selected = kNoControl;
    if (leave) {
        if (lobby.game != nullptr)
            lobby.game->outcome_flags =
                static_cast<uint16_t>(lobby.game->outcome_flags | kOutcomeLeaving);
        return ExitConfirmAnswer::leave;
    }
    return closed ? ExitConfirmAnswer::closed : ExitConfirmAnswer::none;
}

// ---------------------------------------------------------------------------
// TIMEOUT

bool timeout_open(Lobby& lobby, Panel& panel, uint32_t player_id) noexcept {
    const auto slot = slot_for_player_id(lobby, player_id);
    if (slot < 0)
        return false;
    lobby.timeout_player = player_id;
    lobby.timeout_refresh_tick = 0;
    panel_set_items(panel, "OUTPUT", {});
    panel.focus = panel_find(panel, "TALK");
    panel_set_text(panel, "NAME", name_of(slot_player(lobby, slot)));
    return true;
}

bool timeout_tick(Lobby& lobby, Panel& panel) noexcept {
    auto& game = *lobby.game;
    const auto tick = now(lobby);
    if (lobby_clock_passed(tick, lobby.timeout_refresh_tick)) {
        lobby.timeout_refresh_tick = tick + kTimeoutRefreshTicks;
        panel.dirty = true;
    }
    auto* output = panel_control(panel, "OUTPUT");
    const int32_t rows = output != nullptr ? output->height / 14 : 0;
    uint32_t at = lobby_chat_head(game);
    for (int32_t shown = 1; shown < rows - 1 && at != lobby_chat_tail(game); ++shown)
        at = at == 0 ? kChatLines - 1 : at - 1;
    std::vector<std::string> lines;
    for (; at != lobby_chat_head(game); at = (at + 1) % kChatLines)
        lines.emplace_back(
            lobby_chat_line(game, at), ::strnlen(lobby_chat_line(game, at), kChatLineBytes)
        );
    panel_set_items(panel, "OUTPUT", std::move(lines));
    const auto slot = slot_for_player_id(lobby, lobby.timeout_player);
    if (slot < 0)
        return true;
    auto& player = slot_player(lobby, slot);
    if (player.in_use == 0 || player.status != kSlotRemote)
        return true;
    const auto silent = base::game_loop::scaled_clock_elapsed(now(lobby), player.last_update_time) /
                        kTicksPerSecond;
    const auto limit = static_cast<uint32_t>(timeout_seconds(game)) + kTimeoutGraceSeconds;
    char text[200];
    std::snprintf(
        text,
        sizeof(text),
        "will be rejected in %d seconds",
        static_cast<int>(limit > silent ? limit - silent : 0)
    );
    panel_set_text(panel, "TIMETEXT", text);
    if (limit <= silent) {
        lobby_reject(lobby, lobby.timeout_player, kRejectTimedOut);
        return true;
    }
    return false;
}

bool timeout_handle_event(Lobby& lobby, Panel& panel) noexcept {
    if (panel.selected == kNoControl)
        return false;
    if (panel_selected_is(panel, "TALK")) {
        auto* talk = panel_control(panel, "TALK");
        const auto text = std::string(control_text(*talk));
        if (!text.empty()) {
            lobby_say(lobby, local_player(lobby), text.c_str());
            lobby.game->gui_flags |= 1U;
            set_control_text(*talk, "");
        }
        panel.focus = panel_find(panel, "TALK");
        panel.selected = kNoControl;
        return false;
    }
    if (panel_selected_is(panel, "REJECT")) {
        panel.selected = kNoControl;
        lobby_reject(lobby, lobby.timeout_player, kRejectTimedOut);
        return true;
    }
    panel.selected = kNoControl;
    return false;
}

// ---------------------------------------------------------------------------
// VIEWMAP / SELMAP

std::vector<uint8_t> fit_map_picture(
    std::span<const uint8_t> pixels,
    int32_t picture_width,
    int32_t picture_height,
    int32_t box_width,
    int32_t box_height,
    int32_t world_width,
    int32_t world_height
) {
    const int32_t playable_width = world_width - kMinimapRightMargin;
    const int32_t playable_height = world_height - kMinimapBottomMargin;
    if (playable_width <= 0 || playable_height <= 0 || box_width <= 0 || box_height <= 0 ||
        box_width > kMapPictureMaxSide || box_height > kMapPictureMaxSide || picture_width <= 0 ||
        picture_height <= 0 || picture_width > std::numeric_limits<uint16_t>::max() ||
        picture_height > std::numeric_limits<uint16_t>::max() ||
        pixels.size() / static_cast<size_t>(picture_width) < static_cast<size_t>(picture_height))
        return {};
    const auto scale = [](int32_t value, int32_t numerator, int32_t denominator) {
        return static_cast<int32_t>(static_cast<int64_t>(value) * numerator / denominator);
    };
    int32_t shown_width = box_width;
    int32_t shown_height = box_height;
    int32_t used_width = picture_width;
    int32_t used_height = picture_height;
    int32_t left = 0;
    int32_t top = 0;
    if (playable_width >= playable_height) {
        shown_height = scale(playable_height, box_height, playable_width);
        used_height = scale(picture_height, playable_height, playable_width);
        top = (box_height - shown_height) / 2;
    } else {
        shown_width = scale(playable_width, box_width, playable_height);
        used_width = scale(picture_width, playable_width, playable_height);
        left = (box_width - shown_width) / 2;
    }
    std::vector<uint8_t> fitted(
        static_cast<size_t>(box_width) * static_cast<size_t>(box_height), 0
    );
    if (shown_width < 1 || shown_height < 1 || used_width < 1 || used_height < 1)
        return fitted;
    oa::Surface target{};
    present::init_surface(target, box_width, box_height, box_width, fitted.data());
    std::vector<uint8_t> texels(std::max(pixels.size(), kTextureWindowBytes), 0);
    std::copy(pixels.begin(), pixels.end(), texels.begin());
    oa::Sprite texture{};
    texture.width = static_cast<uint16_t>(picture_width);
    texture.height = static_cast<uint16_t>(picture_height);
    texture.encoding = OA_SPRITE_RAW;
    texture.data = texels.data();
    const present::PolygonVertex quad[4] = {
        {left, top},
        {left + shown_width, top},
        {left + shown_width, top + shown_height},
        {left, top + shown_height}
    };
    const present::model::TexturePoint uv[4] = {
        {0, 0}, {used_width - 1, 0}, {used_width - 1, used_height - 1}, {0, used_height - 1}
    };
    present::model::texture_quad(&target, &texture, quad, uv);
    return fitted;
}

void map_summary_update(Lobby& lobby, Panel& panel) noexcept {
    const data::campaign::CampaignFile* map =
        lobby.maps.map_context != nullptr ? lobby.maps.map_context(lobby.maps.context) : nullptr;
    const char* name = map != nullptr               ? data::campaign::campaign_localized_name(map)
                       : lobby.maps.name != nullptr ? lobby.maps.name(lobby.maps.context)
                                                    : "";
    if (panel_find(panel, "MAPNAME") != kNoControl)
        panel_set_text(panel, "MAPNAME", name != nullptr ? name : "");
    if (map != nullptr) {
        char size[100];
        std::snprintf(
            size,
            sizeof(size),
            "%.*s  %s: %.*s",
            static_cast<int>(::strnlen(map->memory, sizeof(map->memory))),
            map->memory,
            translated(lobby, "Players"),
            static_cast<int>(::strnlen(map->num_players, sizeof(map->num_players))),
            map->num_players
        );
        panel_set_text(panel, "SIZE", size);
    } else {
        const char* size =
            lobby.maps.size_text != nullptr ? lobby.maps.size_text(lobby.maps.context) : "";
        panel_set_text(panel, "SIZE", size != nullptr ? size : "");
    }
    if (auto* picture = panel_control(panel, "MAPPIC")) {
        picture->picture.clear();
        picture->picture_width = picture->picture_height = 0;
        const char* path =
            map != nullptr
                ? data::campaign::campaign_path(map, data::campaign::CampaignPath::mission)
                : nullptr;

        struct MapFile {
            Lobby* lobby;
            const char* path;
        } file{&lobby, path};

        const formats::tnt::MapFileReader reader{
            &file, [](void* context, uint32_t offset, void* out, uint32_t bytes) {
                const auto& open = *static_cast<MapFile*>(context);
                return open.path != nullptr && open.lobby->maps.read_map != nullptr &&
                       open.lobby->maps.read_map(
                           open.lobby->maps.context, open.path, offset, out, bytes
                       );
            }
        };
        formats::tnt::RadarPicture radar;
        int32_t width = 0;
        int32_t height = 0;
        if (formats::tnt::load_radar_picture(reader, radar, &width, &height)) {
            picture->picture = fit_map_picture(
                radar.pixels,
                radar.width,
                radar.height,
                picture->width,
                picture->height,
                width << kWorldPixelsPerCellShift,
                height << kWorldPixelsPerCellShift
            );
            if (!picture->picture.empty()) {
                picture->picture_width = picture->width;
                picture->picture_height = picture->height;
            }
        }
    }
    const char* description =
        lobby.maps.description != nullptr ? lobby.maps.description(lobby.maps.context) : "";
    panel_set_text(panel, "DESCRIPTION", description != nullptr ? description : "");
    panel.dirty = true;
}

void viewmap_open(Lobby& lobby, Panel& panel) noexcept {
    map_summary_update(lobby, panel);
}

bool viewmap_follow_host(Lobby& lobby, Panel& panel, std::string_view host_map) noexcept {
    const char* bound = lobby.maps.name != nullptr ? lobby.maps.name(lobby.maps.context) : nullptr;
    if (host_map == std::string_view(bound != nullptr ? bound : ""))
        return false;
    const std::string wanted(host_map);
    if (lobby.maps.select != nullptr && lobby.maps.select(lobby.maps.context, wanted.c_str())) {
        map_summary_update(lobby, panel);
        return true;
    }
    if (panel_find(panel, "MAPNAME") != kNoControl)
        panel_set_text(panel, "MAPNAME", wanted);
    if (auto* picture = panel_control(panel, "MAPPIC")) {
        picture->picture.clear();
        picture->picture_width = picture->picture_height = 0;
    }
    panel_set_text(panel, "DESCRIPTION", "");
    panel.dirty = true;
    return true;
}

bool viewmap_handle_event(Lobby& lobby, Panel& panel) noexcept {
    if (panel.selected == kNoControl)
        return false;
    const bool ok = panel_selected_is(panel, "OK");
    panel.selected = kNoControl;
    if (ok)
        play(lobby, "Multi");
    return ok;
}

bool mapselect_open(Lobby& lobby, MapSelect& select, Panel& panel) noexcept {
    select.names.clear();
    const bool selected = lobby.maps.selected != nullptr && lobby.maps.selected(lobby.maps.context);
    const int32_t count = lobby.maps.count != nullptr ? lobby.maps.count(lobby.maps.context) : 0;
    if (!selected || count <= 0) {
        message(lobby, "There are no multiplayer maps to choose from");
        return false;
    }
    select.previous = lobby.maps.name(lobby.maps.context);
    for (int32_t index = 0; index < count; ++index)
        if (const char* name = lobby.maps.at(lobby.maps.context, index))
            select.names.emplace_back(name);
    std::sort(select.names.begin(), select.names.end());
    panel_fill_list(panel, "MAPNAMES", select.names);
    for (std::size_t row = 0; row < select.names.size(); ++row)
        if (select.names[row] == select.previous) {
            panel_select_list_row(panel, "MAPNAMES", static_cast<int32_t>(row));
            break;
        }
    mapselect_preview(lobby, select, panel);
    return true;
}

void mapselect_preview(Lobby& lobby, MapSelect& select, Panel& panel) noexcept {
    const auto* list = panel_control(panel, "MAPNAMES");
    const auto row = list != nullptr ? list->list_selection : -1;
    const bool chosen =
        row >= 0 && static_cast<std::size_t>(row) < select.names.size() &&
        lobby.maps.select != nullptr &&
        lobby.maps.select(lobby.maps.context, select.names[static_cast<std::size_t>(row)].c_str());
    panel_set_active(panel, "MAPPIC", chosen);
    if (chosen)
        map_summary_update(lobby, panel);
}

bool mapselect_handle_event(Lobby& lobby, MapSelect& select, Panel& panel) noexcept {
    if (panel.selected == kNoControl)
        return false;
    if (panel_selected_is(panel, "MAPNAMES") || panel_selected_is(panel, "LOAD")) {
        const bool commit = panel_selected_is(panel, "LOAD");
        panel.selected = kNoControl;
        play(lobby, "Multi");
        mapselect_preview(lobby, select, panel);
        auto& mine = local_info(lobby);
        std::snprintf(
            mine.map_name, sizeof(mine.map_name), "%s", lobby.maps.name(lobby.maps.context)
        );
        mine.map_hash =
            lobby.maps.content_hash != nullptr ? lobby.maps.content_hash(lobby.maps.context) : 0;
        lobby_send_player_info(lobby);
        notify(lobby, 5);
        lobby_publish_session(lobby);
        for (int32_t slot = 0; slot < kSlotCount; ++slot)
            if (!local_or_computer(slot_player(lobby, slot)))
                info(lobby, slot).options &= static_cast<uint16_t>(~option::ready);
        return commit;
    }
    if (panel_selected_is(panel, "PREVMENU")) {
        panel.selected = kNoControl;
        play(lobby, "Previous");
        if (lobby.maps.select != nullptr)
            (void)lobby.maps.select(lobby.maps.context, select.previous.c_str());
        lobby_send_player_info(lobby);
        return true;
    }
    panel.selected = kNoControl;
    return false;
}

// ---------------------------------------------------------------------------
// CONTROL / ALLIES

void control_update(Lobby& lobby, Panel& panel) noexcept {
    const auto options = local_info(lobby).options;
    panel_set_stage(panel, "WATCHING", (options & 0xff) >> 7);
    panel_set_stage(panel, "GAMEOPEN", (options & option::game_closed) == 0 ? 1 : 0);
    panel.dirty = true;
}

bool control_handle_event(Lobby& lobby, Panel& panel, uint8_t* confirm) noexcept {
    *confirm = kNoSlot;
    if (panel.selected == kNoControl)
        return false;
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        char name[32];
        std::snprintf(name, sizeof(name), "LIVEPLYR%d", static_cast<int>(slot));
        if (panel_selected_is(panel, name)) {
            panel.selected = kNoControl;
            *confirm = static_cast<uint8_t>(slot);
            return false;
        }
    }
    auto& mine = local_info(lobby);
    if (panel_selected_is(panel, "WATCHING")) {
        mine.options ^= option::watching_allowed;
        play(lobby, "Options");
        control_update(lobby, panel);
        lobby_send_player_info(lobby);
        panel.selected = kNoControl;
        return false;
    }
    if (panel_selected_is(panel, "OK")) {
        panel.selected = kNoControl;
        lobby_publish_session(lobby);
        play(lobby, "Options");
        if ((mine.options & option::watching_allowed) == 0)
            for (int32_t slot = 0; slot < kSlotCount; ++slot) {
                auto& player = slot_player(lobby, slot);
                if (player.in_use != 0 && player.status == kSlotRemote &&
                    (info(lobby, slot).options & option::watcher) != 0)
                    lobby_reject(lobby, player.player_id, kRejectWatching);
            }
        return true;
    }
    panel.selected = kNoControl;
    return false;
}

bool control_open(Lobby& lobby, Panel& panel) noexcept {
    if ((local_info(lobby).options & option::watcher) != 0)
        return false;
    allies_update_rows(lobby, panel, true);
    control_update(lobby, panel);
    return true;
}

void allies_update_indicators(Lobby& lobby, Panel& panel) noexcept {
    const auto local = lobby.game->local_player_index;
    auto& me = local_player(lobby);
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if (player.in_use != 0 && (info(lobby, slot).options & option::watcher) != 0)
            continue;
        if (!slot_active(player) || !slot_participating(player) || slot == local ||
            info(lobby, slot).color == kNoColor)
            continue;
        char name[32];
        std::snprintf(name, sizeof(name), "LIVEALLY%d", static_cast<int>(slot));
        panel_set_stage(panel, name, lobby_player_allied_back(me)[slot] << 1 | me.alliance[slot]);
    }
    panel.dirty = true;
}

void allies_update_rows(Lobby& lobby, Panel& panel, bool skip_local) noexcept {
    const auto local = lobby.game->local_player_index;
    auto& me = local_player(lobby);
    const bool watching_started = (lobby.game->session_flags & kNetFlagGameStarted) != 0;
    int32_t row = 0;
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        for (const char* pattern : {"PLAYER%d", "LOGO%d", "ALLY%d", "TEAMICONS%d"})
            if (auto* control = control_at(panel, pattern, slot))
                control->active = 0;
    }
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        const bool watcher =
            player.in_use != 0 && (info(lobby, slot).options & option::watcher) != 0;
        if (watcher || !slot_active(player) || (slot == local && skip_local) ||
            (watching_started && !slot_participating(player)) ||
            info(lobby, slot).color == kNoColor)
            continue;
        set_text(panel, "PLAYER%d", row, name_of(player));
        if (auto* line = control_at(panel, "PLAYER%d", row)) {
            line->active = 1;
            char link[32];
            std::snprintf(link, sizeof(link), "LIVEPLYR%d", static_cast<int>(slot));
            set_control_link(*line, link);
        }
        if (auto* ally = control_at(panel, "ALLY%d", row)) {
            const bool shown = slot_participating(player) && !local_or_computer(player) &&
                               !slot_remote_defeated(lobby, player) && slot_participating(me);
            if (shown)
                ally->active = 1;
            char link[32];
            std::snprintf(link, sizeof(link), "LIVEALLY%d", static_cast<int>(slot));
            set_control_link(*ally, link);
            if (lobby_player_team(player) == lobby_player_team(me) &&
                lobby_player_team(player) != kNoTeam)
                ally->grayed = true;
        }
        if (auto* team = control_at(panel, "TEAMICONS%d", row)) {
            team->active = 1;
            team->grayed = !(local_or_computer(player) && !watching_started);
        }
        if (auto* logo = control_at(panel, "LOGO%d", row)) {
            logo->active = 1;
            logo->stage = info(lobby, slot).color;
        }
        ++row;
    }
    panel.dirty = true;
}

bool allies_handle_event(Lobby& lobby, Panel& panel) noexcept {
    if (panel.selected == kNoControl)
        return true;
    auto& me = local_player(lobby);
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        char name[32];
        std::snprintf(name, sizeof(name), "LIVEALLY%d", static_cast<int>(slot));
        auto& player = slot_player(lobby, slot);
        if (!panel_selected_is(panel, name) || !slot_active(player))
            continue;
        play(lobby, "Options");
        const auto value = static_cast<uint8_t>(me.alliance[slot] ^ 1U);
        me.alliance[slot] = value;
        lobby_set_alliance(lobby, me, player, value, false);
        char line[96];
        std::snprintf(
            line,
            sizeof(line),
            " %s %s",
            value == 0 ? "broke alliance with" : "allied with",
            name_of(player).c_str()
        );
        lobby_say(lobby, me, line);
        allies_update_indicators(lobby, panel);
    }
    if (panel_selected_is(panel, "VICTORY")) {
        play(lobby, "Options");
        panel.selected = kNoControl;
        return false;
    }
    if (panel_selected_is(panel, "OK")) {
        play(lobby, "Options");
        auto& mine = local_info(lobby);
        const auto before = (mine.status & status::allied_victory) != 0;
        const auto* victory = panel_control(panel, "VICTORY");
        const bool on = victory != nullptr && (victory->stage & 1U) != 0;
        mine.status = static_cast<uint16_t>(
            (mine.status & ~status::allied_victory) | (on ? status::allied_victory : 0)
        );
        if (before != on)
            lobby_send_player_info(lobby);
        panel.selected = kNoControl;
        return true;
    }
    panel.selected = kNoControl;
    return false;
}

void allies_open(Lobby& lobby, Panel& panel) noexcept {
    int32_t allies = 0;
    int32_t teams = 0;
    for (int32_t index = 1; index < panel.count; ++index) {
        auto& control = panel.controls[static_cast<std::size_t>(index)];
        char replacement[32];
        if (control_name(control) == "ALLYx") {
            std::snprintf(replacement, sizeof(replacement), "ALLY%d", static_cast<int>(allies++));
            set_control_name(control, replacement);
        } else if (control_name(control) == "TEAMICONSx") {
            std::snprintf(
                replacement, sizeof(replacement), "TEAMICONS%d", static_cast<int>(teams++)
            );
            set_control_name(control, replacement);
        }
    }
    allies_update_rows(lobby, panel, false);
    allies_update_indicators(lobby, panel);
    lobby_update_team_icons(lobby, panel);
    auto& me = local_player(lobby);
    const auto& mine = local_info(lobby);
    panel_set_stage(panel, "VICTORY", (mine.status >> 1) & 1U);
    const bool grayed = team_member_count(lobby, lobby_player_team(me)) >= 2 ||
                        (mine.options & option::watcher) != 0;
    panel_set_grayed(panel, "VICTORY", grayed);
}

} // namespace oa::ui::frontend_multiplayer
