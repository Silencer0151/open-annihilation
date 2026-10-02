// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/ai.hpp"
#include "oa/sim/combat_state.hpp"
#include "oa/sim/unit_movement/movement.hpp"
#include "oa/test/game_assets.hpp"
#include "oa/platform/files.hpp"
#include "oa/test/scratch_directory.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <vector>

using namespace oa::sim::ai;

namespace {
int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

enum Type : uint16_t {
    ARMCOM = 1,
    ARMMEX,
    ARMSOLAR,
    ARMLAB,
    ARMPW,
    ARMCK,
    ARMFIG,
    ARMPT,
    ARMLLT,
    CORMEX,
    TYPE_COUNT
};

struct Order {
    std::string kind;
    uint16_t unit{};
    uint16_t arg{};
    oa::FixedVec3 at;
    bool queue{};
};

// Fake match: a World, squads and a scripted random stream.
struct Fake {
    oa::World world{};
    std::vector<oa::Unit> units = std::vector<oa::Unit>(64);
    std::vector<oa::UnitDef> defs = std::vector<oa::UnitDef>(TYPE_COUNT);
    std::map<std::pair<int, int>, std::vector<uint16_t>> squads;
    std::vector<Order> orders;
    std::vector<uint32_t> rolls;
    std::size_t roll = 0;
    uint8_t strength[TYPE_COUNT][3]{};
    bool allies[10][10]{};
    bool holds_order[64]{};        // the unit has a primary order
    uint32_t knowledge_tick[10]{}; // the match's Sightings.refreshed_tick

    Fake() {
        world.units = units.data();
        world.unit_slot_count = static_cast<uint32_t>(units.size());
        world.unit_defs = defs.data();
        world.unit_def_count = TYPE_COUNT;
        for (int i = 0; i < 10; ++i) {
            allies[i][i] = true;
            world.game.players[i].index = static_cast<uint8_t>(i);
        }
        for (auto& row : strength)
            row[0] = row[1] = row[2] = 50;
    }

    void player(int index, uint8_t status, uint32_t first, uint32_t last) {
        auto& p = world.game.players[index];
        p.in_use = 1;
        p.status = status;
        p.first_unit = oa::oa_ref_from_index(first);
        p.last_unit = oa::oa_ref_from_index(last);
        p.energy = 1000.0F;
        p.metal = 500.0F;
        p.energy_storage = 1000.0F;
        p.metal_storage = 500.0F;
        p.energy_produced = 10.0F;
        p.metal_produced = 5.0F;
    }

    oa::Unit& spawn(uint32_t slot, int owner, uint16_t type, int32_t x, int32_t z, uint32_t flags) {
        auto& u = units[slot];
        u = {};
        u.type_index = type;
        u.owner_index = static_cast<uint8_t>(owner);
        u.owner = oa::oa_ref_from_index(static_cast<uint32_t>(owner));
        u.position = {x << 16, 0, z << 16};
        u.flags = OA_UNIT_FLAG_LIVE | OA_UNIT_FLAG_SELECTABLE | flags;
        return u;
    }

    void set_squad(uint16_t slot, int squad) {
        auto& u = units[slot];
        if (u.squad != -1)
            if (u.squad != 0 || true) {
                auto& old = squads[{u.owner_index, u.squad}];
                for (auto& member : old)
                    if (member == slot) {
                        member = old.back();
                        old.pop_back();
                        break;
                    }
            }
        if (squad != -1)
            squads[{u.owner_index, squad}].push_back(slot);
        u.squad = squad;
    }
};

Fake* fake_of(void* context) {
    return static_cast<Fake*>(context);
}

ComputerHost host_for(Fake& fake) {
    ComputerHost host{};
    host.context = &fake;
    host.world = &fake.world;
    host.difficulty = OA_DIFFICULTY_EASY;
    host.map_cells_x = 64;
    host.map_cells_z = 64;
    host.random = [](void* c, uint32_t bound) -> uint32_t {
        auto* f = fake_of(c);
        if (bound < 2)
            return 0;
        const auto value = f->roll < f->rolls.size() ? f->rolls[f->roll] : 0u;
        ++f->roll;
        return value % bound;
    };
    host.squad_size = [](void* c, uint8_t p, Squad s) -> uint32_t {
        return static_cast<uint32_t>(fake_of(c)->squads[{p, static_cast<int>(s)}].size());
    };
    host.squad_member = [](void* c, uint8_t p, Squad s, uint32_t i) -> uint16_t {
        return fake_of(c)->squads[{p, static_cast<int>(s)}].at(i);
    };
    host.set_squad = [](void* c, uint16_t u, Squad s) {
        fake_of(c)->set_squad(u, static_cast<int>(s));
    };
    host.allied = [](void* c, uint8_t p, uint8_t o) { return fake_of(c)->allies[p][o]; };
    host.primary_order = [](void* c, uint16_t u, uint8_t*, uint8_t*) {
        return fake_of(c)->holds_order[u];
    };
    host.unit_visible = [](void*, uint8_t, uint16_t) { return true; };
    host.strengths = [](void* c, uint8_t, uint16_t t) -> const uint8_t* {
        return t < TYPE_COUNT ? fake_of(c)->strength[t] : nullptr;
    };
    host.site_clear = [](void*, uint16_t, int32_t x, int32_t z) {
        return x >= 0 && z >= 0 && x < 60 && z < 60;
    };
    host.building_site = host.site_clear;
    host.cell_metal = [](void*, int32_t, int32_t) -> uint8_t { return 0; };
    host.metal_feature = [](void*, int32_t, int32_t) { return false; };
    host.order_move = [](void* c, uint16_t u, const oa::FixedVec3* at, bool q) {
        fake_of(c)->orders.push_back({"move", u, 0, *at, q});
        return true;
    };
    host.order_patrol = [](void* c, uint16_t u, const oa::FixedVec3* at, bool q) {
        fake_of(c)->orders.push_back({"patrol", u, 0, *at, q});
        return true;
    };
    host.order_attack = [](void* c, uint16_t u, uint16_t t) {
        fake_of(c)->orders.push_back({"attack", u, t, {}, false});
        return true;
    };
    host.order_build = [](void* c, uint16_t u, uint16_t t, const oa::FixedVec3* at) {
        fake_of(c)->orders.push_back({"build", u, t, *at, false});
        return true;
    };
    host.order_factory = [](void* c, uint16_t u, uint16_t t, int32_t) {
        fake_of(c)->orders.push_back({"factory", u, t, {}, false});
        return true;
    };
    host.set_active = [](void* c, uint16_t u, bool on) {
        fake_of(c)->orders.push_back({on ? "on" : "off", u, 0, {}, false});
    };
    return host;
}

void describe(
    ComputerPlayers& state,
    uint16_t id,
    const char* name,
    const char* side,
    const char* categories,
    uint32_t flags,
    int8_t bm_code
) {
    auto& t = state.types[id];
    std::snprintf(t.unit_name, sizeof t.unit_name, "%s", name);
    std::snprintf(t.side, sizeof t.side, "%s", side);
    std::snprintf(t.categories, sizeof t.categories, "%s", categories);
    t.flags = flags;
    t.bm_code = bm_code;
    t.min_water_depth = -10000;
    t.footprint_x = t.footprint_z = 2;
}

void describe_catalog(ComputerPlayers& state) {
    check(computer_players_reserve_types(&state, TYPE_COUNT), "reserve types");
    describe(state, ARMCOM, "ARMCOM", "ARM", "ARM COMMANDER", OA_UNIT_DEF_FLAG_BUILDER, 1);
    state.types[ARMCOM].abilities = OA_UNIT_DEF_ABILITY_CAN_CAPTURE;
    describe(state, ARMMEX, "ARMMEX", "ARM", "ARM LEVEL1", 0, 0);
    describe(state, ARMSOLAR, "ARMSOLAR", "ARM", "ARM LEVEL1", 0, 0);
    describe(state, ARMLAB, "ARMLAB", "ARM", "ARM PLANT LEVEL1", OA_UNIT_DEF_FLAG_BUILDER, 0);
    describe(state, ARMPW, "ARMPW", "ARM", "ARM KBOT LEVEL1", 0, 1);
    describe(state, ARMCK, "ARMCK", "ARM", "ARM CONSTR LEVEL1", OA_UNIT_DEF_FLAG_BUILDER, 1);
    describe(state, ARMFIG, "ARMFIG", "ARM", "ARM VTOL", OA_UNIT_DEF_FLAG_CAN_FLY, 1);
    describe(state, ARMPT, "ARMPT", "ARM", "ARM SHIP", 0, 1);
    state.types[ARMPT].min_water_depth = 12;
    describe(state, ARMLLT, "ARMLLT", "ARM", "ARM LEVEL1", 0, 0);
    describe(state, CORMEX, "CORMEX", "CORE", "CORE LEVEL1", 0, 0);
}

constexpr const char* kBuildLists = R"(
[CANBUILD]
	{
	[ARMCOM]
		{
		canbuild1=ARMSOLAR;
		canbuild2=ARMMEX;
		canbuild3=ARMLAB;
		canbuild4=NOSUCHUNIT;
		canbuild5=CORMEX;
		}
	[ARMLAB]
		{
		canbuild1=ARMPW;
		canbuild2=ARMCK;
		}
	}
)";

