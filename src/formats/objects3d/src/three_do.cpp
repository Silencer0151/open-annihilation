// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/objects3d.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace oa::formats::objects3d {
namespace {

// Little-endian on-disk records: the object, vertex and primitive records in
// file order. The reserved words are zero in every shipped model; they are
// decoded but not used.
constexpr int32_t kVersionSignature = 1;
constexpr uint64_t kObjectFieldCount = 13;
constexpr uint64_t kPrimitiveFieldCount = 8;
constexpr uint64_t kVertexFieldCount = 3;
constexpr uint64_t kObjectRecordBytes = kObjectFieldCount * sizeof(int32_t);
constexpr uint64_t kPrimitiveRecordBytes = kPrimitiveFieldCount * sizeof(int32_t);
constexpr uint64_t kVertexRecordBytes = kVertexFieldCount * sizeof(int32_t);
constexpr uint64_t kVertexIndexBytes = sizeof(uint16_t);
constexpr uint32_t kMaxObjects = 65'536;
constexpr uint32_t kMaxElementsPerObject = 1'000'000;
constexpr uint32_t kMaxStringBytes = 4'096;

struct ObjectRecord {
    int32_t version_signature{};
    int32_t number_of_vertexes{};
    int32_t number_of_primitives{};
    int32_t offset_to_selection_prim{};
    int32_t x_from_parent{};
    int32_t y_from_parent{};
    int32_t z_from_parent{};
    int32_t offset_to_object_name{};
    int32_t reserved{};
    int32_t offset_to_vertex_array{};
    int32_t offset_to_primitive_array{};
    int32_t offset_to_sibling_object{};
    int32_t offset_to_child_object{};
};

struct VertexRecord {
    int32_t x{};
    int32_t y{};
    int32_t z{};
};

struct PrimitiveRecord {
    int32_t color_index{};
    int32_t number_of_vertex_indexes{};
    int32_t reserved{};
    int32_t offset_to_vertex_index_array{};
    int32_t offset_to_texture_name{};
    int32_t word_after_texture_name{};
    int32_t word_before_is_colored{};
    int32_t is_colored{};
};

static_assert(sizeof(ObjectRecord) == kObjectRecordBytes);
static_assert(sizeof(VertexRecord) == kVertexRecordBytes);
static_assert(sizeof(PrimitiveRecord) == kPrimitiveRecordBytes);
static_assert(std::is_standard_layout_v<ObjectRecord>);
static_assert(std::is_standard_layout_v<VertexRecord>);
static_assert(std::is_standard_layout_v<PrimitiveRecord>);
static_assert(offsetof(ObjectRecord, reserved) == 32);
static_assert(offsetof(ObjectRecord, offset_to_vertex_array) == 36);
static_assert(offsetof(ObjectRecord, offset_to_child_object) == 48);
static_assert(offsetof(VertexRecord, z) == 8);
static_assert(offsetof(PrimitiveRecord, number_of_vertex_indexes) == 4);
static_assert(offsetof(PrimitiveRecord, reserved) == 8);
static_assert(offsetof(PrimitiveRecord, offset_to_vertex_index_array) == 12);
static_assert(offsetof(PrimitiveRecord, word_after_texture_name) == 20);
static_assert(offsetof(PrimitiveRecord, word_before_is_colored) == 24);
static_assert(offsetof(PrimitiveRecord, is_colored) == 28);

[[nodiscard]] constexpr uint32_t as_offset(int32_t stored) noexcept {
    return static_cast<uint32_t>(stored);
}

using base::bytes::ByteReader;
using base::bytes::DecodeCode;
using base::bytes::DecodeError;
using base::bytes::Decoded;

/// Reads one on-disk record of little-endian int32 fields into host values.
///
/// @param[in,out] reader the file; a record past its end fails the reader
/// @param offset file offset of the record
/// @return the record, zeroed on failure
template <class Record>
[[nodiscard]] Record read_record(ByteReader& reader, uint64_t offset) {
    static_assert(std::is_trivially_copyable_v<Record>);
    static_assert(sizeof(Record) % sizeof(int32_t) == 0);
    Record value{};
    if (!reader.fits(offset, sizeof(Record))) {
        reader.fail(DecodeCode::truncated, offset, "3DO record lies past the end of the file");
        return value;
    }
    auto* raw = reinterpret_cast<unsigned char*>(&value);
    for (uint64_t word = 0; word < sizeof(Record); word += sizeof(int32_t)) {
        const auto decoded = reader.i32_at(offset + word);
        std::memcpy(raw + word, &decoded, sizeof(decoded));
    }
    return value;
}

/// Checks that an array of `count` records of `stride` bytes lies in the file.
///
/// @param[in,out] reader the file; a bad array fails the reader
/// @param offset file offset of the array
/// @param count records in the array
/// @param stride bytes per record
/// @return true when the array is within the limits and the file
bool check_array(ByteReader& reader, uint32_t offset, uint32_t count, uint64_t stride) {
    if (count > kMaxElementsPerObject)
        return reader.fail(
            DecodeCode::limit_exceeded, offset, "3DO array count exceeds the safety limit"
        );
    if (count != 0 && offset == 0)
        return reader.fail(DecodeCode::malformed, offset, "3DO non-empty array has a null offset");
    if (!reader.fits(offset, static_cast<uint64_t>(count) * stride))
        return reader.fail(
            DecodeCode::truncated, offset, "3DO array lies past the end of the file"
        );
    return true;
}

/// Reads an object or texture name.
///
/// @param[in,out] reader the file; a bad name fails the reader
/// @param offset file offset of the name
/// @return the name without its NUL
std::string read_name(ByteReader& reader, uint32_t offset) {
    return std::string(reader.c_string_at(offset, kMaxStringBytes - 1));
}

class Parser {
  public:

