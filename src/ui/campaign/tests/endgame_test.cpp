// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/campaign/endgame.hpp"
#include "oa/ui/campaign/screens.hpp"

#include "oa/formats/fnt.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/core/world.h"
#include "oa/ui/frontend_renderer.hpp"
#include "oa/formats/gaf.hpp"
#include "oa/ui/screen_registry.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace oa::ui::campaign;

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

// Palette index of every stand-in glyph pixel, and what light-table row
// kScoreNameLight turns it into.
constexpr uint8_t kGlyphColor = 0x30;
constexpr uint8_t kLitGlyphColor = 0x31;

// A stand-in for the label font as the GUI loads it: every printable byte is
// a block of kGlyphColor 10 rows high hanging one row below the pen, 6 wide
// but for the 5-wide '1' and the 7-wide space, which is never drawn. 'I'
// makes the line 12 rows high.
const oa::formats::fnt::Font& test_font() {
    static const oa::formats::fnt::Font font = [] {
        oa::formats::fnt::Font glyphs{};
        for (int code = ' '; code <= '~'; ++code) {
            oa::formats::fnt::Glyph glyph{};
            glyph.width = code == ' ' ? 7 : code == '1' ? 5 : 6;
            glyph.height = 10;
            glyph.origin_y = -1;
            glyph.pixels.assign(static_cast<std::size_t>(glyph.width) * glyph.height, kGlyphColor);
            glyph.coverage.assign(glyph.pixels.size(), 1);
            glyphs.glyphs[static_cast<std::size_t>(code)] = std::move(glyph);
        }
        glyphs.nominal_height = 10;
        return glyphs;
    }();
    return font;
}

void fade_tests() {
    static oa::Game game{};
    static EndgameFade fade{};
    uint8_t desired[kPaletteBytes];
    uint8_t current[kPaletteBytes];
    for (std::size_t i = 0; i < kPaletteBytes; ++i) {
        desired[i] = static_cast<uint8_t>(i % 256);
        current[i] = static_cast<uint8_t>(255 - i % 256);
    }
    game.endgame_fade_done = 1;
    endgame_build_fade(game, &fade, desired, current, 16);
    expect(fade.steps[0] == -15 && fade.steps[255] == 15, "fade step sizes");
    expect(fade.steps[127] == -1 && fade.steps[128] == 1, "minimum step of one");
    expect(game.endgame_fade_done == 0, "a new fade is not done");
    uint32_t tick = 0;
    int steps = 0;
    while (game.endgame_fade_done == 0 && steps < 100) {
        if (endgame_fade_step(game, &fade, tick))
            ++steps;
        ++tick;
    }
    expect(
        game.endgame_fade_done == 1 && std::memcmp(fade.current, desired, kPaletteBytes) == 0 &&
            steps == 31,
        "fade reaches the target"
    );

    endgame_reset_step_timer(game, 100);
    expect(
        game.endgame_fade_tick == 101 && game.endgame_shade_countdown == kShadeSteps &&
            game.endgame_fade_done == 0,
        "shade timer reset"
    );
    int32_t applied = 0;
    for (uint32_t t = 100; t < 140 && game.endgame_fade_done == 0; ++t)
        if (endgame_shade_step(game, t) != INT32_MIN)
            ++applied;
    expect(applied == kShadeSteps && game.endgame_shade_countdown == 0, "ten shade steps");
}

// A session record carrying the OTA's timemul and killmul.
oa::data::campaign::CampaignFile* session_of(oa::data::campaign::SessionKind kind) {
    static oa::data::campaign::CampaignFile sessions[4]{};
    oa::data::campaign::CampaignFile& session = sessions[static_cast<int32_t>(kind) & 3];
    session.kind = kind;
    session.time_multiplier = 0.3f;
    session.kill_multiplier = 33.3f;
    return &session;
}

void set_active(oa::World& world, uint32_t index, uint8_t status, uint16_t options) {
    oa::Player& player = world.game.players[index];
    player.in_use = 1;
    player.status = status;
    player.index = static_cast<uint8_t>(index);
    player.info = oa::oa_ref_from_index(index);
    world.player_info[index].options = options;
    world.player_info[index].color = static_cast<uint8_t>(index + 1);
}

// The local player, an active watcher that never built, a departed slot that
// built units and a computer.
void build_players(oa::World& world) {
    world.game.tick = 5432; // 90 steps of 60 ticks
    set_active(world, 0, OA_PLAYER_STATUS_LOCAL, 0);
    oa::Player& p0 = world.game.players[0];
    std::strcpy(p0.name, "Commander");
    p0.kills = 3;
    p0.losses = 1;
    p0.energy_produced_total = 1234.9;
    p0.metal_produced_total = 88.5;
    p0.energy_wasted_total = -5.7;
    p0.metal_wasted_total = 150.2;
    set_active(world, 1, OA_PLAYER_STATUS_MIRRORED, OA_SETUP_OPTION_WATCHER);
    std::strcpy(world.game.players[1].name, "Watcher");
    oa::Player& p2 = world.game.players[2];
    std::strcpy(p2.name, "Gone");
    p2.units_created = 4;
    set_active(world, 3, OA_PLAYER_STATUS_COMPUTER, 0);
    oa::Player& p3 = world.game.players[3];
    std::strcpy(p3.name, "Computer");
    p3.kills = 12;
    p3.units_created = 20;
}