void test_sort_squads() {
    ComputerPlayers state{};
    describe_catalog(state);
    oa::Unit unit{};
    unit.flags = OA_UNIT_FLAG_BUILDING;
    check(computer_sort_squad(unit, state.types[ARMSOLAR]) == Squad::structures, "structure");
    unit.flags |= OA_UNIT_FLAG_HAS_WEAPONS;
    check(computer_sort_squad(unit, state.types[ARMLLT]) == Squad::armed_structures, "defence");
    unit.flags = OA_UNIT_FLAG_HAS_WEAPONS;
    check(computer_sort_squad(unit, state.types[ARMPW]) == Squad::land_army, "army");
    check(computer_sort_squad(unit, state.types[ARMCK]) == Squad::builders, "builder");
    check(computer_sort_squad(unit, state.types[ARMFIG]) == Squad::aircraft, "aircraft");
    check(computer_sort_squad(unit, state.types[ARMPT]) == Squad::navy, "navy");
    unit.flags = 0;
    check(computer_sort_squad(unit, state.types[ARMPW]) == Squad::none, "unarmed mobile");
    computer_players_release(&state);
}

void test_profile() {
    for (const auto difficulty : {OA_DIFFICULTY_EASY, OA_DIFFICULTY_HARD}) {
        Fake fake;
        fake.player(0, OA_PLAYER_STATUS_LOCAL, 1, 5);
        fake.player(1, OA_PLAYER_STATUS_COMPUTER, 10, 30);
        auto host = host_for(fake);
        host.difficulty = difficulty;
        ComputerPlayers state{};
        describe_catalog(state);
        check(
            computer_players_configure(
                &state,
                R"(// sample
plan easy
Weight ARM 0.2
Weight ARMLAB 0.5
Weight PLANT 2
Limit ARMMEX 8
limit level1 3
plan hard
limit ARMMEX 1
weight ARMSOLAR 0.1 # trailing comment
)",
                kBuildLists
            ),
            "configure"
        );
        check(computer_players_initialize(&state, host), "initialize");
        check(!state.players[0].present && state.players[1].present, "computer controller only");
        const auto& k = state.players[1].knowledge;
        if (difficulty == OA_DIFFICULTY_EASY) {
            check(k.weight_percent[ARMPW] == 20, "category weight scales");
            check(k.weight_percent[ARMLAB] == 10, "exact weight locks after category");
            check(k.weight_percent[CORMEX] == 100, "other side untouched");
            check(k.limits[ARMMEX] == 8 && k.limit_locked[ARMMEX] == 1, "exact limit locks");
            check(k.limits[ARMSOLAR] == 3, "category limit");
        } else {
            check(k.weight_percent[ARMPW] == 100, "easy plan skipped on hard");
            check(k.limits[ARMMEX] == 1, "hard limit");
            check(k.weight_percent[ARMSOLAR] == 10, "comment stripped");
        }
        const auto& com = state.types[ARMCOM];
        check(
            com.build_count == 4 && com.build_ids[0] == ARMSOLAR && com.build_ids[2] == ARMLAB &&
                com.build_ids[3] == CORMEX,
            "side build list"
        );
        check(
            state.types[ARMCK].has_build_list && state.types[ARMCK].build_count == 0,
            "builder without list"
        );
        check(
            k.base_weights[ARMLAB] == 60 && k.base_weights[ARMSOLAR] == 40 &&
                k.base_weights[ARMCOM] == 20 && k.base_weights[ARMPW] == 0,
            "base weights"
        );
        computer_players_release(&state);
    }
}

