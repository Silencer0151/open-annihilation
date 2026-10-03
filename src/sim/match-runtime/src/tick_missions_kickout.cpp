// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// orders.build-site-kickout: a builder waiting for its site to clear orders
// the local player's own units off the footprint, each to a free spot nearby,
// and keeps what it sent where so a unit already on its way is not sent back
// to its old order twice.
#include "ground_missions.hpp"

#include "oa/sim/match_runtime/rule_state.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace oa::sim::match_runtime {

namespace {
namespace kickout {

// What the kickout keeps per unit slot: whether it sent the unit, and the
// whole x, z and y of the point it sent it to.
constexpr std::size_t spot_present = 0;
constexpr std::size_t spot_x = 1;
constexpr std::size_t spot_z = 2;
constexpr std::size_t spot_y = 3;

// The most different units one footprint's cells name.
constexpr std::size_t unit_capacity = 256;
// An order aimed at an unfinished unit that has taken less than this much of
// its build energy only moves when another of the owner's units works on it.
constexpr double cheap_frame_energy = 600.0;
// The order phase from which another unit counts as working on the frame.
constexpr uint8_t working_phase = 2;
// The search starts this many world units out per cell of the built type's
// footprint width and widens in steps of a cell to twice that.
constexpr int32_t search_radius_per_cell = 24;
constexpr int32_t search_step = 16;
// The search's angular step is one cell of arc; it gives up past half a turn.
constexpr double arc_step = 16.0;
constexpr double half_turn = 3.141592654;
// A unit headed for a target leaves at an eighth of a turn off its course.
constexpr uint16_t course_offset = 0x2000;
// A random start angle is a draw from 0 to 359 divided by 57, in radians: a
// little more than a whole turn at most.
constexpr uint32_t random_degrees = 360;
constexpr double random_degree_divisor = 57.0;
constexpr double radians_to_word = 65536.0 / (2.0 * 3.14159265358979323846);
// A free spot keeps a cell off the map edges.
constexpr int32_t edge_margin = 16;
constexpr int32_t world_units_per_cell = 16;

/// Returns the whole part of a signed 16.16 coordinate as the game reads it:
/// its high 16 bits, unsigned.
///
/// @param value signed 16.16 coordinate
/// @return the high 16 bits, 0 to 65535
int32_t whole(int32_t value) {
    return static_cast<int32_t>(static_cast<uint16_t>(static_cast<uint32_t>(value) >> 16));
}

/// Returns a point whose x, y and z are whole world units, fractions zero.
///
/// @param x whole x
/// @param y whole y
/// @param z whole z
/// @return the signed 16.16 point
sim::ground_orders::Point whole_point(int32_t x, int32_t y, int32_t z) {
    const auto fixed = [](int32_t value) {
        return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint16_t>(value)) << 16);
    };
    return {fixed(x), fixed(y), fixed(z)};
}

/// Converts radians to an angle word, 65536 per turn, truncated.
///
/// @param radians the angle
/// @return the word
uint16_t angle_word(double radians) {
    return static_cast<uint16_t>(static_cast<int32_t>(radians * radians_to_word));
}

/// Returns the unit vector at an angle word: x along its sine, z along its
/// cosine, as direction() reads a vector.
///
/// @param angle angle word
/// @return {x, z}
std::array<double, 2> unit_vector(uint16_t angle) {
    const auto rotated = base::game_math::rotate_pair(0, 1, static_cast<int16_t>(0u - angle));
    return {rotated.first, rotated.second};
}

/// Truncates toward zero, as a float-to-integer conversion does.
///
/// @param value finite value
/// @return the integer part
int32_t truncate(double value) {
    return static_cast<int32_t>(value);
}

/// Returns the bytes of the kickout's table for the state digest and saves.
///
/// @param context the match
/// @return the table's bytes
std::span<const uint8_t> spot_bytes(void* context) {
    const auto spots = static_cast<Match*>(context)->build_site_kickout_spots();
    return {reinterpret_cast<const uint8_t*>(spots.data()), spots.size_bytes()};
}

/// Restores the kickout's table from a save.
///
/// @param context the match
/// @param bytes the saved bytes
/// @return false when they do not fit the table
bool spot_restore(void* context, std::span<const uint8_t> bytes) {
    const auto spots = static_cast<Match*>(context)->build_site_kickout_spots();
    if (bytes.size() != spots.size_bytes())
        return false;
    std::memcpy(spots.data(), bytes.data(), bytes.size());
    return true;
}

} // namespace kickout
} // namespace