void score_tests() {
    static oa::World world{};
    build_players(world);
    world.game.outcome_flags = 0x10;
    oa::data::campaign::CampaignFile& campaign =
        *session_of(oa::data::campaign::SessionKind::campaign);
    campaign.mission_index = 2;
    build_score_summary(world, &campaign);
    const oa::Game& game = world.game;
    expect(
        game.victory == 1 && game.mission_index == 2 && game.mission_results[2] == 'W',
        "victory recorded"
    );
    const oa::ScoreEntry& local = game.scores[0];
    expect(std::strcmp(local.name, "Commander") == 0, "local name");
    expect(local.values[score_kills] == 3 && local.values[score_losses] == 1, "kills and losses");
    expect(
        local.values[score_energy_produced] == 1234 && local.values[score_metal_produced] == 88,
        "production truncates"
    );
    expect(
        local.values[score_energy_wasted] == -5 && local.values[score_metal_wasted] == 150,
        "waste truncates toward zero"
    );
    // trunc(90 * 0.3f = 27.0000011) + trunc(3 * 33.3f = 99.8999977) = 27 + 99.
    expect(local.values[score_total] == 126, "score formula");
    expect(game.scores[1].name[0] == '\0', "active watcher that never built is left out");
    expect(
        std::strcmp(game.scores[2].name, "Gone") == 0 && game.scores[2].values[score_total] == 27,
        "departed builder listed"
    );
    // 27 + trunc(12 * 33.3f = 399.5999908) = 27 + 399.
    expect(game.scores[3].values[score_total] == 426, "computer score");
    expect(
        game.score_maxima[score_kills] == 12 && game.score_maxima[score_losses] == 10, "kill maxima"
    );
    expect(
        game.score_maxima[score_energy_produced] == 1234 &&
            game.score_maxima[score_metal_produced] == 100 && game.score_maxima[score_total] == 426,
        "maxima start at 100"
    );

    // A negative score is shown as zero: trunc(90 * -2) + 99 = -81.
    campaign.time_multiplier = -2.0f;
    world.game.outcome_flags = 0;
    build_score_summary(world, &campaign);
    expect(
        world.game.scores[0].values[score_total] == 0 && world.game.mission_results[2] == 'L',
        "negative score clamps; defeat recorded"
    );
    campaign.time_multiplier = 0.3f;
    world.game.outcome_flags = 0x10;
    build_score_summary(world, nullptr);
    expect(
        world.game.scores[0].values[score_total] == 0 &&
            world.game.scores[0].values[score_kills] == 3,
        "without a session record nothing weights the score"
    );
    build_score_summary(world, session_of(oa::data::campaign::SessionKind::skirmish));
    expect(
        world.game.scores[0].values[score_total] == 126 && world.game.mission_results[2] == 'L',
        "a skirmish scores with its own multipliers and records no mission"
    );

    static ScoreLayout layout;
    world.game.endgame_column = 5;
    layout_score_entries(world, &layout);
    expect(world.game.endgame_column == 0, "layout restarts the columns");
    expect(
        layout.row_count == 3 && layout.rows[0].player == 0 && layout.rows[1].player == 2 &&
            layout.rows[2].player == 3,
        "one row per named entry"
    );
    expect(
        layout.rows[0].y == kScoreFirstRowY &&
            layout.rows[1].y == kScoreFirstRowY + kScoreRowStep &&
            layout.rows[2].bars[score_total].x == 0x22c &&
            layout.rows[0].bars[score_kills].x == 0x70,
        "row and bar placement"
    );
    expect(
        layout.rows[2].color == 4 && layout.rows[1].color == 0,
        "swatch colour from the setup record"
    );
    const ScoreBar& score_bar = layout.rows[2].bars[score_total];
    expect(
        score_bar.value == 426 && score_bar.maximum == 426 && !score_bar.active &&
            score_bar.current == 0,
        "bars start hidden at zero"
    );
    expect(layout.rows[0].bars[score_kills].rate == 1.0f, "rate is at least one");
    expect(!score_bars_settled(&layout), "hidden bars are not settled");

    // Shown bars count up by trunc(rate) = 28 every other tick: 15 steps reach
    // 420 at tick 29, the 16th clamps to 426 at tick 31.
    activate_score_column(&layout, score_total);
    expect(
        layout.rows[0].bars[score_total].active && layout.rows[2].bars[score_total].active &&
            !layout.rows[2].bars[score_kills].active,
        "a column activates in every row"
    );
    for (uint32_t t = 1; t <= 30; ++t)
        advance_score_bars(&layout, t);
    expect(score_bar.current == 420 && score_bar.running, "bar counts up");
    advance_score_bars(&layout, 31);
    expect(score_bar.current == 426 && !score_bar.running, "bar stops at its value");
    for (uint32_t column = 0; column < score_column_count; ++column)
        activate_score_column(&layout, column);
    for (uint32_t t = 32; t < 400; ++t)
        advance_score_bars(&layout, t);
    expect(score_bars_settled(&layout), "every bar settles");

    ScoreBar half{};
    half.x = 0x22c;
    half.current = 213;
    half.maximum = 426;
    ScoreBarDraw draw{};
    draw_score_bar(half, 0x5d, test_font(), &draw);
    expect(
        draw.left == 0x22c && draw.top == 0x5d && draw.right == 0x22c + 0x43 &&
            draw.bottom == 0x5d + 0x12,
        "bar frame"
    );
    expect(
        draw.inner_left == 0x22e && draw.inner_right == 0x22c + 0x41 &&
            draw.fill_right == 0x22e + 31,
        "fill is trunc(213 / 426 * 63)"
    );
    expect(std::strcmp(draw.label, "213") == 0, "bar value text");
    // "213" is 17 wide and the line 12 high: 33 - 8 across, 9 - 6 down.
    expect(draw.label_x == 0x22c + 25 && draw.label_y == 0x5d + 3, "value centred on its bar");
    half.current = -5;
    draw_score_bar(half, 0x71, test_font(), &draw);
    expect(
        std::strcmp(draw.label, "-5") == 0 && draw.label_x == 0x22c + 27 &&
            draw.label_y == 0x71 + 3,
        "a negative value is signed"
    );
    half.current = 1;
    half.maximum = 3;
    draw_score_bar(half, 0, test_font(), &draw);
    expect(draw.fill_right == 0x22e + 21, "53-bit quotient rounds 1 / 3 * 63 to 21");
}

