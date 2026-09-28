// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Command console: per-command state changes, dispatch masks, the passphrase,
// chat echo modes, the extension hook and the hotkey dispatcher.
#include "oa/ui/console/console.hpp"
#include "oa/ui/console/game_fields.hpp"
#include "oa/ui/console/hotkeys.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

namespace console = oa::ui::console;
using console::game_load;

int g_failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);          \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

struct Recorder {
    std::vector<std::string> messages;
    std::vector<std::string> calls;
    int saves = 0;
    int cache = 0;
    std::vector<int> sight_resets;
    std::string script;
    std::vector<std::string> files;
};

Recorder g_rec;

void rec_save(void*) {
    ++g_rec.saves;
}

// "<kind>:<text>", with "@<sender>" after it for a line a player sent; the
// console's own lines come from no player.
void rec_post(void*, const char* text, uint8_t kind, uint8_t sender) {
    std::string line = std::to_string(kind) + ":" + text;
    if (sender != console::kMessageNoSender)
        line += "@" + std::to_string(sender);
    g_rec.messages.push_back(line);
}

void rec_cache(void*) {
    ++g_rec.cache;
}

void rec_sight(void*, bool refill) {
    g_rec.sight_resets.push_back(refill ? 1 : 0);
}

uint16_t rec_logos(void*) {
    return 8;
}

void rec_player_info(void*) {
    g_rec.calls.push_back("player info");
}

void rec_metal(void*, uint8_t from, uint8_t to, float amount) {
    g_rec.calls.push_back(
        "metal " + std::to_string(from) + ">" + std::to_string(to) + " " +
        std::to_string(static_cast<int>(amount))
    );
}

void rec_energy(void*, uint8_t from, uint8_t to, float amount) {
    g_rec.calls.push_back(
        "energy " + std::to_string(from) + ">" + std::to_string(to) + " " +
        std::to_string(static_cast<int>(amount))
    );
}

void rec_kill_player(void*, uint8_t player) {
    g_rec.calls.push_back("kill " + std::to_string(player));
}

void rec_kill_all(void*) {
    g_rec.calls.push_back("kill all");
}

void rec_disable(void*) {
    g_rec.calls.push_back("disable conditions");
}

void rec_snap(void*, uint16_t, oa::FixedVec3*) {
}

void rec_create(void*, uint8_t player, uint16_t type, const oa::FixedVec3* position) {
    g_rec.calls.push_back(
        "create " + std::to_string(player) + " " + std::to_string(type) + " " +
        std::to_string(position->x >> 16) + "," + std::to_string(position->z >> 16)
    );
}

char* rec_read(void*, const char* path, int32_t* length) {
    g_rec.calls.push_back(std::string("read ") + path);
    if (g_rec.script.empty())
        return nullptr;
    char* text = static_cast<char*>(std::malloc(g_rec.script.size() + 1));
    std::memcpy(text, g_rec.script.c_str(), g_rec.script.size() + 1);
    *length = static_cast<int32_t>(g_rec.script.size());
    return text;
}

void rec_free(void*, char* text) {
    std::free(text);
}

void rec_dirs(void*, const char* path) {
    g_rec.calls.push_back(std::string("mkdir ") + path);
}

void rec_poster(
    void*, const char* dir, const char* prefix, int32_t x, int32_t y, int32_t w, int32_t h
) {
    char text[256];
    std::snprintf(text, sizeof text, "poster %s %s %d %d %d %d", dir, prefix, x, y, w, h);
    g_rec.calls.push_back(text);
}

uint32_t rec_now(void*) {
    return 4321;
}

void rec_save_game(void*, const char* path, const char* description, int32_t kind) {
    g_rec.calls.push_back(
        std::string("save ") + path + "|" + description + "|" + std::to_string(kind)
    );
}

void rec_weight(void*, uint8_t player, const char* type, float percent) {
    g_rec.calls.push_back(
        "weight " + std::to_string(player) + " " + type + " " +
        std::to_string(static_cast<int>(percent))
    );
}

void rec_limit(void*, uint8_t player, const char* type, int32_t limit) {
    g_rec.calls.push_back(
        "limit " + std::to_string(player) + " " + type + " " + std::to_string(limit)
    );
}

void rec_weights(void*, uint8_t player, const char* path) {
    g_rec.calls.push_back("weights " + std::to_string(player) + " " + path);
}

void rec_voice(void*) {
    g_rec.calls.push_back("novelty voice");
}

void rec_sound_3d(void*) {
    g_rec.calls.push_back("3d sound");
}

void rec_crash(void*, console::CrashTest test) {
    g_rec.calls.push_back("crash " + std::to_string(static_cast<int>(test)));
}

void rec_mission(void*, uint8_t mission, int32_t parameter_1, int32_t parameter_2) {
    g_rec.calls.push_back(
        "mission " + std::to_string(mission) + " " + std::to_string(parameter_1) + " " +
        std::to_string(parameter_2)
    );
}

void rec_search_nodes(void*, int32_t nodes) {
    g_rec.calls.push_back("search nodes " + std::to_string(nodes));
}

