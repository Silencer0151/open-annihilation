// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/model_runtime/instance.hpp"
#include "oa/sim/model_runtime/model_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

struct Host final : oa::sim::model_runtime::ModelHost {
    using ModelHost::ModelHost;

    void emit_sfx(uint32_t, int32_t) override {}

    void explode(uint32_t, int32_t) override {}

    int32_t get_unit_value(int32_t, int32_t, int32_t, int32_t, int32_t) override { return 0; }

    void set_unit_value(int32_t, int32_t) override {}

    void attach_unit(int32_t, int32_t, int32_t) override {}

    void drop_unit(int32_t) override {}

    void ignored_piece_op(uint32_t, uint32_t, uint32_t) override {}

    void dont_shadow(uint32_t) override {}

    uint32_t is_carrying_unit(uint32_t) override { return 0; }

    uint32_t carrier_unit_id() override { return 0; }

    uint32_t random_bounded(uint32_t) override { return 0; }
};
} // namespace

int main() {
    try {
        auto model = std::make_shared<oa::formats::objects3d::Model>();
        model->objects.resize(3);
        model->objects[0].name = "base";
        model->objects[0].first_child = 1;
        model->objects[0].offset_from_parent = {65536, 0, 0};
        model->objects[0].vertices = {{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}};
        model->objects[1].name = "Turret";
        model->objects[1].parent = 0;
        model->objects[1].next_sibling = 2;
        model->objects[1].offset_from_parent = {0, 65536, 0};
        model->objects[2].name = "flare";
        model->objects[2].parent = 0;

        require(
            oa::sim::model_runtime::count_linked_objects(*model, 0) == 3 &&
                oa::sim::model_runtime::count_linked_objects(*model, 1) == 2 &&
                oa::sim::model_runtime::count_linked_objects(*model, 2) == 1,
            "linked object count did not follow child then sibling"
        );
        auto rooted_siblings = std::make_shared<oa::formats::objects3d::Model>();
        rooted_siblings->objects.resize(2);
        rooted_siblings->objects[0].next_sibling = 1;
        require(
            oa::sim::model_runtime::count_linked_objects(*rooted_siblings, 0) == 2 &&
                oa::sim::model_runtime::count_linked_objects(*rooted_siblings, 1) == 1,
            "root sibling chain was not counted"
        );
        require(
            oa::sim::model_runtime::count_linked_objects(*model, 3) == 0,
            "an object index past the model counts nothing"
        );

        constexpr std::string_view names[] = {"flare", "turret"};
        auto instance = oa::sim::model_runtime::make_instance(model, 0x1234, names);
        require(
            instance.pieces().size() == 3 && instance.root_piece() == 2,
            "COB reorder/root relink differs"
        );
        require(
            instance.pieces()[0].object_index == 2 && instance.pieces()[1].object_index == 1,
            "case-insensitive COB ordering differs"
        );
        require(
            instance.pieces()[2].first_child == 1 && instance.pieces()[1].next_sibling == 0,
            "relinked hierarchy differs"
        );
        require(
            instance.pieces()[2].flags == 7 && instance.owner_token() == 0x1234,
            "constructor state differs"
        );

        Host host(instance);
        require(
            host.piece_visible(0) == 0 && host.piece_visible(2) == 1,
            "initial visibility does not follow constructor flags"
        );
        host.set_piece_position(1, 0, 32768);
        host.set_piece_angle(1, 2, 0x4000);
        host.set_piece_visible(1, 1);
        require(
            host.piece_position(1, 0) == 32768 && host.piece_angle(1, 2) == 0x4000 &&
                host.piece_visible(1) == 1,
            "model Host bridge differs"
        );
        host.set_piece_visible(1, 0);
        require((instance.pieces()[1].flags & 1U) == 0, "HIDE did not clear render visibility");
        instance.pieces()[1].transform_marker = 1234;
        host.set_piece_visible(1, 2);
        require(
            host.piece_visible(1) == 0 && instance.pieces()[1].transform_marker == 0,
            "visibility callback did not use the low-bit/full-input behavior"
        );
        instance.pieces()[1].transform_marker = 1234;
        host.set_piece_cached(1, 2);
        require(
            host.piece_cached(1) == 0 && instance.pieces()[1].transform_marker == 1234,
            "cache callback changed marker or ignored low-bit normalization"
        );
        const auto attachment = instance.attachment_position(1);
        require(
            attachment.x == -32768 && attachment.y == 65536 && attachment.z == 0,
            "attachment accumulation differs"
        );
        instance.rebuild_transforms();
        const auto& root = instance.pieces()[2];
        require(
            root.transformed_vertices[0].x == -131072,
            "runtime vertex sign/offset transform differs"
        );
        require(!instance.transforms_dirty(), "transform rebuild did not clear dirty state");

        // piece_box takes in the vertices rebuild_transforms gives under the
        // same root rotation, whatever the instance last rebuilt, and changes
        // nothing.
        auto boxed_model = std::make_shared<oa::formats::objects3d::Model>(*model);
        boxed_model->objects[1].vertices = {
            {3 * 65536, -2 * 65536, 5 * 65536}, {-7 * 65536, 4 * 65536, 65536}
        };
        auto boxed = oa::sim::model_runtime::make_instance(boxed_model, 0, names);
        Host boxed_host(boxed);
        boxed_host.set_piece_angle(1, 2, 0x1234);
        boxed_host.set_piece_angle(2, 0, 0x0800);
        boxed_host.set_piece_position(1, 1, 3 * 65536);
        const oa::sim::model_runtime::RotationWords turned{0x0300, 0x2000, -0x0400};
        boxed.rebuild_transforms(turned);
        std::vector<oa::sim::model_runtime::PieceBox> expected(boxed.pieces().size());
        for (std::size_t piece = 0; piece < expected.size(); ++piece)
            for (const auto& vertex : boxed.pieces()[piece].transformed_vertices) {
                auto& box = expected[piece];
                box.low = {
                    std::min(box.low.x, vertex.x),
                    std::min(box.low.y, vertex.y),
                    std::min(box.low.z, vertex.z)
                };
                box.high = {
                    std::max(box.high.x, vertex.x),
                    std::max(box.high.y, vertex.y),
                    std::max(box.high.z, vertex.z)
                };
            }
        boxed.rebuild_transforms();
        const auto unturned = boxed.pieces()[1].transformed_vertices;
        for (uint32_t piece = 0; piece < expected.size(); ++piece) {
            const auto box = boxed.piece_box(piece, turned, {});
            const auto& want = expected[piece];
            require(
                box.low.x == want.low.x && box.low.y == want.low.y && box.low.z == want.low.z &&
                    box.high.x == want.high.x && box.high.y == want.high.y &&
                    box.high.z == want.high.z,
                "piece box differs from the rebuilt transforms' box"
            );
        }
        require(
            boxed.pieces()[1].transformed_vertices.size() == unturned.size() &&
                boxed.pieces()[1].transformed_vertices[0].x == unturned[0].x &&
                boxed.pieces()[1].transformed_vertices[1].z == unturned[1].z,
            "piece box changed the instance's transforms"
        );
        const auto outside = boxed.piece_box(9, turned, {{1, 2, 3}, {4, 5, 6}});
        require(
            outside.low.x == 1 && outside.high.z == 6, "a piece past the pieces changed the box"
        );

        constexpr std::string_view excess[] = {"base", "turret", "flare", "extra"};
        (void)oa::sim::model_runtime::make_instance(model, 0, excess);
        auto malformed = std::make_shared<oa::formats::objects3d::Model>(*model);
        malformed->objects[1].next_sibling = 99;
        require(
            oa::sim::model_runtime::model_hierarchy_error(malformed) != nullptr,
            "invalid public Model link was accepted"
        );
        const auto refused = oa::sim::model_runtime::make_instance(malformed, 7);
        require(
            refused.pieces().empty() && refused.root_piece() == oa::sim::model_runtime::kNoPiece &&
                refused.owner_token() == 7 && refused.find_piece(0) == nullptr,
            "a refused model gives an instance with no pieces"
        );
        require(
            oa::sim::model_runtime::model_hierarchy_error(model) == nullptr,
            "a well-linked model is refused"
        );
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
