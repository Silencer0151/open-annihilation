// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/base/sha256.hpp"
#include "oa/sim/ballistics.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>

namespace {

constexpr int pitch_samples = 1 << 18;

// SplitMix64.
uint64_t next(uint64_t& state) {
    uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

int32_t spread_int(uint64_t r) {
    return std::bit_cast<int32_t>(static_cast<uint32_t>(r)) >> static_cast<unsigned>(r >> 59);
}

// Launch pitches over random offsets, velocities, gravities and minimum
// angles, pinned by digest; without the high arc they are 3.1c's own results.
bool pitch_samples_match(bool accept_high_arc, std::string_view expected) {
    using namespace oa::sim::ballistics;
    namespace sha256 = oa::base::sha256;
    sha256::Hasher hasher{};
    uint64_t state = 7;
    for (int i = 0; i < pitch_samples; ++i) {
        const int32_t dx = spread_int(next(state)) >> 4;
        const int32_t dy = spread_int(next(state)) >> 6;
        const int32_t dz = spread_int(next(state)) >> 4;
        const uint64_t r = next(state);
        const auto velocity = static_cast<int32_t>(r & 0x3ffff) + 1;
        const auto gravity = static_cast<int32_t>((r >> 18) & 0x3fff);
        const auto minimum_bits = static_cast<uint32_t>(r >> 32);
        const float minimum =
            static_cast<float>(static_cast<int32_t>(minimum_bits & 0xffff) - 0x8000) *
            (1.0F / 16384.0F);
        const BallisticParameters parameters{velocity, minimum, gravity, accept_high_arc};
        const int16_t pitch = launch_pitch(parameters, dx, dy, dz);
        sha256::update(hasher, std::bit_cast<std::array<uint8_t, 2>>(pitch));
    }
    const auto text = sha256::to_hex(sha256::finish(hasher));
    const std::string digest{text.begin(), text.end()};
    if (digest == expected)
        return true;
    std::fprintf(stderr, "launch pitch digest %s\n", digest.c_str());
    return false;
}

// weapons.high-arc-ballistic: the second (high) root is taken above a quarter
// of pi instead of at most a quarter of pi. A level target 1000 units away at
// 700000/65536 units per tick under the default gravity has a flat root of
// pitch 6504 (35.7 degrees) and a high one of pitch 9879 (54.3 degrees).
bool high_arc_matches() {
    using namespace oa::sim::ballistics;
    constexpr int32_t velocity = 700'000;
    constexpr int32_t gravity = 7'089;
    constexpr int32_t away = 1000 * 65536;
    constexpr int32_t below = 100 * 65536; // the target 100 units below
    bool ok = true;
    for (const bool high_arc : {false, true}) {
        // A flat root above the minimum is taken first either way.
        ok = ok && launch_pitch({velocity, 0.0F, gravity, high_arc}, away, 0, 0) == 6504;
        ok = ok && launch_pitch({velocity, 0.0F, gravity, high_arc}, away, below, 0) == 4719;
    }
    // Flat root below a 0.7 radian minimum: 3.1c finds no pitch, since the
    // high root is above a quarter of pi; the hack lobs.
    ok = ok && launch_pitch({velocity, 0.7F, gravity, false}, away, 0, 0) == invalid_launch_pitch;
    ok = ok && launch_pitch({velocity, 0.7F, gravity, true}, away, 0, 0) == 9879;
    ok = ok && launch_pitch({velocity, 0.7F, gravity, true}, away, below, 0) == 10625;
    // The high root must still lie above the minimum.
    ok = ok && launch_pitch({velocity, 1.0F, gravity, true}, away, 0, 0) == invalid_launch_pitch;
    ok = ok && launch_pitch({velocity, 1.0F, gravity, true}, away, below, 0) == 10625;
    // A NaN minimum still rejects the second root.
    ok = ok && launch_pitch(
                   {velocity, std::numeric_limits<float>::quiet_NaN(), gravity, true}, away, 0, 0
               ) == 6504;
    // The reach test and the turret aim use the same solver.
    const std::array<uint32_t, 3> source{static_cast<uint32_t>(away), 0, 0};
    const std::array<uint32_t, 3> target{0, 0, 0};
    ok = ok && !ballistic_feasible({velocity, 0.7F, gravity, false}, source, target);
    ok = ok && ballistic_feasible({velocity, 0.7F, gravity, true}, source, target);
    const TurretAimGeometry aim{{away, 0, 0}, {0, 0, 0}, 0};
    ok = ok && !turret_aim_ballistic({velocity, 0.7F, gravity, false}, aim).feasible;
    const auto lobbed = turret_aim_ballistic({velocity, 0.7F, gravity, true}, aim);
    ok = ok && lobbed.feasible && lobbed.pitch == 9879;
    const ReachUnitGeometry shooter{source, 10, 0, 0};
    const ReachUnitGeometry victim{target, 10, 0, 0};
    WeaponReachParameters gun{ballistic_weapon_flag, 2000, {velocity, 0.7F, gravity, false}, {}};
    ok = ok && !weapon_can_reach(gun, shooter, victim, 0);
    ok = ok && !fire_can_reach(gun, shooter, target, 0);
    gun.ballistic.accept_high_arc = true;
    ok = ok && weapon_can_reach(gun, shooter, victim, 0);
    ok = ok && fire_can_reach(gun, shooter, target, 0);
    if (!ok)
        std::fprintf(stderr, "high arc launch pitches differ\n");
    return ok;
}

// The reach tests under each weapon data key, against an airborne, a surface
// and a submerged target, and against a point.
bool reach_keys_match() {
    using namespace oa::sim::ballistics;
    constexpr uint8_t sea = 20;
    constexpr uint32_t airborne = target_air_state;
    const auto at = [](int32_t height) { return static_cast<uint32_t>(height) << 16U; };
    const ReachUnitGeometry dry_shooter{{0, at(30), 0}, 5, 0, 0};
    const ReachUnitGeometry sub{{0, at(5), 0}, 5, 0, 0};
    const ReachUnitGeometry plane{{0, at(80), 0}, 5, airborne, 0};
    const ReachUnitGeometry ship{{0, at(20), 0}, 8, 0, target_type_floats_flag};
    const ReachUnitGeometry tank{{0, at(30), 0}, 8, 0, 0};
    const ReachUnitGeometry hover{{0, at(20), 0}, 8, 0, target_type_hover_flag};
    const ReachUnitGeometry submerged{{0, at(5), 0}, 8, 0, 0};
    const ReachUnitGeometry awash{{0, at(12), 0}, 8, 0, target_type_floats_flag};
    const auto can = [&](uint32_t flags,
                         ReachKeys keys,
                         const ReachUnitGeometry& from,
                         const ReachUnitGeometry& to) {
        return weapon_can_reach({flags, 100, {}, keys}, from, to, sea);
    };
    bool ok = true;
    // not_to_air: a non-water weapon refuses an airborne target; others stay.
    ok = ok && can(0, {}, dry_shooter, plane);
    ok = ok && !can(0, {true, false, false}, dry_shooter, plane);
    ok = ok && can(0, {true, false, false}, dry_shooter, tank);
    ok = ok && !can(to_air_weapon_flag, {true, false, false}, dry_shooter, plane);
    // A water weapon: 3.1c refuses a target above sea unless its type floats,
    // and a hovering one whose half height is above sea.
    ok = ok && !can(water_weapon_flag, {}, sub, tank);
    ok = ok && !can(water_weapon_flag, {}, sub, plane);
    ok = ok && can(water_weapon_flag, {}, sub, ship);
    ok = ok && !can(water_weapon_flag, {}, sub, hover);
    ok = ok && can(water_weapon_flag, {}, sub, submerged);
    // surface_fire skips both of those tests.
    const ReachKeys surface{false, true, false};
    ok = ok && can(water_weapon_flag, surface, sub, tank);
    ok = ok && can(water_weapon_flag, surface, sub, plane);
    ok = ok && can(water_weapon_flag, surface, sub, hover);
    ok = ok && can(water_weapon_flag, surface, sub, submerged);
    // ... except against an airborne target when the weapon is also not_to_air.
    const ReachKeys surface_not_air{true, true, false};
    ok = ok && !can(water_weapon_flag, surface_not_air, sub, plane);
    ok = ok && can(water_weapon_flag, surface_not_air, sub, tank);
    ok = ok && can(water_weapon_flag, surface_not_air, sub, hover);
    // not_to_air alone changes nothing on the water path.
    ok = ok && can(water_weapon_flag, {true, false, false}, sub, ship);
    // not_to_underwater: on the water path a target whose model top is at or
    // below sea level is out of reach (top 13 and top 20 at sea 20).
    const ReachKeys not_under{false, false, true};
    ok = ok && !can(water_weapon_flag, not_under, sub, submerged);
    ok = ok && !can(water_weapon_flag, not_under, sub, awash);
    ok = ok && can(water_weapon_flag, not_under, sub, ship);
    const ReachUnitGeometry topping{{0, at(13), 0}, 8, 0, target_type_floats_flag};
    ok = ok && can(water_weapon_flag, not_under, sub, topping);
    ok = ok && can(water_weapon_flag, {false, true, true}, sub, tank);
    ok = ok && !can(water_weapon_flag, {false, true, true}, sub, submerged);
    // not_to_underwater does nothing on the non-water path, whose own test
    // already refuses such a target.
    ok = ok && !can(0, {}, dry_shooter, submerged);
    ok = ok && can(0, not_under, dry_shooter, ship);
    // fire_can_reach: surface_fire lets a non-water weapon fire at a point from
    // below sea level and without a launch pitch.
    const std::array<uint32_t, 3> point{0, at(30), 0};
    ok = ok && !fire_can_reach({0, 100, {}, {}}, sub, point, sea);
    ok = ok && fire_can_reach({0, 100, {}, surface}, sub, point, sea);
    const WeaponReachParameters unsolvable{ballistic_weapon_flag, 100, {1, 0.0F, 7'089}, {}};
    const std::array<uint32_t, 3> away{at(50), at(30), 0};
    ok = ok && !fire_can_reach(unsolvable, dry_shooter, away, sea);
    auto surfacing = unsolvable;
    surfacing.keys.surface_fire = true;
    ok = ok && fire_can_reach(surfacing, dry_shooter, away, sea);
    // Range still applies.
    const std::array<uint32_t, 3> far{at(101), at(30), 0};
    ok = ok && !fire_can_reach({0, 100, {}, surface}, sub, far, sea);
    if (!ok)
        std::fprintf(stderr, "reach under weapon keys differs\n");
    return ok;
}

} // namespace

int main() {
    using namespace oa::sim::ballistics;
    const BallisticParameters unordered_angle{
        35'680'500, std::numeric_limits<float>::quiet_NaN(), 7'089
    };
    const std::array<uint32_t, 3> source{28'997'670, 10'425'216, 8'430'310};
    const std::array<uint32_t, 3> target{4'263'416, 28'560'985, 2'943'459};
    if (!ballistic_feasible(unordered_angle, source, target))
        return 1;
    const auto unordered_dx = static_cast<int32_t>(source[0] - target[0]);
    const auto unordered_dy = static_cast<int32_t>(source[1] - target[1]);
    const auto unordered_dz = static_cast<int32_t>(source[2] - target[2]);
    if (launch_pitch(unordered_angle, unordered_dx, unordered_dy, unordered_dz) != 6480)
        return 6;
    if (launch_pitch(unordered_angle, unordered_dx, unordered_dy, unordered_dz) ==
        invalid_launch_pitch)
        return 7;

    const BallisticParameters level{35'680'500, 0.0F, 7'089};
    if (launch_pitch(level, 1000 * 65536, 0, 0) != 1)
        return 8;
    if (launch_pitch(level, 1000 * 65536, 100 * 65536, 0) != 1037)
        return 9;
    if (launch_pitch(level, 1000 * 65536, -100 * 65536, 0) != 1041)
        return 10;
    if (launch_pitch(level, 0, 0, 0) != invalid_launch_pitch)
        return 11;
    if (launch_pitch({1000, 0.0F, 7'089}, 1000 * 65536, 0, 0) != invalid_launch_pitch)
        return 12;
    if (ballistic_feasible(level, source, target) !=
        (launch_pitch(level, unordered_dx, unordered_dy, unordered_dz) != invalid_launch_pitch))
        return 13;

    const ReachUnitGeometry dry_source{{0, 10U << 16U, 0}, 1, 0, 0};
    const ReachUnitGeometry dry_target{{0, 10U << 16U, 0}, 1, 0, 0};
    if (!weapon_can_reach({0, 1, {}}, dry_source, dry_target, 0))
        return 2;
    if (weapon_can_reach({to_air_weapon_flag, 1, {}}, dry_source, dry_target, 0))
        return 3;

    const ReachUnitGeometry surface_target{{0, 1U << 16U, 0}, 1, 0, 0};
    if (weapon_can_reach({water_weapon_flag, 1, {}}, dry_source, surface_target, 0))
        return 4;
    auto floating_target = surface_target;
    floating_target.type_flags = target_type_floats_flag;
    if (!weapon_can_reach({water_weapon_flag, 1, {}}, dry_source, floating_target, 0))
        return 5;

    const TurretAimGeometry aim_north{{1000 * 65536, 0, 0}, {0, 0, 0}, 0};
    const auto north = turret_aim_ballistic(level, aim_north);
    if (!north.feasible || north.pitch != 1)
        return 14;
    if (north.heading != 0x4000)
        return 15;
    const TurretAimGeometry aim_self{{0, 0, 0}, {0, 0, 0}, 0};
    const auto self = turret_aim_ballistic(level, aim_self);
    if (self.feasible || self.pitch != invalid_launch_pitch)
        return 16;

    const auto los_north = turret_aim_line_of_sight(aim_north);
    if (!los_north.feasible || los_north.pitch != 0 || los_north.heading != 0x4000)
        return 17;
    const auto los_self = turret_aim_line_of_sight(aim_self);
    if (!los_self.feasible || los_self.pitch != 0 || los_self.heading != 0)
        return 18;

    // fire_can_reach: range first, then non-water source height, then the
    // ballistic solution. Unlike weapon_can_reach, target height, air state
    // and water-target flags are ignored.
    const std::array<uint32_t, 3> same_target = dry_source.position;
    if (!fire_can_reach({0, 1, {}}, dry_source, same_target, 0))
        return 19;
    const std::array<uint32_t, 3> far_target{2U << 16U, dry_source.position[1], 0};
    if (fire_can_reach({0, 1, {}}, dry_source, far_target, 0))
        return 20;
    const ReachUnitGeometry submerged{{0, 0, 0}, 0, 0, 0};
    if (!fire_can_reach({water_weapon_flag, 1, {}}, submerged, submerged.position, 5))
        return 21;
    if (fire_can_reach({0, 1, {}}, submerged, submerged.position, 5))
        return 22;
    if (!fire_can_reach({to_air_weapon_flag, 1, {}}, dry_source, same_target, 0))
        return 23;
    const ReachUnitGeometry at_sea{{0, 5U << 16U, 0}, 5, 0, 0};
    if (fire_can_reach({0, 1, {}}, at_sea, at_sea.position, 10))
        return 24;
    const ReachUnitGeometry above_sea{{0, 5U << 16U, 0}, 6, 0, 0};
    if (!fire_can_reach({0, 1, {}}, above_sea, above_sea.position, 10))
        return 25;

    const ReachUnitGeometry aim_source{{1000U * 65536U, 10U << 16U, 0}, 1, 0, 0};
    const std::array<uint32_t, 3> aim_target{0, 10U << 16U, 0};
    const WeaponReachParameters ballistic_level{ballistic_weapon_flag, 1000, level};
    if (!fire_can_reach(ballistic_level, aim_source, aim_target, 0))
        return 26;
    if (fire_can_reach(ballistic_level, aim_source, aim_target, 0) !=
        ballistic_feasible(level, aim_source.position, aim_target))
        return 27;
    const WeaponReachParameters ballistic_slow{ballistic_weapon_flag, 1000, {1000, 0.0F, 7'089}};
    if (fire_can_reach(ballistic_slow, aim_source, aim_target, 0))
        return 28;
    const WeaponReachParameters water_ballistic{
        water_weapon_flag | ballistic_weapon_flag, 1000, {1000, 0.0F, 7'089}
    };
    if (!fire_can_reach(water_ballistic, aim_source, aim_target, 0))
        return 29;
    const ReachUnitGeometry low_source{{1000U * 65536U, 0, 0}, 0, 0, 0};
    if (fire_can_reach(ballistic_level, low_source, aim_target, 1))
        return 30;
    const WeaponReachParameters short_range{ballistic_weapon_flag, 999, level};
    if (fire_can_reach(short_range, aim_source, aim_target, 0))
        return 31;
    const ReachUnitGeometry negative_y{{0, 0xFFFF0000U, 0}, 0, 0, 0};
    if (fire_can_reach({0, 1, {}}, negative_y, negative_y.position, 0))
        return 32;
    if (!fire_can_reach({water_weapon_flag, 1, {}}, negative_y, negative_y.position, 0))
        return 33;
    // turret_aim tests weapon flag bit 1 (ballistic) before bit 0 (line of sight);
    // a weapon with neither, self-propelled included, has no solver.
    const TurretAimGeometry aim_high{{1000 * 65536, 100 * 65536, 0}, {0, 0, 0}, 0x1000};
    const auto by_ballistic =
        turret_aim(ballistic_weapon_flag | line_of_sight_weapon_flag, level, aim_high);
    const auto ballistic_only = turret_aim_ballistic(level, aim_high);
    if (!by_ballistic.feasible || by_ballistic.pitch != ballistic_only.pitch ||
        by_ballistic.heading != ballistic_only.heading || by_ballistic.pitch != 1037)
        return 38;
    const auto by_line = turret_aim(line_of_sight_weapon_flag, level, aim_high);
    const auto line_only = turret_aim_line_of_sight(aim_high);
    if (!by_line.feasible || by_line.pitch != line_only.pitch || by_line.heading != 0x3000 ||
        by_line.pitch == by_ballistic.pitch)
        return 39;
    const auto neither = turret_aim(0x00100000U, level, aim_high);
    if (neither.feasible || neither.heading != 0 || neither.pitch != 0)
        return 40;
    if (turret_aim(ballistic_weapon_flag, level, aim_self).feasible)
        return 41;

    if (simulation_gravity(false, 0, 0) != default_simulation_gravity)
        return 34;
    if (simulation_gravity(false, 500, 112) != 0x1fdb)
        return 35;
    if (simulation_gravity(true, 200, 112) != 14563)
        return 36;
    if (simulation_gravity(true, -1, 0) != default_simulation_gravity)
        return 37;
    if (!pitch_samples_match(
            false, "9361d4437f9a9e135c423d47c8d7f218f77b5f96de9c4c6c42d7b1ac4868786e"
        ))
        return 42;
    if (!pitch_samples_match(
            true, "29faa25766fd44829d2d2b2b948d922464620bc6b611f9d3cc4ff29ec8963804"
        ))
        return 43;
    if (!high_arc_matches())
        return 44;
    if (!reach_keys_match())
        return 45;
    return 0;
}
