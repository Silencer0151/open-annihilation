// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"
#include "fixtures.hpp"

#include "oa/ui/hud/kill_board.hpp"

#include <cstring>
#include <string>
#include <vector>

using namespace oa;
using namespace oa::ui::hud;

namespace {

constexpr int32_t kGlyphWidth = 6;
constexpr int32_t kScreenWidth = 640;

struct Op {
    std::string kind;
    std::string text;
    int32_t a{}, b{}, c{}, d{}, e{};
};

struct Recorder {
    std::vector<Op> ops;
    std::vector<std::string> sounds;

    KillBoardSink sink() {
        KillBoardSink s{};
        s.user = this;
        s.play_sound = [](void* user, const char* name) {
            static_cast<Recorder*>(user)->sounds.emplace_back(name);
        };
        s.shade = [](void* user, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t level) {
            static_cast<Recorder*>(user)->ops.push_back({"shade", {}, x0, y0, x1, y1, level});
        };
        s.text =
            [](void* user, const char* text, int32_t x, int32_t y, int32_t width, uint8_t flash) {
                static_cast<Recorder*>(user)->ops.push_back({"text", text, x, y, width, flash, 0});
            };
        s.text_width = [](void*, const char* text) {
            return static_cast<int32_t>(std::strlen(text)) * kGlyphWidth;
        };
        s.logo =
            [](void* user, const Player& player, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
                static_cast<Recorder*>(user)->ops.push_back(
                    {"logo", player.name, x0, y0, x1, y1, 0}
                );
            };
        return s;
    }

    void clear() {
        ops.clear();
        sounds.clear();
    }
};

bool is(
    const Op& op,
    const char* kind,
    const char* text,
    int32_t a,
    int32_t b,
    int32_t c,
    int32_t d,
    int32_t e = 0
) {
    return op.kind == kind && op.text == text && op.a == a && op.b == b && op.c == c && op.d == d &&
           op.e == e;
}

bool shade(const Op& op, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t level) {
    return is(op, "shade", "", x0, y0, x1, y1, level);
}

// Three players in a skirmish: 0 local, 1 and 2 computers.
struct Board {
    hud_test::TestWorld w;
    KillBoard board{};
    Recorder rec;

    Board() {
        for (uint8_t i = 0; i < 3; ++i) {
            const auto status =
                static_cast<uint8_t>(i == 0 ? OA_PLAYER_STATUS_LOCAL : OA_PLAYER_STATUS_COMPUTER);
            auto& player = w.add_player(i, status);
            player.board_row = i;
        }
        w.game().player_count = 3;
        w.game().local_player_index = 0;
    }

