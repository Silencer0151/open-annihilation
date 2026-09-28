// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/spatial_state/spatial.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
namespace spatial = oa::sim::spatial_state;

void check(bool value, std::string_view message) {
    if (!value)
        throw std::runtime_error(std::string(message));
}

struct Host final : spatial::Host {
    int masked{}, changed{}, object_removed{};
    std::array<int16_t, 2> min{}, max{};

    void refresh_plot_height_range(std::array<int16_t, 2> a, std::array<int16_t, 2> b) override {
        ++masked;
        min = a;
        max = b;
    }

    void notify_footprint_changed(std::array<int16_t, 2>, std::array<int16_t, 2>) override {
        ++changed;
    }

    void notify_object_footprint_removed(spatial::Unit&, uint32_t) override { ++object_removed; }
};

spatial::World world(std::vector<spatial::Unit>& units) {
    spatial::World w;
    w.terrain_width = 8;
    w.terrain_height = 8;
    w.plots.resize(64);
    w.bucket_width = 2;
    w.bucket_height = 2;
    w.buckets.resize(4);
    w.units = units;
    w.tick = 77;
    return w;
}

// Rectangle overlap, the overlapping-unit bucket walk and the restored-yard refresh.
void overlap_walk() {
    check(spatial::rectangles_overlap({2, 3}, {2, 2}, {3, 4}, {1, 1}), "rectangles_overlap inside");
    check(
        !spatial::rectangles_overlap({2, 3}, {2, 2}, {4, 3}, {1, 1}),
        "rectangles_overlap touching x edge"
    );
    check(
        !spatial::rectangles_overlap({2, 3}, {2, 2}, {2, 5}, {3, 3}),
        "rectangles_overlap touching z edge"
    );
    check(spatial::rectangles_overlap({2, 3}, {2, 2}, {0, 0}, {3, 4}), "rectangles_overlap corner");

    std::vector<spatial::Unit> units(5);
    for (std::size_t i = 1; i < units.size(); ++i)
        units[i].id = static_cast<spatial::UnitId>(i);
    auto w = world(units);
    // Bucket 0 chains units 1 and 2; unit 3 rides on unit 2; unit 4 is in no bucket.
    units[1].cell = {1, 1};
    units[1].footprint = {2, 2};
    units[1].next_in_bucket = 2;
    units[2].cell = {5, 5};
    units[2].footprint = {1, 1};
    units[2].first_attachment = 3;
    units[3].cell = {2, 2};
    units[3].footprint = {1, 1};
    units[4].cell = {2, 2};
    units[4].footprint = {1, 1};
    w.buckets[0].head = 1;

    struct Visits {
        std::vector<spatial::UnitId> ids;
        spatial::UnitId fail_on{};
    } visits;

    const auto record = [](spatial::Unit& unit, spatial::World&, void* context) {
        auto& seen = *static_cast<Visits*>(context);
        seen.ids.push_back(unit.id);
        return unit.id == seen.fail_on ? spatial::Error::invalid_unit_id : spatial::Error::none;
    };
    check(
        spatial::visit_overlapping_units({2, 2}, {1, 1}, w, record, &visits) ==
                spatial::Error::none &&
            visits.ids == std::vector<spatial::UnitId>{1, 3},
        "visit_overlapping_units visits the overlapping unit and the carried one"
    );
    visits.ids.clear();
    visits.fail_on = 1;
    check(
        spatial::visit_overlapping_units({2, 2}, {1, 1}, w, record, &visits) ==
                spatial::Error::invalid_unit_id &&
            visits.ids == std::vector<spatial::UnitId>{1},
        "visit_overlapping_units stops on a visit error"
    );
    // Buckets one past the rectangle's either side are walked: cell 7 of
    // bucket 0 reaches bucket 1 and back.
    visits.ids.clear();
    visits.fail_on = 0;
    check(
        spatial::visit_overlapping_units({8, 8}, {0, 0}, w, record, &visits) ==
                spatial::Error::none &&
            visits.ids.empty(),
        "visit_overlapping_units checks overlap before visiting"
    );

    // A yard registered closed, then restored open: the open cells stay
    // occupied and the closed-only cell is let go.
    std::vector<spatial::Unit> yard_units(2);
    yard_units[1].id = 1;
    auto yw = world(yard_units);
    Host host;
    const std::array<uint8_t, 2> mask{6, 4};
    auto& yard = yard_units[1];
    yard.cell = {2, 2};
    yard.footprint = {2, 1};
    yard.flags = spatial::masked_footprint_flag | 1;
    yard.yard_mask = mask;
    check(spatial::register_unit(yard, yw, host) == spatial::Error::none, "yard registration");
    check(
        yw.plots[2 * 8 + 2].ground == 1 && yw.plots[2 * 8 + 3].ground == 1, "closed yard occupancy"
    );
    yard.yard_open = true;
    const auto changes = host.changed;
    check(
        spatial::refresh_footprint_occupancy(yard, yw, host) == spatial::Error::none,
        "refresh_footprint_occupancy refresh"
    );
    check(
        yw.plots[2 * 8 + 2].ground == 1 && yw.plots[2 * 8 + 3].ground == 0,
        "refresh_footprint_occupancy open yard occupancy"
    );
    // Its open cell still holds its own id, which occupy counts as an
    // occupant: the yard is left marked against itself.
    const auto both = spatial::collision_self | spatial::collision_other;
    check(
        (yard.flags & both) == both && host.changed == changes + 1,
        "refresh_footprint_occupancy flags and notice"
    );
}

