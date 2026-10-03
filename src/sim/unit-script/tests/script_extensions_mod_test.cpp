// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit-script extensions under an installed mod's own scripts: every COB file
// in its scripts directory runs in the interpreter for a minute of game time
// (Create, then Activate), as a unit of a four-player world whose players are
// allied, unallied, local, computer and remote, with live, free and
// half-built units. Each GET at an index the mod's profile mounts must read
// what the extension table says (unit_script_get_value), checked against a
// separate reading of that table; scripts that walk every unit id must finish
// each walk within a tick. Prints how many scripts read each extension.
// Skips without OA_MOD_GAME_DIR and OA_MOD_PROFILES_DIR.

#include "oa/data/defs/asset_files.hpp"
#include "oa/formats/cob.hpp"
#include "oa/test/check.hpp"
#include "oa/test/mod_install.hpp"
#include "unit_script_fixture.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace us = oa::sim::unit_script;
namespace mr = oa::data::match_rules;
namespace vm = oa::sim::script_vm;

namespace {

constexpr uint32_t caller_slot = 3;

/// The four-player world the scripts run in.
struct World {
    uint16_t units_per_player;
    uint32_t slot_count;
    us::test::WorldFixture fixture;
    oa::World* world = fixture.world;

    /// Builds the world.
    ///
    /// @param limit units per player
    explicit World(uint16_t limit)
        : units_per_player(limit), slot_count(limit * 10U + 1U), fixture(slot_count) {
        world->game.units_per_player = units_per_player;
        world->game.max_units_setting = units_per_player;
        world->units[0].owner_index = 0xff;
        for (uint32_t slot = 1; slot < slot_count; ++slot) {
            oa::Unit& unit = world->units[slot];
            unit.owner_index = static_cast<uint8_t>((slot - 1) / units_per_player);
            unit.flags = 0;
            unit.def = 1;
        }
        for (uint8_t player = 0; player < OA_PLAYER_COUNT; ++player) {
            world->game.players[player].index = player;
            world->game.players[player].alliance[player] = 1;
        }
        world->game.players[0].alliance[1] = 1;
        world->game.players[1].alliance[0] = 1;
        world->game.players[2].alliance[0] = 1; // one way: player 0 does not ally 2
        world->game.players[0].status = OA_PLAYER_STATUS_LOCAL;
        world->game.players[1].status = OA_PLAYER_STATUS_COMPUTER;
        world->game.players[2].status = OA_PLAYER_STATUS_MIRRORED;
        world->game.players[3].status = OA_PLAYER_STATUS_MIRRORED;
        world->unit_defs[0].max_damage = 1000;
        world->unit_defs[0].model_height = 8 << 16;
        for (uint32_t player = 0; player < 4; ++player)
            for (uint32_t index = 0; index < 6; ++index) {
                oa::Unit& unit = world->units[player * units_per_player + 1 + index];
                unit.flags = OA_UNIT_FLAG_LIVE;
                unit.health = 500;
                unit.position = {
                    static_cast<int32_t>((100 + 40 * index) << 16),
                    0,
                    static_cast<int32_t>((100 + 60 * player) << 16)
                };
                unit.build_remaining = index == 2 ? 0.375F : 0.0F;
                unit.veteran_level = static_cast<uint16_t>(index * 3);
            }
        world->units[7].build_remaining = 0.75F; // a free slot's stale record
    }
};

/// What the extension table (unit_script_get_value) says a GET reads, under
/// exact fidelity: a separate reading of the table the engine implements.
int32_t expected(
    const oa::World& world, const oa::Unit& caller, mr::ScriptExtension extension, int32_t argument
) {
    const uint32_t highest = world.game.max_units_setting * 10U;
    const uint32_t id = std::bit_cast<uint32_t>(argument) & 0xffffU;
    const bool readable = id <= highest && id < world.unit_slot_count;
    const auto owner_status = [&](uint32_t owner) -> uint8_t {
        return owner < OA_PLAYER_COUNT ? world.game.players[owner].status : 0;
    };
    switch (extension) {
    case mr::ScriptExtension::none:
        return 0;
    case mr::ScriptExtension::unit_kills_x100:
        return caller.veteran_level * 100;
    case mr::ScriptExtension::unit_min_id:
        return 1;
    case mr::ScriptExtension::unit_max_id:
        return static_cast<int32_t>(highest);
    case mr::ScriptExtension::unit_my_id:
        return caller.id;
    case mr::ScriptExtension::unit_owner_of:
        return readable ? world.units[id].owner_index : 0;
    case mr::ScriptExtension::unit_build_percent_left_of: {
        const uint64_t offset =
            (uint64_t{std::bit_cast<uint32_t>(argument)} * 0x118U) & 0xffffffffU;
        if (offset % 0x118U != 0 || offset / 0x118U + 1U >= world.unit_slot_count)
            return 0;
        const float remaining = world.units[offset / 0x118U].build_remaining;
        if (remaining == 0.0F)
            return 0;
        return 1 - static_cast<int32_t>(std::trunc(static_cast<double>(remaining) * -99.0));
    }
    case mr::ScriptExtension::unit_allied_with: {
        if (!readable)
            return 0;
        const uint32_t owner = world.units[id].owner_index;
        return owner < OA_PLAYER_COUNT &&
                       world.game.players[caller.owner_index].alliance[owner] != 0
                   ? 1
                   : 0;
    }
    case mr::ScriptExtension::unit_is_local: {
        if (!readable)
            return 0;
        const uint8_t status = owner_status(world.units[id].owner_index);
        return status == OA_PLAYER_STATUS_LOCAL || status == OA_PLAYER_STATUS_COMPUTER ? 1 : 0;
    }
    }
    return 0;
}

oa::FixedVec3 piece_at(void* context, oa::World*, oa::Unit* unit, uint32_t) {
    (void)context;
    return unit->position;
}

uint16_t direction_of(void*, int32_t dx, int32_t dz) {
    return static_cast<uint16_t>(std::atan2(dx, dz) * 32768.0 / 3.14159265358979323846);
}

uint32_t distance_of(void*, int32_t dx, int32_t dz) {
    return static_cast<uint32_t>(std::hypot(static_cast<double>(dx), static_cast<double>(dz)));
}

int32_t height_at(void*, oa::World*, int32_t, int32_t) {
    return 0;
}

void set_flag(void*, oa::World*, oa::Unit* unit, uint8_t mask, bool enabled) {
    unit->state_flags =
        static_cast<uint8_t>(enabled ? unit->state_flags | mask : unit->state_flags & ~mask);
}

void set_yard(void*, oa::World*, oa::Unit* unit, int32_t open) {
    unit->build_flags =
        static_cast<uint8_t>((unit->build_flags & ~OA_UNIT_BUILD_YARD_OPEN) | ((open & 1) << 2));
}

/// What one run saw.
struct Tally {
    std::array<std::set<std::string>, mr::script_extension_ids.size()> scripts{};
    std::array<size_t, mr::script_extension_ids.size()> reads{};
    size_t mismatches = 0;
    size_t full_walks = 0; ///< scripts that read allied-with for every id
};

// A host whose GET and SET go to the engine's handlers, checking each mounted
// GET against the extension table.
struct CheckingHost : us::test::RecordingHost {
    CheckingHost(
        oa::World* world,
        oa::Unit* unit,
        const us::UnitValueServices& services,
        Tally& tally,
        std::string name,
        size_t pieces
    )
        : RecordingHost(pieces), world(world), unit(unit), services(services), tally(tally),
          name(std::move(name)) {}