// The downloadable weight and limit passes run once per computer player, each applying the
// downloadable types' directives to every controller in type order. With two
// computer players a category weight compounds over four passes (100 -> 50
// -> 25 -> 12 -> 6); an exact name locks the weight after its first use; the
// limit pass skips a type whose limit the profile locked, so ARMCOM's LEVEL1
// weight is applied only by the two weight passes.
void test_downloadable_directives() {
    Fake fake;
    fake.player(1, OA_PLAYER_STATUS_COMPUTER, 10, 30);
    fake.player(2, OA_PLAYER_STATUS_COMPUTER, 31, 50);
    auto host = host_for(fake);
    ComputerPlayers state{};
    describe_catalog(state);
    state.types[ARMFIG].flags |= OA_UNIT_DEF_FLAG_DOWNLOADABLE;
    std::snprintf(
        state.types[ARMFIG].ai_directives,
        sizeof state.types[ARMFIG].ai_directives,
        "weight VTOL 0.5\nweight ARMPT 0.5\n"
    );
    state.types[ARMCOM].flags |= OA_UNIT_DEF_FLAG_DOWNLOADABLE;
    std::snprintf(
        state.types[ARMCOM].ai_directives,
        sizeof state.types[ARMCOM].ai_directives,
        "weight SHIP 0.5\nweight LEVEL1 0.5\n"
    );
    check(
        computer_players_configure(&state, "plan any\nlimit ARMCOM 2\n", kBuildLists), "configure"
    );
    check(computer_players_initialize(&state, host), "initialize");
    for (const int player : {1, 2}) {
        const auto& k = state.players[player].knowledge;
        check(k.weight_percent[ARMFIG] == 6, "category weight compounds once per pass and player");
        check(k.limit_locked[ARMCOM] == 1, "profile limit locks ARMCOM");
        check(k.weight_percent[ARMMEX] == 25, "limit-locked type skipped by the limit passes");
        // SHIP halves ARMPT first, then ARMFIG's exact directive halves and locks it.
        check(k.weight_percent[ARMPT] == 25 && k.weight_locked[ARMPT] == 1, "exact weight locks");
    }
    computer_players_release(&state);
}

std::string read_all(std::FILE* file) {
    std::string text;
    std::rewind(file);
    char buffer[256];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof buffer, file)) != 0)
        text.append(buffer, n);
    return text;
}

// Layout of the weight report: time from 30 Hz ticks, controller from Player.status,
// null paths as "(null)", then per type the limit ("n/a " when unlimited),
// signed strength triple, weight percentage, unit name and name.
void test_weight_report() {
    Fake fake;
    fake.player(1, OA_PLAYER_STATUS_COMPUTER, 10, 30);
    std::snprintf(fake.world.game.players[1].name, sizeof fake.world.game.players[1].name, "Robot");
    fake.world.game.tick = 108000 + 2 * 1800 + 3 * 30 + 7;
    fake.world.unit_def_count = ARMSOLAR + 1;
    std::snprintf(fake.defs[ARMMEX].unit_name, sizeof fake.defs[ARMMEX].unit_name, "ARMMEX");
    std::snprintf(fake.defs[ARMMEX].name, sizeof fake.defs[ARMMEX].name, "Metal Extractor");
    fake.strength[ARMMEX][1] = 0xfb;
    auto host = host_for(fake);
    host.difficulty = OA_DIFFICULTY_HARD;
    ComputerPlayers state{};
    describe_catalog(state);
    check(
        computer_players_configure(
            &state, "plan any\nlimit ARMMEX 8\nweight ARMSOLAR 0.5\n", kBuildLists
        ),
        "configure"
    );
    check(computer_players_initialize(&state, host), "initialize");
    const auto scratch = oa::test::make_scratch_directory("oa-ai-computer-report");
    std::FILE* out = oa::platform::open_file(scratch / "weights.txt", "w+b");
    computer_write_report(&state, host, 1, {"Maps/Test.TNT", nullptr}, out);
    const std::string expected = "Match clock: 01:02:03\r\n"
                                 "Name: 'Robot' in player slot 1\r\n"
                                 "Played by: AI\r\n"
                                 "Map file: 'Maps/Test.TNT'\r\n"
                                 "AI settings file: '(null)'\r\n"
                                 "Challenge level: 'HARD'\r\n"
                                 "================================================\r\n"
                                 "Columns: build limit - priority : metal value : energy value "
                                 "= weight percent before economy adjustment - short name : "
                                 "full name\r\n"
                                 "n/a  -  50 :  50 :  50 = 100 - '\t\t:'\r\n"
                                 "   8 -  50 :  -5 :  50 = 100 - 'ARMMEX\t\t:Metal Extractor'\r\n"
                                 "n/a  -  50 :  50 :  50 =  50 - '\t\t:'\r\n";
    check(read_all(out) == expected, "weight report text");
    std::fclose(out);
    out = oa::platform::open_file(scratch / "header.txt", "w+b");
    fake.world.game.players[0].status = OA_PLAYER_STATUS_MIRRORED;
    computer_write_report(&state, host, 0, {}, out);
    const auto header = read_all(out);
    check(
        header.find("Played by: INVALID\r\n") != std::string::npos &&
            header.find("Map file: '(null)'") != std::string::npos &&
            header.ends_with("short name : full name\r\n"),
        "report header only without a controller"
    );
    std::fclose(out);
    std::error_code removal;
    std::filesystem::remove_all(scratch, removal);
    computer_players_release(&state);
}

void test_score_and_pick() {
    Fake fake;
    fake.player(1, OA_PLAYER_STATUS_COMPUTER, 10, 30);
    auto host = host_for(fake);
    ComputerPlayers state{};
    describe_catalog(state);
    check(computer_players_configure(&state, "", kBuildLists), "configure");
    check(computer_players_initialize(&state, host), "initialize");
    auto& p = fake.world.game.players[1];
    // Full stores and an income of at least 200 energy and 5 metal: all
    // weight on the base priority.
    p.energy_produced = 200.0F;
    fake.strength[ARMSOLAR][0] = 80;
    fake.strength[ARMSOLAR][1] = 10;
    fake.strength[ARMSOLAR][2] = 30;
    check(computer_build_score(&state, host, 1, ARMSOLAR) == 80, "base priority score");
    // The build score grades the income, not the stores:
    // energy under 50 adds 100 to the energy need, under 200 adds 10; metal
    // under 3 adds 100 to the metal need, under 5 adds 20.
    p.energy_produced = 49.0F;
    check(computer_build_score(&state, host, 1, ARMSOLAR) == 30, "low energy income");
    p.energy_produced = 199.0F;
    check(
        computer_build_score(&state, host, 1, ARMSOLAR) == (30 * 10 + 80 * 90) / 100,
        "modest energy income"
    );
    p.energy_produced = 200.0F;
    p.metal_produced = 2.0F;
    check(computer_build_score(&state, host, 1, ARMSOLAR) == 10, "low metal income");
    p.metal_produced = 4.0F;
    check(
        computer_build_score(&state, host, 1, ARMSOLAR) == (10 * 20 + 80 * 80) / 100,
        "modest metal income"
    );
    p.metal_produced = 5.0F;
    // Metal short: metal need 100 takes every share; score is strength[1].
    p.metal = 30.0F;
    p.metal_storage = 500.0F;
    p.metal_produced = 0.0F;
    check(computer_build_score(&state, host, 1, ARMSOLAR) == 10, "metal need score");
    // Energy need: (1000 - 600) * 0.125 = 50; metal need 0 -> e 50, base 50.
    p.metal = 500.0F;
    p.metal_produced = 5.0F;
    p.energy = 600.0F;
    check(
        computer_build_score(&state, host, 1, ARMSOLAR) == (30 * 50 + 80 * 50) / 100,
        "energy split score"
    );
    p.energy = 40.0F;
    check(computer_build_score(&state, host, 1, ARMSOLAR) == 0, "low energy rejects");
    p.energy = 1000.0F;
    state.players[1].knowledge.limits[ARMSOLAR] = 0;
    check(computer_build_score(&state, host, 1, ARMSOLAR) == 0, "limit rejects");
    state.players[1].knowledge.limits[ARMSOLAR] = unlimited;
    state.players[1].knowledge.weight_percent[ARMSOLAR] = 50;
    check(computer_build_score(&state, host, 1, ARMSOLAR) == 40, "weight scales score");

    // Pick: running total 40 (ARMSOLAR), 90, 140; rolls keep the later ones.
    fake.spawn(10, 1, ARMCOM, 100, 100, 0);
    fake.rolls = {0, 10, 139, 200};
    const auto pick = computer_pick_build(&state, host, 1, 10);
    check(pick == ARMMEX || pick == ARMSOLAR || pick == ARMLAB || pick == 0, "pick in list");
    fake.roll = 0;
    fake.rolls = {0, 0, 0, 0};
    check(computer_pick_build(&state, host, 1, 10) == 0, "other-side pick rejected");
    computer_players_release(&state);
}