// Bucket build on a 20 by 17 plot map: 3 by 3 buckets holding their highest
// plot or sea level, the area heights over each 3 by 3 block and the edges.
void bucket_grid() {
    std::vector<spatial::Unit> units(1);
    spatial::World w;
    w.terrain_width = 20;
    w.terrain_height = 17;
    w.plots.resize(20 * 17);
    w.units = units;
    w.sea_level = 20;
    const auto set = [&](std::size_t x, std::size_t z, uint8_t height) {
        w.plots[z * 20 + x].high_height = height;
    };
    set(3, 2, 40);
    set(12, 5, 90);
    set(19, 16, 25);
    set(9, 9, 60);
    set(0, 16, 70);
    w.outside_bucket.head = 3;
    spatial::build_buckets(w);
    check(
        w.bucket_width == 3 && w.bucket_height == 3 && w.buckets.size() == 9, "build_buckets size"
    );
    const uint8_t high[9] = {40, 90, 20, 20, 60, 20, 70, 20, 25};
    const uint8_t area[9] = {90, 90, 90, 90, 90, 90, 70, 70, 60};
    const uint32_t edges[9] = {5, 1, 9, 4, 0, 8, 6, 2, 10};
    for (std::size_t i = 0; i < 9; ++i)
        check(
            w.buckets[i].high_height == high[i] && w.buckets[i].area_high_height == area[i] &&
                w.buckets[i].edges == edges[i] && w.buckets[i].head == spatial::no_unit,
            "build_buckets bucket"
        );
    check(
        w.outside_bucket.edges == 0x1f && w.outside_bucket.high_height == 0 &&
            w.outside_bucket.area_high_height == 0 && w.outside_bucket.head == spatial::no_unit,
        "build_buckets off-map bucket"
    );
    // One bucket deep: the column pass leaves no area height.
    w.terrain_width = 16;
    w.terrain_height = 8;
    w.plots.assign(16 * 8, {});
    w.sea_level = 3;
    spatial::build_buckets(w);
    check(
        w.bucket_width == 2 && w.bucket_height == 1 && w.buckets[0].high_height == 3 &&
            w.buckets[0].area_high_height == 0 && w.buckets[0].edges == 7 &&
            w.buckets[1].area_high_height == 0 && w.buckets[1].edges == 11,
        "build_buckets single row"
    );
}