    void draw(bool wanted, uint32_t now = 0) {
        rec.clear();
        draw_kill_board(*w.world, board, now, wanted, kScreenWidth, rec.sink());
    }
};

// The board moves the slide a quarter of what is left (at least one pixel)
// per frame: "Panel" as it leaves an end, "Options" as it reaches one.
void slides_in_and_out() {
    Board b;
    b.draw(false);
    CHECK(b.rec.ops.empty() && b.rec.sounds.empty() && b.board.slide == 0);
    const int32_t in[] = {
        31, 54, 71, 84, 94, 101, 107, 111, 114, 116, 118, 119, 120, 121, 122, 123, 124, 125
    };
    for (size_t frame = 0; frame < std::size(in); ++frame) {
        b.draw(true);
        CHECK(b.board.slide == in[frame]);
        CHECK(!b.rec.ops.empty());
        const int32_t left = kScreenWidth - in[frame];
        CHECK(shade(b.rec.ops.front(), left, 0x20, left + 0x7d, 3 * 0x28 + 0x2e, -0x18));
        if (frame == 0)
            CHECK(b.rec.sounds == std::vector<std::string>{"Panel"});
        else if (frame + 1 == std::size(in))
            CHECK(b.rec.sounds == std::vector<std::string>{"Options"});
        else
            CHECK(b.rec.sounds.empty());
    }
    b.draw(true);
    CHECK(b.board.slide == kBoardWidth && b.rec.sounds.empty());

    const int32_t out[] = {94, 71, 54, 41, 31, 24, 18, 14, 11, 9, 7, 6, 5, 4, 3, 2, 1, 0};
    for (size_t frame = 0; frame < std::size(out); ++frame) {
        b.draw(false);
        CHECK(b.board.slide == out[frame]);
        CHECK(!b.rec.ops.empty());
        if (frame == 0)
            CHECK(b.rec.sounds == std::vector<std::string>{"Panel"});
        else if (frame + 1 == std::size(out))
            CHECK(b.rec.sounds == std::vector<std::string>{"Options"});
        else
            CHECK(b.rec.sounds.empty());
    }
    b.draw(false);
    CHECK(b.rec.ops.empty() && b.rec.sounds.empty());
}

// The board fully out against a 640-wide screen: shaded 125x(n*40+14) from
// y 32, the headers, then per player in board-row order the two-pass
// highlight of the local row, the colour logo, the name and the counts.
void draws_rows_in_board_order() {
    Board b;
    b.board.slide = kBoardWidth;
    b.w.player(2).board_row = 0;
    b.w.player(0).board_row = 1;
    b.w.player(1).board_row = 2;
    b.w.player(0).kills = 7;
    b.w.player(0).losses = 12;
    b.w.player(2).kills = 25;
    b.w.player(2).losses = 3;
    b.draw(true);
    const auto& ops = b.rec.ops;
    CHECK(ops.size() == 3 + 4 + 6 + 4);
    CHECK(shade(ops[0], 515, 32, 640, 166, -0x18));
    CHECK(is(ops[1], "text", "Kills", 517, 32, 119, 0));
    CHECK(is(ops[2], "text", "Losses", 640 - 6 * kGlyphWidth - 2, 32, 119, 0));
    // Row 0: player 2 (a computer), no highlight.
    CHECK(is(ops[3], "logo", "Player 2", 522, 48, 634, 84));
    CHECK(is(ops[4], "text", "Player 2", 524, 53, 119, 0));
    CHECK(is(ops[5], "text", "25", 524, 68, 119, 0));
    CHECK(is(ops[6], "text", "3", 634 - kGlyphWidth - 2, 68, 119, 0));
    // Row 1: the local player, lit twice around its logo.
    CHECK(shade(ops[7], 519, 86, 636, 125, 0x1f));
    CHECK(shade(ops[8], 519, 86, 636, 125, 0x14));
    CHECK(is(ops[9], "logo", "Player 0", 522, 88, 634, 124));
    CHECK(is(ops[10], "text", "Player 0", 524, 93, 119, 0));
    CHECK(is(ops[11], "text", "7", 524, 108, 119, 0));
    CHECK(is(ops[12], "text", "12", 634 - 2 * kGlyphWidth - 2, 108, 119, 0));
    // Row 2: player 1.
    CHECK(is(ops[13], "logo", "Player 1", 522, 128, 634, 164));
    CHECK(is(ops[16], "text", "0", 634 - kGlyphWidth - 2, 148, 119, 0));
}

// A deathmatch (session rules 2) board counts commanders instead.
void deathmatch_counts_commanders() {
    Board b;
    b.board.slide = kBoardWidth;
    b.w.game().session_rules = kSessionRulesCommanderCounts;
    b.w.player(1).kills = 40;
    b.w.player(1).losses = 41;
    b.w.player(1).commanders_killed = 2;
    b.w.player(1).commanders_lost = 1;
    b.draw(true);
    // Row 0 is the local player's six calls; player 1's counts follow its
    // logo and name.
    const auto& ops = b.rec.ops;
    CHECK(is(ops[3 + 6 + 2], "text", "2", 524, 108, 119, 0));
    CHECK(is(ops[3 + 6 + 3], "text", "1", 634 - kGlyphWidth - 2, 108, 119, 0));
}

// A row nobody holds (a watcher's) moves the players below it up a row for
// the next frame; this frame draws the next row's player in its place.
void closes_gaps() {
    Board b;
    b.board.slide = kBoardWidth;
    b.w.world->player_info[0].options |= OA_SETUP_OPTION_WATCHER;
    b.draw(true);
    CHECK(b.w.player(1).board_row == 0 && b.w.player(2).board_row == 1);
    CHECK(b.w.player(0).board_row == 0);
    std::vector<std::string> logos;
    for (const auto& op : b.rec.ops)
        if (op.kind == "logo")
            logos.push_back(op.text + "@" + std::to_string(op.b));
    CHECK((logos == std::vector<std::string>{"Player 2@48"}));
    b.draw(true);
    logos.clear();
    for (const auto& op : b.rec.ops)
        if (op.kind == "logo")
            logos.push_back(op.text + "@" + std::to_string(op.b));
    CHECK((logos == std::vector<std::string>{"Player 1@48", "Player 2@88"}));
}

// A kill lights the kill and loss counts for 30; the board fades them by two on
// the first draw after each two-tick step.
void flashes_fade() {
    Board b;
    b.board.slide = kBoardWidth;
    flash_kill(b.board, 1, 0);
    flash_kill(b.board, -1, OA_PLAYER_COUNT);
    CHECK(b.board.kill_flash[1] == kBoardFlashTicks && b.board.loss_flash[0] == kBoardFlashTicks);
    CHECK(b.board.kill_flash[0] == 0 && b.board.loss_flash[1] == 0);
    b.draw(true, 5);
    CHECK(b.board.kill_flash[1] == 28 && b.board.loss_flash[0] == 28);
    CHECK(b.board.next_fade_tick == 6);
    // Player 0 is row 0: its losses carry the flash; player 1's kills in row 1.
    const auto& ops = b.rec.ops;
    CHECK(ops[3 + 2 + 2].kind == "text" && ops[3 + 2 + 2].d == 0);
    CHECK(ops[3 + 2 + 3].kind == "text" && ops[3 + 2 + 3].d == 28);
    CHECK(ops[3 + 6 + 2].kind == "text" && ops[3 + 6 + 2].d == 28);
    b.draw(true, 6);
    CHECK(b.board.kill_flash[1] == 28);
    b.draw(true, 7);
    CHECK(b.board.kill_flash[1] == 26 && b.board.next_fade_tick == 8);
}

void wanted_by_f4_or_held_space() {
    hud_test::TestWorld w;
    CHECK(!kill_board_wanted(w.game(), false, false));
    CHECK(kill_board_wanted(w.game(), true, false));
    CHECK(!kill_board_wanted(w.game(), true, true));
    w.game().graphics_flags = kGraphicsBoardPinned;
    CHECK(kill_board_wanted(w.game(), false, true));
}

} // namespace

int main() {
    slides_in_and_out();
    draws_rows_in_board_order();
    deathmatch_counts_commanders();
    closes_gaps();
    flashes_fade();
    wanted_by_f4_or_held_space();
    return 0;
}
