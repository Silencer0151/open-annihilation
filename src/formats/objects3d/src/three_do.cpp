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
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

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
    int32_t version_signature;
    int32_t number_of_vertexes;
    int32_t number_of_primitives;
    int32_t offset_to_selection_prim;
    int32_t x_from_parent;
    int32_t y_from_parent;
    int32_t z_from_parent;
    int32_t offset_to_object_name;
    int32_t reserved;
    int32_t offset_to_vertex_array;
    int32_t offset_to_primitive_array;
    int32_t offset_to_sibling_object;
    int32_t offset_to_child_object;
};

struct VertexRecord {
    int32_t x;
    int32_t y;
    int32_t z;
};

struct PrimitiveRecord {
    int32_t color_index;
    int32_t number_of_vertex_indexes;
    int32_t reserved;
    int32_t offset_to_vertex_index_array;
    int32_t offset_to_texture_name;
    int32_t word_after_texture_name;
    int32_t word_before_is_colored;
    int32_t is_colored;
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

class Reader {
  public:

    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] uint64_t size() const { return bytes_.size(); }

    [[nodiscard]] uint32_t u32(uint64_t offset, std::string_view what) const {
        require(offset, sizeof(uint32_t), what);
        const auto* p = reinterpret_cast<const unsigned char*>(bytes_.data() + offset);
        uint32_t value = 0;
        for (unsigned byte = 0; byte < sizeof(uint32_t); ++byte)
            value |= static_cast<uint32_t>(p[byte]) << (8U * byte);
        return value;
    }

    [[nodiscard]] uint16_t u16(uint64_t offset, std::string_view what) const {
        require(offset, kVertexIndexBytes, what);
        const auto* p = reinterpret_cast<const unsigned char*>(bytes_.data() + offset);
        return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8U));
    }

    // Copies one on-disk record of little-endian int32 fields into host values.
    template <class Record>
    [[nodiscard]] Record read(uint64_t offset, std::string_view what) const {
        static_assert(std::is_trivially_copyable_v<Record>);
        static_assert(sizeof(Record) % sizeof(int32_t) == 0);
        require(offset, sizeof(Record), what);
        Record value{};
        auto* raw = reinterpret_cast<unsigned char*>(&value);
        for (uint64_t word = 0; word < sizeof(Record); word += sizeof(int32_t)) {
            const auto decoded = u32(offset + word, what);
            std::memcpy(raw + word, &decoded, sizeof(decoded));
        }
        return value;
    }

    [[nodiscard]] std::string string(uint32_t offset, std::string_view what) const {
        if (offset >= bytes_.size())
            fail(offset, what, "offset is outside file");
        const auto available = std::min<uint64_t>(bytes_.size() - offset, kMaxStringBytes);
        const auto* p = reinterpret_cast<const char*>(bytes_.data() + offset);
        for (uint64_t i = 0; i < available; ++i) {
            if (p[i] == '\0')
                return std::string(p, p + i);
        }
        fail(
            offset,
            what,
            available == kMaxStringBytes ? "exceeds string limit" : "is not NUL terminated"
        );
    }

    void object_header(uint32_t offset) const {
        // The first object begins at file offset zero. Unlike an array pointer,
        // that offset is an actual header location, not a null sentinel.
        require(offset, kObjectRecordBytes, "object header");
    }

    void array(uint32_t offset, uint32_t count, uint64_t stride, std::string_view what) const {
        if (count > kMaxElementsPerObject)
            fail(offset, what, "count exceeds safety limit");
        if (count != 0 && offset == 0)
            fail(offset, what, "non-empty array has null offset");
        if (count != 0 && stride > std::numeric_limits<uint64_t>::max() / count)
            fail(offset, what, "size overflows");
        require(offset, static_cast<uint64_t>(count) * stride, what);
    }

  private:

    [[noreturn]] static void fail(uint64_t offset, std::string_view what, std::string_view reason) {
        std::ostringstream out;
        out << "3DO " << what << " at 0x" << std::hex << offset << ": " << reason;
        throw ThreeDoError(out.str());
    }

    void require(uint64_t offset, uint64_t length, std::string_view what) const {
        if (offset > bytes_.size() || length > bytes_.size() - offset)
            fail(offset, what, "range is outside file");
    }

    std::span<const std::byte> bytes_;
};

ObjectRecord read_header(const Reader& r, uint32_t at) {
    r.object_header(at);
    return r.read<ObjectRecord>(at, "object header");
}