// bucket_unlink splices through the link it walked (not always the head) and
// clears the unit's next_in_bucket; bucket_push_front pushes at the head.
void bucket_chain() {
    std::vector<spatial::Unit> units(4);
    for (std::size_t i = 1; i < units.size(); ++i)
        units[i].id = static_cast<spatial::UnitId>(i);
    auto w = world(units);
    spatial::Bucket chain;
    spatial::bucket_push_front(chain, units[1]);
    spatial::bucket_push_front(chain, units[2]);
    spatial::bucket_push_front(chain, units[3]);
    check(
        chain.head == 3 && units[3].next_in_bucket == 2 && units[2].next_in_bucket == 1 &&
            units[1].next_in_bucket == spatial::no_unit,
        "bucket_push_front head insertion"
    );
    check(
        spatial::bucket_unlink(chain, units[2], w) == spatial::Error::none, "bucket_unlink middle"
    );
    check(
        chain.head == 3 && units[3].next_in_bucket == 1 &&
            units[2].next_in_bucket == spatial::no_unit,
        "bucket_unlink keeps the nodes before the unit"
    );
    check(
        spatial::bucket_unlink(chain, units[3], w) == spatial::Error::none && chain.head == 1,
        "bucket_unlink head"
    );
    check(
        spatial::bucket_unlink(chain, units[2], w) == spatial::Error::broken_bucket_chain,
        "unit not on the chain"
    );
}