    explicit Parser(std::span<const std::byte> bytes) : r(bytes) {}

    Decoded<Model> parse() {
        if (r.size() < kObjectRecordBytes)
            return DecodeError{DecodeCode::truncated, 0, "3DO root header is truncated"};
        walk();
        if (!r.ok())
            return r.error();
        return std::move(model);
    }

  private:

    /// One link still to follow: the object record it names and the slot
    /// that receives the object's index.
    struct PendingObject {
        uint32_t at{};
        uint32_t parent{kNoObject};
        uint32_t reached_from{kNoObject}; ///< object whose child or sibling link names it
        bool is_child{};                  ///< a child link of reached_from, else a sibling link
    };

    /// Reads every object reachable from the root, numbering them in
    /// preorder: an object, then its child's subtree, then its sibling's.
    ///
    /// Follows the links with an explicit stack, so a long sibling list or a
    /// deep child chain needs no deeper call stack than a single object.
    /// Stops at the first error, which the reader keeps.
    void walk() {
        std::vector<PendingObject> pending{{0, kNoObject, kNoObject, false}};
        while (!pending.empty()) {
            const PendingObject next = pending.back();
            pending.pop_back();
            const auto index = add_object(next);
            if (!r.ok())
                return;
            if (next.reached_from != kNoObject) {
                auto& from = model.objects[next.reached_from];
                (next.is_child ? from.first_child : from.next_sibling) = index;
            }
            const auto& object = model.objects[index];
            const auto& links = links_[index];
            // The sibling goes on the stack first, so the child's subtree is
            // numbered before it.
            if (links.sibling_at != 0)
                pending.push_back({links.sibling_at, object.parent, index, false});
            if (links.child_at != 0)
                pending.push_back({links.child_at, index, index, true});
        }
    }

    /// Returns whether the object at `at`, already read, is one of the
    /// objects whose links lead to `from`, so that following it again would
    /// loop.
    [[nodiscard]] bool leads_to(uint32_t at, uint32_t from) const {
        const auto found = offset_to_index.find(at);
        for (auto index = from; index != kNoObject; index = reached_from_[index]) {
            if (index == found->second)
                return true;
        }
        return false;
    }

