// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A screen's scroll bars on the RGB surface: SLIDERS art, the track, knob
// and arrows drawn, grayed parts, and a layout's bars and lists bound and
// driven by the pointer.

#include "oa/ui/frontend_renderer/scroll_bars.hpp"

#include <cstdint>
#include <iostream>
#include <source_location>
#include <string_view>
#include <vector>

namespace gui = oa::ui::gui_layout;
namespace input = oa::ui::gui_input;
namespace renderer = oa::ui::frontend_renderer;

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

/// Returns a frame of one colour.
oa::formats::gaf::Frame solid_frame(uint16_t width, uint16_t height, uint8_t index) {
    oa::formats::gaf::Frame frame;
    frame.width = width;
    frame.height = height;
    frame.pixels.assign(static_cast<std::size_t>(width) * height, index);
    frame.coverage.assign(static_cast<std::size_t>(width) * height, 1);
    return frame;
}

/// The colour index frame `frame` of the test art is drawn in.
constexpr uint8_t frame_colour(int32_t frame) {
    return static_cast<uint8_t>(100 + frame);
}

/// Returns a GAF whose SLIDERS sequence is laid out as COMMONGUI.GAF's: a
/// vertical bar's art from frame 0 and a horizontal bar's from frame 10, a
/// 16-pixel track, a knob of 10 by 1, 3 and 1 down and 10 by 10 across, and
/// arrows of 16 by 10 down and 9 by 16 across; each frame in a colour of its own.
oa::formats::gaf::Archive slider_gaf() {
    oa::formats::gaf::Sequence sequence;
    sequence.name = "sliders";
    const uint16_t sizes[20][2] = {
        {16, 16}, {16, 16}, {16, 16}, {10, 1},  {10, 3},  {10, 1},  {16, 10},
        {16, 10}, {16, 10}, {16, 10}, {16, 16}, {16, 16}, {16, 16}, {10, 10},
        {10, 10}, {10, 10}, {9, 16},  {9, 16},  {9, 16},  {9, 16},
    };
    for (int32_t frame = 0; frame < 20; ++frame)
        sequence.frames.push_back(
            solid_frame(sizes[frame][0], sizes[frame][1], frame_colour(frame))
        );
    oa::formats::gaf::Archive archive;
    archive.sequences.push_back(std::move(sequence));
    return archive;
}

/// Returns resources whose GUI palette gives entry i the colour (i, 255 - i, 7).
oa::ui::frontend_renderer::ScreenResources test_screen() {
    renderer::ScreenResources screen;
    for (std::size_t entry = 0; entry < 256; ++entry) {
        screen.gui_palette[entry * 4] = static_cast<uint8_t>(entry);
        screen.gui_palette[entry * 4 + 1] = static_cast<uint8_t>(255 - entry);
        screen.gui_palette[entry * 4 + 2] = 7;
    }
    return screen;
}

/// Returns a blank 640 by 480 surface.
renderer::Surface blank_surface() {
    return {640, 480, std::vector<uint8_t>(640U * 480U * 3U, 0)};
}

/// Returns the palette entry a test palette colour stands for, or -1 for another colour.
int32_t colour_at(const renderer::Surface& surface, int32_t x, int32_t y) {
    const auto at =
        (static_cast<std::size_t>(y) * surface.width + static_cast<std::size_t>(x)) * 3U;
    const auto red = surface.rgb[at];
    if (surface.rgb[at + 1] != 255 - red || surface.rgb[at + 2] != 7)
        return -1;
    return red;
}

gui::Gadget gadget(
    gui::GadgetType type,
    const char* name,
    uint8_t group,
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t height,
    int32_t attributes
) {
    gui::Gadget made;
    made.common.type = type;
    made.common.name = name;
    made.common.association = group;
    made.common.x = x;
    made.common.y = y;
    made.common.width = width;
    made.common.height = height;
    made.common.attributes = attributes;
    made.common.active = 1;
    if (type == gui::GadgetType::scroll_bar)
        made.fields = gui::ScrollBarFields{106, 22, 0, 10, "", ""};
    else if (type == gui::GadgetType::list_box)
        made.fields = gui::ListBoxFields{};
    return made;
}

/// Returns a panel with a horizontal bar, and a list scrolled by a vertical bar.
gui::Layout test_layout() {
    gui::Layout layout;
    layout.gadgets.push_back(gadget(gui::GadgetType::panel, "test.GUI", 0, 0, 0, 640, 480, 0));
    layout.gadgets.push_back(
        gadget(gui::GadgetType::scroll_bar, "VOLUME", 30, 510, 24, 120, 16, 1)
    );
    layout.gadgets.push_back(gadget(gui::GadgetType::list_box, "ROWS", 1, 200, 100, 100, 190, 1));
    layout.gadgets.push_back(
        gadget(gui::GadgetType::scroll_bar, "SLIDER", 1, 300, 100, 16, 190, 2)
    );
    return layout;
}

