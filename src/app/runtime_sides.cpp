// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The SIDEDATA side table, the player records of an offline match, and the
// viewpoint side of the deathmatch commander respawn.
#include "oa/app/runtime.hpp"

#include "oa/app/asset_files.hpp"
#include "oa/sim/scenario/commander_rules.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/sim/selection.hpp"
#include "oa/present/world_renderer/world_camera.hpp"
#include "oa/ui/campaign/single_player.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {
namespace {

constexpr uint32_t kRespawnTickLimit = 30 * 20;
// Start storage the game grants the respawned commander's player from the
// no-player record's player-info block, which holds no resources.
constexpr float kRespawnStorageFloor = 200.0F;
// Message box width the watch-mode notice opens with.
constexpr int32_t kWatchMessageWidth = 500;

// The player's first live unit of its side's commander type, or null.
const oa::Unit* live_side_commander(oa::World& world, uint8_t index) {
    oa::Player* player = oa::world_player(&world, index);
    const oa::PlayerSetupInfo* info =
        player != nullptr ? oa::world_player_info(&world, player) : nullptr;
    if (info == nullptr || info->side >= OA_SIDE_COUNT)
        return nullptr;
    uint32_t count = 0;
    const oa::Unit* units = oa::world_player_units(&world, player, &count);
    for (uint32_t i = 0; i < count; ++i) {
        const oa::UnitDef* def = oa::world_unit_def_of(&world, &units[i]);
        if ((units[i].flags & OA_UNIT_FLAG_LIVE) != 0 && def != nullptr &&
            std::strcmp(def->unit_name, world.game.sides[info->side].commander) == 0)
            return &units[i];
    }
    return nullptr;
}

} // namespace

void Runtime::load_side_table() {
    const oa::data::defs::Files files = asset_files(assets_);
    // No language variant directory, and Side.font stays null: the match HUD
    // loads its own CONSOLE.FNT and nothing reads the handle.
    if (!oa::data::defs::load_side_data(&files, &side_table_, nullptr, nullptr) &&
        side_table_.error[0] != '\0')
        throw std::runtime_error(side_table_.error);
    if (side_table_.count == 0)
        throw std::runtime_error("gamedata/sidedata.tdf contains no side definitions");
    skirmish_ui_.side_count = static_cast<int32_t>(side_table_.count);
}

std::vector<std::string> Runtime::saved_game_side_names() const {
    namespace campaign = oa::ui::campaign;
    static_assert(sizeof(oa::Side::name) == campaign::kSideNameBytes);
    char names[OA_SIDE_COUNT][campaign::kSideNameBytes] = {};
    const auto count = std::min<uint32_t>(side_table_.count, OA_SIDE_COUNT);
    for (uint32_t side = 0; side < count; ++side)
        std::memcpy(names[side], side_table_.sides[side].name, campaign::kSideNameBytes);
    // The dialogs' side list: the names one after another, each ended by a
    // NUL, and one more NUL after the last.
    char list[OA_SIDE_COUNT * campaign::kSideNameBytes + 1] = {};
    (void)campaign::build_side_name_list(names, count, list, sizeof list);
    std::vector<std::string> shown;
    for (const char* name = list; *name != '\0'; name += std::strlen(name) + 1)
        shown.emplace_back(name);
    return shown;
}

void Runtime::bind_player_records(oa::World& world) {
    std::copy(std::begin(side_table_.sides), std::end(side_table_.sides), world.game.sides);
    world.game.side_count = side_table_.count;
    for (uint32_t slot = 0; slot < OA_PLAYER_RECORD_COUNT; ++slot) {
        oa::PlayerSetupInfo& info = world.player_info[slot];
        info = {};
        info.color = static_cast<uint8_t>(slot);
        oa::world_player_record(&world, slot)->info = oa::oa_ref_from_index(slot);
        if (slot == OA_PLAYER_COUNT)
            continue;
        const auto& setup = skirmish_settings_.slots[slot];
        if (setup.controller == entry::controller::disabled)
            continue;
        info.side = static_cast<uint8_t>(setup.side);
        info.color = static_cast<uint8_t>(setup.color);
        info.state = static_cast<uint8_t>(setup.controller);
    }
    world.player_info[match_local_player_].options |= OA_SETUP_OPTION_STARTED;
}