void name_tests() {
    ScoreRow row{};
    row.y = kScoreFirstRowY + kScoreRowStep;
    row.name_x = kScoreNameX;
    ScoreNameDraw name{};
    // 24 wide: half the label less half the name across, half of the row
    // less the line's 12 rows down.
    place_score_name(row, "Core", test_font(), &name);
    expect(
        name.x == kScoreNameX + 45 - 12 && name.y == row.y + 4 &&
            std::strcmp(name.text, "Core") == 0,
        "name centred in its label"
    );
    // "Arm 1" is 6 + 6 + 6 + 7 + 5 = 30 wide; the space takes room undrawn.
    place_score_name(row, "Arm 1", test_font(), &name);
    expect(
        name.x == kScoreNameX + 45 - 15 && std::strcmp(name.text, "Arm 1") == 0,
        "the space counts toward the width"
    );
    // Twenty glyphs are 120 wide: centring measures them all, and the
    // fifteen that fill the 90-pixel label are kept.
    place_score_name(row, "ABCDEFGHIJKLMNOPQRST", test_font(), &name);
    expect(
        name.x == kScoreNameX + 45 - 60 && std::strcmp(name.text, "ABCDEFGHIJKLMNO") == 0,
        "a long name is cut to the label's width"
    );
    // A byte with no glyph takes no room and is kept.
    place_score_name(row, "ABCDEFGHIJKLMN\x01O", test_font(), &name);
    expect(std::strcmp(name.text, "ABCDEFGHIJKLMN\x01O") == 0, "a byte without a glyph is free");
    place_score_name(row, nullptr, test_font(), &name);
    expect(name.x == kScoreNameX + 45 && name.text[0] == '\0', "no name");
}

void state_tests() {
    static oa::Game game{};
    set_endgame_state(game, OA_ENDGAME_STAT_BARS);
    expect(game.endgame_state == OA_ENDGAME_STAT_BARS, "state stored");
}

struct Screen {
    uint32_t now = 1;
    bool input = false;
    bool message = false;
    bool disc = true;
    bool glamour = false;
    bool movies = true;
    int captured = 0;
    int reported = 0;
    std::vector<int> reasons;
    int waits = 0;
    std::vector<int32_t> shades;
    int finished_shade = 0;
    int cd_checks = 0;
    int outcomes = 0;
    int endings = 0;
    uint8_t ending_state = 0xff;
    int palettes = 0;
    std::vector<std::string> streams;
    uint32_t stream_delay = 0;
    int stream_stops = 0;
    int panels = 0;
    int draws = 0;
    int continues = 0;
    int statuses = 0;
    int leaves = 0;
    std::vector<std::string> sounds;
    std::vector<std::string> controls;
};

Screen* g_screen = nullptr;