void rec_search_weight(void*, int32_t weight) {
    g_rec.calls.push_back("search weight " + std::to_string(weight));
}

console::ConsoleHost make_host() {
    console::ConsoleHost host{};
    host.save_game_options = rec_save;
    host.post_message = rec_post;
    host.compact_render_cache = rec_cache;
    host.reset_sight_buffers = rec_sight;
    host.logo_count = rec_logos;
    host.player_info_changed = rec_player_info;
    host.transfer_metal = rec_metal;
    host.transfer_energy = rec_energy;
    host.kill_player_units = rec_kill_player;
    host.kill_all_units = rec_kill_all;
    host.disable_mission_conditions = rec_disable;
    host.snap_build_position = rec_snap;
    host.create_unit = rec_create;
    host.read_text_file = rec_read;
    host.free_text_file = rec_free;
    host.create_directories = rec_dirs;
    host.render_poster = rec_poster;
    host.now_ms = rec_now;
    host.save_game = rec_save_game;
    host.apply_ai_weight = rec_weight;
    host.apply_ai_limit = rec_limit;
    host.write_ai_weights = rec_weights;
    host.crash_test = rec_crash;
    host.toggle_novelty_voice = rec_voice;
    host.toggle_sound_3d = rec_sound_3d;
    host.issue_group_mission = rec_mission;
    host.set_search_node_credit = rec_search_nodes;
    host.set_search_heuristic = rec_search_weight;
    return host;
}

struct Fixture {
    oa::World* world;
    console::ConsoleHost host;
    console::Console con;
    oa::Unit units[4];
    oa::UnitDef defs[4];

    Fixture() : world(oa::world_create()), host(make_host()), con{}, units{}, defs{} {
        g_rec = Recorder{};
        oa::Game& g = world->game;
        for (uint8_t i = 0; i < 2; ++i) {
            oa::Player& p = g.players[i];
            p.in_use = 1;
            p.status = i == 0 ? OA_PLAYER_STATUS_LOCAL : OA_PLAYER_STATUS_COMPUTER;
            p.index = i;
            p.info = oa::oa_ref_from_index(i);
            p.energy_storage = 1000.0f;
            p.metal_storage = 500.0f;
        }
        for (uint8_t i = 2; i < OA_PLAYER_COUNT; ++i)
            g.players[i].index = 10;
        g.local_player_index = 0;
        g.viewpoint_player = 0;
        g.map_width_world = 2048;
        g.map_height_world = 2048;
        g.map_pixel_width = 2016;
        g.map_pixel_height = 1920;
        g.view_cells_width = 40;
        g.view_cells_height = 30;
        world->units = units;
        world->unit_slot_count = 4;
        world->unit_defs = defs;
        world->unit_def_count = 4;
        g.unit_def_count = 4;
        std::snprintf(defs[1].unit_name, sizeof defs[1].unit_name, "ARMCOM");
        std::snprintf(defs[2].unit_name, sizeof defs[2].unit_name, "CORCOM");
        std::snprintf(defs[3].unit_name, sizeof defs[3].unit_name, "ARMSOLAR");
        for (auto& def : defs) {
            def.bounds_min_x = -(10 << 16);
            def.bounds_max_x = 10 << 16;
        }
        CHECK(console::console_init(&con, world, &host));
        g_rec = Recorder{};
    }

