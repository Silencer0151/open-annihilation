// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Presses on the minimap (the radar in the side column) against the game's
// own rule: the cursor each shows, the orders the press gives the
// selection, the selection, the armed command and the view after it, in
// both interface types, for every armed command and the default order,
// over open ground, the dots of own, allied and enemy units and features on
// mapped and unmapped ground, through synthetic SDL input on a skirmish.
#include "oa/app/runtime.hpp"
#include "oa/core/map_plot.h"
#include "oa/data/mission_types.hpp"
#include "oa/sim/feature_runtime.hpp"
#include "oa/sim/map_runtime/feature_defs.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {
namespace {

namespace input = oa::sim::gameplay_input;
using Point = oa::sim::ground_orders::Point;

/// Map pixels between the units and points the check presses on, at the
/// least; more on a map whose radar pixel covers more.
constexpr int32_t kApart = 160;
/// Radar pixels of the game's screen between any two points the check
/// presses on: the radar picks a dot within 2 of the pointer.
constexpr int32_t kRadarPixelsApart = 8;
/// Map pixels between the two Peewees moved as a group, which keeps each
/// within the group's reach of its centre.
constexpr int32_t kPairApart = 48;
/// Map pixels the units keep from the map's edges.
constexpr int32_t kEdgeMargin = 96;
/// A wreck, which can be reclaimed and resurrected.
constexpr std::string_view kWreckName = "armsolar_dead";
/// Map pixels a sight cell spans, the most a sight cell sits north of the
/// ground it covers (half the tallest ground), and the cells around a
/// feature marked mapped or never mapped.
constexpr int32_t kSightCellPixels = 32;
constexpr int32_t kHalfTallestGround = 128;
constexpr int32_t kFogMarginCells = 2;
/// Map cells either side of the open ground's middle a press there may
/// land on.
constexpr int32_t kOpenGroundCells = 2;
/// The most orders the check reads from one unit's queue.
constexpr std::size_t kQueueRead = 8;

/// Fails the check, saying what went wrong.
[[noreturn]] void fail(std::string_view what) {
    throw std::runtime_error("radar order check: " + std::string(what));
}

/// Fails the check, saying what went wrong, unless `ok` holds.
void require(bool ok, std::string_view what) {
    if (!ok)
        fail(what);
}

/// Returns the order kind of an order-table name.
uint8_t kind_of(std::string_view name) {
    const auto kind = oa::data::mission_types::index_for_name(name);
    if (kind == oa::data::mission_types::unknown_mission)
        fail("no order is named " + std::string(name));
    return kind;
}

/// Names an order kind for the check's output.
std::string kind_name(uint8_t kind) {
    const auto names = oa::data::mission_types::registered_names();
    return kind < names.size() ? std::string(names[kind]) : std::to_string(kind);
}

/// Writes a 16.16 point as whole map pixels.
std::string describe(const Point& point) {
    return "(" + std::to_string(point[0] >> 16) + "," + std::to_string(point[1] >> 16) + "," +
           std::to_string(point[2] >> 16) + ")";
}

/// One order a unit should hold at its queue's head.
struct Expected {
    std::string kind;           ///< the order's name in the order table
    uint16_t target{};          ///< the unit it names, 0 for none
    std::optional<Point> point; ///< its point, when the order takes one
};

} // namespace

