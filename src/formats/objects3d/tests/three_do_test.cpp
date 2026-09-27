// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/objects3d.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
using oa::formats::objects3d::ThreeDoError;

void put32(std::vector<std::byte>& data, std::size_t at, int32_t value) {
    const auto v = static_cast<uint32_t>(value);
    for (unsigned i = 0; i != 4; ++i)
        data[at + i] = std::byte((v >> (i * 8U)) & 0xffU);
}

void put16(std::vector<std::byte>& data, std::size_t at, uint16_t value) {
    data[at] = std::byte(value & 0xffU);
    data[at + 1] = std::byte(value >> 8U);
}

void put_string(std::vector<std::byte>& data, std::size_t at, const std::string& value) {
    for (std::size_t i = 0; i < value.size(); ++i)
        data[at + i] = std::byte(value[i]);
    data[at + value.size()] = std::byte{0};
}

void header(
    std::vector<std::byte>& d,
    std::size_t at,
    int32_t vertices,
    int32_t primitives,
    int32_t selection,
    int32_t x,
    int32_t y,
    int32_t z,
    int32_t name,
    int32_t vertex_array,
    int32_t primitive_array,
    int32_t sibling,
    int32_t child
) {
    put32(d, at, 1);
    put32(d, at + 4, vertices);
    put32(d, at + 8, primitives);
    put32(d, at + 12, selection);
    put32(d, at + 16, x);
    put32(d, at + 20, y);
    put32(d, at + 24, z);
    put32(d, at + 28, name);
    put32(d, at + 32, 0);
    put32(d, at + 36, vertex_array);
    put32(d, at + 40, primitive_array);
    put32(d, at + 44, sibling);
    put32(d, at + 48, child);
}

std::vector<std::byte> fixture() {
    std::vector<std::byte> d(300);
    header(d, 0, 3, 1, 0, 65536, 131072, 196608, 200, 64, 100, 0, 220);
    put32(d, 64, 65536);
    put32(d, 68, 0);
    put32(d, 72, 0);
    put32(d, 76, 0);
    put32(d, 80, 65536);
    put32(d, 84, 0);
    put32(d, 88, 0);
    put32(d, 92, 0);
    put32(d, 96, 65536);
    put32(d, 100, 7);
    put32(d, 104, 3);
    put32(d, 108, 0);
    put32(d, 112, 140);
    put32(d, 116, 208);
    put32(d, 120, 11);
    put32(d, 124, 12);
    put32(d, 128, 0);
    put16(d, 140, 0);
    put16(d, 142, 1);
    put16(d, 144, 2);
    put_string(d, 200, "base");
    put_string(d, 208, "panel");
    header(d, 220, 0, 0, -1, -65536, 0, 0, 272, 0, 0, 0, 0);
    put_string(d, 272, "turret");
    return d;
}

void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