// An unscripted lab (CC14's ARMLAB, AC10's CORLAB) under a computer whose
// metal income is below 3. The strategic refresh values a type's metal as
// its metal cost x -0.02, +100 for an extractor and +25 for a maker, and its
// energy as its energy cost x -0.0025 less five times its energy rate, each
// clamped to 0..100, so a kbot keeps only its base priority. The build score then
// gives the metal value the whole share (metal need >= 100), and the pick
// has nothing to pick until the income reaches 3.
void test_lab_idle_without_metal_income() {
    namespace combat = oa::sim::combat_state;
    std::vector<combat::StrategicType> strategic(TYPE_COUNT);
    for (auto& type : strategic)
        type.min_water_depth = -10000;
    auto& pw = strategic[ARMPW]; // ARMPW.FBI
    pw.abilities = combat::ability_can_attack;
    pw.build_cost_energy = 697.0F;
    pw.build_cost_metal = 53.0F;
    pw.energy_use = 0.3F;
    pw.energy_make = 0.3F;
    auto& ck = strategic[ARMCK]; // ARMCK.FBI
    ck.flags = combat::def_flag_builder;
    ck.build_cost_energy = 2410.0F;
    ck.build_cost_metal = 120.0F;
    ck.energy_use = 0.1F;
    ck.energy_make = 8.0F;
    auto& mex = strategic[ARMMEX]; // ARMMEX.FBI
    mex.build_cost_energy = 521.0F;
    mex.build_cost_metal = 50.0F;
    mex.energy_use = 3.0F;
    mex.extracts_metal = 0.001F;
    auto& solar = strategic[ARMSOLAR]; // ARMSOLAR.FBI
    solar.build_cost_energy = 760.0F;
    solar.build_cost_metal = 145.0F;
    solar.energy_use = -20.0F;
    combat::StrategicRefreshState refreshed;
    combat::strategic_refresh(refreshed, strategic, {});
    using Strength = std::array<uint8_t, 3>; // priority, metal value, energy value
    check(refreshed.strengths[ARMPW] == Strength{84, 0, 0}, "kbot has no metal or energy value");
    check(refreshed.strengths[ARMCK] == Strength{100, 0, 0}, "constructor has no metal value");
    check(refreshed.strengths[ARMMEX] == Strength{100, 99, 0}, "extractor metal value");
    check(refreshed.strengths[ARMSOLAR] == Strength{100, 0, 98}, "solar energy value");

    Fake fake;
    fake.player(1, OA_PLAYER_STATUS_COMPUTER, 10, 30);
    for (uint16_t type = 1; type < TYPE_COUNT; ++type)
        std::memcpy(fake.strength[type], refreshed.strengths[type].data(), 3);
    auto host = host_for(fake);
    ComputerPlayers state{};
    describe_catalog(state);
    check(computer_players_configure(&state, "", kBuildLists), "configure");
    check(computer_players_initialize(&state, host), "initialize");
    fake.spawn(10, 1, ARMLAB, 100, 100, OA_UNIT_FLAG_BUILDING);
    auto& p = fake.world.game.players[1];
    p.energy_produced = 200.0F;
    p.metal_produced = 2.9F;
    check(computer_build_score(&state, host, 1, ARMPW) == 0, "kbot scores 0 under 3 metal");
    check(computer_build_score(&state, host, 1, ARMCK) == 0, "constructor scores 0 under 3 metal");
    check(computer_build_score(&state, host, 1, ARMMEX) == 99, "extractor keeps its metal value");
    fake.rolls = {0, 0};
    check(computer_pick_build(&state, host, 1, 10) == 0, "lab idle under 3 metal");
    // Energy income under 50 with no surplus makes the energy need 120; the 20
    // above the metal share goes to the energy value, which kbots lack too.
    p.energy_produced = 10.0F;
    p.energy_requested = 10.0F;
    check(computer_build_score(&state, host, 1, ARMSOLAR) == 98 * 20 / 100, "energy share");
    check(computer_pick_build(&state, host, 1, 10) == 0, "lab idle on low energy too");
    // From 3 metal the need is 20, leaving 80 on the base priority.
    p.energy_produced = 200.0F;
    p.energy_requested = 0.0F;
    p.metal_produced = 3.0F;
    check(
        computer_build_score(&state, host, 1, ARMPW) == 84 * 80 / 100, "kbot priority from 3 metal"
    );
    // From 5 metal with a surplus the base priority takes the whole share:
    // running totals 84 then 184; the second roll (150) keeps ARMPW.
    p.metal_produced = 5.0F;
    p.metal_requested = 1.0F;
    check(computer_build_score(&state, host, 1, ARMCK) == 100, "constructor priority from 5 metal");
    fake.roll = 0;
    fake.rolls = {0, 150};
    check(computer_pick_build(&state, host, 1, 10) == ARMPW, "lab picks from 5 metal");
    computer_players_release(&state);
}