void Runtime::bind_respawn_view() {
    match_->respawn = {
        this,
        [](void* context) { static_cast<Runtime*>(context)->reset_sight_presentation(true); },
        [](void* context) { static_cast<Runtime*>(context)->select_side_commander(); },
    };
    match_->watch = {
        this, [](void* context, oa::sim::match_runtime::Match::WatchNotice notice) {
            auto& self = *static_cast<Runtime*>(context);
            self.reset_sight_presentation(true);
            if (notice == oa::sim::match_runtime::Match::WatchNotice::continue_prompt) {
                auto ctx = self.screen_context();
                oa::ui::frontend_dialogs::open_continue_watching(
                    &ctx, &self, [](void* context, bool keep) {
                        auto& runtime = *static_cast<Runtime*>(context);
                        if (runtime.match_ != nullptr) {
                            runtime.match_->choose_continue_watching(keep);
                            const auto& extension = runtime.extension_;
                            if (keep && extension.match_event != nullptr)
                                extension.match_event(
                                    extension.context, runtime, MatchEvent::watching_kept
                                );
                        }
                    }
                );
            }
            if (notice == oa::sim::match_runtime::Match::WatchNotice::hosting_computers)
                self.show_frontend_message(
                    "You are placed in watch mode because you are hosting AI players which are "
                    "still alive.  If you exit, they will be terminated.",
                    kWatchMessageWidth,
                    1,
                    1
                );
        }
    };
}

void Runtime::reset_sight_presentation(bool refill_mapped) {
    if (refill_mapped)
        radar_explored_.clear();
    radar_state_.reset_sight = true;
}

void Runtime::reset_match_sight(bool refill_mapped) {
    if (!match_ || altitude_sight_blocked_)
        return;
    match_->reset_sight_buffers(refill_mapped);
    reset_sight_presentation(refill_mapped);
}

void Runtime::select_side_commander() {
    if (!match_)
        return;

    struct Finder {
        Runtime* runtime{};
        bool cleared{};
    } finder{this};

    oa::sim::selection::Hooks hooks{};
    hooks.context = &finder;
    hooks.stop_follow = [](void* context) {
        Runtime& runtime = *static_cast<Finder*>(context)->runtime;
        oa::present::world_renderer::camera_stop_follow(runtime.match_->state().game);
        runtime.stop_match_tracking();
    };
    // The camera has no glide target and moves at once.
    hooks.center_camera = [](void* context, const FixedVec3& position, bool) {
        Runtime& runtime = *static_cast<Finder*>(context)->runtime;
        runtime.match_camera_x_ =
            oa::present::world_renderer::world_screen_x(position) - runtime.visible_map_width() / 2;
        runtime.match_camera_z_ = oa::present::world_renderer::world_screen_y(position) -
                                  runtime.visible_map_height() / 2;
    };
    hooks.reset_command = [](void* context) {
        static_cast<Finder*>(context)->runtime->reset_match_command();
    };
    hooks.selection_cleared = [](void* context) {
        auto& finder = *static_cast<Finder*>(context);
        finder.cleared = true;
        finder.runtime->selected_match_unit_ = 0;
    };
    oa::World& world = match_->state();
    oa::sim::selection::find_commander(world, true, hooks);
    if (!finder.cleared)
        return;
    for (uint32_t slot = 1; slot < world.unit_slot_count; ++slot)
        if ((world.units[slot].flags & OA_UNIT_FLAG_SELECTED) != 0)
            adopt_selection(static_cast<uint16_t>(slot));
    apply_match_hud_for_selection();
}

void Runtime::select_and_follow_commander(bool add) {
    if (!match_)
        return;
    oa::World& world = match_->state();
    auto& categories = unit_table_.tables.categories;
    const auto* mask = oa::data::defs::category_registry_find_or_add(
        &categories, oa::sim::selection::ctrl_c_category
    );
    if (mask == nullptr)
        return;
    oa::sim::selection::Hooks hooks{};
    hooks.context = this;
    hooks.reset_command = [](void* context) {
        static_cast<Runtime*>(context)->reset_match_command();
    };
    oa::sim::selection::apply_type_mask_selection(world, *mask, add, hooks);
    world.game.follow_unit = match_tracking_ ? oa::oa_unit_ref_from_slot(tracked_match_unit_) : 0u;
    oa::sim::selection::follow_commander(world, categories);
    if (world.game.follow_unit != 0u) {
        tracked_match_unit_ =
            static_cast<uint16_t>(oa::oa_unit_slot_from_ref(world.game.follow_unit));
        match_tracking_ = true;
    }
    selected_match_unit_ = 0;
    for (uint32_t slot = 1; slot < world.unit_slot_count; ++slot)
        if ((world.units[slot].flags & OA_UNIT_FLAG_SELECTED) != 0)
            adopt_selection(static_cast<uint16_t>(slot));
    apply_match_hud_for_selection();
}