int main() {
    try {
        overlap_walk();
        bucket_grid();
        bucket_chain();
        std::vector<spatial::Unit> units(6);
        for (std::size_t i = 1; i < units.size(); ++i)
            units[i].id = static_cast<spatial::UnitId>(i);
        auto w = world(units);
        Host host;
        auto& ground = units[1];
        ground.cell = {2, 3};
        ground.footprint = {2, 2};
        ground.position = {1u << 23, 0, 0};
        ground.flags = 1;
        ground.object_present = true;
        check(
            spatial::register_unit(ground, w, host) == spatial::Error::none,
            "ground registration failed"
        );
        check(
            ground.object_tick == 77 && ground.bucket_linked && ground.bucket == 1 &&
                w.buckets[1].head == 1,
            "object tick or bucket registration"
        );
        check(
            w.plots[3 * 8 + 2].ground == 1 && w.plots[3 * 8 + 3].ground == 1 &&
                w.plots[4 * 8 + 2].ground == 1 && w.plots[4 * 8 + 3].ground == 1,
            "ground footprint coverage"
        );

        auto& air = units[2];
        air.cell = {2, 3};
        air.footprint = {1, 1};
        air.position = {1u << 23, 0, 0};
        air.flags = 2;
        check(
            spatial::register_unit(air, w, host) == spatial::Error::none &&
                w.plots[3 * 8 + 2].air == 2,
            "air layer registration"
        );
        auto& collision = units[3];
        collision.cell = {2, 3};
        collision.footprint = {1, 1};
        collision.position = {1u << 23, 0, 0};
        collision.flags = 1;
        check(
            spatial::register_unit(collision, w, host) == spatial::Error::none &&
                w.plots[3 * 8 + 2].ground == 1,
            "ordinary collision replaced occupied plot"
        );
        check(
            (ground.flags & spatial::collision_other) != 0 &&
                (collision.flags & spatial::collision_self) != 0,
            "ordinary collision flags"
        );
        ground.owner_object_present = true;
        ground.owner_status = 3;
        auto& reverse = units[4];
        reverse.cell = {2, 3};
        reverse.footprint = {1, 1};
        reverse.position = {1u << 23, 0, 0};
        reverse.flags = 1;
        check(
            spatial::register_unit(reverse, w, host) == spatial::Error::none,
            "status3 collision registration"
        );
        check(
            (ground.flags & spatial::collision_self) != 0 &&
                (reverse.flags & spatial::collision_other) != 0 && w.plots[3 * 8 + 2].ground == 4,
            "status3 reversed collision flags or overwrite"
        );

        auto old_bucket = reverse.bucket;
        reverse.cell = {-1, 0};
        reverse.position = {0, 0, 0};
        check(
            spatial::register_unit(reverse, w, host) == spatial::Error::none && !reverse.bucket &&
                w.outside_bucket.head == 4,
            "outside bucket registration"
        );
        check(
            old_bucket && w.buckets[*old_bucket].head == 3 && units[3].next_in_bucket == 2,
            "intrusive bucket unlink"
        );
        auto& masked = units[4];
        const std::array<uint8_t, 1> mask{5};
        masked.cell = {1, 1};
        masked.footprint = {1, 1};
        masked.position = {0, 0, 0};
        masked.flags = spatial::masked_footprint_flag;
        masked.yard_mask = mask;
        check(
            spatial::register_unit(masked, w, host) == spatial::Error::none &&
                w.plots[9].ground == 4 && (w.plots[9].flags & 2) && host.masked == 1 &&
                host.changed == 1,
            "masked footprint registration"
        );
        check(
            spatial::can_change_yard(masked, true, w),
            "inactive open-yard mask was treated as blocked"
        );
        w.plots[9].ground = 3;
        check(
            !spatial::can_change_yard(masked, false, w), "active yard mask ignored foreign occupant"
        );
        w.plots[9].ground = 4;
        check(
            spatial::change_yard(masked, true, w, host) && w.plots[9].ground == 0 &&
                masked.yard_open && host.changed == 2,
            "yard open update"
        );
        check(
            spatial::change_yard(masked, false, w, host) && w.plots[9].ground == 4 &&
                !masked.yard_open && host.changed == 3,
            "yard close update"
        );
        check(
            spatial::change_yard(masked, 2, w, host) && !masked.yard_open &&
                w.plots[9].ground == 4 && host.changed == 4,
            "nonzero even yard value lost validation/store distinction"
        );
        auto& negative = units[5];
        negative.cell = {2, 2};
        negative.footprint = {-1, 1};
        negative.position = {0, 0, 0};
        negative.flags = 1;
        check(
            spatial::register_unit(negative, w, host) == spatial::Error::none &&
                negative.bucket == 0 && w.plots[2 * 8 + 2].ground == 0,
            "negative footprint changed the interior-bucket/empty-loop behavior"
        );

        std::vector<spatial::Unit> moving_units(4);
        for (std::size_t i = 1; i < moving_units.size(); ++i)
            moving_units[i].id = static_cast<spatial::UnitId>(i);
        auto movement = world(moving_units);
        movement.sea_level = 20;
        auto& mover = moving_units[1];
        mover.footprint = {1, 1};
        mover.bm_code = 1;
        mover.max_slope = 4;
        mover.max_water_slope = 8;
        mover.max_water_depth = 10;
        mover.min_water_depth = -10;
        movement.plots[2 * 8 + 2].low_height = 18;
        movement.plots[2 * 8 + 2].high_height = 21;
        check(
            spatial::can_occupy(mover, 0, {2, 2}, 1, movement) == true,
            "mobile occupancy rejected valid terrain"
        );
        check(
            spatial::can_unload_at(mover, 0x200000, 0x200000, true, false, movement),
            "can_unload_at accepted a clear pad"
        );
        check(
            spatial::can_unload_at(mover, 0x200000, 0x200000, false, false, movement),
            "can_unload_at accepts a pad the player does not see"
        );
        movement.plots[2 * 8 + 2].blocking_feature = true;
        check(
            !spatial::can_unload_at(mover, 0x200000, 0x200000, true, false, movement),
            "can_unload_at rejected a blocking feature"
        );
        movement.plots[2 * 8 + 2].blocking_feature = false;
        // A seaplane (amphibious, 255 deep) may set down on water 12 deep; an
        // aircraft that is not amphibious may not, whatever its depths.
        {
            auto sea = movement;
            sea.plots[2 * 8 + 2].low_height = 8;
            sea.plots[2 * 8 + 2].high_height = 9;
            auto seaplane = mover;
            seaplane.max_water_depth = 255;
            check(
                spatial::can_unload_at(seaplane, 0x200000, 0x200000, true, false, sea),
                "can_unload_at refused an amphibious aircraft on water"
            );
            check(
                !spatial::can_unload_at(seaplane, 0x200000, 0x200000, true, true, sea),
                "can_unload_at let an aircraft that is not amphibious set down on water"
            );
            auto land = movement;
            land.plots[2 * 8 + 2].low_height = 22;
            land.plots[2 * 8 + 2].high_height = 23;
            check(
                spatial::can_unload_at(mover, 0x200000, 0x200000, true, true, land),
                "can_unload_at refused an aircraft that is not amphibious on dry land"
            );
        }
        movement.plots[2 * 8 + 2].blocking_feature = true;
        check(
            spatial::can_occupy(mover, 0, {2, 2}, 1, movement) == false, "blocking feature ignored"
        );
        check(
            spatial::can_occupy(mover, 0, {2, 2}, 0, movement) == true,
            "nonchecking mode scanned plots"
        );
        check(
            spatial::can_occupy(mover, 0, {7, 2}, 2, movement) == true &&
                spatial::can_occupy(mover, 0, {7, 2}, 1, movement) == false,
            "out-of-bounds mode2 exception"
        );
        mover.bm_code = 0;
        check(
            !spatial::can_occupy(mover, 0, {2, 2}, 1, movement).has_value(),
            "yard-map branch was silently approximated"
        );

        auto& departing = moving_units[1];
        departing.bm_code = 1;
        departing.cell = {2, 2};
        departing.position = {0, 0, 0};
        departing.footprint = {1, 1};
        departing.flags = 1;
        auto& waiting = moving_units[2];
        waiting.cell = {2, 2};
        waiting.position = {0, 0, 0};
        waiting.footprint = {1, 1};
        waiting.flags = 1;
        check(
            spatial::register_unit(departing, movement, host) == spatial::Error::none &&
                spatial::register_unit(waiting, movement, host) == spatial::Error::none,
            "removal collision setup"
        );
        departing.object_present = true;
        departing.object_tick = 12;
        check(
            movement.plots[2 * 8 + 2].ground == 1 &&
                (departing.flags & spatial::collision_other) != 0 &&
                (waiting.flags & spatial::collision_self) != 0,
            "removal collision flags"
        );
        check(
            spatial::remove_occupancy(departing, movement, host) == spatial::Error::none &&
                movement.plots[2 * 8 + 2].ground == 2,
            "removal did not wake overlapping occupant"
        );
        check(
            (departing.flags & (spatial::collision_other | spatial::collision_self)) == 0 &&
                (waiting.flags & spatial::collision_self) == 0,
            "removal collision flags not consumed"
        );
        check(
            departing.object_tick == movement.tick && host.object_removed == 1,
            "object removal tick/cache callback"
        );

        // remove_unit: a dying unit leaves its footprint, then its bucket chain
        // (walked from the bucket head through next_in_bucket); a carried unit
        // (spatial_link_locked) is in no chain. Both end with no bucket
        // (bucket_linked false).
        std::vector<spatial::Unit> dying_units(5);
        for (std::size_t i = 1; i < dying_units.size(); ++i) {
            auto& unit = dying_units[i];
            unit.id = static_cast<spatial::UnitId>(i);
            unit.cell = {static_cast<int16_t>(i), static_cast<int16_t>(i)};
            unit.footprint = {1, 1};
            unit.flags = 1;
        }
        auto dying = world(dying_units);
        dying_units[4].spatial_link_locked = true;
        for (std::size_t i = 1; i < dying_units.size(); ++i)
            check(
                spatial::register_unit(dying_units[i], dying, host) == spatial::Error::none,
                "dying unit registration"
            );
        check(
            dying.buckets[0].head == 3 && dying_units[3].next_in_bucket == 2 &&
                dying_units[2].next_in_bucket == 1 && dying_units[4].bucket_linked,
            "dying chain setup"
        );
        check(
            spatial::remove_unit(dying_units[2], dying, host) == spatial::Error::none &&
                dying.buckets[0].head == 3 && dying_units[3].next_in_bucket == 1 &&
                dying_units[2].next_in_bucket == spatial::no_unit &&
                !dying_units[2].bucket_linked && dying.plots[2 * 8 + 2].ground == spatial::no_unit,
            "remove_unit unlinks a chain member"
        );
        check(
            spatial::remove_unit(dying_units[3], dying, host) == spatial::Error::none &&
                dying.buckets[0].head == 1 && !dying_units[3].bucket_linked,
            "remove_unit unlinks the chain head"
        );
        check(
            dying.plots[4 * 8 + 4].ground == 4 &&
                spatial::remove_unit(dying_units[4], dying, host) == spatial::Error::none &&
                dying.buckets[0].head == 1 && dying_units[1].next_in_bucket == spatial::no_unit &&
                !dying_units[4].bucket_linked && dying.plots[4 * 8 + 4].ground == spatial::no_unit,
            "remove_unit leaves the chain alone for a carried unit"
        );
        check(
            spatial::register_unit(dying_units[2], dying, host) == spatial::Error::none &&
                dying.buckets[0].head == 2 && dying_units[2].next_in_bucket == 1,
            "a removed unit links again at the head"
        );
        std::cout << "spatial-state tests passed\n";
        int32_t cell_x = -1, cell_z = -1;
        check(
            spatial::position_to_cell(0x200000, 0x300000, 16, 16, cell_x, cell_z),
            "position_to_cell inside"
        );
        check(cell_x == 2 && cell_z == 3, "position_to_cell cells");
        check(
            !spatial::position_to_cell(-1, 0, 16, 16, cell_x, cell_z), "position_to_cell off map"
        );
        spatial::Plot placed[8]{};
        placed[0].feature_word = 9;
        placed[1].feature_word = 0xfffe;
        placed[1].feature_back_x = 1;
        placed[2].feature_word = 4;
        placed[3].feature_word = 0xfffd;
        placed[3].feature_back_x = 3;
        placed[4].feature_word = 0xfffe;
        placed[4].feature_back_z = 1;
        placed[5].feature_word = 0xffff;
        placed[6].feature_word = 0xfffe;
        placed[6].feature_back_x = 2;
        placed[6].feature_back_z = 1;
        placed[7].feature_word = 0xfffe;
        placed[7].feature_back_z = 2;
        check(spatial::plot_index_at(0, 0, placed, 4, 2) == 0, "plot_index_at origin");
        check(spatial::plot_index_at(3, 1, placed, 4, 2) == 7, "plot_index_at corner");
        check(spatial::plot_index_at(1, 0, placed, 4, 2) == 1, "plot_index_at keeps fffe");
        check(!spatial::plot_index_at(-1, 0, placed, 4, 2), "plot_index_at neg x");
        check(!spatial::plot_index_at(0, -1, placed, 4, 2), "plot_index_at neg z");
        check(!spatial::plot_index_at(4, 0, placed, 4, 2), "plot_index_at past width");
        check(!spatial::plot_index_at(0, 2, placed, 4, 2), "plot_index_at past height");
        check(!spatial::plot_index_at(1, 0, std::span(placed, 1), 4, 2), "plot_index_at short map");
        check(
            spatial::feature_origin_plot_at(0x200000, 0, placed, 4, 2) == 2,
            "feature_origin_plot_at direct"
        );
        check(
            spatial::feature_origin_plot_at(0x2fffff, 0, placed, 4, 2) == 2,
            "feature_origin_plot_at shift"
        );
        check(
            spatial::feature_origin_plot_at(0x100000, 0, placed, 4, 2) == 0,
            "feature_origin_plot_at back x"
        );
        check(
            spatial::feature_origin_plot_at(0, 0x100000, placed, 4, 2) == 0,
            "feature_origin_plot_at back z"
        );
        check(
            spatial::feature_origin_plot_at(0x200000, 0x100000, placed, 4, 2) == 0,
            "feature_origin_plot_at back xz"
        );
        check(
            spatial::feature_origin_plot_at(0x300000, 0, placed, 4, 2) == 3,
            "feature_origin_plot_at not fffe"
        );
        check(
            spatial::feature_origin_plot_at(0x100000, 0x100000, placed, 4, 2) == 5,
            "feature_origin_plot_at empty"
        );
        check(
            !spatial::feature_origin_plot_at(-1, 0, placed, 4, 2), "feature_origin_plot_at off map"
        );
        check(
            !spatial::feature_origin_plot_at(0x400000, 0, placed, 4, 2),
            "feature_origin_plot_at past width"
        );
        check(
            !spatial::feature_origin_plot_at(0, 0x200000, placed, 4, 2),
            "feature_origin_plot_at past height"
        );
        check(
            !spatial::feature_origin_plot_at(0x300000, 0x100000, placed, 4, 2),
            "feature_origin_plot_at origin off map"
        );
        spatial::Plot chain[2]{};
        chain[0].feature_word = 0xfffe;
        chain[0].feature_back_x = 1;
        chain[1].feature_word = 0xfffe;
        chain[1].feature_back_x = 1;
        check(
            spatial::feature_origin_plot_at(0x100000, 0, chain, 2, 1) == 0,
            "feature_origin_plot_at one step"
        );
        spatial::Plot same[1]{};
        same[0].feature_word = 0xfffe;
        check(
            spatial::feature_origin_plot_at(0, 0, same, 1, 1) == 0,
            "feature_origin_plot_at zero back"
        );
        spatial::Plot heights[4]{};
        heights[0].high_height = 10;
        heights[0].low_height = 4;
        heights[1].high_height = 5;
        heights[1].low_height = 2;
        heights[2].high_height = 255;
        heights[2].low_height = 255;
        heights[3].low_height = 1;
        check(spatial::plot_mean_height_at(0, 0, heights, 2, 2) == 7, "plot_mean_height_at origin");
        check(
            spatial::plot_mean_height_at(15 << 16, 0, heights, 2, 2) == 7,
            "plot_mean_height_at truncates 15"
        );
        check(
            spatial::plot_mean_height_at(31 << 16, 0, heights, 2, 2) == 3,
            "plot_mean_height_at truncates 31"
        );
        check(
            spatial::plot_mean_height_at(16 << 16, 0, heights, 2, 2) == 3,
            "plot_mean_height_at cell x"
        );
        check(
            spatial::plot_mean_height_at(0, 16 << 16, heights, 2, 2) == 255,
            "plot_mean_height_at cell z"
        );
        check(
            spatial::plot_mean_height_at(16 << 16, 16 << 16, heights, 2, 2) == 0,
            "plot_mean_height_at zero mean"
        );
        check(
            spatial::plot_mean_height_at((-15) << 16, 0, heights, 2, 2) == 7,
            "plot_mean_height_at toward zero"
        );
        check(
            spatial::plot_mean_height_at((-16) << 16, 0, heights, 2, 2) == -1,
            "plot_mean_height_at negative cell"
        );
        check(
            spatial::plot_mean_height_at(0x0000ffff, 0x00100001, heights, 2, 2) == 255,
            "plot_mean_height_at high word"
        );
        check(
            spatial::plot_mean_height_at(32 << 16, 0, heights, 2, 2) == -1,
            "plot_mean_height_at past width"
        );
        check(
            spatial::plot_mean_height_at(0, 32 << 16, heights, 2, 2) == -1,
            "plot_mean_height_at past height"
        );
        check(
            spatial::plot_mean_height_at(0, 0, std::span(heights, 0), 2, 2) == -1,
            "plot_mean_height_at short map"
        );
        const auto corner = spatial::plot_index_to_cell(7, 4);
        check(corner && (*corner)[0] == 3 && (*corner)[1] == 1, "plot_index_to_cell corner");
        check(
            spatial::plot_index_at((*corner)[0], (*corner)[1], placed, 4, 2) == 7,
            "plot_index_to_cell roundtrip"
        );
        const auto origin = spatial::plot_index_to_cell(0, 4);
        check(origin && (*origin)[0] == 0 && (*origin)[1] == 0, "plot_index_to_cell origin");
        const auto past = spatial::plot_index_to_cell(8, 4);
        check(past && (*past)[0] == 0 && (*past)[1] == 2, "plot_index_to_cell past map");
        const auto before = spatial::plot_index_to_cell(-7, 4);
        check(
            before && (*before)[0] == -3 && (*before)[1] == -1, "plot_index_to_cell negative slot"
        );
        const auto signed_width = spatial::plot_index_to_cell(7, -3);
        check(
            signed_width && (*signed_width)[0] == 1 && (*signed_width)[1] == -2,
            "plot_index_to_cell signed width"
        );
        const auto wrapped = spatial::plot_index_to_cell(32768, 1);
        check(
            wrapped && (*wrapped)[0] == 0 && (*wrapped)[1] == -32768, "plot_index_to_cell short z"
        );
        check(!spatial::plot_index_to_cell(7, 0), "plot_index_to_cell zero width");
        const uint16_t words[] = {4, 0xfffe};
        check(
            spatial::resolve_feature_index(4, 0, 0, 2, 0, words, 10) == 4,
            "resolve_feature_index direct"
        );
        check(
            spatial::resolve_feature_index(0xfffe, 1, 0, 2, 1, words, 10) == 4,
            "resolve_feature_index redirect"
        );
        check(
            !spatial::resolve_feature_index(0xffff, 0, 0, 2, 0, words, 10),
            "resolve_feature_index empty"
        );

        // A 2x2 building on a 6x6 map: the lowest plot under its level cells,
        // or the waterline surface when no yard cell levels the site.
        spatial::World site;
        site.terrain_width = site.terrain_height = 6;
        site.plots.resize(36);
        site.sea_level = 100;
        for (auto& plot : site.plots) {
            plot.low_height = 40;
            plot.high_height = 44;
        }
        site.plots[2 * 6 + 3].low_height = 30;
        const std::array<uint8_t, 4> level{8, 8, 8, 8}, water{0x35, 0x35, 0x35, 0x35};
        check(
            spatial::footprint_build_height(2, 2, level, 1, 2, 2, site) == 30,
            "footprint_build_height level"
        );
        check(
            spatial::footprint_build_height(2, 2, water, 1, 2, 2, site) == 99,
            "footprint_build_height water"
        );
        check(
            spatial::footprint_build_height(2, 2, water, -3, 2, 2, site) == 103,
            "footprint_build_height waterline above the sea"
        );
        check(
            spatial::footprint_build_height(2, 2, level, 1, 0, 2, site) == 0,
            "footprint_build_height x edge"
        );
        check(
            spatial::footprint_build_height(2, 2, level, 1, 2, 0, site) == 0,
            "footprint_build_height z edge"
        );
        check(
            spatial::footprint_build_height(2, 2, level, 1, 4, 2, site) == 0,
            "footprint_build_height far edge"
        );
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
