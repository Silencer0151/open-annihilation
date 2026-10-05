// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/objects3d.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using oa::base::bytes::DecodeCode;

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

/// Builds a model of `count` objects, each linked to the next by its
/// sibling link or, when `nested`, by its child link. Every object is 256
/// units (1/256) above its parent, and the last holds one vertex at Y 5.0.
std::vector<std::byte> chain_model(std::size_t count, bool nested) {
    constexpr std::size_t record = 52;
    const std::size_t vertex_at = count * record;
    std::vector<std::byte> d(vertex_at + 12);
    for (std::size_t i = 0; i < count; ++i) {
        const auto at = i * record;
        const auto next = i + 1 < count ? static_cast<int32_t>(at + record) : 0;
        const bool last = i + 1 == count;
        header(
            d,
            at,
            last ? 1 : 0,
            0,
            -1,
            0,
            256,
            0,
            0,
            last ? static_cast<int32_t>(vertex_at) : 0,
            0,
            nested ? 0 : next,
            nested ? next : 0
        );
    }
    put32(d, vertex_at + 4, 5 * 65536);
    return d;
}

/// Builds one object whose `primitives` primitives all name one array of
/// `indices` vertex indices and one texture name of `name_bytes` letters.
///
/// @param primitives count of primitives
/// @param indices vertex indices in the shared array, each naming vertex 0
/// @param name_bytes letters of the shared texture name; 0 for none
/// @return the file
std::vector<std::byte>
aliased_primitives(std::size_t primitives, std::size_t indices, std::size_t name_bytes) {
    constexpr std::size_t record = 52;
    constexpr std::size_t vertex_at = record;
    constexpr std::size_t indices_at = vertex_at + 12;
    const std::size_t name_at = indices_at + indices * 2;
    const std::size_t primitives_at = name_at + name_bytes + 1;
    std::vector<std::byte> d(primitives_at + primitives * 32);
    header(
        d,
        0,
        1,
        static_cast<int32_t>(primitives),
        -1,
        0,
        0,
        0,
        0,
        static_cast<int32_t>(vertex_at),
        static_cast<int32_t>(primitives_at),
        0,
        0
    );
    if (name_bytes != 0)
        put_string(d, name_at, std::string(name_bytes, 'a'));
    for (std::size_t i = 0; i < primitives; ++i) {
        const auto at = primitives_at + i * 32;
        put32(d, at + 4, static_cast<int32_t>(indices));
        put32(d, at + 12, static_cast<int32_t>(indices_at));
        put32(d, at + 16, name_bytes != 0 ? static_cast<int32_t>(name_at) : 0);
    }
    return d;
}

void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

/// Requires a model load to fail with a code at an offset.
///
/// @param bytes the file
/// @param code the error code expected
/// @param offset the file offset expected
/// @param message what the failure means
void rejects(
    const std::vector<std::byte>& bytes, DecodeCode code, uint64_t offset, const char* message
) {
    const auto loaded = oa::formats::objects3d::load_3do(bytes);
    require(
        !loaded.ok() && loaded.error.code == code && loaded.error.offset == offset &&
            loaded.error.message != nullptr,
        message
    );
}

/// Loads a model the test expects to be valid.
///
/// @param bytes the file
/// @return the model
oa::formats::objects3d::Model load(const std::vector<std::byte>& bytes) {
    auto loaded = oa::formats::objects3d::load_3do(bytes);
    require(loaded.ok(), "a valid model was refused");
    return std::move(*loaded.value);
}
} // namespace