// The match's knowledge refresh as it reaches a computer player: every 30
// ticks the walk reports the player's live, finished own units.
void refresh_knowledge(ComputerPlayers& state, Fake& fake, uint8_t index) {
    constexpr uint32_t refresh_period = 30;
    auto* knowledge = computer_player_knowledge(&state, index);
    const auto tick = fake.world.game.tick;
    if (knowledge == nullptr || fake.knowledge_tick[index] + refresh_period > tick)
        return;
    computer_knowledge_clear(&state, *knowledge);
    KnowledgeTally tally;
    for (const auto& unit : fake.units)
        if ((unit.flags & OA_UNIT_FLAG_LIVE) != 0 && unit.owner_index == index &&
            unit.build_remaining == 0.0F)
            computer_knowledge_count(&state, *knowledge, unit, tally);
    computer_knowledge_settle(*knowledge, tally);
    fake.knowledge_tick[index] = tick;
}

// One match tick of the computer players: each player's controller order
// tick when it is a computer, then its knowledge refresh.
void tick_players(ComputerPlayers& state, Fake& fake, const ComputerHost& host) {
    for (uint8_t index = 0; index < OA_PLAYER_COUNT; ++index) {
        const auto& player = host.world->game.players[index];
        if (player.in_use == 0)
            continue;
        if (player_has_controller(player) && player.status == OA_PLAYER_STATUS_COMPUTER)
            computer_player_tick_orders(&state, host, index);
        refresh_knowledge(state, fake, index);
    }
}

uint32_t standing_move(const oa::Unit& unit) {
    return (unit.flags & OA_UNIT_FLAG_MOVE_ORDER_MASK) >> OA_UNIT_FLAG_MOVE_ORDER_SHIFT;
}

uint32_t standing_fire(const oa::Unit& unit) {
    return (unit.flags & OA_UNIT_FLAG_FIRE_ORDER_MASK) >> OA_UNIT_FLAG_FIRE_ORDER_SHIFT;
}

void tick_until(
    ComputerPlayers& state, Fake& fake, const ComputerHost& host, uint32_t first, uint32_t last
) {
    for (uint32_t tick = first; tick <= last; ++tick) {
        fake.world.game.tick = tick;
        tick_players(state, fake, host);
    }
}

// The squad sort takes only units with OA_UNIT_FLAG_SELECTABLE. A unit an InitialMission script
// holds (which clears the bit) keeps its squad and its scripted standing
// orders until its MakeSelectable order runs.
void test_scripted_units_stay_unsorted() {
    Fake fake;
    fake.player(0, OA_PLAYER_STATUS_LOCAL, 1, 9);
    fake.player(1, OA_PLAYER_STATUS_COMPUTER, 10, 40);
    auto host = host_for(fake);
    ComputerPlayers state{};
    describe_catalog(state);
    check(computer_players_configure(&state, "", kBuildLists), "scripted: configure");
    constexpr uint32_t hold_fire = 0, hold_position = 0;
    auto& free_tank = fake.spawn(12, 1, ARMPW, 300, 300, OA_UNIT_FLAG_HAS_WEAPONS);
    auto& scripted = fake.spawn(13, 1, ARMPW, 320, 300, OA_UNIT_FLAG_HAS_WEAPONS);
    scripted.flags = (scripted.flags & ~OA_UNIT_FLAG_SELECTABLE) | OA_UNIT_FLAG_NOT_SELECTABLE;
    auto& tower =
        fake.spawn(14, 1, ARMLLT, 340, 300, OA_UNIT_FLAG_BUILDING | OA_UNIT_FLAG_HAS_WEAPONS);
    tower.flags &= ~OA_UNIT_FLAG_SELECTABLE;
    fake.rolls.assign(4096, 7);
    check(computer_players_initialize(&state, host), "scripted: initialize");
    tick_until(state, fake, host, 1, 30);
    check(free_tank.squad == static_cast<int32_t>(Squad::land_army), "unscripted tank sorted");
    check(
        standing_move(free_tank) == 2 && standing_fire(free_tank) == 2,
        "unscripted tank roams and fires at will"
    );
    check(scripted.squad == 0 && tower.squad == 0, "scripted units keep no squad");
    check(
        standing_move(scripted) == hold_position && standing_fire(scripted) == hold_fire,
        "scripted standing orders untouched"
    );
    // MakeSelectable: clear OA_UNIT_FLAG_NOT_SELECTABLE, set OA_UNIT_FLAG_SELECTABLE; the
    // next sort takes the unit.
    scripted.flags = (scripted.flags & ~OA_UNIT_FLAG_NOT_SELECTABLE) | OA_UNIT_FLAG_SELECTABLE;
    tick_until(state, fake, host, 31, 60);
    check(scripted.squad == static_cast<int32_t>(Squad::land_army), "selectable tank sorted");
    check(
        standing_move(scripted) == 2 && standing_fire(scripted) == 2,
        "selectable tank takes the standing orders"
    );
    check(tower.squad == 0, "tower still held by its script");
    computer_players_release(&state);
}

// The structures task queues a pick only for a factory whose primary order list is
// empty: a sorted factory still busy with a build or a Wait gets none until
// its orders run out.
void test_busy_factory_gets_no_pick() {
    Fake fake;
    fake.player(0, OA_PLAYER_STATUS_LOCAL, 1, 9);
    fake.player(1, OA_PLAYER_STATUS_COMPUTER, 10, 40);
    auto host = host_for(fake);
    ComputerPlayers state{};
    describe_catalog(state);
    check(computer_players_configure(&state, "", kBuildLists), "busy factory: configure");
    fake.spawn(11, 1, ARMLAB, 240, 200, OA_UNIT_FLAG_BUILDING);
    fake.spawn(12, 1, ARMLAB, 300, 200, OA_UNIT_FLAG_BUILDING);
    fake.holds_order[12] = true;
    fake.rolls.assign(4096, 7);
    check(computer_players_initialize(&state, host), "busy factory: initialize");
    const auto picks = [&fake](uint16_t slot) {
        uint32_t count = 0;
        for (const auto& order : fake.orders)
            count += order.kind == "factory" && order.unit == slot ? 1u : 0u;
        return count;
    };
    tick_until(state, fake, host, 1, 120);
    check(
        fake.units[11].squad == static_cast<int32_t>(Squad::structures) &&
            fake.units[12].squad == static_cast<int32_t>(Squad::structures),
        "both factories sorted into the structures squad"
    );
    check(picks(11) != 0, "the idle factory queues a pick");
    check(picks(12) == 0, "a factory holding an order gets no pick");
    fake.holds_order[12] = false;
    tick_until(state, fake, host, 121, 180);
    check(picks(12) != 0, "the factory queues a pick once its orders run out");
    computer_players_release(&state);
}