EndgameHost host_for(Screen& s, FrontendHost& frontend) {
    g_screen = &s;
    frontend = {};
    frontend.context = &s;
    frontend.disc_present = [](void* c) { return static_cast<Screen*>(c)->disc; };
    frontend.set_control_value = [](void* c, const char* name, int32_t value) {
        if (value != 0)
            static_cast<Screen*>(c)->controls.emplace_back(name);
    };
    EndgameHost h{};
    h.context = &s;
    h.now = [](void* c) { return static_cast<Screen*>(c)->now; };
    h.ticks_per_second = [](void*) { return 30u; };
    h.capture_frame = [](void* c) { return ++static_cast<Screen*>(c)->captured > 0; };
    h.report_end = [](void* c) { ++static_cast<Screen*>(c)->reported; };
    h.disconnect_message = [](void* c, uint8_t reason) {
        static_cast<Screen*>(c)->reasons.push_back(reason);
    };
    h.message_open = [](void* c) { return static_cast<Screen*>(c)->message; };
    h.draw_wait_frame = [](void* c) { ++static_cast<Screen*>(c)->waits; };
    h.apply_shade = [](void* c, int32_t level) {
        static_cast<Screen*>(c)->shades.push_back(level);
    };
    h.finish_shade = [](void* c) { ++static_cast<Screen*>(c)->finished_shade; };
    h.open_cd_check = [](void* c) { ++static_cast<Screen*>(c)->cd_checks; };
    h.show_outcome = [](void* c) { ++static_cast<Screen*>(c)->outcomes; };
    h.glamour_palette = [](void* c) -> const uint8_t* {
        static const uint8_t kGlamour[kPaletteBytes] = {200, 100, 50};
        return static_cast<Screen*>(c)->glamour ? kGlamour : nullptr;
    };
    h.movies_enabled = [](void* c) { return static_cast<Screen*>(c)->movies; };
    h.play_ending = [](void* c, uint8_t next) {
        auto& s2 = *static_cast<Screen*>(c);
        ++s2.endings;
        s2.ending_state = next;
    };
    h.apply_palette = [](void* c, const uint8_t*) { ++static_cast<Screen*>(c)->palettes; };
    h.play_stream = [](void* c, const char* path, int32_t volume, uint32_t delay) {
        auto& s2 = *static_cast<Screen*>(c);
        if (volume == 0)
            s2.streams.emplace_back(path);
        s2.stream_delay = delay;
    };
    h.stop_stream = [](void* c) { ++static_cast<Screen*>(c)->stream_stops; };
    h.open_panel = [](void* c) { ++static_cast<Screen*>(c)->panels; };
    h.input = [](void* c) {
        auto& s2 = *static_cast<Screen*>(c);
        const bool waiting = s2.input;
        s2.input = false;
        return waiting;
    };
    h.draw_continue = [](void* c, const char* text) {
        if (std::strcmp(text, "Click to continue.") == 0)
            ++static_cast<Screen*>(c)->continues;
    };
    h.play_sound = [](void* c, const char* name) {
        static_cast<Screen*>(c)->sounds.emplace_back(name);
    };
    h.send_status = [](void* c) { ++static_cast<Screen*>(c)->statuses; };
    h.draw_panel = [](void* c) { ++static_cast<Screen*>(c)->draws; };
    h.leave_to_panel = [](void* c) { ++static_cast<Screen*>(c)->leaves; };
    h.frontend = &frontend;
    return h;
}

// One frame: the bars step, then the screen, as the overlay runs them.
void frame(
    oa::World& world,
    oa::data::campaign::CampaignFile* campaign,
    EndgameScreen& screen,
    const EndgameHost& host,
    Screen& s
) {
    advance_score_bars(&screen.layout, s.now);
    endgame_tick(world, campaign, screen, host);
    ++s.now;
}

void skirmish_screen_tests() {
    static oa::World world{};
    build_players(world);
    oa::data::campaign::CampaignFile& session =
        *session_of(oa::data::campaign::SessionKind::skirmish);
    build_score_summary(world, &session);
    Screen s;
    FrontendHost frontend{};
    const EndgameHost host = host_for(s, frontend);
    static EndgameScreen screen{};
    set_endgame_state(world.game, OA_ENDGAME_CAPTURE);

    frame(world, &session, screen, host, s);
    expect(
        world.game.endgame_state == OA_ENDGAME_SHADE_START && s.captured == 0 && s.reported == 0,
        "skirmish keeps no frame and reports nothing"
    );
    frame(world, &session, screen, host, s);
    expect(world.game.endgame_state == OA_ENDGAME_SHADING, "shade starts");
    for (int i = 0; i < 40 && world.game.endgame_state == OA_ENDGAME_SHADING; ++i)
        frame(world, &session, screen, host, s);
    expect(
        s.shades.size() == 10 && s.shades.front() == 10 - kShadeBias &&
            s.shades.back() == 1 - kShadeBias,
        "ten shade levels"
    );
    expect(
        world.game.endgame_state == OA_ENDGAME_DISC_CHECK && s.finished_shade == 1, "shade finished"
    );
    frame(world, &session, screen, host, s);
    expect(world.game.endgame_state == OA_ENDGAME_OUTCOME, "no disc check outside a campaign");
    frame(world, &session, screen, host, s);
    expect(
        world.game.endgame_state == OA_ENDGAME_STAT_BARS && s.outcomes == 1 && s.panels == 1,
        "panel opens"
    );
    expect(
        screen.layout.row_count == 3 && s.controls.size() == 1 && s.controls[0] == "MainMenu",
        "score rows and the Main Menu button"
    );

    // One column every ten ticks, the score column last with its own sound.
    uint32_t kills_at = 0;
    for (int i = 0; i < 80 && world.game.endgame_column < 7; ++i) {
        frame(world, &session, screen, host, s);
        if (kills_at == 0 && screen.layout.rows[0].bars[score_kills].active)
            kills_at = s.now - 1;
    }
    expect(world.game.endgame_column == 7 && s.sounds.size() == 7, "seven columns activated");
    expect(s.sounds[0] == "EndGameStatBar" && s.sounds[6] == "EndGameScore", "column sounds");
    expect(
        world.game.endgame_next_tick == s.now - 1 + kStatColumnDelay && kills_at != 0,
        "columns ten ticks apart"
    );
    expect(s.statuses == 0, "no status broadcast outside multiplayer");
    for (int i = 0; i < 200 && world.game.endgame_state == OA_ENDGAME_STAT_BARS; ++i)
        frame(world, &session, screen, host, s);
    expect(
        world.game.endgame_state == OA_ENDGAME_PANEL && s.leaves == 1,
        "buttons take over once bars settle"
    );
    expect(screen.layout.rows[2].bars[score_total].current == 426, "score bar shows the score");
}