    int32_t
    get_unit_value(int32_t selector, int32_t first, int32_t second, int32_t, int32_t) override {
        const int32_t value =
            us::unit_script_get_value(world, unit, selector, first, second, services);
        if (selector < 0 || selector > 0xffff || (selector >= 1 && selector <= 20))
            return value;
        const mr::ScriptExtension extension =
            services.rules.rules().script_get.find(static_cast<uint16_t>(selector));
        const auto index = static_cast<size_t>(extension);
        tally.scripts[index].insert(name);
        ++tally.reads[index];
        if (extension == mr::ScriptExtension::unit_allied_with)
            walked.insert(std::bit_cast<uint32_t>(first) & 0xffffU);
        const int32_t want = expected(*world, *unit, extension, first);
        if (value != want) {
            if (++tally.mismatches <= 20)
                std::fprintf(
                    stderr,
                    "%s: get %d(%d) read %d, the table says %d\n",
                    name.c_str(),
                    selector,
                    first,
                    value,
                    want
                );
        }
        return value;
    }

    void set_unit_value(int32_t selector, int32_t value) override {
        us::unit_script_set_value(world, unit, selector, value, services);
    }

    oa::World* world;
    oa::Unit* unit;
    us::UnitValueServices services;
    Tally& tally;
    std::string name;
    std::set<uint32_t> walked;
};

/// The names of a directory's files with an extension.
std::vector<std::string>
listed(const oa::data::defs::Files& files, const char* directory, const char* extension) {
    std::vector<std::string> names;
    files.list(
        files.context,
        directory,
        extension,
        [](void* user, const char* name) {
            static_cast<std::vector<std::string>*>(user)->emplace_back(name);
        },
        &names
    );
    return names;
}

/// Starts a function by name when the script has it.
void start(vm::Vm& machine, const oa::formats::cob::CobProgram& cob, const char* function) {
    for (size_t index = 0; index < cob.scripts.size(); ++index)
        if (cob.scripts[index].name == function) {
            (void)machine.start(static_cast<uint32_t>(index));
            return;
        }
}

/// What running a set of scripts saw.
struct Run {
    size_t scripts = 0;
    size_t faults = 0;
    size_t limit_faults = 0;
    std::vector<std::string> walkers; ///< scripts that read allied-with for every id
};

/// Runs scripts for a number of game ticks each, as a unit of a world of a
/// unit limit.
Run run_scripts(
    const oa::AssetStore& assets,
    const std::vector<std::string>& names,
    const mr::MatchRules& rules,
    uint16_t limit,
    uint32_t game_ticks,
    Tally& tally
) {
    Run run;
    for (const std::string& name : names) {
        const std::string path = "scripts/" + name;
        const auto bytes = assets.load_file_contents(path);
        if (!bytes)
            continue;
        auto parsed = oa::formats::cob::parse_cob(*bytes);
        if (!parsed.ok())
            continue;
        const oa::formats::cob::CobProgram& cob = *parsed.value;
        World world{limit};
        oa::Unit* caller = &world.world->units[caller_slot];
        us::UnitValueServices services{
            nullptr,
            piece_at,
            direction_of,
            distance_of,
            height_at,
            set_flag,
            set_yard,
            mr::MatchRulesView{&rules},
            nullptr
        };
        CheckingHost host(world.world, caller, services, tally, name, cob.piece_names.size());
        vm::Vm machine(us::test::vm_program(cob), host, 30);
        ++run.scripts;
        start(machine, cob, "Create");
        bool faulted = false;
        for (uint32_t tick = 0; tick < game_ticks && !faulted; ++tick) {
            if (tick == 30)
                start(machine, cob, "Activate");
            const vm::TickResult result = machine.tick(1);
            if (!result.ok()) {
                faulted = true;
                ++run.faults;
                if (result.error->code == vm::ErrorCode::instruction_limit) {
                    ++run.limit_faults;
                    std::fprintf(stderr, "%s: a walk did not finish in one tick\n", name.c_str());
                }
            }
        }
        if (host.walked.size() >= world.slot_count - 1)
            run.walkers.push_back(name);
    }
    return run;
}

} // namespace