void test_controller_presence() {
    oa::Player player{};
    check(!player_has_controller(player), "a free slot has no controller");
    player.status = OA_PLAYER_STATUS_LOCAL;
    check(player_has_controller(player), "a closed local slot still gets one");
    player.in_use = 1;
    check(player_has_controller(player), "local player");
    player.status = OA_PLAYER_STATUS_COMPUTER;
    check(player_has_controller(player), "computer player");
    player.status = OA_PLAYER_STATUS_MIRRORED;
    check(
        !player_has_controller(player), "a mirrored player runs its controller on its own machine"
    );
    player.in_use = 0;
    check(
        player_has_controller(player),
        "the mirrored-player test reads the record's in-use word first"
    );
}

void test_tick() {
    Fake fake;
    fake.player(0, OA_PLAYER_STATUS_LOCAL, 1, 9);
    fake.player(1, OA_PLAYER_STATUS_COMPUTER, 10, 40);
    auto host = host_for(fake);
    ComputerPlayers state{};
    describe_catalog(state);
    check(computer_players_configure(&state, "", kBuildLists), "configure");
    fake.strength[CORMEX][0] = fake.strength[CORMEX][1] = fake.strength[CORMEX][2] = 0;
    fake.spawn(1, 0, ARMLLT, 900, 900, OA_UNIT_FLAG_BUILDING | OA_UNIT_FLAG_HAS_WEAPONS);
    fake.spawn(10, 1, ARMCOM, 200, 200, 0);
    fake.spawn(11, 1, ARMLAB, 240, 200, OA_UNIT_FLAG_BUILDING);
    for (uint32_t slot = 12; slot < 16; ++slot)
        fake.spawn(slot, 1, ARMPW, 300 + static_cast<int32_t>(slot), 300, OA_UNIT_FLAG_HAS_WEAPONS);
    fake.rolls.assign(4096, 7);
    check(computer_players_initialize(&state, host), "initialize");
    // The first squad sort comes after the 30-tick countdown.
    for (uint32_t tick = 1; tick <= 29; ++tick) {
        fake.world.game.tick = tick;
        tick_players(state, fake, host);
    }
    check(fake.units[10].squad == 0, "no sort before the countdown");
    fake.world.game.tick = 30;
    tick_players(state, fake, host);
    // Tasks already ran on empty squads; the next due runs see the members.
    for (uint32_t tick = 31; tick <= 120; ++tick) {
        fake.world.game.tick = tick;
        tick_players(state, fake, host);
    }
    check(
        fake.units[10].squad == 4 && fake.units[11].squad == 1 && fake.units[12].squad == 3,
        "sorted into squads"
    );
    check(
        ((fake.units[12].flags & OA_UNIT_FLAG_MOVE_ORDER_MASK) >> OA_UNIT_FLAG_MOVE_ORDER_SHIFT) ==
                2 &&
            ((fake.units[10].flags & OA_UNIT_FLAG_MOVE_ORDER_MASK) >>
             OA_UNIT_FLAG_MOVE_ORDER_SHIFT) == 1,
        "standing move orders"
    );
    bool factory = false, build = false, attack = false;
    for (const auto& order : fake.orders) {
        factory |= order.kind == "factory" && order.unit == 11 &&
                   (order.arg == ARMPW || order.arg == ARMCK);
        build |= order.kind == "build" && order.unit == 10;
    }
    check(factory, "idle factory queues a unit");
    check(build, "commander places a build");
    // Four army units exceed the strike minimum: the strike squad recruits
    // them and, with no home squad having members, attacks the nearest enemy.
    fake.orders.clear();
    fake.set_squad(11, 0);
    fake.units[11].flags = 0;
    fake.set_squad(10, 0);
    fake.units[10].flags = 0;
    fake.world.game.tick = 400;
    tick_players(state, fake, host);
    std::size_t strike = fake.squads[{1, 2}].size();
    for (const auto& order : fake.orders)
        attack |= order.kind == "attack" && order.arg == 1;
    check(strike == 4, "strike squad recruited the army");
    check(attack, "strike squad attacks the nearest enemy");
    computer_players_release(&state);
}

// The sighted weight near a point: the signed base weights of the units on the
// player's seen list within the radius on the ground plane, a unit's distance
// taken from the whole parts of its squared 16.16 offsets.
void test_sighted_weight() {
    Fake fake;
    fake.player(1, OA_PLAYER_STATUS_COMPUTER, 10, 30);
    auto host = host_for(fake);
    ComputerPlayers state{};
    describe_catalog(state);
    check(computer_players_configure(&state, "", kBuildLists), "configure");
    check(computer_players_initialize(&state, host), "initialize");
    auto* knowledge = computer_player_knowledge(&state, 1);
    check(knowledge != nullptr && knowledge->base_weights[ARMLLT] == 40, "structure weight");
    knowledge->base_weights[ARMPW] = -5;
    constexpr int32_t x = 500, z = 700;
    const oa::FixedVec3 at{x << 16, 0, z << 16};
    fake.spawn(40, 0, ARMLLT, x + 96, z + 128, OA_UNIT_FLAG_BUILDING); // exactly 160 away
    fake.spawn(41, 0, ARMLLT, x + 161, z, OA_UNIT_FLAG_BUILDING);      // just outside
    fake.spawn(42, 0, ARMPW, x, z - 10, 0).flags = 0;                  // dead, still counts
    fake.spawn(43, 0, ARMLLT, x - 160, z, OA_UNIT_FLAG_BUILDING);
    // 200/65536 of a unit past 160: the square's whole part is still 25600.
    fake.units[43].position.x -= 200;
    fake.spawn(44, 0, ARMLLT, x, z, OA_UNIT_FLAG_BUILDING);
    fake.units[44].type_index = TYPE_COUNT + 3; // outside the type table: adds nothing
    fake.spawn(45, 0, ARMSOLAR, x + 20, z + 20, OA_UNIT_FLAG_BUILDING); // not sighted
    std::array<uint16_t, 6> seen{40, 41, 42, 43, 44, 63};
    oa::sim::detection::Sightings sightings{};
    sightings.seen = seen.data();
    sightings.capacity = static_cast<uint32_t>(seen.size());
    sightings.seen_count = static_cast<uint32_t>(seen.size());
    // 40 + 40 - 5 for units 40, 43 and 42; slot 63 is an empty record of type 0.
    check(
        computer_sighted_weight(&state, *knowledge, sightings, fake.world, at, 160) == 75,
        "sighted weight within 160"
    );
    check(
        computer_sighted_weight(&state, *knowledge, sightings, fake.world, at, 159) == -5,
        "sighted weight within 159"
    );
    check(
        computer_sighted_weight(&state, *knowledge, sightings, fake.world, at, 161) == 115,
        "sighted weight within 161"
    );
    sightings.seen_count = 0;
    check(
        computer_sighted_weight(&state, *knowledge, sightings, fake.world, at, 160) == 0,
        "no sightings"
    );
    computer_players_release(&state);
}

