// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "gadget_internal.hpp"

#include <cstdint>

namespace oa::ui::gui_input {
using namespace detail;

namespace {

// Frame advances one elapsed step may take before a malformed sequence
// (non-positive durations) is abandoned.
constexpr std::size_t kCursorAdvanceLimit = 0x10000;

void show_image(GadgetPanel& panel, const formats::gaf::Frame* image) {
    if (panel.host.set_cursor_image != nullptr)
        panel.host.set_cursor_image(panel.host.context, image);
}

const formats::gaf::Frame* shown_image(GadgetPanel& panel) {
    return panel.host.cursor_image != nullptr ? panel.host.cursor_image(panel.host.context)
                                              : nullptr;
}

} // namespace

void set_cursor_sequence(GadgetPanel& panel, const formats::gaf::Sequence* sequence) {
    panel.cursor = sequence;
    sim::sprite_animation::initialize(panel.cursor_frame, sequence, 0);
    panel.cursor_flags |= cursor_flag::animating;
    show_image(panel, sim::sprite_animation::current_frame(panel.cursor_frame));
}

void follow_hover_cursor(GadgetPanel& panel, bool over_panel) {
    const formats::gaf::Sequence* sequence = over_panel ? panel.hover_cursor : panel.cursor;
    if (sequence == nullptr) {
        const formats::gaf::Frame* still =
            over_panel ? panel.hover_cursor_still : panel.cursor_still;
        if (shown_image(panel) == still)
            return;
        show_image(panel, still);
        panel.cursor_flags &= ~cursor_flag::animating;
        panel.cursor_frame.sequence = nullptr;
        return;
    }
    if (panel.cursor_frame.sequence == sequence)
        return;
    sim::sprite_animation::initialize(panel.cursor_frame, sequence, 0);
    show_image(panel, sim::sprite_animation::current_frame(panel.cursor_frame));
    panel.cursor_flags |= cursor_flag::animating;
}

void reset_cursor(GadgetPanel& panel, const formats::gaf::Frame* image) {
    panel.cursor_image = image;
    panel.cursor_still = image;
    panel.hover_cursor_still = image;
    show_image(panel, image);
    panel.cursor_flags &= ~cursor_flag::animating;
    panel.buttons = 0;
    panel.hover_cursor = nullptr;
    panel.cursor = nullptr;
}

void tick_cursor_and_read_pointer(GadgetPanel& panel) {
    if ((panel.cursor_flags & cursor_flag::animating) != 0) {
        const auto before = panel.cursor_frame.frame_index;
        (void)sim::sprite_animation::advance_elapsed_bounded(
            panel.cursor_frame, static_cast<int16_t>(panel.tick_delta), kCursorAdvanceLimit
        );
        if (panel.cursor_frame.frame_index != before)
            show_image(panel, sim::sprite_animation::current_frame(panel.cursor_frame));
    }
    PointerEvent next{};
    const bool queued =
        panel.host.peek_pointer != nullptr && panel.host.peek_pointer(panel.host.context, next);
    if (!queued) {
        if (panel.host.current_pointer != nullptr)
            panel.host.current_pointer(panel.host.context, panel.pointer);
        return;
    }
    if (!panel.owner || panel.owner->table.records.empty())
        return;
    const auto root = ui::gui_layout::screen_rect(panel.owner->records(), 0);
    if (!ui::gui_layout::rect_contains(root, next.x, next.y) && next.buttons != 0)
        return;
    if (panel.host.pop_pointer != nullptr)
        (void)panel.host.pop_pointer(panel.host.context, next);
    panel.buttons = next.buttons;
    panel.pointer = next;
}

} // namespace oa::ui::gui_input