    ~Fixture() {
        world->units = nullptr;
        world->unit_defs = nullptr;
        oa::world_destroy(world);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    oa::Game& game() { return world->game; }

    uint32_t run(const char* text, uint32_t mask = console::command_class::all) {
        return console::console_execute(&con, text, mask);
    }
};

// One console-flag toggle per row: the command flips exactly its bit.
void test_console_flag_toggles() {
    struct Row {
        const char* command;
        uint16_t bit;
    };

    const Row rows[] = {
        {"NoShake", console::console_flag::no_shake},
        {"Clock", console::console_flag::clock},
        {"ShootAll", console::console_flag::shoot_all},
        {"Radar", console::console_flag::full_radar},
        {"DoubleShot", console::console_flag::double_shot},
        {"HalfShot", console::console_flag::half_shot},
        {"SelBoxes", console::console_flag::selection_boxes},
        {"TreeDeath", console::console_flag::tree_death},
    };
    for (const Row& row : rows) {
        Fixture f;
        CHECK(console::console_flags(f.game()) == 0);
        f.run(row.command);
        CHECK(console::console_flags(f.game()) == row.bit);
        f.run(row.command);
        CHECK(console::console_flags(f.game()) == 0);
    }
}

void test_graphics_toggles() {
    struct Row {
        const char* command;
        uint16_t bit;
        int saves;
        int cache;
    };

    const Row rows[] = {
        {"Shading", console::graphics_flag::shading, 1, 1},
        {"AntiAlias", console::graphics_flag::anti_alias, 1, 1},
        {"Shadow", console::graphics_flag::shadow, 1, 1},
        {"Dither", console::graphics_flag::dither, 1, 0},
        {"TShadow", console::graphics_flag::vehicle_shadow, 0, 0},
        {"FShadow", console::graphics_flag::feature_shadow, 0, 0},
        {"SwitchAlt", console::graphics_flag::switch_alt, 1, 0},
    };
    for (const Row& row : rows) {
        Fixture f;
        f.run(row.command);
        CHECK(f.game().graphics_flags == row.bit);
        CHECK(g_rec.saves == row.saves);
        CHECK(g_rec.cache == row.cache);
    }
    Fixture f;
    f.run("SwitchAlt 1");
    CHECK(f.game().graphics_flags == console::graphics_flag::switch_alt);
    f.run("SwitchAlt 2");
    CHECK(f.game().graphics_flags == 0);
    CHECK(g_rec.saves == 0);
}

void test_visibility_and_values() {
    Fixture f;
    f.run("LOS");
    CHECK(f.game().visibility_flags == console::visibility_flag::line_of_sight);
    f.run("Mapping");
    CHECK(f.game().visibility_flags == 3);
    f.run("LOSType");
    CHECK(f.game().visibility_flags == 7);
    f.run("NowISee");
    CHECK(f.game().visibility_flags == console::visibility_flag::los_type);
    CHECK((g_rec.sight_resets == std::vector<int>{0, 1, 0, 1}));

    f.run("ScrollSpeed 7");
    CHECK(f.game().scroll_speed == 7);
    f.run("SeaLevel 33");
    CHECK(f.game().sea_level == 33);
    f.run("IFace 1");
    CHECK(game_load<int32_t>(f.game(), console::game_offset::interface_mode) == 1);
    f.run("Gamma 12");
    CHECK(game_load<int32_t>(f.game(), console::game_offset::gamma) == 12);
    f.run("ScreenChat");
    CHECK(game_load<uint32_t>(f.game(), console::game_offset::screen_chat) == 1);
    f.run("ShowRanges");
    CHECK(game_load<uint32_t>(f.game(), console::game_offset::show_ranges) == 1);
    f.run("Profile");
    CHECK(f.game().profiling == 1);
    f.run("Edge");
    CHECK(f.game().map_pixel_width == 2048 - 0x20);
    CHECK(f.game().map_pixel_height == 2048 - 0x80);
    f.run("Contour 1.5");
    CHECK(f.con.contour_values[0] == 384);
    CHECK(f.con.contour_values[1] == 192);
    f.run("Sing");
    CHECK(g_rec.calls.back() == "novelty voice");
    // Sound3D flips the 3D switch, then saves the options.
    const auto saves = g_rec.saves;
    f.run("Sound3D");
    CHECK(g_rec.calls.back() == "3d sound");
    CHECK(g_rec.saves == saves + 1);
}

void test_resource_cheats() {
    Fixture f;
    f.game().players[0].energy = 5.0f;
    f.game().players[0].metal = 7.0f;
    f.run("ATM");
    CHECK(f.game().players[0].energy == 1005.0f);
    CHECK(f.game().players[0].metal == 1007.0f);
    f.run("NoMetal");
    CHECK(f.game().players[0].metal == 0.0f);
    f.run("NoEnergy 1 250");
    CHECK(f.game().players[1].energy == 250.0f);
    f.run("NoEnergy 4 250"); // inactive slot
    CHECK(f.game().players[4].energy == 0.0f);
    f.run("Give 1 300 metal");
    f.run("Give 1 40 ENERGY");
    f.run("Give 5 40 energy");
    CHECK((g_rec.calls == std::vector<std::string>{"metal 0>1 300", "energy 0>1 40"}));
}

void test_passphrase() {
    Fixture f;
    f.run("Now Film Chris Include Reload Assert");
    CHECK((console::console_flags(f.game()) & console::console_flag::developer) != 0);
    f.run("Now Film Chris Include Reload assert"); // case-sensitive words
    CHECK((console::console_flags(f.game()) & console::console_flag::developer) == 0);
    f.run("Now Film Chris Include Reload Assert");
    f.run("Now Film Chris Include Reload Assert extra");
    CHECK((console::console_flags(f.game()) & console::console_flag::developer) == 0);
    f.run("now Film Chris Include Reload Assert"); // command names ignore case
    CHECK((console::console_flags(f.game()) & console::console_flag::developer) != 0);
}

void test_masks_and_chat() {
    Fixture f;
    // Without the passphrase or cheats, the chat line reaches options only.
    CHECK(console::console_chat_mask(&f.con) == 0x11);
    const char* echo = nullptr;
    auto mode = console::console_submit_chat_line(&f.con, "  +atm", 1, &echo);
    CHECK(mode == 1);
    CHECK(std::strcmp(echo, "+atm") == 0);
    CHECK(f.game().players[0].metal == 0.0f);
    mode = console::console_submit_chat_line(&f.con, "+clock", 1, &echo);
    CHECK(mode == console::kChatModeLocalOnly);
    CHECK((console::console_flags(f.game()) & console::console_flag::clock) != 0);
    mode = console::console_submit_chat_line(&f.con, "hello", 3, &echo);
    CHECK(mode == 3);

    f.con.cheats_enabled = true;
    CHECK(console::console_chat_mask(&f.con) == 0x13);
    mode = console::console_submit_chat_line(&f.con, "+atm", 1, &echo);
    CHECK(mode == console::kChatModeEveryone);
    CHECK(f.game().players[0].metal == 1000.0f);
    // Developer commands still need the passphrase.
    console::console_submit_chat_line(&f.con, "+SeaLevel 9", 1, &echo);
    CHECK(f.game().sea_level == 0);
    console::console_submit_chat_line(&f.con, "+Now Film Chris Include Reload Assert", 1, &echo);
    CHECK(console::console_chat_mask(&f.con) == 0x17);
    console::console_submit_chat_line(&f.con, "+SeaLevel 9", 1, &echo);
    CHECK(f.game().sea_level == 9);

    // A null line re-runs the previous command.
    f.run("ATM", 0x02);
    const float metal = f.game().players[0].metal;
    console::console_execute(&f.con, nullptr, console::command_class::all);
    CHECK(f.game().players[0].metal == metal + 1000.0f);
    // Unknown commands return 0 without the developer class.
    CHECK(f.run("NoSuchCommand", 0x11) == 0);
}

void test_share_commands() {
    Fixture f;
    f.run("ShareMetal");
    CHECK(g_rec.messages.empty()); // not a live game
    f.game().session_flags = 1;
    f.run("ShareMetal");
    CHECK(console::share_flags(f.world->player_info[0]) == console::share_flag::metal);
    f.run("ShareAll");
    CHECK(
        console::share_flags(f.world->player_info[0]) ==
        (console::share_flag::energy | console::share_flag::mapping | console::share_flag::radar)
    );
    CHECK(g_rec.messages.size() == 5);
    CHECK(g_rec.messages[0] == "2:Toggled ShareMetal to: ON");
    CHECK(g_rec.messages[1] == "2:Toggled ShareMetal to: OFF");
    CHECK(g_rec.messages[4] == "2:Toggled ShareRadar to: ON");
    g_rec.messages.clear();
    f.run("SetShareMetal 200");
    f.run("SetShareEnergy 5000");
    CHECK(console::player_load<float>(f.game().players[0], 0xe4) == 200.0f);
    CHECK(console::player_load<float>(f.game().players[0], 0xe8) == 1000.0f);
    CHECK(g_rec.messages[0] == "2:OK.  Will share metal if above 200");
    CHECK(g_rec.messages[1] == "2:OK.  Will share energy if above 5000");
    CHECK(g_rec.calls.back() == "player info");
}

// The console alone has no traffic, compression or page commands: the option
// mask finds nothing, and "Senderror" reaches the developer fallback, which
// looks for a debugdat script of that name. The share toggles stay.
void test_extension_commands_absent() {
    Fixture f;
    f.game().session_flags = 1;
    constexpr uint32_t kOptions =
        console::command_class::option | console::command_class::private_echo;
    for (const char* command : {"Drop 0", "Compression", "BPS", "Page Bob hi", "P Bob hi"})
        CHECK(f.run(command, kOptions) == 0);
    CHECK(console::console_flags(f.game()) == 0);
    CHECK(game_load<uint32_t>(f.game(), console::game_offset::show_bandwidth) == 0);
    CHECK(f.game().compression_off == 0);
    CHECK(g_rec.messages.empty());
    f.run("Senderror 40");
    CHECK(f.game().send_error_percent == 0);
    CHECK((g_rec.calls == std::vector<std::string>{"read debugdat\\Senderror.txt"}));
    f.run("ShareMetal", kOptions);
    f.run("ShareEnergy", kOptions);
    CHECK(
        console::share_flags(f.world->player_info[0]) ==
        (console::share_flag::metal | console::share_flag::energy)
    );
    CHECK(
        (g_rec.messages ==
         std::vector<std::string>{"2:Toggled ShareMetal to: ON", "2:Toggled ShareEnergy to: ON"})
    );
    CHECK(g_rec.calls.back() == "player info");
}

struct Extension {
    int calls = 0;
    console::Console* console = nullptr;
    std::size_t commands_before = 0;
    bool fallback_set = false;
    console::Console* active_in_handler = nullptr;
};

Extension g_extension;

void probe_command(oa::ui::services::TokenLine* line) {
    console::Console* active = console::console_active();
    g_extension.active_in_handler = active;
    console::console_post(
        active, oa::ui::services::token_line_get(line, 1, ""), console::kMessageService
    );
}

void extend_with_probe(void* context, console::Console* con) {
    auto& extension = *static_cast<Extension*>(context);
    ++extension.calls;
    extension.console = con;
    extension.commands_before = con->commands.count;
    extension.fallback_set = con->commands.fallback != nullptr;
    oa::ui::services::command_table_set(
        &con->commands, "Probe", probe_command, console::command_class::option
    );
}

// extend runs once, after the console's own commands and fallback, and what
// it registers dispatches like the console's own commands; the handler finds
// its console through console_active and posts through console_post.
void test_extension_hook() {
    oa::World* world = oa::world_create();
    g_extension = Extension{};
    console::ConsoleHost host = make_host();
    host.extension_context = &g_extension;
    host.extend = extend_with_probe;
    console::Console con{};
    CHECK(console::console_init(&con, world, &host));
    g_rec = Recorder{};
    CHECK(g_extension.calls == 1 && g_extension.console == &con);
    CHECK(g_extension.fallback_set && g_extension.commands_before > 0);
    CHECK(con.commands.count == g_extension.commands_before + 1);
    CHECK(console::console_active() == nullptr);
    CHECK(
        console::console_execute(&con, "probe hello", console::command_class::option) ==
        console::command_class::option
    );
    CHECK(g_extension.active_in_handler == &con);
    CHECK(console::console_active() == nullptr);
    CHECK(console::console_execute(&con, "Probe again", console::command_class::cheat) == 0);
    console::console_post(&con, "direct", console::kMessageNotice);
    CHECK((g_rec.messages == std::vector<std::string>{"4:hello", "2:direct"}));

    const console::ConsoleHost bare{};
    console::Console quiet{};
    CHECK(console::console_init(&quiet, world, &bare));
    console::console_post(&quiet, "dropped", console::kMessageNotice);
    CHECK(console::console_init(&quiet, world, nullptr));
    console::console_post(&quiet, "dropped", console::kMessageNotice);
    CHECK(g_extension.calls == 1 && g_rec.messages.size() == 2);
    oa::world_destroy(world);
}

void test_logo_and_control() {
    Fixture f;
    f.run("Logo 3 1");
    CHECK(f.world->player_info[1].color == 3);
    f.run("Logo 9 1");
    CHECK(g_rec.messages.back() == "2:Invalid logo setting");
    f.run("View 1");
    CHECK(f.game().viewpoint_player == 1);
    f.run("View 7");
    CHECK(f.game().viewpoint_player == 1);
    f.run("Control 1");
    CHECK(f.game().local_player_index == 1);
    f.run("AI 0");
    CHECK(f.game().players[0].status == OA_PLAYER_STATUS_COMPUTER);
    CHECK(f.world->player_info[0].state == OA_PLAYER_STATUS_COMPUTER);
    f.run("AI 0");
    CHECK(f.game().players[0].status == OA_PLAYER_STATUS_LOCAL);
}

void test_outcomes_and_kill() {
    Fixture f;
    f.world->player_info[0].side = 0;
    f.run("IWin");
    CHECK(g_rec.calls.back() == "kill 1");
    CHECK(f.game().outcome_flags == 0x34);
    f.run("ILose");
    CHECK(g_rec.calls.back() == "kill 0");
    CHECK(f.game().outcome_flags == 0x64);
    g_rec.calls.clear();
    f.run("Kill");
    f.run("Kill 1");
    CHECK(
        (g_rec.calls ==
         std::vector<std::string>{"kill all", "disable conditions", "kill 1", "disable conditions"})
    );
}

void test_selectable_and_observer() {
    Fixture f;
    f.units[1].flags = OA_UNIT_FLAG_LIVE;
    f.units[2].flags = 0;
    f.run("Selectable");
    CHECK(f.units[1].flags == (OA_UNIT_FLAG_LIVE | console::kUnitFlagSelectable));
    CHECK(f.units[2].flags == 0);
    f.game().follow_unit = 5;
    f.run("BigBrother");
    CHECK(f.game().periodic_flags == console::kPeriodicFlagObserver);
    CHECK(f.game().periodic_countdown == 1);
    f.run("BigBrother");
    CHECK(f.game().periodic_flags == 0);
    CHECK(f.game().follow_unit == 0);
}

void test_film_and_poster() {
    Fixture f;
    f.run("Film c:\\movies\\");
    CHECK(
        std::strcmp(
            console::game_bytes(f.game(), console::game_offset::output_directory), "c:\\movies"
        ) == 0
    );
    CHECK(game_load<uint32_t>(f.game(), console::game_offset::output_directory_changed) == 1);
    f.run("FilmSpeed 0");
    CHECK(f.game().capture_rate == 0);
    f.run("FilmSpeed 3");
    CHECK(f.game().capture_rate == 3);
    g_rec.calls.clear();
    f.game().camera_x = 100;
    f.game().camera_y = 50;
    f.run("MakePoster 1000 800");
    // Centre (100 + 320 - 500) clamps to 0; y = 50 + 240 - 400 clamps to 0.
    CHECK(g_rec.calls.size() == 2);
    CHECK(g_rec.calls[0] == "mkdir c:\\movies\\screenshots");
    CHECK(g_rec.calls[1] == "poster c:\\movies\\screenshots BIGSHOT 0 0 1000 800");
    CHECK(f.game().last_frame_time == 4321);
    g_rec.calls.clear();
    f.run("MakePoster all");
    CHECK(g_rec.calls[1] == "poster c:\\movies\\screenshots BIGSHOT 0 0 2016 1920");
    g_rec.calls.clear();
    f.run("MakePoster 10 10"); // clamped up to the view size, centred on it
    CHECK(g_rec.calls[1] == "poster c:\\movies\\screenshots BIGSHOT 100 50 640 480");
    g_rec.calls.clear();
    f.run("Save test");
    CHECK(g_rec.calls[0] == "mkdir savegame");
    CHECK(g_rec.calls[1] == "save savegame\\test.sav|Generic Game Description|666");
}

void test_spawn_and_scripts() {
    Fixture f;
    f.run("Now Film Chris Include Reload Assert");
    console::game_store(
        f.game(), console::game_offset::cursor_position, oa::FixedVec3{100 << 16, 0, 200 << 16}
    );
    // Not registered: the developer fallback spawns by pattern.
    f.run("ARM* 1");
    CHECK((g_rec.calls == std::vector<std::string>{"create 1 1 100,200", "create 1 3 152,200"}));
    CHECK(std::strcmp(f.con.last_command, "ARM* 1") == 0);
    g_rec.calls.clear();
    // No match: the debugdat script of that name runs with the line as %N.
    g_rec.script = "SeaLevel %1\nScrollSpeed 4 # comment\n";
    f.run("myscript 17");
    CHECK(g_rec.calls[0] == "read debugdat\\myscript.txt");
    CHECK(f.game().sea_level == 17);
    CHECK(f.game().scroll_speed == 4);
    // Include always reads debugdat\Include.txt (token 0).
    g_rec.calls.clear();
    g_rec.script = "Move 2 3\n";
    f.run("Include anything");
    CHECK(g_rec.calls[0] == "read debugdat\\Include.txt");
    // The script's Move is undone: the cursor is restored after the script.
    CHECK(game_load<oa::FixedVec3>(f.game(), console::game_offset::cursor_position).x == 100 << 16);
    f.run("Move 2 3");
    CHECK(game_load<oa::FixedVec3>(f.game(), console::game_offset::cursor_position).x == 132 << 16);
    CHECK(game_load<oa::FixedVec3>(f.game(), console::game_offset::cursor_position).z == 248 << 16);
}

void test_name_matching() {
    CHECK(console::console_name_matches("ARMCOM", "armcom"));
    CHECK(console::console_name_matches("ARMCOM", "ARM*"));
    CHECK(console::console_name_matches("ARMCOM", "*COM"));
    CHECK(console::console_name_matches("ARMCOM", "A?MC*"));
    CHECK(!console::console_name_matches("ARMCOM", "COR*"));
    CHECK(!console::console_name_matches("ARMCOM", "ARM"));
    CHECK(console::console_name_matches("ARMCOM", "*"));
}

void test_ai_profile() {
    Fixture f;
    f.game().difficulty = OA_DIFFICULTY_MEDIUM;
    f.run("weight ARMSOLAR 50", console::command_class::ai_profile);
    CHECK(g_rec.calls.empty());
    f.run("plan easy hard", console::command_class::ai_profile);
    CHECK(!f.con.ai_plan_matches);
    f.run("plan easy medium", console::command_class::ai_profile);
    CHECK(f.con.ai_plan_matches);
    // "weight" applies to every player with a controller, which the local
    // and the computer player have and a mirrored one or a free slot do not;
    // "limit" applies to computer players only.
    f.game().players[2].in_use = 1;
    f.game().players[2].status = OA_PLAYER_STATUS_MIRRORED;
    f.run("weight ARMSOLAR 50", console::command_class::ai_profile);
    f.run("limit ARMSOLAR 3", console::command_class::ai_profile);
    CHECK(
        (g_rec.calls == std::vector<std::string>{
                            "weight 0 ARMSOLAR 50", "weight 1 ARMSOLAR 50", "limit 1 ARMSOLAR 3"
                        })
    );
    f.run("plan hard any", console::command_class::ai_profile);
    CHECK(!f.con.ai_plan_matches); // "any" is only recognised as token 1
    f.run("plan any", console::command_class::ai_profile);
    CHECK(f.con.ai_plan_matches);
    // AI directives are not reachable from chat.
    CHECK(f.run("plan any", 0x13) == 0);
}

// PrintWeights needs exactly "PrintWeights <player> <file>" and an active slot.
void test_print_weights() {
    Fixture f;
    f.run("PrintWeights 1");
    f.run("PrintWeights 1 w.txt extra");
    f.run("PrintWeights 4 w.txt");
    f.run("PrintWeights 1 w.txt");
    f.run("PrintWeights x ai.txt");
    CHECK((g_rec.calls == std::vector<std::string>{"weights 1 w.txt", "weights 0 ai.txt"}));
}

// "Assign" looks its name up in the mission table (Stop 45,
// Standing_FireOrder 43) and passes tokens 1 and 2 as numbers; "Search"
// stores a nonzero node count and, with exactly three tokens, the weight
// times 65536 truncated. Both are developer commands; "ShootAll"
// is an option.
void test_unit_commands() {
    using Calls = std::vector<std::string>;
    Fixture f;
    const uint32_t chat = console::console_chat_mask(&f.con);
    CHECK(f.run("Assign Stop 1", chat) == 0);
    CHECK(f.run("Search 500 2", chat) == 0);
    CHECK(g_rec.calls.empty());
    CHECK(f.run("ShootAll", chat) != 0);
    CHECK(console::console_flags(f.game()) == console::console_flag::shoot_all);

    f.run("Assign Standing_FireOrder 2");
    f.run("assign STOP 1");
    f.run("Assign Unknown 1");
    f.run("Assign 5 7");
    f.run("Assign");
    CHECK((g_rec.calls == Calls{"mission 43 0 2", "mission 45 0 1"}));

    g_rec.calls.clear();
    f.run("Search 500");
    f.run("Search 0 2.5");
    f.run("Search 700 1.5 x");
    f.run("Search -3 0.25");
    f.run("Search 1 -1.5");
    CHECK(
        (g_rec.calls == Calls{
                            "search nodes 500",
                            "search weight 163840",
                            "search nodes 700",
                            "search nodes -3",
                            "search weight 16384",
                            "search nodes 1",
                            "search weight -98304",
                        })
    );
}

void test_debug_break_gate() {
    Fixture f;
    f.run("DebugBreak 1");
    CHECK(g_rec.calls.empty());
    f.run("Now Film Chris Include Reload Assert");
    f.game().outcome_flags = console::outcome_flag::debug_keys;
    f.run("DebugBreak 3");
    f.run("DebugBreak 7");
    f.run("DebugBreak");
    CHECK((g_rec.calls == std::vector<std::string>{"crash 3", "crash 0"}));
}

// --- Hotkeys ---------------------------------------------------------------

struct KeyRecorder {
    std::vector<std::string> calls;
    bool shift = false;
    bool alt = false;
    std::vector<std::string> listing;
};

KeyRecorder g_keys;

bool key_shift(void*) {
    return g_keys.shift;
}

bool key_alt(void*) {
    return g_keys.alt;
}

int32_t key_session(void*) {
    return 2;
}

void key_sound(void*, const char* name) {
    g_keys.calls.push_back(std::string("sound ") + name);
}

void key_speed(void*, int32_t direction) {
    g_keys.calls.push_back("speed " + std::to_string(direction));
}

void key_squad(void*, int32_t squad, bool add) {
    g_keys.calls.push_back("squad " + std::to_string(squad) + (add ? "+" : ""));
}

void key_build(void*, int32_t index) {
    g_keys.calls.push_back("build " + std::to_string(index));
}

void key_category(void*, const char* category, bool) {
    g_keys.calls.push_back(std::string("category ") + category);
}

void key_options(void*) {
    g_keys.calls.push_back("options");
}

void key_list(void*, const char* pattern, void (*visit)(void*, const char*), void* user) {
    g_keys.calls.push_back(std::string("list ") + pattern);
    for (const auto& name : g_keys.listing)
        visit(user, name.c_str());
}

void key_movie(void*, const char* path) {
    g_keys.calls.push_back(std::string("movie ") + path);
}

console::HotkeyHost make_key_host() {
    console::HotkeyHost host{};
    host.shift_down = key_shift;
    host.alt_down = key_alt;
    host.session_kind = key_session;
    host.play_sound = key_sound;
    host.change_game_speed = key_speed;
    host.select_squad = key_squad;
    host.select_build_item = key_build;
    host.select_by_category = key_category;
    host.open_options_panel = key_options;
    host.list_files = key_list;
    host.begin_movie_capture = key_movie;
    return host;
}

void test_hotkeys() {
    Fixture f;
    const console::HotkeyHost keys = make_key_host();
    g_keys = KeyRecorder{};
    console::hotkey_dispatch(&f.con, &keys, '+');
    console::hotkey_dispatch(&f.con, &keys, '_');
    console::hotkey_dispatch(&f.con, &keys, '3');
    g_keys.alt = true;
    console::hotkey_dispatch(&f.con, &keys, '3');
    g_keys.alt = false;
    console::hotkey_dispatch(&f.con, &keys, 0xab);                 // Ctrl+B
    console::hotkey_dispatch(&f.con, &keys, console::hotkey::tab); // not multiplayer: options
    CHECK(
        (g_keys.calls == std::vector<std::string>{
                             "speed 1",
                             "speed -1",
                             "build 2",
                             "squad 3",
                             "sound SelectSquad",
                             "category CTRL_B",
                             "options"
                         })
    );
    CHECK((f.game().frame_flags & console::kFrameFlagOptionsOpen) != 0);
    console::hotkey_dispatch(&f.con, &keys, console::hotkey::escape);
    CHECK((f.game().frame_flags & console::kFrameFlagOptionsOpen) == 0);

    console::hotkey_dispatch(&f.con, &keys, '~');
    CHECK(f.game().graphics_flags == console::graphics_flag::damage_bars);
    console::hotkey_dispatch(&f.con, &keys, console::hotkey::f1 + 3);
    CHECK(
        f.game().graphics_flags ==
        (console::graphics_flag::damage_bars | console::graphics_flag::kill_board)
    );
    console::hotkey_dispatch(&f.con, &keys, console::hotkey::pause);
    CHECK(f.game().sim_run_flags == console::kSimRunPaused);

    // F11 needs the passphrase; debug keys then take over '='.
    console::hotkey_dispatch(&f.con, &keys, console::hotkey::f11);
    CHECK((f.game().outcome_flags & console::outcome_flag::debug_keys) == 0);
    f.run("Now Film Chris Include Reload Assert");
    console::hotkey_dispatch(&f.con, &keys, console::hotkey::f11);
    CHECK((f.game().outcome_flags & console::outcome_flag::debug_keys) != 0);
    g_keys.calls.clear();
    f.game().players[1].energy = 3.0f;
    console::hotkey_dispatch(&f.con, &keys, '=');
    CHECK(g_keys.calls.empty()); // speed locked in debug mode
    CHECK(f.game().players[1].energy == 1000.0f);
    CHECK(f.game().players[1].metal == 500.0f);
    console::hotkey_dispatch(&f.con, &keys, 'm');
    console::hotkey_dispatch(&f.con, &keys, 'm');
    CHECK(f.game().debug_overlay == 2);
    f.units[2].type_index = 7;
    f.game().cursor_unit_id = 2;
    console::hotkey_dispatch(&f.con, &keys, ']');
    CHECK((f.units[2].flags & OA_UNIT_FLAG_DEATH_PENDING) != 0);
    CHECK(f.units[2].last_attacker_owner == OA_PLAYER_COUNT);
    console::hotkey_dispatch(&f.con, &keys, console::hotkey::f11);
    CHECK((f.game().outcome_flags & console::outcome_flag::debug_keys) == 0);
    CHECK(f.game().debug_overlay == 0);

    // '\' re-runs the last console line.
    f.run("ATM");
    const float metal = f.game().players[0].metal;
    console::hotkey_dispatch(&f.con, &keys, console::hotkey::repeat_command);
    CHECK(f.game().players[0].metal == metal + 1000.0f);

    // Ctrl+F10 starts MOVIE<n+1> after the highest existing folder.
    std::snprintf(console::game_bytes(f.game(), console::game_offset::output_directory), 16, "out");
    g_keys.calls.clear();
    g_keys.listing = {"MOVIE003", "MOVIE011", "MOVIEX"};
    console::hotkey_dispatch(&f.con, &keys, console::hotkey::control_f10);
    CHECK(f.game().capture_enabled == 12);
    CHECK(std::strcmp(f.game().capture_path, "out\\MOVIE012") == 0);
    CHECK((g_keys.calls == std::vector<std::string>{"list out\\MOVIE*", "movie out\\MOVIE012"}));
    console::hotkey_dispatch(&f.con, &keys, console::hotkey::control_f10);
    CHECK(f.game().capture_enabled == 0);
}

// ScrollSpeed keeps the low byte and IFace the whole value, both saving;
// SwitchAlt with an odd argument sets the bit without saving, and a digit
// then picks its squad without Alt and a build item with it.
void test_option_saves() {
    Fixture f;
    f.run("ScrollSpeed 300");
    CHECK(f.game().scroll_speed == 44);
    CHECK(g_rec.saves == 1);
    f.run("IFace 1");
    CHECK(game_load<int32_t>(f.game(), console::game_offset::interface_mode) == 1);
    CHECK(g_rec.saves == 2);
    f.run("SwitchAlt 3");
    CHECK(f.game().graphics_flags == console::graphics_flag::switch_alt);
    CHECK(g_rec.saves == 2);
    const console::HotkeyHost keys = make_key_host();
    g_keys = KeyRecorder{};
    console::hotkey_dispatch(&f.con, &keys, '3');
    g_keys.alt = true;
    console::hotkey_dispatch(&f.con, &keys, '3');
    g_keys.alt = false;
    CHECK((g_keys.calls == std::vector<std::string>{"squad 3", "sound SelectSquad", "build 2"}));
}

void test_indexed_names() {
    const console::HotkeyHost keys = make_key_host();
    g_keys = KeyRecorder{};
    g_keys.listing = {"SHOT0007.PCX", "SHOT0002.PCX"};
    char name[128];
    console::next_indexed_file_name(name, sizeof name, &keys, "shots", "SHOT", "PCX");
    CHECK(std::strcmp(name, "shots\\SHOT0008.PCX") == 0);
    CHECK(g_keys.calls.back() == "list shots\\SHOT*.PCX");
    g_keys.listing.clear();
    console::next_indexed_file_name(name, sizeof name, &keys, "dir\\", "A", "pcx");
    CHECK(std::strcmp(name, "dir\\A0001.pcx") == 0);
    console::next_indexed_file_name(name, sizeof name, &keys, "", "A", "pcx");
    CHECK(std::strcmp(name, "A0001.pcx") == 0);
}

} // namespace

int main() {
    test_console_flag_toggles();
    test_graphics_toggles();
    test_visibility_and_values();
    test_resource_cheats();
    test_passphrase();
    test_masks_and_chat();
    test_share_commands();
    test_extension_commands_absent();
    test_extension_hook();
    test_logo_and_control();
    test_outcomes_and_kill();
    test_selectable_and_observer();
    test_film_and_poster();
    test_spawn_and_scripts();
    test_name_matching();
    test_ai_profile();
    test_print_weights();
    test_debug_break_gate();
    test_unit_commands();
    test_hotkeys();
    test_option_saves();
    test_indexed_names();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("console: all checks passed");
    return 0;
}