// What the siege test's host answers beyond the fake match.
struct SiegeView {
    std::array<uint16_t, 4> seen{};
    oa::sim::detection::Sightings sightings{};
    bool reach[64]{};
    int32_t visible_below_z = 0; // player 1 sees points with z under this, world units
};

SiegeView siege_view;

// The siege task: with members in squad 9 it turns its search now and then,
// steps it, weighs a point the player sees by what it has sighted around it,
// keeps the better draw as its target and orders its members to attack there.
void test_siege() {
    Fake fake;
    auto& siege = siege_view;
    siege = {};
    fake.world.game.map_width_world = 1024;
    fake.world.game.map_height_world = 512;
    fake.player(0, OA_PLAYER_STATUS_LOCAL, 1, 9);
    fake.player(1, OA_PLAYER_STATUS_COMPUTER, 10, 40);
    auto host = host_for(fake);
    host.point_visible = [](void*, uint8_t player, const oa::FixedVec3* at) {
        return player == 1 && (at->z >> 16) < siege_view.visible_below_z;
    };
    host.sightings = [](void*, uint8_t player) -> const oa::sim::detection::Sightings* {
        return player == 1 ? &siege_view.sightings : nullptr;
    };
    host.weapon_reaches = [](void*, uint16_t unit, const oa::FixedVec3*) {
        return unit < 64 && siege_view.reach[unit];
    };
    host.order_attack_point = [](void* c, uint16_t unit, const oa::FixedVec3* at) {
        fake_of(c)->orders.push_back({"attack point", unit, 0, *at, false});
        return true;
    };
    fake.defs[ARMPW].abilities = OA_UNIT_DEF_ABILITY_CAN_ATTACK;
    fake.defs[ARMLLT].abilities = OA_UNIT_DEF_ABILITY_CAN_ATTACK;
    ComputerPlayers state{};
    describe_catalog(state);
    check(computer_players_configure(&state, "", kBuildLists), "configure");
    check(computer_players_initialize(&state, host), "initialize");
    auto& ai = state.players[1];
    auto& task = ai.tasks[static_cast<uint32_t>(Squad::siege)];
    check(task.kind == TaskKind::siege, "siege task");
    const oa::FixedVec3 centre{512 << 16, 0, 256 << 16};
    const auto same = [](const oa::FixedVec3& a, const oa::FixedVec3& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    };
    check(
        same(task.siege_target, centre) && same(task.siege_probe, centre) &&
            same(task.siege_step, centre) && task.siege_weight == 0,
        "the siege starts at the map's centre"
    );
    // Only the siege runs: every other task and the sort wait.
    for (auto& other : ai.tasks)
        if (&other != &task)
            other.next_tick = 1000000;
    ai.sort_countdown = 1000000;
    fake.world.game.tick = 100;
    // An empty squad takes only the reschedule's draw.
    fake.rolls = {7};
    fake.roll = 0;
    computer_player_tick_orders(&state, host, 1);
    check(fake.roll == 1 && task.next_tick == 100 + 7 + 30, "empty siege squad reschedules");
    check(same(task.siege_probe, centre) && fake.orders.empty(), "empty siege squad stays");

    // Members: two armed walkers, two armed structures (one in reach) and a
    // builder that cannot attack.
    fake.spawn(20, 1, ARMPW, 100, 100, OA_UNIT_FLAG_HAS_WEAPONS).movement = 1;
    fake.spawn(21, 1, ARMLLT, 120, 100, OA_UNIT_FLAG_BUILDING | OA_UNIT_FLAG_HAS_WEAPONS);
    fake.spawn(22, 1, ARMLLT, 140, 100, OA_UNIT_FLAG_BUILDING | OA_UNIT_FLAG_HAS_WEAPONS);
    fake.spawn(23, 1, ARMCK, 160, 100, 0).movement = 1;
    fake.spawn(24, 1, ARMPW, 180, 100, OA_UNIT_FLAG_HAS_WEAPONS).movement = 1;
    siege.reach[22] = true;
    for (const uint16_t slot : std::array<uint16_t, 5>{20, 21, 22, 23, 24}) {
        auto& unit = fake.units[slot];
        unit.def = oa::world_unit_def_ref(&fake.world, &fake.defs[unit.type_index]);
        fake.set_squad(slot, static_cast<int>(Squad::siege));
    }
    // Sighted around the first step: two structures of weight 40 in reach, one
    // out of it.
    fake.spawn(2, 0, ARMLLT, 512 + 96, 576 + 128, OA_UNIT_FLAG_BUILDING);
    fake.spawn(3, 0, ARMLLT, 512, 576 - 30, OA_UNIT_FLAG_BUILDING);
    fake.spawn(4, 0, ARMLLT, 512 + 170, 576, OA_UNIT_FLAG_BUILDING);
    siege.seen = {2, 3, 4, 0};
    siege.sightings.seen = siege.seen.data();
    siege.sightings.capacity = 4;
    siege.sightings.seen_count = 3;
    siege.visible_below_z = 800;
    // Reschedule 9; the search turns (0 of 10) to half a turn, so it steps 320
    // world units toward +z; the point weighs 80 and its draw 12 beats the
    // target's, which draws nothing below 2.
    fake.world.game.tick = task.next_tick;
    fake.rolls = {9, 0, 0x8000, 12};
    fake.roll = 0;
    computer_player_tick_orders(&state, host, 1);
    const oa::FixedVec3 step{
        -oa::sim::unit_movement::sine_scaled(0x8000, 0x1400000),
        0,
        -oa::sim::unit_movement::cosine_scaled(0x8000, 0x1400000)
    };
    check(step.x == 0 && step.z == 0x1400000, "half a turn steps along +z");
    const oa::FixedVec3 first{512 << 16, 0, 576 << 16};
    check(fake.roll == 4, "four draws");
    check(same(task.siege_step, step) && same(task.siege_probe, first), "the search stepped");
    check(
        same(task.siege_target, first) && task.siege_weight == 80, "the richer point is the target"
    );
    std::vector<uint16_t> ordered;
    for (const auto& order : fake.orders) {
        check(order.kind == "attack point" && same(order.at, first), "attack at the target");
        ordered.push_back(order.unit);
    }
    check(
        ordered == std::vector<uint16_t>{20, 22, 24},
        "walkers, and structures in reach, attack; the builder does not"
    );
    // The next run keeps the heading, steps past what the player sees and
    // leaves the target; one after that sees a poorer point, whose draw loses.
    fake.orders.clear();
    fake.world.game.tick = task.next_tick;
    siege.visible_below_z = 800;
    fake.rolls = {1, 3};
    fake.roll = 0;
    computer_player_tick_orders(&state, host, 1);
    const oa::FixedVec3 second{512 << 16, 0, 896 << 16};
    check(fake.roll == 2 && same(task.siege_probe, second), "unseen step draws nothing more");
    check(same(task.siege_target, first) && task.siege_weight == 80, "the target stays");
    check(fake.orders.size() == 3, "the members attack the target again");
    fake.world.game.tick = task.next_tick;
    siege.visible_below_z = 2000;
    siege.sightings.seen_count = 1; // unit 2 alone, far from the third point
    fake.rolls = {1, 3, 5};
    fake.roll = 0;
    computer_player_tick_orders(&state, host, 1);
    check(fake.roll == 3, "a seen point with nothing near draws only against the target");
    check(same(task.siege_target, first) && task.siege_weight == 80, "a poorer point loses");
    computer_players_release(&state);
}