void Match::keep_build_site_kickout() {
    if (!input_.rules.orders.build_site_kickout.kickout)
        return;
    build_site_kickout_spot_count_ = slots_.size();
    build_site_kickout_spots_ =
        std::make_unique<std::array<uint16_t, 4>[]>(build_site_kickout_spot_count_);
    (void)add_rule_state(
        rule_state_, {"build-site-kickout", this, kickout::spot_bytes, kickout::spot_restore}
    );
}

uint8_t
TickHost::GroundMissions::move_command_kind_of(TickHost& host, sim::unit_spawn::Slot& slot) {
    const auto& type = match_unit_def(host.match, slot.record);
    CommandSource source;
    source.can_move = (type.abilities & OA_UNIT_DEF_ABILITY_CAN_MOVE) != 0;
    source.object_present = host.match.ground_runtime(slot.unit_index) != nullptr;
    source.type_flags = type.flags;
    const auto name =
        resolve_combat_command(2, source, std::nullopt, host.match.state().game.sea_level);
    if (name == "QMove")
        return qmove_kind;
    return combat_order_kind(name);
}

bool TickHost::GroundMissions::on_kickout_move(TickHost& host, sim::unit_spawn::Slot& slot) {
    auto* head = slot.unit->primary;
    if (!head || head->kind != move_command_kind_of(host, slot))
        return false;
    const auto spots = host.match.build_site_kickout_spots();
    if (slot.unit_index >= spots.size())
        return false;
    auto& spot = spots[slot.unit_index];
    if (spot[kickout::spot_present] == 0)
        return false;
    const auto& to = host.owned(*head).extra.destination;
    if (spot[kickout::spot_x] == kickout::whole(to[0]) &&
        spot[kickout::spot_z] == kickout::whole(to[2]) &&
        spot[kickout::spot_y] == kickout::whole(to[1]))
        return true;
    spot = {};
    return false;
}

bool TickHost::GroundMissions::kickout_moves(
    TickHost& host,
    sim::unit_spawn::Slot& slot,
    int32_t site_x,
    int32_t site_z,
    int16_t footprint_x,
    int16_t footprint_z
) {
    auto* head = slot.unit->primary;
    if (head && head->kind == move_command_kind_of(host, slot)) {
        // A unit walking into the footprint moves.
        const auto& to = host.owned(*head).extra.destination;
        const auto cell_x = kickout::whole(to[0]) >> 4;
        const auto cell_z = kickout::whole(to[2]) >> 4;
        const auto first_x = site_x / kickout::world_units_per_cell - footprint_x / 2;
        const auto first_z = site_z / kickout::world_units_per_cell - footprint_z / 2;
        return cell_x >= first_x && cell_x < first_x + footprint_x && cell_z >= first_z &&
               cell_z < first_z + footprint_z;
    }
    // An idle unit, or one whose order has no target, moves.
    if (!head)
        return true;
    auto& record = host.owned(*head);
    auto* aimed = record.construction.target ? record.construction.target : record.attack.target;
    if (!aimed)
        return true;
    // So does one whose target is finished, or an unfinished one that has
    // taken its first 600 energy.
    const auto left = aimed->record.build_remaining;
    if (!(left > 0.0F))
        return true;
    const auto spent =
        static_cast<double>(match_unit_def(host.match, aimed->record).build_cost_energy) *
        (1.0 - static_cast<double>(left));
    if (!(kickout::cheap_frame_energy > spent))
        return true;
    // Below that the unit moves only when another of the owner's units is
    // working on the same frame.
    auto& world = host.match.state();
    const auto* owner = oa::world_unit_owner(&world, &slot.record);
    if (!owner)
        return false;
    uint32_t count = 0;
    auto* first = oa::world_player_units(&world, owner, &count);
    for (uint32_t i = 0; first && i < count; ++i) {
        const auto index = first[i].id;
        if (&first[i] == &slot.record || index == 0 || index >= host.match.slots_.size() ||
            host.match.ground_runtime(index) == nullptr)
            continue;
        auto* other = host.match.slots_[index].unit;
        auto* other_head = other ? other->primary : nullptr;
        if (!other_head)
            continue;
        auto& other_record = host.owned(*other_head);
        auto* other_aimed = other_record.construction.target ? other_record.construction.target
                                                             : other_record.attack.target;
        if (other_aimed == aimed && other_head->phase >= kickout::working_phase)
            return true;
    }
    return false;
}

