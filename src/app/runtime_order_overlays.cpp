// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The order overlays the battlefield draws while Shift is held: the
// order-overlay module over the match's order queues, drawn into the
// battlefield frame, and the ShowRanges check.
#include "oa/app/runtime.hpp"
#include "match_models.hpp"

#include "oa/formats/gaf.hpp"
#include "oa/sim/match_runtime.hpp"
#include "oa/ui/console/game_fields.hpp"
#include "oa/ui/hud/game_fields.hpp"
#include "oa/ui/hud/order_overlays.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace oa::app {

namespace {

namespace hud = oa::ui::hud;

// "pathicon", the last of the cursor sequences (kCursorNames), which the
// runtime keeps in place of their entries in Game.sprite_and_effect_tables.
constexpr uint8_t kPathIconCursor = 21;
constexpr std::size_t kQueueRecordsFirstCapacity = 32;

// One unit's primary queue as the module walks it, with the unit it came
// from so that a target seen through it is written back.
struct OverlayQueue {
    uint16_t unit{};
    std::vector<hud::OrderOverlay> nodes;
};

struct OverlayLabel {
    std::string text;
    int x{};
    int y{};
};

} // namespace

Runtime::OrderOverlayPass Runtime::draw_order_overlays(
    oa::present::world_renderer::Surface& destination,
    const oa::present::world_renderer::BattlefieldViewport& viewport
) {
    OrderOverlayPass pass;
    if (!match_)
        return pass;
    // The markers are cursor sequences, which a headless run has not loaded
    // for the pointer; the runtime keeps them in place of their entries in
    // Game.sprite_and_effect_tables.
    if (!cursors_loaded_)
        load_game_cursors();
    oa::World& world = match_->state();
    const float scale = viewport.scale == 0.0F ? 1.0F : viewport.scale;

    struct Frame {
        Runtime* runtime{};
        oa::present::world_renderer::Surface* destination{};
        const oa::present::world_renderer::BattlefieldViewport* viewport{};
        float scale{};
        OrderOverlayPass* pass{};
        std::deque<OverlayQueue> queues;
        std::vector<OverlayLabel> labels;
        std::array<hud::SpriteSequence, Runtime::kCursorNames.size()> sequences{};

        [[nodiscard]] oa::present::world_renderer::ScreenPoint at(int32_t x, int32_t y) const {
            return {
                viewport->destination_x +
                    static_cast<int32_t>(std::lround((x - hud::kBattlefieldLeft) * scale)),
                viewport->destination_y +
                    static_cast<int32_t>(std::lround((y - hud::kBattlefieldTop) * scale))
            };
        }
    } frame{this, &destination, &viewport, scale, &pass, {}, {}, {}};

    for (std::size_t index = 0; index < frame.sequences.size(); ++index)
        if (const auto* sequence = cursor_sequence(static_cast<uint8_t>(index));
            sequence != nullptr && !sequence->frames.empty())
            frame.sequences[index] = {
                static_cast<uint16_t>(sequence->frames.size()), sequence->frames.front().duration
            };

    hud::OverlayContext context{};
    context.world = &world;
    context.view.focus_unit =
        world.game.follow_unit != 0
            ? oa::world_unit_at(&world, oa::oa_unit_slot_from_ref(world.game.follow_unit))
            : nullptr;
    context.view.camera_x = static_cast<int32_t>(viewport.source_x);
    context.view.camera_y = static_cast<int32_t>(viewport.source_y);
    context.missions = hud::kMissionOverlays;
    context.path_pips = frame.sequences[kPathIconCursor];
    context.indicators = frame.sequences.data();
    context.show_ranges = hud::show_ranges(world.game);
    context.sink.user = &frame;
    context.sink.line =
        [](void* user, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color) {
            auto& self = *static_cast<Frame*>(user);
            const auto from = self.at(x0, y0);
            const auto to = self.at(x1, y1);
            self.runtime->draw_match_line(
                *self.destination, from.x, from.y, to.x, to.y, self.runtime->palette_rgb(color)
            );
            ++self.pass->lines;
        };
    context.sink.label = [](void* user, const char* text, int32_t x, int32_t y) {
        auto& self = *static_cast<Frame*>(user);
        const auto at = self.at(x, y);
        self.labels.push_back({text, at.x, at.y});
        self.pass->labels.emplace_back(text);
    };
    context.sink.sprite = [](void* user,
                             hud::OverlaySprite sprite,
                             uint8_t index,
                             uint16_t frame_index,
                             int32_t x,
                             int32_t y) {
        auto& self = *static_cast<Frame*>(user);
        const auto cursor = sprite == hud::OverlaySprite::path_pip ? kPathIconCursor : index;
        const auto* sequence = self.runtime->cursor_sequence(cursor);
        if (sequence == nullptr || frame_index >= sequence->frames.size())
            return;
        const auto* source = &sequence->frames[frame_index];
        auto found = self.runtime->overlay_sprite_frames_.find(source);
        if (found == self.runtime->overlay_sprite_frames_.end()) {
            auto rendered = oa::formats::gaf::render_normal(*source);
            if (!rendered.ok())
                return;
            found = self.runtime->overlay_sprite_frames_.emplace(source, std::move(*rendered.frame))
                        .first;
        }
        self.runtime->blit_gaf_hotspot(
            *self.destination,
            found->second,
            self.at(x, y),
            self.runtime->match_palette_,
            self.scale
        );
        ++self.pass->sprites;
    };
    context.sink.ground_height = [](void* user, const FixedVec3& point) {
        auto& match = *static_cast<Frame*>(user)->runtime->match_;
        return match.map_height(static_cast<uint32_t>(point.x), static_cast<uint32_t>(point.z));
    };
    // The paths start where the frame shows each unit (a frame between ticks).
    context.sink.place = [](void* user, const oa::Unit& unit) -> FixedVec3 {
        auto& runtime = *static_cast<Frame*>(user)->runtime;
        return shown_unit_position(runtime.match_models(), runtime.match_->state(), unit.id);
    };
    context.sink.can_see = [](void* user, const oa::Player* viewer, const oa::Unit& unit) {
        auto& match = *static_cast<Frame*>(user)->runtime->match_;
        if (viewer == nullptr)
            return false;
        try {
            return match.unit_visible(viewer->index, unit.id);
        } catch (const std::exception&) {
            return false;
        }
    };
    // Only the primary queue is drawn; the secondary one feeds the stockpile
    // readout alone. OrderOverlay::progress is not carried: only that readout
    // reads it.
    context.sink.orders =
        [](void* user, const oa::Unit& unit, bool secondary) -> hud::OrderOverlay* {
        auto& self = *static_cast<Frame*>(user);
        if (secondary)
            return nullptr;
        auto& match = *self.runtime->match_;
        oa::World& world = match.state();
        using View = oa::sim::match_runtime::Match::OrderRecordView;
        std::vector<View> records(kQueueRecordsFirstCapacity);
        std::size_t count = 0;
        for (;;) {
            count = match.queue_records(unit.id, false, records.data(), records.size());
            if (count < records.size())
                break;
            records.resize(records.size() * 2);
        }
        if (count == 0)
            return nullptr;
        auto& queue = self.queues.emplace_back();
        queue.unit = unit.id;
        queue.nodes.resize(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& record = records[index];
            auto& node = queue.nodes[index];
            node.mission = record.kind;
            node.unit = oa::world_unit_at(&world, unit.id);
            node.target = record.target != 0 ? oa::world_unit_at(&world, record.target) : nullptr;
            node.position = {record.point[0], record.point[1], record.point[2]};
            node.seen_x = record.seen_x;
            node.seen_z = record.seen_z;
            node.parameter = static_cast<uint32_t>(record.parameter_1);
            node.flags = static_cast<uint32_t>(record.preserve_flags) |
                         static_cast<uint32_t>(record.command_flags) << 8 |
                         static_cast<uint32_t>(record.flags) << 16;
            node.state = record.flags;
            node.issue_tick = record.issue_tick;
            node.next = index + 1 < count ? &queue.nodes[index + 1] : nullptr;
        }
        return queue.nodes.data();
    };

    hud::draw_selection_overlays(context);

    for (const auto& queue : frame.queues)
        for (std::size_t index = 0; index < queue.nodes.size(); ++index)
            if (const auto& node = queue.nodes[index]; (node.flags & hud::kOrderTargetSeen) != 0)
                match_->note_order_target_seen(queue.unit, index, node.seen_x, node.seen_z);
    if (!frame.labels.empty()) {
        ensure_ui_colors();
        renderer::Surface text{destination.width, destination.height, std::move(destination.rgb)};
        auto* const kept_target = overlay_target_;
        overlay_target_ = &text;
        for (const auto& label : frame.labels)
            draw_match_label(label.x, label.y, label.text, ui_colors_[kUiColorText]);
        overlay_target_ = kept_target;
        destination.rgb = std::move(text.rgb);
    }
    return pass;
}