    /// Reads the object record a pending link names, with its vertices and
    /// primitives, and records its own links.
    ///
    /// @return the object's index; on failure the reader holds the error
    uint32_t add_object(const PendingObject& next) {
        const auto at = next.at;
        if (offset_to_index.contains(at)) {
            if (leads_to(at, next.reached_from))
                r.fail(DecodeCode::cycle, at, "3DO object hierarchy contains a cycle");
            else
                r.fail(DecodeCode::malformed, at, "3DO object is referenced more than once");
            return kNoObject;
        }
        if (model.objects.size() >= kMaxObjects) {
            r.fail(DecodeCode::limit_exceeded, at, "3DO object count exceeds the safety limit");
            return kNoObject;
        }
        const auto parent = next.parent;
        // The first object begins at file offset zero: unlike an array
        // offset, zero is a real header location, not a null.
        const ObjectRecord h = read_record<ObjectRecord>(r, at);
        if (!r.ok())
            return kNoObject;
        if (h.number_of_vertexes < 0 || h.number_of_primitives < 0) {
            r.fail(DecodeCode::malformed, at, "3DO object has a negative element count");
            return kNoObject;
        }
        if (h.version_signature != kVersionSignature) {
            r.fail(DecodeCode::unsupported_version, at, "3DO object has an unsupported version");
            return kNoObject;
        }
        const auto index = static_cast<uint32_t>(model.objects.size());
        offset_to_index.emplace(at, index);
        reached_from_.push_back(next.reached_from);
        links_.push_back(
            {as_offset(h.offset_to_sibling_object), as_offset(h.offset_to_child_object)}
        );
        model.objects.emplace_back();
        auto& object = model.objects[index];
        object.version_signature = h.version_signature;
        object.selection_primitive = h.offset_to_selection_prim;
        object.offset_from_parent = {h.x_from_parent, h.y_from_parent, h.z_from_parent};
        object.parent = parent;
        const auto name_at = as_offset(h.offset_to_object_name);
        if (name_at != 0)
            object.name = read_name(r, name_at);

        const auto vertex_count = static_cast<uint32_t>(h.number_of_vertexes);
        const auto vertices_at = as_offset(h.offset_to_vertex_array);
        if (!check_array(r, vertices_at, vertex_count, kVertexRecordBytes))
            return kNoObject;
        object.vertices.reserve(vertex_count);
        for (uint32_t i = 0; i < vertex_count; ++i) {
            const uint64_t p = static_cast<uint64_t>(vertices_at) + i * kVertexRecordBytes;
            const VertexRecord vertex = read_record<VertexRecord>(r, p);
            object.vertices.push_back({vertex.x, vertex.y, vertex.z});
        }

        const auto primitive_count = static_cast<uint32_t>(h.number_of_primitives);
        const auto primitives_at = as_offset(h.offset_to_primitive_array);
        if (!check_array(r, primitives_at, primitive_count, kPrimitiveRecordBytes))
            return kNoObject;
        object.primitives.reserve(primitive_count);
        for (uint32_t i = 0; i < primitive_count; ++i) {
            const uint64_t p = static_cast<uint64_t>(primitives_at) + i * kPrimitiveRecordBytes;
            const PrimitiveRecord raw = read_record<PrimitiveRecord>(r, p);
            if (raw.number_of_vertex_indexes < 0) {
                r.fail(
                    DecodeCode::malformed,
                    p + offsetof(PrimitiveRecord, number_of_vertex_indexes),
                    "3DO primitive has a negative vertex count"
                );
                return kNoObject;
            }
            const auto n = static_cast<uint32_t>(raw.number_of_vertex_indexes);
            const auto indices_at = as_offset(raw.offset_to_vertex_index_array);
            if (!check_array(r, indices_at, n, kVertexIndexBytes))
                return kNoObject;
            Primitive primitive;
            primitive.color_index = raw.color_index;
            primitive.word_after_texture_name = raw.word_after_texture_name;
            primitive.word_before_is_colored = raw.word_before_is_colored;
            primitive.is_colored = raw.is_colored;
            const auto texture_at = as_offset(raw.offset_to_texture_name);
            if (texture_at != 0)
                primitive.texture_name = read_name(r, texture_at);
            primitive.vertex_indices.reserve(n);
            for (uint32_t j = 0; j < n; ++j) {
                const auto index_at = static_cast<uint64_t>(indices_at) + j * kVertexIndexBytes;
                const auto vertex = r.u16_at(index_at);
                if (vertex >= vertex_count) {
                    r.fail(DecodeCode::out_of_range, index_at, "3DO vertex index is out of range");
                    return kNoObject;
                }
                primitive.vertex_indices.push_back(vertex);
            }
            object.primitives.push_back(std::move(primitive));
        }
        return r.ok() ? index : kNoObject;
    }

    /// The two links an object record holds, as file offsets; zero for none.
    struct ObjectLinks {
        uint32_t sibling_at{};
        uint32_t child_at{};
    };

    ByteReader r;
    Model model;
    std::unordered_map<uint32_t, uint32_t> offset_to_index;
    std::vector<uint32_t> reached_from_; ///< per object, the object whose link named it
    std::vector<ObjectLinks> links_;     ///< per object, its links
};

/// Adds two coordinates, failing when the sum leaves int32.
///
/// @param a first addend
/// @param b second addend
/// @param[out] sum the sum
/// @return true when it fits
bool checked_add(int32_t a, int32_t b, int32_t& sum) {
    const auto wide = static_cast<int64_t>(a) + b;
    if (wide < std::numeric_limits<int32_t>::min() || wide > std::numeric_limits<int32_t>::max())
        return false;
    sum = static_cast<int32_t>(wide);
    return true;
}

int32_t wrapping_negate(int32_t value) {
    return std::bit_cast<int32_t>(0U - std::bit_cast<uint32_t>(value));
}

int32_t signed_high_word(int32_t value) {
    return static_cast<int16_t>(std::bit_cast<uint32_t>(value) >> 16U);
}

} // namespace