std::optional<std::array<int32_t, 2>> TickHost::GroundMissions::kickout_spot(
    TickHost& host, sim::unit_spawn::Slot& slot, int32_t site_x, int32_t site_z, int32_t radius
) {
    auto& game = host.match.state().game;
    const auto unit_x = kickout::whole(slot.record.position.x);
    const auto unit_z = kickout::whole(slot.record.position.z);
    const auto random_angle = kickout::angle_word(
        static_cast<double>(host.match.random_bounded(kickout::random_degrees)) /
        kickout::random_degree_divisor
    );
    uint16_t angle = random_angle;
    auto* head = slot.unit->primary;
    sim::simulation_state::Unit* aimed = nullptr;
    if (head) {
        auto& record = host.owned(*head);
        aimed = record.construction.target ? record.construction.target : record.attack.target;
    }
    // A unit the kickout already sent starts the search at random.
    const bool sent_before = on_kickout_move(host, slot);
    if (!sent_before && aimed) {
        // A unit headed for a target leaves an eighth of a turn off its
        // course, on the side away from the site, from where that course
        // meets the search circle.
        const auto course = base::game_math::direction(
            kickout::whole(aimed->record.position.x) - unit_x,
            kickout::whole(aimed->record.position.z) - unit_z
        );
        const auto toward_site = [&](uint16_t heading) {
            const auto along = kickout::unit_vector(heading);
            return along[0] * static_cast<double>(site_x - unit_x) +
                   along[1] * static_cast<double>(site_z - unit_z);
        };
        const auto left = static_cast<uint16_t>(course - kickout::course_offset);
        const auto right = static_cast<uint16_t>(course + kickout::course_offset);
        const auto heading = toward_site(left) > toward_site(right) ? right : left;
        angle = heading;
        const auto along = kickout::unit_vector(heading);
        const double from_x = unit_x - site_x;
        const double from_z = unit_z - site_z;
        const double reach = static_cast<double>(radius);
        const double half_b = along[0] * from_x + along[1] * from_z;
        const double c = from_x * from_x + from_z * from_z - reach * reach;
        const double discriminant = half_b * half_b - c;
        if (discriminant >= 0.0) {
            // The crossing further along the axis the course runs closer to
            // is tried first, then the other; the first ahead of the unit wins.
            const double root = base::game_math::square_root(discriminant);
            const double far = -half_b + root;
            const double near = -half_b - root;
            const bool along_x = std::abs(along[1]) <= std::abs(along[0]);
            const bool positive = along_x ? along[0] > 0.0 : along[1] > 0.0;
            const std::array<double, 2> tries{positive ? far : near, positive ? near : far};
            for (const double t : tries) {
                if (!(t >= 0.0))
                    continue;
                const double x = static_cast<double>(unit_x) + t * along[0];
                const double z = static_cast<double>(unit_z) + t * along[1];
                if (x > kickout::edge_margin && game.map_pixel_width > x &&
                    z > kickout::edge_margin && game.map_pixel_height > z) {
                    angle = base::game_math::direction(
                        kickout::truncate((x - site_x) * 256.0),
                        kickout::truncate((z - site_z) * 256.0)
                    );
                }
                break;
            }
        }
    } else if (!sent_before && (unit_x != site_x || unit_z != site_z)) {
        // Any other unit leaves straight away from the site's centre.
        angle = base::game_math::direction(unit_x - site_x, unit_z - site_z);
    }
    const auto& type = match_unit_def(host.match, slot.record);
    const auto& spatial = host.match.spatial_;
    const auto width = static_cast<int32_t>(spatial.terrain_width);
    const auto height = static_cast<int32_t>(spatial.terrain_height);
    // Rings from the radius out to twice it, each walked from the start
    // angle outward on both sides until half a turn.
    for (int32_t ring = radius; ring < radius * 2; ring += kickout::search_step) {
        for (double turned = 0.0; turned < kickout::half_turn;
             turned += kickout::arc_step / static_cast<double>(ring)) {
            for (const int32_t side : {-1, 1}) {
                const auto heading = static_cast<uint16_t>(
                    angle + static_cast<uint16_t>(side * kickout::angle_word(turned))
                );
                const auto offset = kickout::unit_vector(heading);
                const auto x = site_x + kickout::truncate(offset[0] * ring);
                const auto z = site_z + kickout::truncate(offset[1] * ring);
                if (x < kickout::edge_margin || x >= game.map_pixel_width ||
                    z < kickout::edge_margin || z >= game.map_pixel_height)
                    continue;
                const auto cell_x = x / kickout::world_units_per_cell;
                const auto cell_z = z / kickout::world_units_per_cell;
                if (cell_x <= 0 || cell_x + 1 >= width || cell_z <= 0 || cell_z + 1 >= height)
                    continue;
                const auto& plot = spatial.plots
                                       [static_cast<std::size_t>(cell_z) * width +
                                        static_cast<std::size_t>(cell_x)];
                if (plot.ground != 0 || plot.feature_height != 0)
                    continue;
                if (static_cast<int32_t>(plot.high_height) - static_cast<int32_t>(plot.low_height) <
                    static_cast<int32_t>(type.max_slope))
                    return std::array<int32_t, 2>{x, z};
            }
        }
    }
    return std::nullopt;
}