void Runtime::check_player_records(std::string_view context) {
    const std::string label(context);
    if (!match_)
        throw std::runtime_error(label + " player check needs a match");
    oa::World& world = match_->state();
    if (world.game.side_count < 2 || std::strcmp(world.game.sides[0].commander, "ARMCOM") != 0 ||
        std::strcmp(world.game.sides[1].commander, "CORCOM") != 0)
        throw std::runtime_error(label + " player check: Game.sides lacks ARMCOM and CORCOM");
    uint32_t bound = 0;
    for (uint32_t slot = 0; slot < OA_PLAYER_COUNT; ++slot) {
        const auto& setup = skirmish_settings_.slots[slot];
        if (setup.controller == entry::controller::disabled)
            continue;
        const oa::PlayerSetupInfo* info = oa::world_player_info(&world, &world.game.players[slot]);
        if (info == nullptr || info->side != setup.side || info->color != setup.color ||
            info->state != setup.controller)
            throw std::runtime_error(
                label + " player check: Player.info of player " + std::to_string(slot) +
                " does not carry its slot"
            );
        ++bound;
    }
    if (bound < 2)
        throw std::runtime_error(label + " player check: fewer than two players are bound");
    std::cout << label << " player check: Game.sides " << world.game.sides[0].commander << "/"
              << world.game.sides[1].commander << ", Player.info bound for " << bound
              << " players\n";
}

void Runtime::check_deathmatch_respawn() {
    exercise_click(skirmish::resource_name(skirmish::Button::start));
    if (screen_ != Screen::match || !match_)
        throw std::runtime_error("respawn check: Start did not enter a match");
    check_player_records("skirmish");
    oa::World& world = match_->state();
    const uint8_t local = match_local_player_;
    oa::Player& player = world.game.players[local];
    if (sim::scenario::host_player_index(world) != OA_PLAYER_COUNT)
        throw std::runtime_error("respawn check: the skirmish seats a host");
    world.game.session_rules = static_cast<int32_t>(sim::scenario::CommanderRule::deathmatch);
    clear_local_selection();
    match_->destroy_player_units(local);
    bool defeated = false;
    const oa::Unit* commander = nullptr;
    for (uint32_t step = 0; step < kRespawnTickLimit && commander == nullptr; ++step) {
        ++match_timing_.tick;
        match_->simulation().tick = match_timing_.tick;
        match_->tick();
        if (player.unit_count == 0)
            defeated = true;
        else if (defeated)
            commander = live_side_commander(world, local);
    }
    if (!defeated)
        throw std::runtime_error("respawn check: the local units survived the sweep");
    if (commander == nullptr || match_->outcome() != sim::scenario::Outcome::ongoing)
        throw std::runtime_error("respawn check: no commander respawned under the deathmatch rule");
    if (selected_match_unit_ != commander->id || (commander->flags & OA_UNIT_FLAG_SELECTED) == 0)
        throw std::runtime_error("respawn check: the respawned commander is not selected");
    const int32_t centre_x = oa::present::world_renderer::world_screen_x(commander->position);
    const int32_t centre_z = oa::present::world_renderer::world_screen_y(commander->position);
    if (match_camera_x_ != centre_x - visible_map_width() / 2 ||
        match_camera_z_ != centre_z - visible_map_height() / 2)
        throw std::runtime_error("respawn check: the camera is not centred on the new commander");
    if (player.shared_metal_storage != kRespawnStorageFloor ||
        player.shared_energy_storage != kRespawnStorageFloor)
        throw std::runtime_error(
            "respawn check: the start storage did not come from the no-player record"
        );
    std::cout << "respawn check: " << oa::world_unit_def_of(&world, commander)->unit_name
              << " respawned at tick " << match_timing_.tick << " (" << centre_x << ", " << centre_z
              << "), selected and centred, start storage " << player.shared_metal_storage << "\n";
    return_to_skirmish_menu();
}

} // namespace oa::app