template <class F>
void rejects(F&& f, const char* message) {
    try {
        f();
    } catch (const ThreeDoError&) {
        return;
    }
    throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        auto bytes = fixture();
        const auto model = oa::formats::objects3d::load_3do(bytes);
        require(model.objects.size() == 2, "hierarchy was not flattened in preorder");
        require(
            model.objects[0].name == "base" && model.objects[1].name == "turret",
            "piece names differ"
        );
        require(
            model.objects[0].first_child == 1 && model.objects[1].parent == 0,
            "piece topology differs"
        );
        require(
            oa::formats::objects3d::maximum_height_fixed(model) == 196608,
            "model height calculation differs"
        );
        const auto bounds = oa::formats::objects3d::derive_unit_type_bounds(model, 3, 2);
        require(
            bounds.bounds_min_x == -1572864 && bounds.bounds_min_y == 0 &&
                bounds.bounds_min_z == -1048576 && bounds.bounds_max_x == 1572864 &&
                bounds.model_height == 196608 && bounds.bounds_max_z == 1048576 &&
                bounds.size_x == 3145728 && bounds.size_y == 196608 && bounds.size_z == 2097152,
            "UNITINFO bounds calculation differs"
        );
        const auto& p = model.objects[0].primitives[0];
        require(p.vertex_indices.size() == 3 && p.vertex_indices[2] == 2, "uint16 indices differ");
        require(p.texture_name == "panel" && p.source_visible(), "texture metadata differs");
        require(
            p.word_after_texture_name == 11 && p.word_before_is_colored == 12,
            "uninterpreted primitive words were not preserved"
        );
        const auto flat = oa::formats::objects3d::flatten_for_render(model);
        require(
            flat.size() == 1 && flat[0].is_selection_primitive,
            "selection primitive identity was lost"
        );
        require(
            flat[0].vertices[0].x == -131072 && flat[0].vertices[0].y == 131072 &&
                flat[0].vertices[0].z == -196608,
            "runtime X/Z conversion or fixed coordinates differ"
        );

        auto truncated = bytes;
        truncated.resize(51);
        rejects([&] { (void)oa::formats::objects3d::load_3do(truncated); }, "truncation accepted");
        auto bad_index = bytes;
        put16(bad_index, 144, 3);
        rejects(
            [&] { (void)oa::formats::objects3d::load_3do(bad_index); }, "bad vertex index accepted"
        );
        auto cycle = bytes;
        put32(cycle, 264, 220);
        rejects([&] { (void)oa::formats::objects3d::load_3do(cycle); }, "object cycle accepted");
        auto unterminated = bytes;
        for (std::size_t i = 272; i < unterminated.size(); ++i)
            unterminated[i] = std::byte{'x'};
        rejects(
            [&] { (void)oa::formats::objects3d::load_3do(unterminated); },
            "unterminated name accepted"
        );

        oa::formats::objects3d::Model height_edge;
        height_edge.objects.resize(2);
        height_edge.objects[0].first_child = 1;
        height_edge.objects[0].offset_from_parent.y = 100;
        height_edge.objects[1].parent = 0;
        height_edge.objects[1].vertices = {{0, -200, 0}};
        require(
            oa::formats::objects3d::maximum_height_fixed(height_edge) == 100,
            "recursive zero clamp in the height calculation differs"
        );

        // The accumulator adds the negated X/Z vertex to the negated offset
        // and widens a box that starts at zero; the child is not visited.
        oa::formats::objects3d::Object boxed;
        boxed.offset_from_parent = {1 << 16, 4 << 16, -(2 << 16)};
        boxed.vertices = {
            {3 << 16, 1 << 16, -(5 << 16)},
            {-(7 << 16), 2 << 16, 4 << 16},
            {2 << 16, -(3 << 16), 9 << 16}
        };
        boxed.first_child = 1;
        const auto box = oa::formats::objects3d::object_bounds(boxed);
        require(
            box.minimum.x == -(4 << 16) && box.maximum.x == 6 << 16 && box.minimum.y == 0 &&
                box.maximum.y == 6 << 16 && box.minimum.z == -(7 << 16) && box.maximum.z == 7 << 16,
            "root object bounds differ from the loaded-orientation accumulator"
        );
        boxed.vertices.pop_back();
        const auto flat_box = oa::formats::objects3d::object_bounds(boxed);
        require(
            flat_box.minimum.x == 0 && flat_box.maximum.x == 0 && flat_box.maximum.y == 0 &&
                flat_box.minimum.z == 0 && flat_box.maximum.z == 0,
            "an object of two vertices widened the bounds"
        );

        const auto shadow = oa::formats::objects3d::shadow_bitmap_extent(model);
        require(
            shadow.width == 5 && shadow.height == 5 && shadow.origin_x == 3 && shadow.origin_y == 2,
            "shadow bitmap extent for the fixture differs"
        );
        oa::formats::objects3d::Model hidden;
        hidden.objects.resize(2);
        hidden.objects[0].vertices = {{100 << 16, 0, 0}, {0, 100 << 16, 0}};
        hidden.objects[1].vertices = {
            {0x0001'ffff, 0, 0}, {0, static_cast<int32_t>(-5) << 16, 0}, {0, 8 << 16, 0x0002'0001}
        };
        const auto shifted = oa::formats::objects3d::shadow_bitmap_extent(hidden);
        require(
            shifted.width == 8 && shifted.height == 6 && shifted.origin_x == 4 &&
                shifted.origin_y == 2,
            "shadow projection shift or sub-three-vertex skip differs"
        );
        require(
            oa::formats::objects3d::shadow_bitmap_extent({}).width == 4 &&
                oa::formats::objects3d::shadow_bitmap_extent({}).height == 4 &&
                oa::formats::objects3d::shadow_bitmap_extent({}).origin_x == 2 &&
                oa::formats::objects3d::shadow_bitmap_extent({}).origin_y == 2,
            "empty shadow extent does not keep the zero baseline"
        );
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