void skip_tests() {
    static oa::World world{};
    build_players(world);
    build_score_summary(world, session_of(oa::data::campaign::SessionKind::skirmish));
    Screen s;
    FrontendHost frontend{};
    const EndgameHost host = host_for(s, frontend);
    static EndgameScreen screen{};
    screen = {};
    layout_score_entries(world, &screen.layout);
    world.game.endgame_state = OA_ENDGAME_STAT_BARS;
    world.game.endgame_next_tick = 1000;
    frame(world, nullptr, screen, host, s);
    expect(world.game.endgame_column == 0 && s.draws == 1, "waits for the next column");
    s.input = true;
    frame(world, nullptr, screen, host, s);
    bool all = true;
    for (const ScoreBar& bar : screen.layout.rows[0].bars)
        all = all && bar.active;
    expect(
        all && s.sounds.size() == 2 && s.sounds[0] == "ActivateAllStatBars" &&
            world.game.endgame_column == 1,
        "input shows every column at once"
    );
}

void multiplayer_screen_tests() {
    static oa::World world{};
    build_players(world);
    oa::data::campaign::CampaignFile& session =
        *session_of(oa::data::campaign::SessionKind::multiplayer);
    build_score_summary(world, &session);
    Screen s;
    FrontendHost frontend{};
    const EndgameHost host = host_for(s, frontend);
    static EndgameScreen screen{};
    world.game.players[0].reject_reason = 6;
    world.game.endgame_state = OA_ENDGAME_CAPTURE;
    s.message = true;
    frame(world, &session, screen, host, s);
    expect(
        world.game.endgame_state == OA_ENDGAME_WAIT_MESSAGE && screen.frame_captured &&
            s.reported == 1,
        "multiplayer keeps the frame and reports the end"
    );
    expect(
        s.reasons.size() == 1 && s.reasons[0] == 6 && world.game.players[0].reject_reason == 0,
        "disconnect message shown once"
    );
    frame(world, &session, screen, host, s);
    expect(
        s.waits == 1 && world.game.endgame_state == OA_ENDGAME_WAIT_MESSAGE,
        "waits under the message"
    );
    s.message = false;
    frame(world, &session, screen, host, s);
    expect(world.game.endgame_state == OA_ENDGAME_SHADE_START, "message closed");

    world.game.players[0].reject_reason = 2;
    world.game.endgame_state = OA_ENDGAME_CAPTURE;
    frame(world, &session, screen, host, s);
    expect(
        s.reasons.size() == 1 && world.game.players[0].reject_reason == 2,
        "reason 2 shows no message"
    );

    layout_score_entries(world, &screen.layout);
    world.game.endgame_state = OA_ENDGAME_STAT_BARS;
    world.game.endgame_next_tick = s.now + 5;
    s.input = true;
    frame(world, &session, screen, host, s);
    expect(world.game.endgame_column == 0, "input does not hurry a multiplayer column");
    for (int i = 0; i < 6; ++i)
        frame(world, &session, screen, host, s);
    expect(world.game.endgame_column == 1 && s.statuses == 2, "each column sends the status twice");
}