void Runtime::check_console_range_overlays(const std::function<void(const char*)>& enter_line) {
    const auto require = [](bool ok, const std::string& what) {
        if (!ok)
            throw std::runtime_error("console range overlay check: " + what);
    };
    oa::World& world = match_->state();
    uint16_t commander = 0;
    for (const auto& slot : match_->world().slots)
        if (commander == 0 && slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == match_local_player_)
            commander = slot.unit_index;
    require(commander != 0, "no local commander");
    const auto& unit = *match_->world().slots[commander].unit;
    const auto kept_selection = selected_match_unit_;
    clear_local_selection();
    adopt_selection(commander);
    const std::array<uint32_t, 3> start = unit.position;
    const oa::sim::ground_orders::Point destination{
        std::bit_cast<int32_t>(start[0]) + (96 << 16),
        std::bit_cast<int32_t>(start[1]),
        std::bit_cast<int32_t>(start[2])
    };
    match_->issue_ground_move(commander, destination, false);
    const auto pass = [&] {
        render_match_surface();
        oa::present::world_renderer::Surface surface{
            match_world_cpu_.width, match_world_cpu_.height, match_world_cpu_.rgb
        };
        auto viewport = live_viewport(
            static_cast<uint32_t>(match_camera_x_), static_cast<uint32_t>(match_camera_z_)
        );
        viewport.destination_x = 0;
        viewport.destination_y = 0;
        viewport.surface_width = surface.width;
        viewport.surface_height = surface.height;
        return draw_order_overlays(surface, viewport);
    };
    const auto has = [](const OrderOverlayPass& drawn, const char* label) {
        return std::find(drawn.labels.begin(), drawn.labels.end(), label) != drawn.labels.end();
    };
    const bool ranges_before = hud::show_ranges(world.game);
    if (ranges_before)
        enter_line("+showranges");
    const auto plain = pass();
    require(plain.labels.empty(), "ranges were labelled with ShowRanges off");
    require(plain.sprites != 0, "the Move_Ground order drew no target marker");
    enter_line("+showranges");
    require(hud::show_ranges(world.game), "+showranges did not turn the range display on");
    const auto ranged = pass();
    require(
        has(ranged, "sight") && has(ranged, "build distance") && ranged.lines > plain.lines,
        "ShowRanges did not circle the commander's sight and build distance"
    );
    enter_line("+showranges");
    require(
        !hud::show_ranges(world.game) && pass().labels.empty(), "+showranges did not turn it off"
    );
    if (ranges_before)
        enter_line("+showranges");
    match_->stop_orders(commander);
    clear_local_selection();
    adopt_selection(kept_selection);
    apply_match_hud_for_selection();
    render_match_surface();
    std::cout << "console range overlay check: with Shift the commander's Move_Ground order draws "
                 "its target marker, and +showranges adds its labelled sight and build distance "
                 "circles through the order overlays\n";
}

} // namespace oa::app