int main() {
    const oa::test::ModInstall install =
        oa::test::require_mod_install("unit-script-extensions-mod-install");
    const oa::AssetStore assets = oa::test::open_mod_assets(install);
    const oa::data::defs::Files files = oa::data::defs::asset_store_files(&assets);
    const mr::MatchRules& rules = install.profile.rules;
    if (rules.script_get.count == 0)
        oa::test::skip_mod_test(
            "unit-script-extensions-mod-install", "the installed mod's profile mounts no extension"
        );
    OA_CHECK(rules.script_fidelity == mr::ScriptFidelity::exact);

    Tally tally;
    const Run run = run_scripts(assets, listed(files, "scripts", "cob"), rules, 50, 1800, tally);
    tally.full_walks = run.walkers.size();
    std::printf("%zu scripts ran, %zu stopped on a fault\n", run.scripts, run.faults);
    for (size_t index = 1; index < mr::script_extension_ids.size(); ++index)
        std::printf(
            "  %-28s %4zu scripts, %8zu reads\n",
            std::string{mr::script_extension_ids[index]}.c_str(),
            tally.scripts[index].size(),
            tally.reads[index]
        );
    std::printf(
        "  %zu reads at indices nothing is mounted at\n",
        tally.reads[static_cast<size_t>(mr::ScriptExtension::none)]
    );
    std::printf("  %zu scripts walked every unit id\n", tally.full_walks);
    OA_CHECK(run.scripts > 0);
    OA_CHECK(tally.mismatches == 0);
    OA_CHECK(run.limit_faults == 0);

    // The scripts that walk every id, at the largest unit limit a profile
    // reaches in practice: each walk still ends within its tick.
    Tally large;
    const Run walked = run_scripts(assets, run.walkers, rules, 1500, 300, large);
    std::printf(
        "%zu walking scripts at 1500 units per player: %zu faults, %zu reads of allied-with\n",
        walked.scripts,
        walked.faults,
        large.reads[static_cast<size_t>(mr::ScriptExtension::unit_allied_with)]
    );
    OA_CHECK(large.mismatches == 0);
    OA_CHECK(walked.limit_faults == 0);
    // Scripts that read an extension at all, and some that walk every id.
    size_t reading = 0;
    for (size_t index = 1; index < tally.scripts.size(); ++index)
        reading += tally.scripts[index].empty() ? 0 : 1;
    OA_CHECK(reading > 0);
    return oa::test::check_exit_status();
}