void TickHost::GroundMissions::send_off_site(
    TickHost& host, sim::unit_spawn::Slot& slot, const sim::ground_orders::Point& move
) {
    const auto unit_index = slot.unit_index;
    auto& match = host.match;
    auto* head = slot.unit->primary;
    const auto move_kind = move_command_kind_of(host, slot);
    // A unit the move command gives no order stays.
    if (move_kind == 0)
        return;
    if (!head) {
        // A unit without orders is sent as a move order.
        match.issue_or_cancel_order(unit_index, move_kind, false, 0, &move, 0, 0);
    } else {
        auto* saved_next = head->next;
        auto& record = host.owned(*head);
        auto* aimed =
            record.construction.target ? record.construction.target : record.attack.target;
        const bool building =
            head->kind == (match_unit_def(match, slot.record).flags & OA_UNIT_DEF_FLAG_CAN_FLY
                               ? vtol_mobile_build_kind
                               : mobile_build_kind);
        const auto& to = record.extra.destination;
        if (building && (!aimed || aimed->record.build_remaining == 1.0F)) {
            // A builder whose frame is not started walks to its site again
            // once it has moved.
            head->phase = 0;
            push_move_front(host, slot, *head, move_kind, move);
        } else if (
            kickout::whole(to[0]) == 0 && kickout::whole(to[2]) == 0 && kickout::whole(to[1]) == 0
        ) {
            // An order without a point gives way to the move.
            match.issue_or_cancel_order(unit_index, move_kind, false, 0, &move, 0, 0);
        } else if (on_kickout_move(host, slot)) {
            // A unit already sent is sent on, its later orders kept.
            head->next = nullptr;
            match.issue_or_cancel_order(unit_index, move_kind, false, 0, &move, 0, 0);
            if (slot.unit->primary)
                slot.unit->primary->next = saved_next;
        } else {
            // Any other order stops, is given again from its start (a
            // builder's unfinished frame as the repair command gives it) with
            // the unit's later orders kept, and waits behind the move.
            const auto point = kickout::whole_point(
                kickout::whole(to[0]), kickout::whole(to[1]), kickout::whole(to[2])
            );
            uint8_t kind = head->kind;
            GroundMissions stopped(host, slot, *head, 0);
            if (building && aimed)
                kind = stopped.repair_command_kind(*aimed);
            const auto aimed_index = aimed ? host.slot(*aimed).unit_index : uint16_t{0};
            (void)stopped.stop();
            head->next = nullptr;
            if (kind != 0)
                match.issue_or_cancel_order(unit_index, kind, false, aimed_index, &point, 0, 0);
            if (slot.unit->primary) {
                slot.unit->primary->next = saved_next;
                push_move_front(host, slot, *slot.unit->primary, move_kind, move);
            }
        }
    }
    auto* sent = slot.unit->primary;
    const auto spots = match.build_site_kickout_spots();
    if (!sent || unit_index >= spots.size())
        return;
    const auto& to = host.owned(*sent).extra.destination;
    spots[unit_index] = {
        1,
        static_cast<uint16_t>(kickout::whole(to[0])),
        static_cast<uint16_t>(kickout::whole(to[2])),
        static_cast<uint16_t>(kickout::whole(to[1]))
    };
}