int main() {
    try {
        auto bytes = fixture();
        const auto model = load(bytes);
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
            oa::formats::objects3d::maximum_height_fixed(model).value == 196608,
            "model height calculation differs"
        );
        const auto bounds =
            oa::formats::objects3d::derive_unit_type_bounds(model, 3, 2).value.value();
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
        const auto flat = oa::formats::objects3d::flatten_for_render(model).value.value();
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
        rejects(truncated, DecodeCode::truncated, 0, "truncation accepted");
        auto bad_index = bytes;
        put16(bad_index, 144, 3);
        rejects(bad_index, DecodeCode::out_of_range, 144, "bad vertex index accepted");
        auto cycle = bytes;
        put32(cycle, 264, 220);
        rejects(cycle, DecodeCode::cycle, 220, "object cycle accepted");
        auto unterminated = bytes;
        for (std::size_t i = 272; i < unterminated.size(); ++i)
            unterminated[i] = std::byte{'x'};
        rejects(unterminated, DecodeCode::truncated, 272, "unterminated name accepted");

        // 65,536 objects, the most a model may hold, load from a sibling
        // list and from a child chain alike, without exhausting the stack.
        const auto siblings = load(chain_model(65'536, false));
        require(
            siblings.objects.size() == 65'536 && siblings.objects[0].next_sibling == 1 &&
                siblings.objects[65'534].next_sibling == 65'535 &&
                siblings.objects[65'535].parent == oa::formats::objects3d::kNoObject,
            "long sibling list differs"
        );
        // Siblings stand on the root's own level: the highest is the last
        // object's vertex plus its own offset.
        require(
            oa::formats::objects3d::maximum_height_fixed(siblings).value == 5 * 65536 + 256,
            "long sibling list height differs"
        );
        const auto nested = load(chain_model(65'536, true));
        require(
            nested.objects.size() == 65'536 && nested.objects[0].first_child == 1 &&
                nested.objects[65'535].parent == 65'534 &&
                nested.objects[65'535].first_child == oa::formats::objects3d::kNoObject,
            "deep child chain differs"
        );
        // Each of the 65,536 levels adds its offset to the vertex.
        require(
            oa::formats::objects3d::maximum_height_fixed(nested).value == 5 * 65536 + 65'536 * 256,
            "deep child chain height differs"
        );
        // The 65,537th object is refused where its record lies.
        rejects(
            chain_model(65'537, false),
            DecodeCode::limit_exceeded,
            65'536 * 52,
            "a sibling list over the object limit accepted"
        );
        rejects(
            chain_model(65'537, true),
            DecodeCode::limit_exceeded,
            65'536 * 52,
            "a child chain over the object limit accepted"
        );
        // Primitives that share one index array or one texture name each
        // copy it, up to 1,048,576 indices and 1 MiB of names a model.
        require(
            load(aliased_primitives(1024, 1024, 0)).objects[0].primitives.size() == 1024,
            "a model copying exactly the most vertex indices was refused"
        );
        const std::size_t aliased_at = 52 + 12 + 1024 * 2 + 1;
        rejects(
            aliased_primitives(1025, 1024, 0),
            DecodeCode::limit_exceeded,
            aliased_at + 1024 * 32 + 4,
            "primitives sharing one index array past the model's budget accepted"
        );
        rejects(
            aliased_primitives(1025, 0, 1024),
            DecodeCode::limit_exceeded,
            52 + 12,
            "primitives sharing one texture name past the model's budget accepted"
        );
        auto shared = chain_model(3, true);
        put32(shared, 44, 104);
        rejects(shared, DecodeCode::malformed, 104, "an object reached by two links accepted");
        auto deep_cycle = chain_model(1'000, true);
        put32(deep_cycle, 999 * 52 + 48, 52);
        rejects(deep_cycle, DecodeCode::cycle, 52, "a link back up a deep chain accepted");

        // A model built in memory may have links no loaded model has.
        oa::formats::objects3d::Model looped;
        looped.objects.resize(2);
        looped.objects[0].first_child = 1;
        looped.objects[1].next_sibling = 1;
        const auto looped_height = oa::formats::objects3d::maximum_height_fixed(looped);
        require(
            !looped_height.ok() && looped_height.error.code == DecodeCode::cycle,
            "a sibling link to itself was not reported as a cycle"
        );
        looped.objects[1].next_sibling = 7;
        require(
            oa::formats::objects3d::maximum_height_fixed(looped).error.code ==
                    DecodeCode::out_of_range &&
                !oa::formats::objects3d::derive_unit_type_bounds(looped, 1, 1).ok(),
            "an out-of-range link was not reported"
        );
        looped.objects[1].next_sibling = oa::formats::objects3d::kNoObject;
        looped.objects[1].parent = 1;
        require(
            !oa::formats::objects3d::flatten_for_render(looped).ok(),
            "a piece that is its own parent was flattened"
        );

        oa::formats::objects3d::Model height_edge;
        height_edge.objects.resize(2);
        height_edge.objects[0].first_child = 1;
        height_edge.objects[0].offset_from_parent.y = 100;
        height_edge.objects[1].parent = 0;
        height_edge.objects[1].vertices = {{0, -200, 0}};
        require(
            oa::formats::objects3d::maximum_height_fixed(height_edge).value == 100,
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