void test_art() {
    const auto gaf = slider_gaf();
    const auto frames = renderer::scroll_art_frames(gaf);
    expect(
        frames.size() == 20 && frames[3].width == 10 && frames[3].height == 1 &&
            frames[16].width == 9 && frames[16].height == 16,
        "SLIDERS' frame sizes are read in order, its name without regard to case"
    );
    const oa::formats::gaf::Archive empty;
    expect(renderer::scroll_art_frames(empty).empty(), "no SLIDERS, no frames");
    const auto art = renderer::scroll_art(nullptr, gaf);
    expect(art.panel.empty() && art.shared.size() == 20, "a panel without its own GAF shares");
    expect(
        renderer::scroll_art_sequence(nullptr, gaf, input::ScrollArt::shared) ==
                &gaf.sequences.front() &&
            renderer::scroll_art_sequence(&gaf, empty, input::ScrollArt::panel) ==
                &gaf.sequences.front() &&
            renderer::scroll_art_sequence(&gaf, gaf, input::ScrollArt::none) == nullptr,
        "a bar is drawn from the art it was bound with"
    );
    expect(
        renderer::gaf_named_after("guis/SELMAP.GUI", "anims/selmap.gaf") &&
            !renderer::gaf_named_after("guis/selmap.gui", "anims/skirmish.gaf") &&
            !renderer::gaf_named_after("", "anims/selmap.gaf"),
        "a panel's own GAF is named after its GUI file"
    );
}

void test_drawing() {
    const auto gaf = slider_gaf();
    const auto screen = test_screen();
    std::vector<uint8_t> gray;
    const auto paint = renderer::grayed_paint(screen, gray);
    expect(paint.palette == &screen.gui_palette && paint.shade_palette == nullptr, "GUI palette");

    // A horizontal bar: track start, middles and end, the knob 3 pixels past
    // its position and centred down the track.
    auto layout = test_layout();
    auto scrolls = renderer::bind_layout_scrolls(layout, renderer::scroll_art(nullptr, gaf), 14);
    auto* volume = renderer::find_layout_bar(scrolls, 1);
    expect(volume != nullptr && volume->bar.rect.x == 519, "VOLUME is bound between its arrows");
    if (volume == nullptr)
        return;
    volume->bar.knob = 20;
    auto surface = blank_surface();
    renderer::draw_scroll_bar(
        surface, paint, &gaf.sequences.front(), volume->bar, 0, 0, input::ScrollPart::none
    );
    expect(
        colour_at(surface, 519, 24) == frame_colour(10) &&
            colour_at(surface, 540, 24) == frame_colour(11) &&
            colour_at(surface, 620, 39) == frame_colour(12),
        "the track is its start, middle and end frames"
    );
    expect(
        colour_at(surface, 519 + 20 + 3, 27) == frame_colour(13) &&
            colour_at(surface, 519 + 20 + 3 + 9, 36) == frame_colour(13) &&
            colour_at(surface, 519 + 20 + 2, 30) == frame_colour(11),
        "the knob is its start frame, 3 pixels past its position and centred down the bar"
    );
    expect(
        colour_at(surface, 510, 24) == frame_colour(16) &&
            colour_at(surface, 629, 39) == frame_colour(18),
        "the arrows show their frames"
    );
    volume->bar.knob = 200;
    surface = blank_surface();
    renderer::draw_scroll_bar(
        surface, paint, &gaf.sequences.front(), volume->bar, 0, 0, input::ScrollPart::forward_arrow
    );
    expect(
        colour_at(surface, 620 - 10 - 2 + 1, 30) == frame_colour(13) &&
            colour_at(surface, 620 - 1, 30) == frame_colour(12),
        "a knob past the end stops its width and 2 pixels before the bar's last column"
    );
    expect(colour_at(surface, 625, 30) == frame_colour(19), "a held arrow shows its held face");

    // A vertical bar: the knob's start, middle and end frames down the bar,
    // centred on the track, and at least 4 pixels above its end.
    auto* slider = renderer::find_layout_bar(scrolls, 3);
    expect(slider != nullptr && !slider->bar.active, "a list's bar is hidden until it fills");
    if (slider == nullptr)
        return;
    slider->bar.active = true;
    slider->bar.knob_size = 20;
    slider->bar.knob = 5;
    surface = blank_surface();
    renderer::draw_scroll_bar(
        surface, paint, &gaf.sequences.front(), slider->bar, 0, 0, input::ScrollPart::none
    );
    const int32_t top = slider->bar.rect.y + 5 + 3;
    expect(
        colour_at(surface, 303, top) == frame_colour(3) &&
            colour_at(surface, 303, top + 1) == frame_colour(4) &&
            colour_at(surface, 312, top + 19) == frame_colour(5) &&
            colour_at(surface, 302, top + 10) == frame_colour(1) &&
            colour_at(surface, 303, top + 20) == frame_colour(1),
        "a vertical knob runs its size down the bar from 3 pixels past its position"
    );
    slider->bar.knob = 170;
    surface = blank_surface();
    renderer::draw_scroll_bar(
        surface, paint, &gaf.sequences.front(), slider->bar, 0, 0, input::ScrollPart::none
    );
    const int32_t bottom = slider->bar.rect.y + slider->bar.rect.height - 4;
    expect(
        colour_at(surface, 303, bottom) == frame_colour(5) &&
            colour_at(surface, 303, bottom + 1) != frame_colour(5),
        "a vertical knob ends at least 4 pixels above the bar's end"
    );
    slider->bar.active = false;
    surface = blank_surface();
    renderer::draw_scroll_bar(
        surface, paint, &gaf.sequences.front(), slider->bar, 0, 0, input::ScrollPart::none
    );
    expect(colour_at(surface, 303, 150) == -1, "a hidden bar draws nothing");

    // An offset moves the drawing.
    surface = blank_surface();
    volume->bar.knob = 0;
    renderer::draw_scroll_bar(
        surface, paint, &gaf.sequences.front(), volume->bar, -500, 100, input::ScrollPart::none
    );
    expect(colour_at(surface, 19, 124) == frame_colour(10), "the panel origin places the bar");
}

