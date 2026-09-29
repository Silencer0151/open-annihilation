// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Scroll bars and the lists they scroll: binding to SLIDERS art, the knob
// under the pointer and the arrows, and a list kept in step with its bar.

#include "oa/ui/gui_input/scroll_bar.hpp"

#include <cstdint>
#include <iostream>
#include <source_location>
#include <string_view>
#include <vector>

namespace gui = oa::ui::gui_layout;
namespace input = oa::ui::gui_input;

namespace {

int failures = 0;

void expect(
    bool value,
    std::string_view what,
    const std::source_location where = std::source_location::current()
) {
    if (value)
        return;
    ++failures;
    std::cerr << where.file_name() << ':' << where.line() << ": " << what << '\n';
}

/// Returns SLIDERS frame sizes laid out as COMMONGUI.GAF's: a vertical bar's
/// art from frame 0 and a horizontal bar's from frame 10, each a 16-pixel
/// track, a 10-pixel knob and two 9-pixel arrows with held faces.
std::vector<input::ScrollFrame> shared_frames() {
    std::vector<input::ScrollFrame> frames(20);
    for (int base : {0, 10}) {
        const bool across = base == 10;
        for (int frame = 0; frame < 3; ++frame)
            frames[static_cast<std::size_t>(base + frame)] = {16, 16};
        for (int frame = 3; frame < 6; ++frame)
            frames[static_cast<std::size_t>(base + frame)] = {10, 10};
        for (int frame = 6; frame < 10; ++frame)
            frames[static_cast<std::size_t>(base + frame)] =
                across ? input::ScrollFrame{9, 16} : input::ScrollFrame{16, 9};
    }
    return frames;
}

/// Returns a scroll bar gadget of 106 positions, thickness 22 and a 10-pixel knob.
gui::Gadget
bar_gadget(uint8_t group, int16_t x, int16_t y, int16_t width, int16_t height, int32_t attributes) {
    gui::Gadget gadget;
    gadget.common.type = gui::GadgetType::scroll_bar;
    gadget.common.association = group;
    gadget.common.x = x;
    gadget.common.y = y;
    gadget.common.width = width;
    gadget.common.height = height;
    gadget.common.attributes = attributes;
    gadget.common.active = 1;
    gadget.fields = gui::ScrollBarFields{106, 22, 0, 10, "", ""};
    return gadget;
}

/// Returns a list gadget with no item height of its own.
gui::Gadget list_gadget(uint8_t group, int16_t y, int16_t height) {
    gui::Gadget gadget;
    gadget.common.type = gui::GadgetType::list_box;
    gadget.common.association = group;
    gadget.common.y = y;
    gadget.common.height = height;
    gadget.common.attributes = 1;
    gadget.common.active = 1;
    gadget.fields = gui::ListBoxFields{};
    return gadget;
}

/// Returns METAL as LOUNGE2.GUI has it: 120 by 16, moving across.
input::ScrollBar metal_bar() {
    return input::scroll_bar_from_gadget(bar_gadget(30, 510, 24, 120, 16, 1));
}

void test_loading() {
    auto gadget = bar_gadget(30, 510, 24, 120, 16, 1);
    const auto bar = input::scroll_bar_from_gadget(gadget);
    expect(
        bar.maximum == 22 && bar.range == 106 && bar.knob_size == 10 && bar.group == 30 &&
            bar.active && !bar.grayed && bar.art == input::ScrollArt::unbound,
        "a bar's maximum is its GUI thickness, and it loads unbound"
    );
    std::get<gui::ScrollBarFields>(gadget.fields).locked = true;
    expect(input::scroll_bar_from_gadget(gadget).grayed, "a locked bar loads grayed");
    expect(
        input::scroll_bar_part(bar, 520, 30) == input::ScrollPart::none,
        "an unbound bar takes no pointer"
    );
}

void test_binding() {
    input::ScrollArtFrames art;
    art.shared = shared_frames();

    auto metal = metal_bar();
    metal.knob = 40;
    input::bind_scroll_bar(metal, art);
    expect(
        metal.rect.x == 519 && metal.rect.y == 24 && metal.rect.width == 102 &&
            metal.rect.height == 16,
        "a horizontal bar lies between its arrows"
    );
    expect(
        metal.range == 88 && metal.knob_size == 10 && metal.art == input::ScrollArt::shared &&
            metal.art_base == 10 && metal.knob == 0,
        "a horizontal bar takes the shared art from frame 10, 88 positions and the knob at 0"
    );
    expect(
        metal.back_arrow.x == 510 && metal.back_arrow.y == 24 && metal.back_arrow.width == 9 &&
            metal.back_arrow.height == 16,
        "the back arrow sits at the bar's start"
    );
    expect(
        metal.forward_arrow.x == 621 && metal.forward_arrow.y == 24 &&
            metal.forward_arrow.width == 9 && metal.forward_arrow.height == 16,
        "the forward arrow sits at the bar's end"
    );
    expect(
        input::scroll_bar_part(metal, 512, 30) == input::ScrollPart::back_arrow &&
            input::scroll_bar_part(metal, 629, 39) == input::ScrollPart::forward_arrow &&
            input::scroll_bar_part(metal, 519, 24) == input::ScrollPart::bar &&
            input::scroll_bar_part(metal, 621, 40) == input::ScrollPart::bar &&
            input::scroll_bar_part(metal, 630, 30) == input::ScrollPart::none,
        "the arrows take the pointer over their frames, and the bar to its far edges"
    );
    metal.active = false;
    expect(
        input::scroll_bar_part(metal, 512, 30) == input::ScrollPart::none,
        "a hidden bar's arrows are hidden"
    );

    auto updown = input::scroll_bar_from_gadget(bar_gadget(7, 100, 100, 20, 120, 2));
    input::bind_scroll_bar(updown, art);
    expect(
        updown.rect.x == 100 && updown.rect.y == 109 && updown.rect.width == 16 &&
            updown.rect.height == 102 && updown.range == 106 && updown.art_base == 0,
        "a vertical bar takes the art from frame 0, its width and the room between its arrows"
    );
    expect(
        updown.back_arrow.y == 100 && updown.forward_arrow.x == 100 &&
            updown.forward_arrow.y == 211 && updown.forward_arrow.height == 9,
        "a vertical bar's arrows sit at its top and bottom"
    );

    auto own = metal_bar();
    input::ScrollArtFrames panel_art;
    panel_art.panel = shared_frames();
    input::bind_scroll_bar(own, panel_art);
    expect(
        own.art == input::ScrollArt::panel && own.art_base == 0 && own.rect.x == 526 &&
            own.rect.width == 88 && own.range == 74,
        "a panel's own art is taken from its first frame"
    );

    auto bare = metal_bar();
    input::bind_scroll_bar(bare, {});
    expect(
        bare.art == input::ScrollArt::none && bare.range == 114 && bare.rect.x == 510 &&
            bare.back_arrow.width == 0 && bare.forward_arrow.width == 0,
        "without art a bar has no arrows and its longer side less 6 positions"
    );
}

void test_values() {
    auto metal = metal_bar();
    input::ScrollArtFrames art;
    art.shared = shared_frames();
    input::bind_scroll_bar(metal, art);
    metal.maximum = 0x2711;
    // 2549 puts the knob at position 23 of 87, which stands for 2643.
    input::scroll_set_value(metal, 2549);
    expect(metal.knob == 23 && input::scroll_value(metal) == 2643, "a value rounds its knob up");
    input::scroll_set_value(metal, 50000);
    expect(metal.knob == 87, "a value past the maximum takes the last position");
    input::scroll_set_value(metal, 0);
    expect(metal.knob == 0 && input::scroll_value(metal) == 0, "0 takes the first position");
    input::scroll_set_value(metal, -500);
    expect(metal.knob == -3, "a negative value puts the knob before the start");
    metal.range = 1;
    input::scroll_set_value(metal, 500);
    expect(metal.knob == 0 && input::scroll_value(metal) == 0, "one position holds 0");
}

void test_pointer() {
    input::ScrollArtFrames art;
    art.shared = shared_frames();
    auto metal = metal_bar();
    input::bind_scroll_bar(metal, art);
    constexpr int32_t id = 4;
    metal.knob = 9;
    input::ScrollHold hold;
    uint32_t tick = 500;

    // A press on the knob drags it pixel for pixel, once an update, however
    // often the pointer moves in between; the release lands on it.
    const int32_t knob_x = metal.rect.x + metal.knob + 5;
    expect(!input::scroll_press(metal, hold, id, knob_x, 32, tick), "a press moves nothing");
    expect(hold.bar == id && hold.dragging, "a press on the knob drags it");
    for (const int32_t moved : {5, 12, 20})
        input::scroll_move(hold, knob_x + moved, 5);
    expect(metal.knob == 9, "the pointer's moves alone leave the knob");
    expect(
        input::scroll_hold_tick(metal, hold, tick) && metal.knob == 29,
        "the next update moves the knob to the pointer, once"
    );
    expect(
        !input::scroll_hold_tick(metal, hold, tick) &&
            !input::scroll_hold_tick(metal, hold, tick + 1) && metal.knob == 29,
        "a dragged knob does not creep"
    );
    expect(
        !input::scroll_release(metal, hold, knob_x + 20, 5) && metal.knob == 29,
        "released on the knob, it stays"
    );
    expect(hold.bar == input::kNoScrollBar, "the release ends the hold");
    (void)input::scroll_press(metal, hold, id, metal.rect.x + metal.knob + 3, 32, tick);
    input::scroll_move(hold, 700, 32);
    expect(
        input::scroll_hold_tick(metal, hold, tick) && metal.knob == 87,
        "a drag stops at the last position"
    );
    input::scroll_move(hold, 0, 32);
    expect(input::scroll_hold_tick(metal, hold, tick) && metal.knob == 0, "and at the first");
    expect(
        !input::scroll_release(metal, hold, 0, 32) && metal.knob == 0,
        "released before the knob at the start, it stays"
    );
    // A drag moved and released between two updates ends where it was last
    // updated, and the release steps it once toward the pointer.
    (void)input::scroll_press(metal, hold, id, metal.rect.x + metal.knob + 3, 32, tick);
    input::scroll_move(hold, metal.rect.x + 30, 32);
    expect(
        input::scroll_release(metal, hold, metal.rect.x + 30, 32) && metal.knob == 1,
        "the release does not drag"
    );

    // A press on the bar beside the knob holds it: one position toward the
    // pointer each tick, and one more on release.
    metal.knob = 40;
    expect(
        !input::scroll_press(metal, hold, id, 610, 30, tick) && !hold.dragging && hold.bar == id,
        "a press beside the knob holds the bar and does not drag"
    );
    expect(
        !input::scroll_hold_tick(metal, hold, tick) && metal.knob == 40,
        "no step in the tick of the press"
    );
    for (uint32_t step = 1; step <= 5; ++step) {
        expect(input::scroll_hold_tick(metal, hold, tick + step), "a held bar steps each tick");
        expect(!input::scroll_hold_tick(metal, hold, tick + step), "and once a tick");
    }
    expect(metal.knob == 45, "five ticks, five steps toward the pointer");
    expect(
        input::scroll_release(metal, hold, 610, 30) && metal.knob == 46,
        "the release steps once more"
    );
    (void)input::scroll_press(metal, hold, id, 520, 30, tick);
    expect(
        input::scroll_release(metal, hold, 520, 30) && metal.knob == 45,
        "a quick click before the knob steps back once, however far the pointer is"
    );

    // An arrow steps at once, then after 15 ticks once a tick; its handler
    // runs even at the end.
    tick = 700;
    expect(
        input::scroll_press(metal, hold, id, 625, 30, tick) && metal.knob == 46 &&
            hold.part == input::ScrollPart::forward_arrow,
        "a forward arrow steps at once"
    );
    int32_t steps = 0;
    for (uint32_t step = 0; step < 15; ++step)
        if (input::scroll_hold_tick(metal, hold, tick + step))
            ++steps;
    expect(steps == 0 && metal.knob == 46, "a held arrow waits 15 ticks");
    expect(input::scroll_hold_tick(metal, hold, tick + 15) && metal.knob == 47, "then repeats");
    expect(!input::scroll_hold_tick(metal, hold, tick + 15), "once a tick");
    expect(input::scroll_hold_tick(metal, hold, tick + 16) && metal.knob == 48, "every tick");
    expect(
        !input::scroll_release(metal, hold, 625, 30) && hold.bar == input::kNoScrollBar &&
            metal.knob == 48,
        "an arrow's release does nothing"
    );
    metal.knob = 0;
    expect(
        input::scroll_press(metal, hold, id, 512, 30, tick) && metal.knob == 0,
        "a back arrow at the start still runs the handler"
    );
    input::scroll_let_go(hold);
    metal.knob = 87;
    expect(
        input::scroll_press(metal, hold, id, 625, 30, tick) && metal.knob == 87,
        "a forward arrow at the end still runs the handler"
    );

    // A bar grayed while held lets go.
    metal.grayed = true;
    expect(
        !input::scroll_hold_tick(metal, hold, tick + 40) && hold.bar == input::kNoScrollBar,
        "a grayed bar ends the hold"
    );
    expect(
        !input::scroll_press(metal, hold, id, 625, 30, tick) && metal.knob == 87 &&
            hold.bar == input::kNoScrollBar,
        "a grayed bar's arrow ignores a press"
    );
    metal.grayed = false;
    metal.active = false;
    expect(
        !input::scroll_press(metal, hold, id, 560, 30, tick) && hold.bar == input::kNoScrollBar,
        "a hidden bar is not held"
    );
    metal.active = true;
    expect(
        !input::scroll_press(metal, hold, id, 300, 30, tick) && hold.bar == input::kNoScrollBar,
        "a press off the bar is not held"
    );

    // A vertical bar moves its knob down.
    auto updown = input::scroll_bar_from_gadget(bar_gadget(7, 100, 100, 20, 120, 2));
    input::bind_scroll_bar(updown, art);
    updown.knob = 10;
    const auto knob = input::scroll_knob_rect(updown);
    expect(
        knob.left == 101 && knob.right == 115 && knob.top == 121 && knob.bottom == 131,
        "a vertical knob is grabbed two pixels past its position"
    );
    (void)input::scroll_press(updown, hold, 2, 105, 125, tick);
    input::scroll_move(hold, 300, 135);
    expect(
        input::scroll_hold_tick(updown, hold, tick) && updown.knob == 20,
        "a vertical knob follows the pointer down"
    );
}

void test_lists() {
    input::ScrollArtFrames art;
    art.shared = shared_frames();
    constexpr int32_t line_height = 14;
    auto rows = input::scroll_list_from_gadget(list_gadget(1, 100, 190));
    auto names = input::scroll_list_from_gadget(list_gadget(1, 100, 190));
    auto chat = input::scroll_list_from_gadget(list_gadget(2, 300, 96));
    auto bar = input::scroll_bar_from_gadget(bar_gadget(1, 300, 100, 16, 190, 2));
    rows.first = 3;
    rows.selection = 4;
    for (auto* list : {&rows, &names, &chat})
        input::scroll_list_trim(*list, line_height);
    input::bind_scroll_bar(bar, art);
    expect(
        rows.height == 176 && chat.height == 96 && rows.first == 0 && rows.selection == 0,
        "the first draw trims each list to whole 16-pixel rows and shows its first row"
    );
    expect(bar.rect.y == 109 && bar.rect.height == 172, "the bar lies between its arrows");

    // Eleven 15-pixel rows fit in 176 pixels; twelve do not.
    expect(
        !input::scroll_list_fill(rows, 11, line_height) && rows.item_height == 15 &&
            rows.last_first == 0,
        "rows that fit do not call for the bar"
    );
    bar.knob = 5;
    expect(input::scroll_list_fill(rows, 12, line_height), "rows that overflow call for the bar");
    (void)input::scroll_list_fill(names, 12, line_height);
    input::scroll_bar_fit_list(bar, rows, line_height);
    expect(
        rows.last_first == 1 && bar.knob_size == 154 && bar.range == 15,
        "rows that overflow size a knob of 11/12 of 169 pixels and 15 positions"
    );
    expect(bar.knob == 5, "filling a list leaves the knob where it was");
    bar.knob = 1;
    expect(
        input::scroll_list_follow_bar(rows, bar) && rows.first == 0,
        "one position of 14 does not scroll a row"
    );
    bar.knob = 14;
    (void)input::scroll_list_follow_bar(rows, bar);
    expect(
        rows.first == 1 && rows.selection == 0,
        "the last position shows the last page and keeps the selection"
    );

    // A hundred rows: 11 on a page, the last page from row 89, a knob of
    // 11/100 of 169 pixels and 151 positions.
    (void)input::scroll_list_fill(rows, 100, line_height);
    (void)input::scroll_list_fill(names, 100, line_height);
    input::scroll_bar_fit_list(bar, rows, line_height);
    expect(
        rows.last_first == 89 && bar.knob_size == 18 && bar.range == 151,
        "a hundred rows size the knob"
    );
    bar.knob = 40;
    (void)input::scroll_list_follow_bar(rows, bar);
    expect(rows.first == 23, "the knob shows 89 * 40 / 150 rows on");
    input::scroll_list_select(rows, &bar, line_height, 30);
    expect(
        rows.selection == 30 && rows.first == 23 && bar.knob == 40,
        "a pick on the page does not scroll"
    );
    input::scroll_list_select(rows, &bar, line_height, 60);
    expect(
        rows.selection == 60 && rows.first == 60 && bar.knob == 101,
        "a pick off the page scrolls to it and moves the knob to 151 * 60 / 89"
    );
    input::scroll_list_select(rows, &bar, line_height, 95);
    expect(
        rows.first == 89 && bar.knob == 151,
        "a pick on the last page stops at it, the knob one past its last position"
    );

    // Up and Down move a row and scroll at the page's edges.
    rows.first = 40;
    rows.selection = 50;
    expect(
        input::scroll_list_step(rows, &bar, line_height, true) && rows.selection == 51 &&
            rows.first == 41,
        "Down past the page's last row scrolls a row"
    );
    input::scroll_bar_follow_list(bar, rows);
    expect(bar.knob == 69, "the knob follows the list to 41 * 151 / 89");
    expect(
        input::scroll_list_step(rows, &bar, line_height, false) && rows.selection == 50 &&
            rows.first == 41,
        "Up on the page moves the selection"
    );
    rows.selection = 41;
    (void)input::scroll_list_step(rows, &bar, line_height, false);
    expect(rows.selection == 40 && rows.first == 40, "Up past the top scrolls back");
    rows.first = 89;
    rows.selection = 99;
    expect(
        !input::scroll_list_step(rows, &bar, line_height, true) && rows.selection == 99 &&
            rows.first == 89,
        "Down stops at the last row"
    );
    rows.first = 0;
    rows.selection = 0;
    expect(
        !input::scroll_list_step(rows, &bar, line_height, false) && rows.selection == 0,
        "Up stops at the first row"
    );
    rows.selection = 60;
    expect(
        !input::scroll_list_step(rows, &bar, line_height, true) && rows.selection == 60 &&
            rows.first == 60,
        "a selection off the page is brought into view first"
    );

    // A press picks the row under the pointer.
    int32_t row = -1;
    expect(
        input::scroll_list_press(rows, line_height, 148, row) == input::ScrollListPress::changed &&
            row == 63,
        "a press picks its row"
    );
    rows.selection = 63;
    expect(
        input::scroll_list_press(rows, line_height, 150, row) == input::ScrollListPress::same,
        "again, the same row"
    );
    expect(
        input::scroll_list_press(rows, line_height, 272, row) == input::ScrollListPress::changed &&
            row == 70,
        "a press on the page's last pixels picks its last row"
    );
    row = -1;
    expect(
        input::scroll_list_press(rows, line_height, 273, row) == input::ScrollListPress::missed &&
            input::scroll_list_press(rows, line_height, 101, row) ==
                input::ScrollListPress::missed &&
            row == -1,
        "a press on the list's edge misses"
    );
    expect(!input::scroll_list_fill(rows, 3, line_height), "three rows call for no bar");
    expect(
        input::scroll_list_press(rows, line_height, 190, row) == input::ScrollListPress::changed &&
            row == 2,
        "a press below the last row picks the last row"
    );
    expect(
        input::scroll_list_press(chat, line_height, 303, row) == input::ScrollListPress::missed,
        "a press on an empty list misses"
    );

    // The wheel's scroll stays within the pages, even for an empty list.
    (void)input::scroll_list_fill(rows, 100, line_height);
    rows.first = 88;
    expect(input::scroll_list_scroll(rows, 1) && rows.first == 89, "the wheel scrolls a row");
    expect(!input::scroll_list_scroll(rows, 1) && rows.first == 89, "not past the last page");
    rows.first = 0;
    expect(!input::scroll_list_scroll(rows, -1) && rows.first == 0, "nor before the first");
    auto empty = input::scroll_list_from_gadget(list_gadget(3, 100, 190));
    expect(
        !input::scroll_list_fill(empty, 0, line_height) && empty.last_first == -1 &&
            !input::scroll_list_scroll(empty, 1) && !input::scroll_list_scroll(empty, -1) &&
            empty.first == 0,
        "an empty list calls for no bar and does not scroll"
    );
    expect(
        !input::scroll_list_scroll(chat, 1) && chat.first == 0,
        "the wheel leaves a list with no pitch alone"
    );
    expect(!input::scroll_list_follow_bar(chat, bar), "a list with no pitch does not follow");
}

} // namespace

int main() {
    test_loading();
    test_binding();
    test_values();
    test_pointer();
    test_lists();
    if (failures != 0) {
        std::cerr << failures << " scroll bar check(s) failed\n";
        return 1;
    }
    std::cout << "scroll bars: binding, knob, arrows and lists behave\n";
    return 0;
}