void campaign_screen_tests() {
    static oa::World world{};
    build_players(world);
    static oa::data::campaign::CampaignFile campaign{};
    campaign.kind = oa::data::campaign::SessionKind::campaign;
    build_score_summary(world, &campaign);
    Screen s;
    FrontendHost frontend{};
    const EndgameHost host = host_for(s, frontend);
    static EndgameScreen screen{};

    s.disc = false;
    world.game.endgame_state = OA_ENDGAME_DISC_CHECK;
    frame(world, &campaign, screen, host, s);
    expect(
        s.cd_checks == 1 && world.game.endgame_state == OA_ENDGAME_PANEL,
        "campaign asks for the disc"
    );

    // The last mission won: frontend state 4 (movies 3 then 5) for an Arm
    // first player, 5 (movies 4 then 5) for Core, 2 with movies off.
    s.disc = true;
    world.game.outcome_flags = 0x10;
    world.game.endgame_state = OA_ENDGAME_OUTCOME;
    frame(world, &campaign, screen, host, s);
    expect(
        s.endings == 1 && s.ending_state == 4 && world.game.endgame_state == OA_ENDGAME_OUTCOME,
        "last Arm victory plays movies 3 and 5"
    );
    world.player_info[0].side = 1;
    frame(world, &campaign, screen, host, s);
    expect(s.endings == 2 && s.ending_state == 5, "last Core victory plays movies 4 and 5");
    s.movies = false;
    frame(world, &campaign, screen, host, s);
    expect(s.endings == 3 && s.ending_state == 2, "movies off: straight to the main menu");
    world.player_info[0].side = 0;
    s.movies = true;
    std::snprintf(
        campaign.paths[static_cast<uint32_t>(oa::data::campaign::CampaignPath::glamour_sound)],
        sizeof campaign.paths[0],
        "%s",
        "camps/briefs/arm01g.WAV"
    );

    world.game.no_movie = 1;
    s.glamour = true;
    frame(world, &campaign, screen, host, s);
    expect(
        world.game.endgame_state == OA_ENDGAME_GLAMOUR && s.panels == 0 && s.palettes == 1,
        "victory fades to the glamour from black"
    );
    for (int i = 0; i < 60 && world.game.endgame_fade_done == 0; ++i)
        frame(world, &campaign, screen, host, s);
    expect(world.game.endgame_fade_done == 1 && s.palettes > 1, "glamour fade applied from black");
    const uint32_t shown_from = world.game.endgame_next_tick;
    for (int i = 0; i < 200 && s.continues == 0; ++i)
        frame(world, &campaign, screen, host, s);
    expect(
        s.streams.size() == 1 && s.streams[0] == "camps/briefs/arm01g.WAV" &&
            s.stream_delay == 0x3c,
        "the glamour sound streams once the fade is done"
    );
    expect(
        s.continues > 0 && s.now - 1 > shown_from + 30 * kContinueDelaySeconds,
        "click to continue after five seconds"
    );
    s.input = true;
    frame(world, &campaign, screen, host, s);
    expect(
        world.game.endgame_state == OA_ENDGAME_STAT_BARS && s.panels == 1 && s.stream_stops == 1,
        "input stops the glamour sound and opens the panel"
    );

    // The glamour sound streams nothing while a match runs (app mode 6).
    world.game.mode = 6;
    start_glamour_sound(world.game, &campaign, host);
    world.game.mode = 7;
    start_glamour_sound(world.game, &campaign, host);
    expect(s.streams.size() == 2, "no glamour sound in app mode 6");
}

const uint8_t* pixel(const oa::ui::frontend_renderer::Surface& surface, int32_t x, int32_t y) {
    return surface.rgb.data() +
           (static_cast<std::size_t>(y) * surface.width + static_cast<std::size_t>(x)) * 3U;
}

bool rgb_is(const uint8_t* rgb, uint8_t r, uint8_t g, uint8_t b) {
    return rgb[0] == r && rgb[1] == g && rgb[2] == b;
}

void set_rgb(oa::PaletteBytes& palette, std::size_t index, uint8_t r, uint8_t g, uint8_t b) {
    palette[index * 4] = r;
    palette[index * 4 + 1] = g;
    palette[index * 4 + 2] = b;
}

// The inclusive bounds of the pixels of one colour inside a rectangle; left
// past right when there are none.
struct Box {
    int32_t left{INT32_MAX};
    int32_t top{INT32_MAX};
    int32_t right{INT32_MIN};
    int32_t bottom{INT32_MIN};
};

Box find_color(
    const oa::ui::frontend_renderer::Surface& surface,
    int32_t left,
    int32_t top,
    int32_t right,
    int32_t bottom,
    const uint8_t* rgb
) {
    Box box{};
    for (int32_t y = top; y <= bottom; ++y)
        for (int32_t x = left; x <= right; ++x)
            if (rgb_is(pixel(surface, x, y), rgb[0], rgb[1], rgb[2])) {
                box.left = std::min(box.left, x);
                box.top = std::min(box.top, y);
                box.right = std::max(box.right, x);
                box.bottom = std::max(box.bottom, y);
            }
    return box;
}

bool box_is(const Box& box, int32_t left, int32_t top, int32_t right, int32_t bottom) {
    return box.left == left && box.top == top && box.right == right && box.bottom == bottom;
}