sim::ground_orders::Point TickHost::GroundMissions::whole_move_point(
    sim::unit_spawn::Slot& slot, std::array<int32_t, 2> spot
) {
    return kickout::whole_point(spot[0], kickout::whole(slot.record.position.y), spot[1]);
}

void TickHost::GroundMissions::push_move_front(
    TickHost& host,
    sim::unit_spawn::Slot& slot,
    sim::simulation_state::Order& head,
    uint8_t move_kind,
    const sim::ground_orders::Point& move
) {
    GroundMissions mover(host, slot, head, 0);
    mover.push_order_front(mover.create_order(move_kind, nullptr, &move, 0, 0, 0));
}

void TickHost::GroundMissions::clear_build_site(
    TickHost& host, int16_t footprint_x, int16_t footprint_z, const sim::ground_orders::Point& site
) {
    auto& match = host.match;
    const auto site_x = kickout::whole(site[0]);
    const auto site_z = kickout::whole(site[2]);
    const auto& spatial = match.spatial_;
    const auto width = static_cast<int32_t>(spatial.terrain_width);
    const auto height = static_cast<int32_t>(spatial.terrain_height);
    // The units on the footprint's cells, each once, in slot order.
    std::array<uint16_t, kickout::unit_capacity> found{};
    std::size_t count = 0;
    const auto first_x =
        site_x / kickout::world_units_per_cell - (static_cast<uint32_t>(footprint_x) >> 1);
    const auto first_z =
        site_z / kickout::world_units_per_cell - (static_cast<uint32_t>(footprint_z) >> 1);
    for (int32_t column = 0; column < footprint_x; ++column) {
        for (int32_t row = 0; row < footprint_z; ++row) {
            const auto x = static_cast<int32_t>(first_x) + column;
            const auto z = static_cast<int32_t>(first_z) + row;
            if (x < 0 || x >= width || z < 0 || z >= height)
                continue;
            const auto occupant =
                spatial.plots[static_cast<std::size_t>(z) * width + static_cast<std::size_t>(x)]
                    .ground;
            if (occupant == 0 || occupant >= match.slots_.size())
                continue;
            const auto end = found.begin() + static_cast<std::ptrdiff_t>(count);
            const auto at = std::lower_bound(found.begin(), end, occupant);
            if ((at != end && *at == occupant) || count == found.size())
                continue;
            std::copy_backward(at, end, end + 1);
            *at = occupant;
            ++count;
        }
    }
    const auto local = match.state().game.local_player_index;
    const auto radius = static_cast<int32_t>(footprint_x) * kickout::search_radius_per_cell;
    for (std::size_t i = 0; i < count; ++i) {
        auto& slot = match.slots_[found[i]];
        // Only the local player's own mobile units move.
        if (!slot.unit || match.ground_runtime(slot.unit_index) == nullptr ||
            slot.record.owner_index != local)
            continue;
        if (!kickout_moves(host, slot, site_x, site_z, footprint_x, footprint_z))
            continue;
        if (const auto spot = kickout_spot(host, slot, site_x, site_z, radius))
            send_off_site(host, slot, whole_move_point(slot, *spot));
    }
}

void TickHost::GroundMissions::send_ahead(
    TickHost& host, sim::unit_spawn::Slot& slot, const sim::ground_orders::Point& point
) {
    send_off_site(
        host,
        slot,
        kickout::whole_point(
            kickout::whole(point[0]), kickout::whole(point[1]), kickout::whole(point[2])
        )
    );
}

bool Match::send_ahead_of_orders(uint16_t unit, const sim::ground_orders::Point& point) {
    // Only the local player's own units with a movement object go.
    if (unit == 0 || unit >= slots_.size() || !slots_[unit].unit ||
        ground_runtime(unit) == nullptr ||
        slots_[unit].record.owner_index != state().game.local_player_index)
        return false;
    TickHost host(*this);
    host.send_ahead(slots_[unit], point);
    return true;
}

void TickHost::send_ahead(sim::unit_spawn::Slot& s, const sim::ground_orders::Point& point) {
    GroundMissions::send_ahead(*this, s, point);
}

} // namespace oa::sim::match_runtime