void test_grayed() {
    const auto gaf = slider_gaf();
    auto screen = test_screen();
    // Every entry grays to 50, and the grayed row of the shade table takes
    // 50 to 60 (entries from 0x80 up are shaded from the row before).
    screen.game_palette = screen.gui_palette;
    screen.shade_table.assign(32U * 256U, 0);
    for (std::size_t entry = 0; entry < 256; ++entry)
        screen.shade_table[(32U - 0x14U) * 256U + entry] = entry == 50 ? 60 : 0;
    std::vector<uint8_t> gray(256, 50);
    const auto paint = renderer::grayed_paint(screen, gray);
    expect(paint.shade_palette != nullptr && paint.gray.size() == 256, "the tables are there");
    auto layout = test_layout();
    auto scrolls = renderer::bind_layout_scrolls(layout, renderer::scroll_art(nullptr, gaf), 14);
    auto& bar = renderer::find_layout_bar(scrolls, 1)->bar;
    bar.grayed = true;
    auto surface = blank_surface();
    renderer::draw_scroll_bar(
        surface, paint, &gaf.sequences.front(), bar, 0, 0, input::ScrollPart::forward_arrow
    );
    expect(
        colour_at(surface, 540, 24) == 60 && colour_at(surface, 512, 30) == 60 &&
            colour_at(surface, 625, 30) == 60 && colour_at(surface, 505, 30) == -1,
        "a grayed bar and its arrows are grayed and shaded over their rectangles"
    );
}