Decoded<Model> load_3do(std::span<const std::byte> bytes) {
    return Parser(bytes).parse();
}

Decoded<int32_t> maximum_height_fixed(const Model& model) {
    if (model.objects.empty())
        return 0;
    const auto wrapping_add = [](int32_t a, int32_t b) {
        return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) + std::bit_cast<uint32_t>(b));
    };

    // One level of the walk: a sibling list, the object of it being visited
    // and the highest Y found on the level so far. The objects of every
    // level still open are marked active, so a link back to one is a cycle.
    struct Level {
        uint32_t index{};
        int32_t maximum{};
        std::size_t chain_begin{}; ///< where this level's objects start in `chain`
    };

    std::vector<uint8_t> active(model.objects.size());
    std::vector<uint32_t> chain;
    std::vector<Level> levels{{0, 0, 0}};
    while (true) {
        auto& level = levels.back();
        if (level.index == kNoObject) {
            for (auto at = level.chain_begin; at < chain.size(); ++at)
                active[chain[at]] = 0;
            chain.resize(level.chain_begin);
            const auto child_maximum = level.maximum;
            levels.pop_back();
            if (levels.empty())
                return child_maximum;
            auto& above = levels.back();
            const auto& object = model.objects[above.index];
            const auto candidate = wrapping_add(child_maximum, object.offset_from_parent.y);
            if (above.maximum < candidate)
                above.maximum = candidate;
            above.index = object.next_sibling;
            continue;
        }
        const auto index = level.index;
        if (index >= model.objects.size())
            return DecodeError{
                DecodeCode::out_of_range, index, "3DO height traversal has an out-of-range link"
            };
        if (active[index] != 0)
            return DecodeError{DecodeCode::cycle, index, "3DO height traversal contains a cycle"};
        active[index] = 1;
        chain.push_back(index);
        const auto& object = model.objects[index];
        for (const auto& vertex : object.vertices) {
            const auto candidate = wrapping_add(vertex.y, object.offset_from_parent.y);
            if (level.maximum < candidate)
                level.maximum = candidate;
        }
        if (object.first_child != kNoObject)
            levels.push_back({object.first_child, 0, chain.size()});
        else
            level.index = object.next_sibling;
    }
}

Decoded<UnitTypeBounds>
derive_unit_type_bounds(const Model& model, int16_t footprint_x, int16_t footprint_z) {
    constexpr int32_t kFootprintCellFixed = 0x0010'0000;
    const auto multiply_wrapping = [](int32_t a, int32_t b) {
        const auto product =
            static_cast<uint64_t>(std::bit_cast<uint32_t>(a)) * std::bit_cast<uint32_t>(b);
        return std::bit_cast<int32_t>(static_cast<uint32_t>(product));
    };
    const auto subtract_wrapping = [](int32_t a, int32_t b) {
        return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) - std::bit_cast<uint32_t>(b));
    };
    const auto axis = [&](int16_t footprint) {
        const auto value = static_cast<int32_t>(footprint);
        const auto minimum = multiply_wrapping(value, -kFootprintCellFixed) / 2;
        const auto maximum = multiply_wrapping(value, kFootprintCellFixed) / 2;
        return std::array{minimum, maximum, subtract_wrapping(maximum, minimum)};
    };
    const auto x = axis(footprint_x);
    const auto z = axis(footprint_z);
    const auto height = maximum_height_fixed(model);
    if (!height.ok())
        return height.error;
    const auto maximum_y = *height.value;
    return UnitTypeBounds{x[0], 0, z[0], x[1], maximum_y, z[1], x[2], maximum_y, z[2]};
}

ObjectBounds object_bounds(const Object& object) noexcept {
    const auto add = [](int32_t a, int32_t b) {
        return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) + std::bit_cast<uint32_t>(b));
    };
    ObjectBounds bounds;
    if (object.vertices.size() <= 2)
        return bounds;
    const FixedVector3 offset{
        wrapping_negate(object.offset_from_parent.x),
        object.offset_from_parent.y,
        wrapping_negate(object.offset_from_parent.z)
    };
    for (const auto& vertex : object.vertices) {
        const FixedVector3 point{
            add(wrapping_negate(vertex.x), offset.x),
            add(vertex.y, offset.y),
            add(wrapping_negate(vertex.z), offset.z)
        };
        bounds.maximum.x = std::max(bounds.maximum.x, point.x);
        bounds.minimum.x = std::min(bounds.minimum.x, point.x);
        bounds.maximum.y = std::max(bounds.maximum.y, point.y);
        bounds.minimum.y = std::min(bounds.minimum.y, point.y);
        bounds.maximum.z = std::max(bounds.maximum.z, point.z);
        bounds.minimum.z = std::min(bounds.minimum.z, point.z);
    }
    return bounds;
}