class Parser {
  public:

    explicit Parser(const Reader& reader) : r(reader) {}

    Model parse() {
        if (r.size() < kObjectRecordBytes)
            throw ThreeDoError("3DO root header is truncated");
        add_object(0, kNoObject);
        return std::move(model);
    }

  private:

    /// Reads the object record at `at` and, recursively, its children and siblings.
    uint32_t add_object(uint32_t at, uint32_t parent) {
        if (active.contains(at))
            error(at, "object hierarchy contains a cycle");
        if (offset_to_index.contains(at))
            error(at, "object is referenced more than once");
        if (model.objects.size() >= kMaxObjects)
            error(at, "object count exceeds safety limit");
        active.insert(at);
        const ObjectRecord h = read_header(r, at);
        if (h.number_of_vertexes < 0 || h.number_of_primitives < 0)
            error(at, "negative element count");
        if (h.version_signature != kVersionSignature)
            error(at, "unsupported version signature");
        const auto index = static_cast<uint32_t>(model.objects.size());
        offset_to_index.emplace(at, index);
        model.objects.emplace_back();
        auto& object = model.objects[index];
        object.version_signature = h.version_signature;
        object.selection_primitive = h.offset_to_selection_prim;
        object.offset_from_parent = {h.x_from_parent, h.y_from_parent, h.z_from_parent};
        object.parent = parent;
        const auto name_at = as_offset(h.offset_to_object_name);
        if (name_at != 0)
            object.name = r.string(name_at, "object name");

        const auto vertex_count = static_cast<uint32_t>(h.number_of_vertexes);
        const auto vertices_at = as_offset(h.offset_to_vertex_array);
        r.array(vertices_at, vertex_count, kVertexRecordBytes, "vertex array");
        object.vertices.reserve(vertex_count);
        for (uint32_t i = 0; i < vertex_count; ++i) {
            const uint64_t p = static_cast<uint64_t>(vertices_at) + i * kVertexRecordBytes;
            const VertexRecord vertex = r.read<VertexRecord>(p, "vertex");
            object.vertices.push_back({vertex.x, vertex.y, vertex.z});
        }

        const auto primitive_count = static_cast<uint32_t>(h.number_of_primitives);
        const auto primitives_at = as_offset(h.offset_to_primitive_array);
        r.array(primitives_at, primitive_count, kPrimitiveRecordBytes, "primitive array");
        object.primitives.reserve(primitive_count);
        for (uint32_t i = 0; i < primitive_count; ++i) {
            const uint64_t p = static_cast<uint64_t>(primitives_at) + i * kPrimitiveRecordBytes;
            const PrimitiveRecord raw = r.read<PrimitiveRecord>(p, "primitive");
            if (raw.number_of_vertex_indexes < 0) {
                error(
                    p + offsetof(PrimitiveRecord, number_of_vertex_indexes),
                    "negative primitive vertex count"
                );
            }
            const auto n = static_cast<uint32_t>(raw.number_of_vertex_indexes);
            const auto indices_at = as_offset(raw.offset_to_vertex_index_array);
            r.array(indices_at, n, kVertexIndexBytes, "vertex index array");
            Primitive primitive;
            primitive.color_index = raw.color_index;
            primitive.word_after_texture_name = raw.word_after_texture_name;
            primitive.word_before_is_colored = raw.word_before_is_colored;
            primitive.is_colored = raw.is_colored;
            const auto texture_at = as_offset(raw.offset_to_texture_name);
            if (texture_at != 0)
                primitive.texture_name = r.string(texture_at, "texture name");
            primitive.vertex_indices.reserve(n);
            for (uint32_t j = 0; j < n; ++j) {
                const auto index_at = static_cast<uint64_t>(indices_at) + j * kVertexIndexBytes;
                const auto vertex = r.u16(index_at, "vertex index");
                if (vertex >= vertex_count)
                    error(index_at, "vertex index is out of range");
                primitive.vertex_indices.push_back(vertex);
            }
            object.primitives.push_back(std::move(primitive));
        }
        const auto child_at = as_offset(h.offset_to_child_object);
        if (child_at != 0) {
            const auto child = add_object(child_at, index);
            model.objects[index].first_child = child;
        }
        const auto sibling_at = as_offset(h.offset_to_sibling_object);
        if (sibling_at != 0) {
            const auto sibling = add_object(sibling_at, parent);
            model.objects[index].next_sibling = sibling;
        }
        active.erase(at);
        return index;
    }