void test_layout_scrolls() {
    const auto gaf = slider_gaf();
    auto layout = test_layout();
    layout.gadgets[2].common.active = 1;
    auto scrolls = renderer::bind_layout_scrolls(layout, renderer::scroll_art(nullptr, gaf), 14);
    const auto& volume_gadget = layout.gadgets[1];
    const auto& volume_fields = std::get<gui::ScrollBarFields>(volume_gadget.fields);
    expect(
        volume_gadget.common.x == 519 && volume_gadget.common.width == 102 &&
            volume_fields.range == 88 && volume_fields.knob_size == 10 &&
            volume_fields.knob_position == 0,
        "a bound bar's gadget takes its rectangle, range, knob size and knob"
    );
    expect(
        layout.gadgets[2].common.height == 176 && layout.gadgets[3].common.active == 0 &&
            layout.gadgets[3].common.y == 110 && layout.gadgets[3].common.height == 170,
        "the list is trimmed, and its bar bound and hidden"
    );

    // A hundred rows show the bar with an 18-pixel knob and 149 positions.
    renderer::fill_layout_list(scrolls, layout, 2, 100);
    auto* rows = renderer::find_layout_list(scrolls, 2);
    auto* slider = renderer::find_layout_bar(scrolls, 3);
    expect(
        rows != nullptr && slider != nullptr && slider->bar.active &&
            layout.gadgets[3].common.active == 1 && rows->list.last_first == 89 &&
            slider->bar.range == 149,
        "rows that overflow show the bar and size its knob"
    );
    if (rows == nullptr || slider == nullptr)
        return;

    // Pressing the down arrow steps the knob; the list follows.
    uint32_t tick = 10;
    auto result = renderer::press_layout_scrolls(scrolls, layout, 305, 285, tick);
    expect(result.taken && result.changed == 3 && slider->bar.knob == 1, "the arrow steps");
    expect(
        renderer::release_layout_scrolls(scrolls, layout, 305, 285).changed == -1 &&
            scrolls.hold.bar == input::kNoScrollBar,
        "the arrow's release changes nothing"
    );
    // Dragging the knob to its end shows the last page.
    const auto knob = input::scroll_knob_rect(slider->bar);
    (void)renderer::press_layout_scrolls(scrolls, layout, 305, knob.top + 1, tick);
    expect(scrolls.hold.dragging, "a press on the knob drags it");
    expect(renderer::move_layout_scrolls(scrolls, 305, 400), "the drag follows the pointer");
    expect(
        renderer::tick_layout_scrolls(scrolls, layout, tick) == 3 && slider->bar.knob == 148 &&
            rows->list.first == 89 &&
            std::get<gui::ScrollBarFields>(layout.gadgets[3].fields).knob_position == 148,
        "the knob's last position shows the last page and its gadget keeps the knob"
    );
    (void)renderer::release_layout_scrolls(scrolls, layout, 305, 400);
    // A press beside the knob holds the bar, stepping once a tick.
    (void)renderer::press_layout_scrolls(scrolls, layout, 305, 150, tick);
    expect(scrolls.hold.bar == 3 && !scrolls.hold.dragging, "a press beside the knob holds");
    expect(renderer::tick_layout_scrolls(scrolls, layout, tick) == -1, "no step in its own tick");
    expect(
        renderer::tick_layout_scrolls(scrolls, layout, tick + 1) == 3 && slider->bar.knob == 147,
        "a step toward the pointer the next tick"
    );
    (void)renderer::release_layout_scrolls(scrolls, layout, 600, 600);

    // Selecting a row off the page scrolls to it and moves the knob.
    renderer::select_layout_list_row(scrolls, layout, 2, 10);
    expect(
        rows->list.first == 10 && slider->bar.knob == 149 * 10 / 89 &&
            std::get<gui::ScrollBarFields>(layout.gadgets[3].fields).knob_position == 149 * 10 / 89,
        "a pick off the page scrolls the list and its knob"
    );

    // Down and Up step the selection a row; past the page's last row the
    // list scrolls a row and the knob follows it.
    expect(
        renderer::step_layout_list_row(scrolls, layout, 2, true) && rows->list.selection == 11 &&
            rows->list.first == 10,
        "Down selects the next row on the page"
    );
    renderer::select_layout_list_row(scrolls, layout, 2, 20);
    expect(
        renderer::step_layout_list_row(scrolls, layout, 2, true) && rows->list.selection == 21 &&
            rows->list.first == 11 && slider->bar.knob == 149 * 11 / 89 &&
            std::get<gui::ScrollBarFields>(layout.gadgets[3].fields).knob_position == 149 * 11 / 89,
        "Down past the page's last row scrolls the list and its knob a row"
    );
    expect(
        renderer::step_layout_list_row(scrolls, layout, 2, false) && rows->list.selection == 20 &&
            rows->list.first == 11,
        "Up selects the previous row on the page"
    );
    expect(
        !renderer::step_layout_list_row(scrolls, layout, 1, true), "a bar steps no list selection"
    );

    // A screen's own changes come back: a hidden list's bar takes no press.
    layout.gadgets[3].common.active = 0;
    std::get<gui::ScrollBarFields>(layout.gadgets[1].fields).knob_position = 44;
    std::get<gui::ScrollBarFields>(layout.gadgets[1].fields).locked = true;
    renderer::refresh_layout_scrolls(scrolls, layout);
    auto* volume = renderer::find_layout_bar(scrolls, 1);
    expect(
        !slider->bar.active && volume->bar.knob == 44 && volume->bar.grayed,
        "a refresh takes up the screen's activity, knob and lock"
    );
    result = renderer::press_layout_scrolls(scrolls, layout, 305, 150, tick);
    expect(!result.taken && scrolls.hold.bar == input::kNoScrollBar, "a hidden bar takes no press");
    result = renderer::press_layout_scrolls(scrolls, layout, 600, 30, tick);
    expect(
        result.taken && result.changed == -1 && scrolls.hold.bar == input::kNoScrollBar,
        "a grayed bar takes the press but does nothing"
    );

    // Three rows fit: the bar hides again.
    renderer::fill_layout_list(scrolls, layout, 2, 3);
    expect(!slider->bar.active && layout.gadgets[3].common.active == 0, "rows that fit hide it");
}

} // namespace

int main() {
    test_art();
    test_drawing();
    test_grayed();
    test_layout_scrolls();
    if (failures != 0) {
        std::cerr << failures << " scroll bar drawing check(s) failed\n";
        return 1;
    }
    std::cout << "scroll bars: art, drawing and a layout's bars and lists behave\n";
    return 0;
}