// The registered overlay runs the screen, takes presses until the buttons
// take over and draws the swatches and shown bars in the published palette.
void overlay_tests() {
    static oa::app::ScreenRegistry registry{};
    oa::app::register_campaign_screens(&registry);
    expect(
        registry.overlay_count == 1 && registry.overlays[0].screen == kScoreOverlayScreen,
        "overlay registered"
    );
    const oa::app::OverlayDesc& overlay = registry.overlays[0];
    static oa::World world{};
    build_players(world);
    build_score_summary(world, session_of(oa::data::campaign::SessionKind::skirmish));

    // The UI colour table as the app builds it: each guipal colour's nearest
    // entry of the drawing palette. Guipal slots 0, 4, 8, 0x11 and 0x14 are
    // black, red, dark grey, light grey and grey.
    static oa::PaletteBytes gui{};
    set_rgb(gui, 0x04, 170, 0, 0);
    set_rgb(gui, 0x08, 85, 85, 85);
    set_rgb(gui, 0x11, 223, 223, 223);
    set_rgb(gui, 0x14, 183, 183, 183);
    static oa::PaletteBytes palette{};
    for (std::size_t i = 1; i < oa::palette_color_count; ++i)
        set_rgb(palette, i, 0, 0, 255);
    set_rgb(palette, 1, 171, 23, 0);
    set_rgb(palette, 2, 91, 91, 91);
    set_rgb(palette, 3, 219, 219, 219);
    set_rgb(palette, 5, 187, 187, 187);
    set_rgb(palette, 9, 40, 200, 40);
    set_rgb(palette, kGlyphColor, 250, 250, 0);
    set_rgb(palette, kLitGlyphColor, 0, 250, 250);
    const oa::PaletteMap table = oa::remap_palette(gui, palette);
    std::copy(table.begin(), table.end(), world.game.ui_colors);
    expect(
        world.game.ui_colors[4] == 1 && world.game.ui_colors[8] == 2 &&
            world.game.ui_colors[0x11] == 3 && world.game.ui_colors[0x14] == 5,
        "UI colour table"
    );

    // 32xlogos stand-in: frame n is a flat 2x2 tile of palette entry 9 for the
    // Computer's colour (4), 0 otherwise.
    static oa::formats::gaf::Sequence logos{};
    logos.frames.resize(6);
    for (std::size_t i = 0; i < logos.frames.size(); ++i) {
        oa::formats::gaf::Frame& tile = logos.frames[i];
        tile.width = 2;
        tile.height = 2;
        tile.transparency_index = 0xff;
        tile.pixels.assign(4, i == 4 ? 9 : 0);
        tile.coverage.assign(4, 1);
    }

    Screen s;
    FrontendHost frontend{};
    const EndgameHost host = host_for(s, frontend);
    static EndgameScreen screen{};
    world.game.endgame_state = OA_ENDGAME_CAPTURE;
    EndgameView view{};
    view.world = &world;
    view.campaign = session_of(oa::data::campaign::SessionKind::skirmish);
    view.screen = &screen;
    view.host = &host;
    view.palette = palette.data();
    view.player_logos = &logos;
    view.label_font = &test_font();
    // Every light-table row leaves each colour as it is, but for row
    // kScoreNameLight, which lights the glyph colour.
    static std::vector<uint8_t> light(32U * 256U);
    for (std::size_t i = 0; i < light.size(); ++i)
        light[i] = static_cast<uint8_t>(i % 256U);
    light[kScoreNameLight * 256U + kGlyphColor] = kLitGlyphColor;
    view.light_table = light.data();
    publish_endgame(view);

    oa::ui::frontend_renderer::Surface surface{640, 480, std::vector<uint8_t>(640U * 480U * 3U, 0)};
    oa::app::ScreenContext ctx{};
    ctx.surface = &surface;
    ctx.screen = kScoreOverlayScreen;
    for (int i = 0; i < 60 && world.game.endgame_state != OA_ENDGAME_STAT_BARS; ++i, ++s.now)
        overlay.tick(&ctx, overlay.state);
    expect(
        world.game.endgame_state == OA_ENDGAME_STAT_BARS && s.panels == 1,
        "overlay reaches the stat bars"
    );

    oa::app::ScreenInput press{};
    press.kind = oa::app::ScreenInputKind::pointer_down;
    ctx.input = &press;
    expect(overlay.event(&ctx, overlay.state) == 1, "a press is taken by the stat bars");
    ctx.input = nullptr;
    overlay.tick(&ctx, overlay.state);
    expect(
        !s.sounds.empty() && s.sounds[0] == "ActivateAllStatBars", "the press shows every column"
    );
    for (int i = 0; i < 400 && world.game.endgame_state == OA_ENDGAME_STAT_BARS; ++i, ++s.now)
        overlay.tick(&ctx, overlay.state);
    expect(world.game.endgame_state == OA_ENDGAME_PANEL, "bars settle through the overlay");
    ctx.input = &press;
    expect(overlay.event(&ctx, overlay.state) == 0, "the buttons get presses once the panel is up");
    ctx.input = nullptr;

    overlay.draw(&ctx, overlay.state);
    // The computer's score bar (third row) is full: the fill spans the well.
    const int32_t row_y = kScoreFirstRowY + 2 * kScoreRowStep;
    expect(
        rgb_is(pixel(surface, 0x22c + 2, row_y + 2), 171, 23, 0) &&
            rgb_is(pixel(surface, 0x22c + 0x41, row_y + 0x10), 171, 23, 0),
        "filled part in UI colour 4, inclusive of the well's far corner"
    );
    expect(
        rgb_is(pixel(surface, 0x22c, row_y), 0, 0, 0) &&
            rgb_is(pixel(surface, 0x22c + 1, row_y + 1), 0, 0, 0),
        "raised box: top-left edges in UI colour 0"
    );
    expect(
        rgb_is(pixel(surface, 0x22c + 0x43, row_y + 0x12), 219, 219, 219) &&
            rgb_is(pixel(surface, 0x22c + 0x42, row_y + 0x11), 219, 219, 219),
        "raised box: bottom-right edges in UI colour 0x11"
    );
    // The local player's kills bar is 3 of 12: trunc(3 / 12 * 63) = 15 columns
    // past the inner left edge are filled, the rest is the well.
    expect(
        rgb_is(pixel(surface, 0x70 + 2 + 15, kScoreFirstRowY + 3), 171, 23, 0) &&
            rgb_is(pixel(surface, 0x70 + 2 + 16, kScoreFirstRowY + 3), 91, 91, 91),
        "unfilled part in UI colour 8"
    );
    // The quad's right and bottom edges are left out, so the swatch covers
    // 0x5A x 0x14 pixels and the swatches of two rows meet without overlap.
    const uint8_t green[3] = {40, 200, 40};
    expect(
        box_is(
            find_color(surface, 0, row_y - kScoreRowStep, 0x6f, row_y + kScoreRowStep, green),
            kScoreNameX,
            row_y,
            kScoreNameX + kScoreSwatchWidth - 2,
            row_y + kScoreSwatchHeight - 2
        ),
        "player colour swatch stretched over its rectangle"
    );

    // Each row's name sits on its own swatch and each value on its own bar:
    // the name lit through row kScoreNameLight, 10 glyph rows starting 5 rows
    // below the row's top; the kills value unlit, 4 rows below the bar's top,
    // inside its well.
    struct RowText {
        int32_t name_left;
        int32_t name_right;
        int32_t value_left;
        int32_t value_right;
    };

    // "Commander", "Gone" and "Computer"; kills 3, 0 and 12.
    constexpr RowText kRowText[3] = {{34, 87, 30, 35}, {49, 72, 30, 35}, {37, 84, 28, 38}};
    const uint8_t* lit = palette.data() + kLitGlyphColor * 4U;
    const uint8_t* unlit = palette.data() + kGlyphColor * 4U;
    for (int32_t r = 0; r < 3; ++r) {
        const int32_t top = kScoreFirstRowY + r * kScoreRowStep;
        const int32_t bottom = top + kScoreRowStep - 1;
        const RowText& text = kRowText[r];
        expect(
            box_is(
                find_color(surface, 0, top, 0x6f, bottom, lit),
                text.name_left,
                top + 5,
                text.name_right,
                top + 14
            ),
            "name centred on its row's swatch"
        );
        expect(
            find_color(surface, 0, top, 0x6f, bottom, unlit).left == INT32_MAX,
            "names are drawn lit"
        );
        expect(
            box_is(
                find_color(surface, 0x70, top, 0x70 + kScoreBarWidth, bottom, unlit),
                0x70 + text.value_left,
                top + 4,
                0x70 + text.value_right,
                top + 13
            ),
            "value centred on its row's bar"
        );
    }

    // While the glamour picture shows, it covers the screen through the
    // fade's current palette and the score rows are not drawn.
    static uint8_t glamour_pixels[4] = {1, 2, 3, 9};
    static const EndgamePicture picture{glamour_pixels, 2, 2};
    view.glamour = &picture;
    publish_endgame(view);
    screen.fade.current[9 * 4] = 12;
    screen.fade.current[9 * 4 + 1] = 34;
    screen.fade.current[9 * 4 + 2] = 56;
    world.game.endgame_state = OA_ENDGAME_GLAMOUR;
    std::fill(surface.rgb.begin(), surface.rgb.end(), 7);
    overlay.draw(&ctx, overlay.state);
    expect(
        rgb_is(pixel(surface, 1, 1), 12, 34, 56) && rgb_is(pixel(surface, 2, 2), 7, 7, 7) &&
            rgb_is(pixel(surface, 0x22c + 3, row_y + 3), 7, 7, 7),
        "glamour picture drawn through the fade palette, without the score rows"
    );
    world.game.endgame_state = OA_ENDGAME_PANEL;

    publish_endgame(EndgameView{});
    std::fill(surface.rgb.begin(), surface.rgb.end(), 0);
    overlay.draw(&ctx, overlay.state);
    expect(rgb_is(pixel(surface, 0x22c + 3, row_y + 3), 0, 0, 0), "nothing drawn once cleared");
}

} // namespace

int main() {
    fade_tests();
    score_tests();
    name_tests();
    state_tests();
    skirmish_screen_tests();
    skip_tests();
    multiplayer_screen_tests();
    campaign_screen_tests();
    overlay_tests();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "endgame tests passed\n";
    return 0;
}