// The build score is nothing for a DOWNLOADABLE type (OA_UNIT_DEF_FLAG_DOWNLOADABLE)
// while the session object's kind is 1, a campaign.
void test_campaign_downloadables() {
    Fake fake;
    fake.player(1, OA_PLAYER_STATUS_COMPUTER, 10, 30);
    auto host = host_for(fake);
    ComputerPlayers state{};
    describe_catalog(state);
    state.types[ARMLLT].flags |= OA_UNIT_DEF_FLAG_DOWNLOADABLE;
    check(computer_players_configure(&state, "", kBuildLists), "configure");
    check(computer_players_initialize(&state, host), "initialize");
    // Full stores and positive income: the score is the base priority.
    check(computer_build_score(&state, host, 1, ARMLLT) == 50, "downloadable outside a campaign");
    state.downloadables_restricted = 1;
    check(computer_build_score(&state, host, 1, ARMLLT) == 0, "campaign skips downloadable");
    check(computer_build_score(&state, host, 1, ARMSOLAR) == 50, "campaign keeps other types");
    computer_players_release(&state);
}

// ReloadAIProfiles resets the computer players' tables, then runs the profile
// again. The plan flag is the one setting the directives share: nothing sets it
// before a session's first load, and the downloadable passes leave it set, so a
// line before the first plan applies on a reload only.
void test_profile_reload() {
    constexpr const char* profile = "weight ARMSOLAR 0.5\nplan easy\nlimit ARMMEX 4\n";
    Fake fake;
    fake.player(0, OA_PLAYER_STATUS_LOCAL, 1, 5);
    fake.player(1, OA_PLAYER_STATUS_COMPUTER, 10, 30);
    fake.rolls.assign(64, 5);
    auto host = host_for(fake);
    ComputerPlayers state{};
    describe_catalog(state);
    check(
        computer_players_reload_profile(&state, host, profile) && !state.initialized,
        "a reload before the first initialisation only stores the profile"
    );
    check(computer_players_configure(&state, profile, kBuildLists), "configure");
    check(computer_players_initialize(&state, host), "initialize");
    auto& k = state.players[1].knowledge;
    check(
        k.weight_percent[ARMSOLAR] == 100 && k.weight_locked[ARMSOLAR] == 0,
        "no plan matched before the first load"
    );
    check(k.limits[ARMMEX] == 4 && k.limit_locked[ARMMEX] == 1, "easy plan limit");
    check(state.plan_matches, "the downloadable passes leave the plan flag set");
    computer_apply_weight(&state, 1, "ARMPW", 0.25F);
    computer_apply_limit(&state, 1, "ARMLLT", 2);
    check(k.weight_percent[ARMPW] == 25 && k.limits[ARMLLT] == 2, "console directives");
    const auto grid = k.land_grid;
    const auto rolls = fake.roll;
    check(computer_players_reload_profile(&state, host, profile), "reload");
    check(k.weight_percent[ARMPW] == 100 && k.weight_locked[ARMPW] == 0, "weights reset");
    check(k.limits[ARMLLT] == unlimited && k.limit_locked[ARMLLT] == 0, "limits reset");
    check(
        k.weight_percent[ARMSOLAR] == 50 && k.weight_locked[ARMSOLAR] == 1,
        "the leading line applies from the kept plan flag"
    );
    check(k.limits[ARMMEX] == 4 && k.limit_locked[ARMMEX] == 1, "easy plan limit again");
    check(
        fake.roll == rolls && k.land_grid.step_x == grid.step_x &&
            k.land_grid.phase_z == grid.phase_z,
        "the build grids are kept and nothing is drawn"
    );
    check(state.players[1].present && !state.players[0].present, "controllers kept");
    computer_players_release(&state);
}

/// Reads a text file of the installed game.
///
/// @param assets the installed game's store
/// @param resource '/'-separated path
/// @return the text, or empty when nothing provides the file
std::string read_text(const oa::AssetStore& assets, const char* resource) {
    const auto bytes = oa::test::read_game_file(assets, resource);
    return {bytes.begin(), bytes.end()};
}

// The installed game's computer-player profile and side build lists.
void test_installed_data(const oa::AssetStore& assets) {
    const auto profile = read_text(assets, "ai/default.txt");
    const auto lists = read_text(assets, "gamedata/sidedata.tdf");
    check(!profile.empty() && !lists.empty(), "the install holds ai/default.txt and sidedata.tdf");
    if (profile.empty() || lists.empty())
        return;
    Fake fake;
    fake.player(1, OA_PLAYER_STATUS_COMPUTER, 10, 30);
    auto host = host_for(fake);
    host.difficulty = OA_DIFFICULTY_MEDIUM;
    ComputerPlayers state{};
    describe_catalog(state);
    check(computer_players_configure(&state, profile, lists), "configure from the install");
    check(computer_players_initialize(&state, host), "initialize from the install");
    const auto& com = state.types[ARMCOM];
    bool mex = false, lab = false;
    for (uint32_t i = 0; i < com.build_count; ++i) {
        mex |= com.build_ids[i] == ARMMEX;
        lab |= com.build_ids[i] == ARMLAB;
    }
    check(mex && lab, "the installed commander build list");
    const auto& k = state.players[1].knowledge;
    check(k.limits[ARMMEX] >= 0, "the installed limit applied");
    check(k.weight_percent[ARMPW] < 100, "the installed ARM weight applied");
    computer_players_release(&state);
}
} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv)) {
        const auto assets = oa::test::require_game_assets("the installed computer-player data");
        test_installed_data(assets);
        if (failures != 0) {
            std::fprintf(stderr, "%d failure(s)\n", failures);
            return 1;
        }
        std::printf("computer player data tests passed\n");
        return 0;
    }
    test_sort_squads();
    test_profile();
    test_downloadable_directives();
    test_weight_report();
    test_score_and_pick();
    test_lab_idle_without_metal_income();
    test_controller_presence();
    test_tick();
    test_scripted_units_stay_unsorted();
    test_busy_factory_gets_no_pick();
    test_campaign_downloadables();
    test_profile_reload();
    test_sighted_weight();
    test_siege();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("computer player tests passed\n");
    return 0;
}