    [[noreturn]] static void error(uint64_t at, std::string_view text) {
        std::ostringstream out;
        out << "3DO object at 0x" << std::hex << at << ": " << text;
        throw ThreeDoError(out.str());
    }

    const Reader& r;
    Model model;
    std::unordered_map<uint32_t, uint32_t> offset_to_index;
    std::unordered_set<uint32_t> active;
};

int32_t checked_add(int32_t a, int32_t b) {
    const auto sum = static_cast<int64_t>(a) + b;
    if (sum < std::numeric_limits<int32_t>::min() || sum > std::numeric_limits<int32_t>::max())
        throw ThreeDoError("3DO accumulated piece offset overflows int32");
    return static_cast<int32_t>(sum);
}

int32_t checked_negate(int32_t value) {
    if (value == std::numeric_limits<int32_t>::min())
        throw ThreeDoError("3DO X/Z coordinate cannot be negated in the game's runtime space");
    return -value;
}

int32_t wrapping_negate(int32_t value) {
    return std::bit_cast<int32_t>(0U - std::bit_cast<uint32_t>(value));
}

int32_t signed_high_word(int32_t value) {
    return static_cast<int16_t>(std::bit_cast<uint32_t>(value) >> 16U);
}

} // namespace

Model load_3do(std::span<const std::byte> bytes) {
    return Parser(Reader(bytes)).parse();
}

int32_t maximum_height_fixed(const Model& model) {
    if (model.objects.empty())
        return 0;
    std::vector<uint8_t> active(model.objects.size());
    const auto wrapping_add = [](int32_t a, int32_t b) {
        return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) + std::bit_cast<uint32_t>(b));
    };
    const auto visit = [&](const auto& self, uint32_t first) -> int32_t {
        int32_t maximum = 0;
        std::vector<uint32_t> chain;
        for (auto index = first; index != kNoObject;) {
            if (index >= model.objects.size())
                throw ThreeDoError("3DO height traversal has an out-of-range object link");
            if (active[index] != 0)
                throw ThreeDoError("3DO height traversal contains a cycle");
            active[index] = 1;
            chain.push_back(index);
            const auto& object = model.objects[index];
            for (const auto& vertex : object.vertices) {
                const auto candidate = wrapping_add(vertex.y, object.offset_from_parent.y);
                if (maximum < candidate)
                    maximum = candidate;
            }
            if (object.first_child != kNoObject) {
                const auto candidate =
                    wrapping_add(self(self, object.first_child), object.offset_from_parent.y);
                if (maximum < candidate)
                    maximum = candidate;
            }
            index = object.next_sibling;
        }
        for (const auto index : chain)
            active[index] = 0;
        return maximum;
    };
    return visit(visit, 0);
}

UnitTypeBounds
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
    const auto maximum_y = maximum_height_fixed(model);
    return {x[0], 0, z[0], x[1], maximum_y, z[1], x[2], maximum_y, z[2]};
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

std::vector<RenderPrimitive> flatten_for_render(const Model& model) {
    std::vector<RenderPrimitive> result;
    std::vector<FixedVector3> origins(model.objects.size());
    for (uint32_t oi = 0; oi < model.objects.size(); ++oi) {
        const auto& object = model.objects[oi];
        FixedVector3 parent{};
        if (object.parent != kNoObject) {
            if (object.parent >= oi || object.parent >= model.objects.size())
                throw ThreeDoError("3DO model has invalid object topology");
            parent = origins[object.parent];
        }
        // The loader negates stored X and Z for both vertices and piece offsets.
        origins[oi] = {
            checked_add(parent.x, checked_negate(object.offset_from_parent.x)),
            checked_add(parent.y, object.offset_from_parent.y),
            checked_add(parent.z, checked_negate(object.offset_from_parent.z))
        };
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
                    throw ThreeDoError("3DO model has invalid vertex index");
                const auto& v = object.vertices[vi];
                flat.vertices.push_back(
                    {checked_add(origins[oi].x, checked_negate(v.x)),
                     checked_add(origins[oi].y, v.y),
                     checked_add(origins[oi].z, checked_negate(v.z))}
                );
            }
            result.push_back(std::move(flat));
        }
    }
    return result;
}

} // namespace oa::formats::objects3d