ShadowBitmapExtent shadow_bitmap_extent(const Model& model) {
    int32_t minimum_x = 0;
    int32_t maximum_x = 0;
    int32_t minimum_y = 0;
    int32_t maximum_y = 0;
    for (auto index = model.objects.size(); index > 0;) {
        --index;
        const auto& object = model.objects[index];
        // The game's piece visible bit is set exactly when the object record's
        // vertex count, the size of this vertex list, is at least 3.
        if (object.vertices.size() < 3)
            continue;
        for (const auto& vertex : object.vertices) {
            const auto runtime_x = wrapping_negate(vertex.x);
            const auto runtime_z = wrapping_negate(vertex.z);
            const auto y_quarter = signed_high_word(vertex.y) >> 2;
            const auto projected_x = signed_high_word(runtime_x) + y_quarter;
            const auto projected_y = signed_high_word(wrapping_negate(runtime_z)) - y_quarter;
            if (projected_x < minimum_x)
                minimum_x = projected_x;
            if (maximum_x < projected_x)
                maximum_x = projected_x;
            if (projected_y < minimum_y)
                minimum_y = projected_y;
            if (maximum_y < projected_y)
                maximum_y = projected_y;
        }
    }
    return {
        (maximum_x - (minimum_x + -2)) + 2,
        (maximum_y - (minimum_y + -2)) + 2,
        -(minimum_x + -2),
        -(minimum_y + -2)
    };
}

Decoded<std::vector<RenderPrimitive>> flatten_for_render(const Model& model) {
    const DecodeError overflow{
        DecodeCode::out_of_range, 0, "3DO accumulated coordinate overflows int32"
    };
    // The loader negates stored X and Z for both vertices and piece offsets;
    // the most negative int32 has no negation.
    const auto negate = [](int32_t value, int32_t& negated) {
        if (value == std::numeric_limits<int32_t>::min())
            return false;
        negated = -value;
        return true;
    };
    std::vector<RenderPrimitive> result;
    std::vector<FixedVector3> origins(model.objects.size());
    for (uint32_t oi = 0; oi < model.objects.size(); ++oi) {
        const auto& object = model.objects[oi];
        FixedVector3 parent{};
        if (object.parent != kNoObject) {
            if (object.parent >= oi || object.parent >= model.objects.size())
                return DecodeError{
                    DecodeCode::out_of_range, oi, "3DO model has invalid object topology"
                };
            parent = origins[object.parent];
        }
        int32_t offset_x{};
        int32_t offset_z{};
        if (!negate(object.offset_from_parent.x, offset_x) ||
            !negate(object.offset_from_parent.z, offset_z) ||
            !checked_add(parent.x, offset_x, origins[oi].x) ||
            !checked_add(parent.y, object.offset_from_parent.y, origins[oi].y) ||
            !checked_add(parent.z, offset_z, origins[oi].z))
            return overflow;
        for (uint32_t pi = 0; pi < object.primitives.size(); ++pi) {
            const auto& primitive = object.primitives[pi];
            RenderPrimitive flat{
                oi,
                pi,
                primitive.color_index,
                primitive.is_colored,
                primitive.texture_name,
                {},
                object.selection_primitive == static_cast<int32_t>(pi)
            };
            flat.vertices.reserve(primitive.vertex_indices.size());
            for (const auto vi : primitive.vertex_indices) {
                if (vi >= object.vertices.size())
                    return DecodeError{
                        DecodeCode::out_of_range, oi, "3DO model has an invalid vertex index"
                    };
                const auto& v = object.vertices[vi];
                int32_t x{};
                int32_t z{};
                FixedVector3 point{};
                if (!negate(v.x, x) || !negate(v.z, z) || !checked_add(origins[oi].x, x, point.x) ||
                    !checked_add(origins[oi].y, v.y, point.y) ||
                    !checked_add(origins[oi].z, z, point.z))
                    return overflow;
                flat.vertices.push_back(point);
            }
            result.push_back(std::move(flat));
        }
    }
    return result;
}

} // namespace oa::formats::objects3d