void Runtime::check_radar_orders() {
    namespace orders = oa::sim::match_runtime;
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        fail("needs the SDL renderer");
    // A skirmish of three: the viewer, an enemy, and a player allied with
    // the viewer both ways.
    exercise_click(menu::resource_name(menu::Button::single_player));
    exercise_click(entry::resource_name(entry::Button::skirmish));
    state_.player_count = 3;
    require(map_player_capacity() >= 3, "the skirmish map lacks three start positions");
    auto& seats = skirmish_settings_.slots;
    seats[2] = seats[1];
    seats[2].controller = entry::controller::computer;
    for (int32_t color = 0; color < 10; ++color)
        if (color != seats[0].color && color != seats[1].color) {
            seats[2].color = color;
            break;
        }
    seats[0].alliance = 1;
    seats[1].alliance = entry::unassigned_alliance;
    seats[2].alliance = 1;
    skirmish_settings_.slot_count = std::max(skirmish_settings_.slot_count, 3);
    exercise_click(skirmish::resource_name(skirmish::Button::start));
    require(screen_ == Screen::match && match_ != nullptr, "Start did not enter a match");
    apply_output_mode();
    auto& world = match_->state();
    auto& game = world.game;
    auto& slots = match_->world().slots;
    const auto local = match_local_player_;
    uint16_t commander = 0;
    std::vector<uint8_t> others;
    for (const auto& slot : slots) {
        if (slot.unit == nullptr || slot.record.type_index == 0)
            continue;
        if (slot.record.owner_index == local) {
            if (commander == 0)
                commander = slot.unit_index;
        } else if (std::find(others.begin(), others.end(), slot.record.owner_index) == others.end())
            others.push_back(slot.record.owner_index);
    }
    require(commander != 0 && others.size() >= 2, "found no commander, enemy and third player");
    const auto allied = [&](uint8_t player) {
        return game.players[local].alliance[player] != 0 &&
               game.players[player].alliance[local] != 0;
    };
    const uint8_t ally_player = allied(others[0]) ? others[0] : others[1];
    const uint8_t enemy_player = ally_player == others[0] ? others[1] : others[0];
    require(
        allied(ally_player) && !allied(enemy_player),
        "the third player is not allied with the viewer both ways"
    );

    // The layout: the units on a grid from the commander toward the map's
    // middle, far enough apart that each has its own dot on the radar.
    render_match_surface();
    const auto radar_source_left = game_screen_point(
        static_cast<float>(radar_picture_.x), static_cast<float>(radar_picture_.y)
    );
    const auto radar_source_right = game_screen_point(
        static_cast<float>(radar_picture_.x + radar_picture_.width),
        static_cast<float>(radar_picture_.y)
    );
    const int32_t radar_source_width = radar_source_right.x - radar_source_left.x;
    require(radar_source_width > 0 && radar_map_w_ > 0, "the radar has no picture");
    const int32_t apart = std::max(
        kApart, kRadarPixelsApart * ((radar_map_w_ + radar_source_width - 1) / radar_source_width)
    );
    const auto map_width = static_cast<int32_t>(selected_tnt_->tile_width * 32U);
    const auto map_height = static_cast<int32_t>(selected_tnt_->tile_height * 32U);
    const auto map_x = [&](uint16_t id) {
        return static_cast<int32_t>(slots[id].unit->position[0] >> 16);
    };
    const auto map_z = [&](uint16_t id) {
        return static_cast<int32_t>(slots[id].unit->position[2] >> 16);
    };
    const int32_t way_x = map_x(commander) < map_width / 2 ? 1 : -1;
    const int32_t way_z = map_z(commander) < map_height / 2 ? 1 : -1;
    const int32_t first_x = map_x(commander) + way_x * apart;
    const int32_t first_z = map_z(commander) + way_z * apart;
    // The middle of grid place (column, row).
    const auto place = [&](int32_t column, int32_t row) {
        return std::array<int32_t, 2>{
            std::clamp(first_x + way_x * column * apart, kEdgeMargin, map_width - kEdgeMargin),
            std::clamp(first_z + way_z * row * apart, kEdgeMargin, map_height - kEdgeMargin)
        };
    };
    const auto spawn = [&](std::string_view name, uint8_t owner, std::array<int32_t, 2> at) {
        const auto type = oa::sim::unit_spawn::find_type_index(spawn_type_names_, name);
        if (type == 0)
            fail("lacks " + std::string(name));
        oa::sim::unit_spawn::Request request;
        request.player = owner;
        request.type = type;
        request.finished = true;
        request.state = kGroundOccupancyState;
        request.position = {
            static_cast<uint32_t>(at[0]) << 16,
            static_cast<uint32_t>(match_->map_height(
                static_cast<uint32_t>(at[0]) << 16, static_cast<uint32_t>(at[1]) << 16
            )) << 16,
            static_cast<uint32_t>(at[1]) << 16
        };
        auto* slot = match_->create(request);
        if (slot == nullptr || slot->unit == nullptr)
            fail("could not spawn " + std::string(name));
        slot->unit->object_present = true;
        // Held fire, so that no unit fights while the check runs.
        slot->record.flags &= ~OA_UNIT_FLAG_FIRE_ORDER_MASK;
        return slot->unit_index;
    };
    // The selected units: two Peewees side by side, a construction kbot, an
    // air transport and a building; the units pressed on: an own Peewee, an
    // own damaged Peewee, an ally's Peewee and a damaged enemy A.K.
    const auto pair_at = place(0, 0);
    const auto peewee = spawn("ARMPW", local, pair_at);
    const auto second_peewee = spawn("ARMPW", local, {pair_at[0] + way_x * kPairApart, pair_at[1]});
    const auto kbot = spawn("ARMCK", local, place(1, 0));
    const auto transport = spawn("ARMATLAS", local, place(2, 0));
    const auto building = spawn("ARMSOLAR", local, place(3, 0));
    const auto own = spawn("ARMPW", local, place(0, 1));
    const auto damaged = spawn("ARMPW", local, place(1, 1));
    const auto ally = spawn("ARMPW", ally_player, place(2, 1));
    const auto enemy = spawn("CORAK", enemy_player, place(3, 1));
    const auto halve_health = [&](uint16_t id) {
        auto& unit = *slots[id].unit;
        unit.health = static_cast<int16_t>(unit.health / 2);
    };
    halve_health(damaged);
    halve_health(enemy);
    // A resurrector, where the installation has one.
    uint16_t resurrector = 0;
    for (const auto* name : {"ARMRECTR", "CORNECRO"})
        if (resurrector == 0 && oa::sim::unit_spawn::find_type_index(spawn_type_names_, name) != 0)
            resurrector = spawn(name, local, place(3, 2));
    std::vector<uint16_t> watched{
        commander, peewee, second_peewee, kbot, transport, building, own, damaged
    };
    if (resurrector != 0)
        watched.push_back(resurrector);

    // Two wrecks: one on ground the viewer has mapped, one on ground it
    // never mapped.
    const auto wreck_index =
        oa::sim::map_runtime::find_feature_index(feature_table_, kWreckName.data());
    require(wreck_index < world.feature_def_count, "found no " + std::string(kWreckName));
    const auto& wreck = world.feature_defs[wreck_index];
    const auto open_cells = [&](int32_t cell_x, int32_t cell_z) {
        for (int32_t row = -1; row <= wreck.footprint_z; ++row)
            for (int32_t column = -1; column <= wreck.footprint_x; ++column) {
                const auto* plot = oa::world_plot(&world, cell_x + column, cell_z + row);
                if (plot == nullptr || plot->feature != oa::sim::spatial_state::no_feature ||
                    plot->ground_unit != 0)
                    return false;
            }
        return true;
    };
    const auto place_wreck = [&](std::array<int32_t, 2> at) {
        const int32_t centre_x = at[0] / OA_MAP_CELL_PIXELS;
        const int32_t centre_z = at[1] / OA_MAP_CELL_PIXELS;
        for (int32_t ring = 0; ring < 6; ++ring)
            for (int32_t dz = -ring; dz <= ring; ++dz)
                for (int32_t dx = -ring; dx <= ring; ++dx) {
                    const auto cell_x = centre_x + dx;
                    const auto cell_z = centre_z + dz;
                    if (open_cells(cell_x, cell_z) &&
                        console_place_feature(kWreckName.data(), cell_x, cell_z))
                        return std::array<int32_t, 2>{cell_x, cell_z};
                }
        fail("found no open ground for " + std::string(kWreckName));
    };
    const auto mapped_wreck = place_wreck(place(0, 2));
    const auto unmapped_wreck = place_wreck(place(1, 2));
    auto& sight = match_->sight_mutable();
    const auto viewer_bit = static_cast<uint16_t>(1U << sight.viewpoint_player);
    const auto map_around = [&](std::array<int32_t, 2> cell, bool mapped) {
        const auto first_column =
            std::max(0, (cell[0] - kFogMarginCells) * OA_MAP_CELL_PIXELS / kSightCellPixels);
        const auto last_column = std::min(
            sight.width - 1,
            (cell[0] + wreck.footprint_x + kFogMarginCells) * OA_MAP_CELL_PIXELS / kSightCellPixels
        );
        const auto first_row = std::max(
            0,
            ((cell[1] - kFogMarginCells) * OA_MAP_CELL_PIXELS - kHalfTallestGround) /
                kSightCellPixels
        );
        const auto last_row = std::min(
            sight.height - 1,
            (cell[1] + wreck.footprint_z + kFogMarginCells) * OA_MAP_CELL_PIXELS / kSightCellPixels
        );
        for (auto row = first_row; row <= last_row; ++row)
            for (auto column = first_column; column <= last_column; ++column) {
                auto& bits =
                    sight.player_bits[static_cast<std::size_t>(row * sight.width + column)];
                bits = mapped ? static_cast<uint16_t>(bits | viewer_bit)
                              : static_cast<uint16_t>(bits & ~viewer_bit);
            }
    };
    map_around(mapped_wreck, true);
    map_around(unmapped_wreck, false);
    // Where a Reclaim of a wreck goes: the middle of its footprint, on the
    // ground where the wreck stands.
    const auto wreck_middle = [&](std::array<int32_t, 2> cell) {
        const auto stands = oa::sim::feature_runtime::feature_center(
            world, static_cast<int16_t>(cell[0]), static_cast<int16_t>(cell[1]), wreck
        );
        return Point{stands.x, stands.y, stands.z};
    };
    const auto open_ground = place(2, 2);

    // Synthetic SDL input, with the modifiers held for the event.
    bool running = true;
    const auto send = [&](SDL_EventType type, uint8_t button, float x, float y, SDL_Keymod mods) {
        float window_x = 0.0F;
        float window_y = 0.0F;
        if (!frame_to_window(sdl_.renderer, x, y, &window_x, &window_y))
            fail(SDL_GetError());
        SDL_Event event{};
        event.type = type;
        if (type == SDL_EVENT_MOUSE_MOTION) {
            event.motion.windowID = SDL_GetWindowID(sdl_.window);
            event.motion.x = window_x;
            event.motion.y = window_y;
        } else {
            event.button.windowID = SDL_GetWindowID(sdl_.window);
            event.button.button = button;
            event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.clicks = 1;
            event.button.x = window_x;
            event.button.y = window_y;
        }
        SDL_SetModState(mods);
        dispatch_event(event, running);
        SDL_SetModState(SDL_KMOD_NONE);
    };
    // The radar's dots, built from the units on it: the units of the other
    // players stand there as radar contacts.
    const auto compose_dots = [&] {
        for (const auto id : {ally, enemy})
            slots[id].record.flags |= OA_UNIT_FLAG_RADAR_CONTACT;
        compose_radar_final();
    };
    // The canvas point of a unit's dot.
    const auto dot_of = [&](uint16_t id) {
        compose_dots();
        for (uint32_t index = 0; index < radar_state_.hot_unit_count; ++index)
            if (radar_state_.hot_units[index].unit_id == id) {
                const auto& hot = radar_state_.hot_units[index];
                const auto point =
                    oa::ui::display_layout::source_to_canvas(match_layout_, hot.x, hot.y);
                return std::pair{
                    static_cast<float>(point.x) + 0.5F, static_cast<float>(point.y) + 0.5F
                };
            }
        fail("unit " + std::to_string(id) + " has no dot on the radar");
    };
    // The canvas point on the radar whose ground, nearest the middle of the
    // given cells, lies in them with no dot under the point. The radar's
    // ground is the battlefield's under the map pixel its pixel stands for.
    const auto radar_point_on = [&](int32_t first_cell_x,
                                    int32_t first_cell_z,
                                    int32_t cells_x,
                                    int32_t cells_z,
                                    std::string_view what) {
        compose_dots();
        const int32_t aim_x = (first_cell_x * 2 + cells_x) * OA_MAP_CELL_PIXELS / 2;
        const int32_t aim_z = (first_cell_z * 2 + cells_z) * OA_MAP_CELL_PIXELS / 2;
        std::optional<std::pair<float, float>> best;
        int64_t best_distance = 0;
        for (int32_t row = 0; row < radar_picture_.height; ++row)
            for (int32_t column = 0; column < radar_picture_.width; ++column) {
                const auto x = static_cast<float>(radar_picture_.x + column) + 0.5F;
                const auto y = static_cast<float>(radar_picture_.y + row) + 0.5F;
                const auto ground = radar_world_point(x, y);
                if (!ground)
                    continue;
                const int32_t map_px = (*ground)[0] >> 16;
                const int32_t map_pz = (*ground)[2] >> 16;
                const auto cell_x = map_px / OA_MAP_CELL_PIXELS;
                const auto cell_z = map_pz / OA_MAP_CELL_PIXELS;
                if (cell_x < first_cell_x || cell_z < first_cell_z ||
                    cell_x >= first_cell_x + cells_x || cell_z >= first_cell_z + cells_z)
                    continue;
                const int64_t dx = map_px - aim_x;
                const int64_t dz = map_pz - aim_z;
                const int64_t distance = dx * dx + dz * dz;
                if (best && distance >= best_distance)
                    continue;
                update_pointer(x, y);
                if (hovered_match_unit_ != 0)
                    continue;
                best = std::pair{x, y};
                best_distance = distance;
            }
        if (!best)
            fail("found no radar pixel over " + std::string(what) + " clear of every dot");
        return *best;
    };
    std::cout << "radar order check: map " << map_width << "x" << map_height << ", radar "
              << radar_picture_.width << "x" << radar_picture_.height << " canvas pixels ("
              << radar_source_width << " on the game's screen) for " << radar_map_w_ << "x"
              << radar_map_h_ << " map pixels; units " << apart << " map pixels apart\n";
    const auto ground_point = radar_point_on(
        open_ground[0] / OA_MAP_CELL_PIXELS - kOpenGroundCells,
        open_ground[1] / OA_MAP_CELL_PIXELS - kOpenGroundCells,
        kOpenGroundCells * 2 + 1,
        kOpenGroundCells * 2 + 1,
        "open ground"
    );
    const auto mapped_wreck_point = radar_point_on(
        mapped_wreck[0], mapped_wreck[1], wreck.footprint_x, wreck.footprint_z, "the mapped wreck"
    );
    const auto unmapped_wreck_point = radar_point_on(
        unmapped_wreck[0],
        unmapped_wreck[1],
        wreck.footprint_x,
        wreck.footprint_z,
        "the wreck on ground never mapped"
    );
    const auto ground_at = [&](std::pair<float, float> at) {
        const auto ground = radar_world_point(at.first, at.second);
        require(ground.has_value(), "a radar point has no map point");
        return *ground;
    };
    {
        const auto mapped = ground_at(mapped_wreck_point);
        const auto unmapped = ground_at(unmapped_wreck_point);
        const auto as_point = [](const Point& point) {
            return std::array<uint32_t, 3>{
                static_cast<uint32_t>(point[0]),
                static_cast<uint32_t>(point[1]),
                static_cast<uint32_t>(point[2])
            };
        };
        require(
            match_->point_mapped(as_point(mapped)) && !match_->point_mapped(as_point(unmapped)),
            "the wrecks do not lie on mapped and never-mapped ground"
        );
    }

    // What a press left behind.
    struct Outcome {
        std::vector<std::vector<orders::Match::OrderRecordView>> queues; ///< per watched unit
        std::vector<uint16_t> selected; ///< the local units selected, in slot order
        MatchCommand command{};
        uint16_t pending_build{};
        std::array<int32_t, 2> camera{};
        std::vector<std::string> sounds;
    };

    const auto outcome = [&] {
        Outcome out;
        for (const auto id : watched) {
            std::array<orders::Match::OrderRecordView, kQueueRead> records{};
            const auto count = match_->queue_records(id, false, records.data(), records.size());
            out.queues.emplace_back(records.begin(), records.begin() + count);
        }
        for (const auto& slot : slots)
            if (slot.unit != nullptr && slot.record.owner_index == local &&
                (slot.unit->flags & OA_UNIT_FLAG_SELECTED) != 0)
                out.selected.push_back(slot.unit_index);
        out.command = match_command_;
        out.pending_build = pending_build_type_;
        out.camera = {match_camera_x_, match_camera_z_};
        return out;
    };
    const auto describe_outcome = [&](const Outcome& out) {
        std::string text = "orders:";
        bool none = true;
        for (std::size_t index = 0; index < watched.size(); ++index)
            for (const auto& record : out.queues[index]) {
                text += " unit " + std::to_string(watched[index]) + " " + kind_name(record.kind);
                if (record.target != 0)
                    text += " on " + std::to_string(record.target);
                text += " at " + describe(record.point) + ";";
                none = false;
            }
        if (none)
            text += " none;";
        text += " selected:";
        for (const auto id : out.selected)
            text += " " + std::to_string(id);
        text += "; command " + std::to_string(static_cast<int>(out.command)) + "; view " +
                std::to_string(out.camera[0]) + "," + std::to_string(out.camera[1]);
        for (const auto& sound : out.sounds)
            text += "; heard " + sound;
        return text;
    };

    // One press on the radar and what it should leave.
    struct Case {
        std::string name{};
        int32_t interface_type{};
        std::vector<uint16_t> selection{}; ///< selected before the press, the first as primary
        MatchCommand command{};            ///< armed before it
        std::pair<float, float> at{};      ///< the radar point pressed
        uint8_t button = SDL_BUTTON_LEFT;
        SDL_Keymod mods = SDL_KMOD_NONE;
        std::optional<input::OrderCursor> cursor{}; ///< the cursor shown there before it
        /// What each watched unit holds after it, head first; a unit left
        /// out holds nothing.
        std::vector<std::pair<uint16_t, std::vector<Expected>>> orders{};
        std::vector<uint16_t> selected_after{}; ///< the selection after it, in slot order
        MatchCommand command_after{};
        bool view_moves = false; ///< the view goes to the point pressed
        /// Runs after the units are set up and before the pointer goes to
        /// the radar; may stand in for the press.
        std::function<void()> before{};
        /// Gives the press itself, in place of a press and release at `at`.
        std::function<void()> press{};
        std::vector<std::string> sounds{}; ///< heard, in order, when the case listens
        bool listens = false;
    };

    // The view a press moves to: the map pixel under the point in the
    // middle of the visible battlefield.
    const auto radar_view = [&](std::pair<float, float> at) {
        return std::array<int32_t, 2>{
            (static_cast<int32_t>(at.first) - radar_picture_.x) * radar_map_w_ /
                    radar_picture_.width -
                visible_map_width() / 2,
            (static_cast<int32_t>(at.second) - radar_picture_.y) * radar_map_h_ /
                    radar_picture_.height -
                visible_map_height() / 2
        };
    };
    const auto sorted = [](std::vector<uint16_t> ids) {
        std::sort(ids.begin(), ids.end());
        return ids;
    };
    // The point a group order sends one of the selection to, measured
    // before any of it is ordered.
    const auto shaped = [&](const std::vector<uint16_t>& selection,
                            input::OrderCommand command,
                            uint16_t id,
                            uint16_t target,
                            const Point& point) {
        clear_local_selection();
        for (const auto unit : selection)
            adopt_selection(unit);
        const auto centre = local_selection_centre(group_order_bound_unit(command, target));
        const auto destination = group_order_destination(centre, command, id, target, point);
        clear_local_selection();
        return destination;
    };
    const auto ground = ground_at(ground_point);
    const auto at_dot = [&](uint16_t id) { return ground_at(dot_of(id)); };
    const auto lc = input::interface_left_click;
    const auto rc = input::interface_right_click;
    const auto none = MatchCommand::none;
    using Cursor = input::OrderCursor;

    // The battlefield's canvas points where a building's site is clear and
    // where it is refused, for the build cases: the site test over the
    // battlefield decides a placement on the radar.
    const auto solar = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "ARMSOLAR");
    require(solar != 0, "lacks ARMSOLAR");
    std::optional<std::pair<float, float>> clear_site;
    std::optional<std::pair<float, float>> refused_site;
    int32_t clear_height = 0;
    const auto find_sites = [&] {
        pending_build_type_ = solar;
        center_camera_on_unit(own);
        render_match_surface();
        clear_site.reset();
        refused_site.reset();
        const auto left = static_cast<float>(match_layout_.left);
        const auto top = static_cast<float>(match_layout_.top);
        for (float y = top + 8.0F; y < top + static_cast<float>(match_layout_.battlefield_height());
             y += 8.0F)
            for (float x = left + 8.0F;
                 x < left + static_cast<float>(match_layout_.battlefield_width());
                 x += 8.0F) {
                const auto site = build_site_under(x, y);
                if (!site)
                    continue;
                if (site->legal && !clear_site) {
                    clear_site = std::pair{x, y};
                    clear_height = site->world[1];
                } else if (!site->legal && !refused_site)
                    refused_site = std::pair{x, y};
            }
        pending_build_type_ = 0;
        require(clear_site && refused_site, "found no clear and refused site on the battlefield");
    };
    find_sites();
    const auto build_point = [&](std::pair<float, float> at) {
        pending_build_type_ = solar;
        const auto site = pending_build_site(ground_at(at));
        pending_build_type_ = 0;
        require(site.has_value(), "found no site under a radar point");
        auto point = site->world;
        point[1] = clear_height;
        return point;
    };
    // The pointer rests over a battlefield site while the building is
    // placed, then goes to the radar.
    const auto rest_over = [&](std::pair<float, float> site) {
        return [&, site] {
            pending_build_type_ = solar;
            center_camera_on_unit(own);
            render_match_surface();
            send(SDL_EVENT_MOUSE_MOTION, 0, site.first, site.second, SDL_KMOD_NONE);
            render_match_surface();
        };
    };
    // An earlier move the Shift case queues behind.
    const auto first_move = map_world_point(open_ground[0] + way_x * apart / 2, open_ground[1]);
    require(first_move.has_value(), "found no ground for the first move");

    std::vector<Case> cases;
    // The default order in the left-click interface.
    cases.push_back(
        {.name = "left-click interface, default order: on open ground the Peewees move in "
                 "their shape",
         .interface_type = lc,
         .selection = {peewee, second_peewee},
         .command = none,
         .at = ground_point,
         .cursor = Cursor::move,
         .orders =
             {{peewee,
               {{"Move_Ground",
                 0,
                 shaped(
                     {peewee, second_peewee}, input::OrderCommand::default_order, peewee, 0, ground
                 )}}},
              {second_peewee,
               {{"Move_Ground",
                 0,
                 shaped(
                     {peewee, second_peewee},
                     input::OrderCommand::default_order,
                     second_peewee,
                     0,
                     ground
                 )}}}},
         .selected_after = {peewee, second_peewee},
         .command_after = none}
    );
    cases.push_back(
        {.name = "left-click interface, default order: on an own unit's dot it selects that "
                 "unit alone",
         .interface_type = lc,
         .selection = {peewee},
         .command = none,
         .at = dot_of(own),
         .cursor = Cursor::select,
         .selected_after = {own},
         .command_after = none}
    );
    cases.push_back(
        {.name = "left-click interface, default order: with Shift on an own unit's dot it adds "
                 "that unit to the selection",
         .interface_type = lc,
         .selection = {peewee},
         .command = none,
         .at = dot_of(own),
         .mods = SDL_KMOD_LSHIFT,
         .cursor = Cursor::select,
         .selected_after = {peewee, own},
         .command_after = none}
    );
    cases.push_back(
        {.name = "left-click interface, default order: on an ally's dot the Peewee moves there",
         .interface_type = lc,
         .selection = {peewee},
         .command = none,
         .at = dot_of(ally),
         .cursor = Cursor::move,
         .orders = {{peewee, {{"Move_Ground", 0, at_dot(ally)}}}},
         .selected_after = {peewee},
         .command_after = none}
    );
    cases.push_back(
        {.name = "left-click interface, default order: on an enemy's dot the Peewee attacks it "
                 "and the construction kbot reclaims it",
         .interface_type = lc,
         .selection = {peewee, kbot},
         .command = none,
         .at = dot_of(enemy),
         .cursor = Cursor::attack,
         .orders =
             {{peewee, {{"Attack_Chase", enemy, std::nullopt}}},
              {kbot, {{"ReclaimUnit", enemy, std::nullopt}}}},
         .selected_after = {peewee, kbot},
         .command_after = none}
    );
    cases.push_back(
        {.name = "left-click interface, default order: on a wreck on mapped ground the "
                 "construction kbot reclaims it",
         .interface_type = lc,
         .selection = {kbot},
         .command = none,
         .at = mapped_wreck_point,
         .cursor = Cursor::reclaim,
         .orders = {{kbot, {{"Reclaim", 0, wreck_middle(mapped_wreck)}}}},
         .selected_after = {kbot},
         .command_after = none}
    );
    if (resurrector != 0)
        cases.push_back(
            {.name = "left-click interface, default order: on a wreck on mapped ground the "
                     "resurrector raises it",
             .interface_type = lc,
             .selection = {resurrector},
             .command = none,
             .at = mapped_wreck_point,
             .cursor = Cursor::resurrect,
             .orders = {{resurrector, {{"Resurrect", 0, wreck_middle(mapped_wreck)}}}},
             .selected_after = {resurrector},
             .command_after = none}
        );
    cases.push_back(
        {.name = "left-click interface, default order: with nothing selected a press on open "
                 "ground does nothing",
         .interface_type = lc,
         .selection = {},
         .command = none,
         .at = ground_point,
         .cursor = Cursor::normal,
         .selected_after = {},
         .command_after = none}
    );
    cases.push_back(
        {.name = "left-click interface, ATTACK: on an enemy's dot the Peewee attacks it",
         .interface_type = lc,
         .selection = {peewee},
         .command = MatchCommand::attack,
         .at = dot_of(enemy),
         .cursor = Cursor::attack,
         .orders = {{peewee, {{"Attack_Chase", enemy, std::nullopt}}}},
         .selected_after = {peewee},
         .command_after = none}
    );
    // Armed commands in the right-click interface.
    cases.push_back(
        {.name = "right-click interface, MOVE: on an enemy's dot the commander captures it and "
                 "the Peewee moves there",
         .interface_type = rc,
         .selection = {commander, peewee},
         .command = MatchCommand::move,
         .at = dot_of(enemy),
         .cursor = Cursor::capture,
         .orders =
             {{commander, {{"Capture", enemy, std::nullopt}}},
              {peewee,
               {{"Move_Ground",
                 0,
                 shaped(
                     {commander, peewee}, input::OrderCommand::move, peewee, enemy, at_dot(enemy)
                 )}}}},
         .selected_after = {commander, peewee},
         .command_after = none}
    );
    cases.push_back(
        {.name = "right-click interface, MOVE: on an own unit's dot the Peewee guards it",
         .interface_type = rc,
         .selection = {peewee},
         .command = MatchCommand::move,
         .at = dot_of(own),
         .cursor = Cursor::guard,
         .orders = {{peewee, {{"Follow_Ground", own, std::nullopt}}}},
         .selected_after = {peewee},
         .command_after = none}
    );
    cases.push_back(
        {.name = "right-click interface, MOVE with only a building selected: nothing, and MOVE "
                 "stays armed",
         .interface_type = rc,
         .selection = {building},
         .command = MatchCommand::move,
         .at = ground_point,
         .cursor = Cursor::normal,
         .selected_after = {building},
         .command_after = MatchCommand::move}
    );
    cases.push_back(
        {.name = "right-click interface, MOVE with Shift: the move goes behind the Peewee's "
                 "orders and MOVE stays armed",
         .interface_type = rc,
         .selection = {peewee},
         .command = MatchCommand::move,
         .at = ground_point,
         .mods = SDL_KMOD_LSHIFT,
         .cursor = Cursor::move,
         .orders = {{peewee, {{"Move_Ground", 0, *first_move}, {"Move_Ground", 0, ground}}}},
         .selected_after = {peewee},
         .command_after = MatchCommand::move,
         .before = [&] { match_->issue_ground_move(peewee, *first_move, false); }}
    );
    cases.push_back(
        {.name = "right-click interface, MOVE pressed on the radar and released over the "
                 "battlefield: the move goes to the press",
         .interface_type = rc,
         .selection = {peewee},
         .command = MatchCommand::move,
         .at = ground_point,
         .cursor = Cursor::move,
         .orders = {{peewee, {{"Move_Ground", 0, ground}}}},
         .selected_after = {peewee},
         .command_after = none,
         .press = [&] {
             send(
                 SDL_EVENT_MOUSE_BUTTON_DOWN,
                 SDL_BUTTON_LEFT,
                 ground_point.first,
                 ground_point.second,
                 SDL_KMOD_NONE
             );
             const auto [x, y] = *clear_site;
             send(SDL_EVENT_MOUSE_MOTION, 0, x, y, SDL_KMOD_NONE);
             send(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, x, y, SDL_KMOD_NONE);
         }}
    );
    cases.push_back(
        {.name = "right-click interface, ATTACK on the dot of a selected own unit: the other "
                 "fires at the ground there and the unit itself is given nothing",
         .interface_type = rc,
         .selection = {peewee, own},
         .command = MatchCommand::attack,
         .at = dot_of(own),
         .cursor = Cursor::attack,
         .orders = {{peewee, {{"Suppress", 0, at_dot(own)}}}},
         .selected_after = {peewee, own},
         .command_after = none}
    );
    cases.push_back(
        {.name = "right-click interface, D-GUN on an own unit's dot: the commander fires at it",
         .interface_type = rc,
         .selection = {commander},
         .command = MatchCommand::dgun,
         .at = dot_of(own),
         .cursor = Cursor::attack,
         .orders = {{commander, {{"AttackSpecial", own, at_dot(own)}}}},
         .selected_after = {commander},
         .command_after = none}
    );
    cases.push_back(
        {.name = "right-click interface, PATROL with a construction kbot and a building: the "
                 "kbot patrols and the building is given nothing",
         .interface_type = rc,
         .selection = {kbot, building},
         .command = MatchCommand::patrol,
         .at = ground_point,
         .cursor = Cursor::patrol,
         .orders =
             {{kbot,
               {{"RepairPatrol",
                 0,
                 shaped({kbot, building}, input::OrderCommand::patrol, kbot, 0, ground)}}}},
         .selected_after = {kbot, building},
         .command_after = none}
    );
    cases.push_back(
        {.name = "right-click interface, GUARD on open ground: nothing, and GUARD stays armed",
         .interface_type = rc,
         .selection = {peewee},
         .command = MatchCommand::guard,
         .at = ground_point,
         .cursor = Cursor::normal,
         .selected_after = {peewee},
         .command_after = MatchCommand::guard}
    );
    cases.push_back(
        {.name = "right-click interface, GUARD on an ally's dot: the Peewee guards it",
         .interface_type = rc,
         .selection = {peewee},
         .command = MatchCommand::guard,
         .at = dot_of(ally),
         .cursor = Cursor::guard,
         .orders = {{peewee, {{"Follow_Ground", ally, std::nullopt}}}},
         .selected_after = {peewee},
         .command_after = none}
    );
    cases.push_back(
        {.name = "right-click interface, REPAIR on a damaged enemy's dot: the construction kbot "
                 "repairs it",
         .interface_type = rc,
         .selection = {kbot},
         .command = MatchCommand::repair,
         .at = dot_of(enemy),
         .cursor = Cursor::repair,
         .orders = {{kbot, {{"RepairUnit", enemy, std::nullopt}}}},
         .selected_after = {kbot},
         .command_after = none}
    );
    cases.push_back(
        {.name = "right-click interface, REPAIR on an undamaged own unit's dot: nothing, and "
                 "REPAIR stays armed",
         .interface_type = rc,
         .selection = {kbot},
         .command = MatchCommand::repair,
         .at = dot_of(own),
         .cursor = Cursor::normal,
         .selected_after = {kbot},
         .command_after = MatchCommand::repair}
    );
    cases.push_back(
        {.name = "right-click interface, REPAIR on a damaged own unit's dot: the construction "
                 "kbot repairs it",
         .interface_type = rc,
         .selection = {kbot},
         .command = MatchCommand::repair,
         .at = dot_of(damaged),
         .cursor = Cursor::repair,
         .orders = {{kbot, {{"RepairUnit", damaged, std::nullopt}}}},
         .selected_after = {kbot},
         .command_after = none}
    );
    cases.push_back(
        {.name = "right-click interface, RECLAIM on a wreck on mapped ground: the construction "
                 "kbot reclaims it",
         .interface_type = rc,
         .selection = {kbot},
         .command = MatchCommand::reclaim,
         .at = mapped_wreck_point,
         .cursor = Cursor::reclaim,
         .orders = {{kbot, {{"Reclaim", 0, wreck_middle(mapped_wreck)}}}},
         .selected_after = {kbot},
         .command_after = none}
    );
    cases.push_back(
        {.name = "right-click interface, RECLAIM on a wreck on ground never mapped: nothing, "
                 "and RECLAIM stays armed",
         .interface_type = rc,
         .selection = {kbot},
         .command = MatchCommand::reclaim,
         .at = unmapped_wreck_point,
         .cursor = Cursor::normal,
         .selected_after = {kbot},
         .command_after = MatchCommand::reclaim}
    );
    cases.push_back(
        {.name = "right-click interface, RECLAIM on an own unit's dot: the construction kbot "
                 "reclaims the unit",
         .interface_type = rc,
         .selection = {kbot},
         .command = MatchCommand::reclaim,
         .at = dot_of(own),
         .cursor = Cursor::reclaim,
         .orders = {{kbot, {{"ReclaimUnit", own, std::nullopt}}}},
         .selected_after = {kbot},
         .command_after = none}
    );
    cases.push_back(
        {.name = "right-click interface, CAPTURE on an enemy's dot: the commander captures it",
         .interface_type = rc,
         .selection = {commander},
         .command = MatchCommand::capture,
         .at = dot_of(enemy),
         .cursor = Cursor::capture,
         .orders = {{commander, {{"Capture", enemy, std::nullopt}}}},
         .selected_after = {commander},
         .command_after = none}
    );
    cases.push_back(
        {.name = "right-click interface, CAPTURE on an own unit's dot: nothing, and CAPTURE "
                 "stays armed",
         .interface_type = rc,
         .selection = {commander},
         .command = MatchCommand::capture,
         .at = dot_of(own),
         .cursor = Cursor::normal,
         .selected_after = {commander},
         .command_after = MatchCommand::capture}
    );
    cases.push_back(
        {.name = "right-click interface, LOAD on an own unit's dot: the transport picks it up",
         .interface_type = rc,
         .selection = {transport},
         .command = MatchCommand::load,
         .at = dot_of(own),
         .cursor = Cursor::load_by_air,
         .orders = {{transport, {{"VTOL_Pickup", own, std::nullopt}}}},
         .selected_after = {transport},
         .command_after = none}
    );
    cases.push_back(
        {.name = "right-click interface, UNLOAD on open ground: the transport unloads there",
         .interface_type = rc,
         .selection = {transport},
         .command = MatchCommand::unload,
         .at = ground_point,
         .cursor = Cursor::unload,
         .orders = {{transport, {{"VTOL_Unload", 0, ground}}}},
         .selected_after = {transport},
         .command_after = none}
    );
    cases.push_back(
        {.name = "right-click interface, BUILD after a clear site on the battlefield: the kbot "
                 "builds at the radar's point, at the battlefield site's height",
         .interface_type = rc,
         .selection = {kbot},
         .command = MatchCommand::build,
         .at = ground_point,
         .orders = {{kbot, {{"MobileBuild", 0, build_point(ground_point)}}}},
         .selected_after = {kbot},
         .command_after = none,
         .before = rest_over(*clear_site),
         .sounds = {"oktobuild"},
         .listens = true}
    );
    cases.push_back(
        {.name = "right-click interface, BUILD after a refused site on the battlefield: nothing "
                 "is built, and BUILD stays armed",
         .interface_type = rc,
         .selection = {kbot},
         .command = MatchCommand::build,
         .at = ground_point,
         .selected_after = {kbot},
         .command_after = MatchCommand::build,
         .before = rest_over(*refused_site),
         .sounds = {"notoktobuild"},
         .listens = true}
    );
    // The view moves the radar keeps: a left press with the default order
    // in the right-click interface, and a right press in either.
    cases.push_back(
        {.name = "right-click interface, default order: a left press moves the view there and "
                 "gives no order",
         .interface_type = rc,
         .selection = {peewee},
         .command = none,
         .at = ground_point,
         .selected_after = {peewee},
         .command_after = none,
         .view_moves = true}
    );
    for (const auto type : {lc, rc})
        cases.push_back(
            {.name = std::string(type == lc ? "left" : "right") +
                     "-click interface, default order: a right press moves the view there and "
                     "gives no order",
             .interface_type = type,
             .selection = {peewee},
             .command = none,
             .at = ground_point,
             .button = SDL_BUTTON_RIGHT,
             .selected_after = {peewee},
             .command_after = none,
             .view_moves = true}
        );

    std::vector<std::string> failures;
    for (auto& one : cases) {
        // The same start for every case: no orders, the selection and the
        // command given, the view on the commander.
        game.interface_type = one.interface_type;
        for (const auto id : watched)
            match_->stop_orders(id);
        clear_local_selection();
        reset_match_command();
        pending_build_type_ = 0;
        for (const auto id : one.selection)
            adopt_selection(id);
        selected_match_unit_ = one.selection.empty() ? uint16_t{0} : one.selection.front();
        apply_match_hud_for_selection();
        match_command_ = one.command;
        if (one.command == MatchCommand::build)
            pending_build_type_ = solar;
        center_camera_on_unit(commander);
        render_match_surface();
        if (one.before)
            one.before();
        std::vector<std::string> heard;
        if (one.listens)
            heard_interface_sounds_ = &heard;
        compose_dots();
        send(SDL_EVENT_MOUSE_MOTION, 0, one.at.first, one.at.second, SDL_KMOD_NONE);
        const auto shown = static_cast<input::OrderCursor>(pick_match_cursor());
        const std::array<int32_t, 2> camera{match_camera_x_, match_camera_z_};
        if (one.press) {
            one.press();
        } else {
            send(SDL_EVENT_MOUSE_BUTTON_DOWN, one.button, one.at.first, one.at.second, one.mods);
            send(SDL_EVENT_MOUSE_BUTTON_UP, one.button, one.at.first, one.at.second, one.mods);
        }
        heard_interface_sounds_ = nullptr;
        auto result = outcome();
        result.sounds = heard;
        std::vector<std::string> wrong;
        if (one.cursor && shown != *one.cursor)
            wrong.push_back(
                "the cursor is " + std::to_string(static_cast<int>(shown)) + ", not " +
                std::to_string(static_cast<int>(*one.cursor))
            );
        for (std::size_t index = 0; index < watched.size(); ++index) {
            const auto id = watched[index];
            std::vector<Expected> expected;
            for (const auto& [unit, list] : one.orders)
                if (unit == id)
                    expected = list;
            const auto& queue = result.queues[index];
            bool same = queue.size() == expected.size();
            for (std::size_t at = 0; same && at < queue.size(); ++at)
                same = queue[at].kind == kind_of(expected[at].kind) &&
                       queue[at].target == expected[at].target &&
                       (!expected[at].point || queue[at].point == *expected[at].point);
            if (same)
                continue;
            std::string want = "unit " + std::to_string(id) + " should hold";
            for (const auto& order : expected) {
                want += " " + order.kind;
                if (order.target != 0)
                    want += " on " + std::to_string(order.target);
                if (order.point)
                    want += " at " + describe(*order.point);
                want += ";";
            }
            if (expected.empty())
                want += " nothing";
            wrong.push_back(want);
        }
        if (result.selected != sorted(one.selected_after))
            wrong.push_back("the selection is not as it should be");
        if (result.command != one.command_after)
            wrong.push_back(
                "the command armed is " + std::to_string(static_cast<int>(result.command)) +
                ", not " + std::to_string(static_cast<int>(one.command_after))
            );
        if (one.command == MatchCommand::build &&
            (result.command == MatchCommand::build) != (result.pending_build == solar))
            wrong.push_back("the building armed does not go with the command");
        const auto view = one.view_moves ? radar_view(one.at) : camera;
        if (result.camera != view)
            wrong.push_back(one.view_moves ? "the view did not go to the point" : "the view moved");
        if (one.listens && result.sounds != one.sounds)
            wrong.push_back("the sounds heard are not as they should be");
        // A radar scroll the press started ends with its release.
        if ((input::pointer_flags(game) & input::pointer_radar_scroll) != 0)
            wrong.push_back("a radar scroll is still running");
        const auto line = one.name + ": " + describe_outcome(result);
        if (wrong.empty()) {
            std::cout << "radar order check: ok, " << line << '\n';
            continue;
        }
        std::string reason;
        for (const auto& item : wrong)
            reason += (reason.empty() ? "" : "; ") + item;
        failures.push_back(one.name + ": " + reason + " (" + describe_outcome(result) + ")");
    }
    clear_local_selection();
    reset_match_command();
    pending_build_type_ = 0;
    if (!failures.empty()) {
        std::string text;
        for (const auto& item : failures) {
            std::cerr << "radar order check: FAILED " << item << '\n';
            text += (text.empty() ? "" : "\n") + item;
        }
        fail(
            std::to_string(failures.size()) + " of " + std::to_string(cases.size()) +
            " cases differ:\n" + text
        );
    }
    std::cout << "radar order check: " << cases.size()
              << " presses on the radar gave the game's own cursor, orders, selection, command "
                 "and view\n";
}

} // namespace oa::app
