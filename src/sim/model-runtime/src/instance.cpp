// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/model_runtime/instance.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace oa::sim::model_runtime {
namespace {

// Radians per angle word: the game's stored double, 2*pi/65536 to 13 digits.
constexpr double kRadiansPerAngleWord = 9.587379924285e-05;

bool equal_fold_ascii(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto ac = static_cast<unsigned char>(a[i]);
        auto bc = static_cast<unsigned char>(b[i]);
        if (ac >= 'A' && ac <= 'Z')
            ac = static_cast<unsigned char>(ac + ('a' - 'A'));
        if (bc >= 'A' && bc <= 'Z')
            bc = static_cast<unsigned char>(bc + ('a' - 'A'));
        if (ac != bc)
            return false;
    }
    return true;
}

int32_t checked(int64_t value, const char* operation) {
    if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max())
        throw std::overflow_error(operation);
    return static_cast<int32_t>(value);
}

void rotate_pair(int32_t& a, int32_t& b, int16_t angle) {
    if (angle == 0)
        return;
    const auto radians = static_cast<double>(angle) * kRadiansPerAngleWord;
    const auto cosine = std::cos(radians);
    const auto sine = std::sin(radians);
    const auto old_a = a;
    const auto old_b = b;
    // Each coordinate rounds to the nearest integer, ties to even.
    a = checked(std::llrint(cosine * old_a - sine * old_b), "3DO rotation overflow");
    b = checked(std::llrint(sine * old_a + cosine * old_b), "3DO rotation overflow");
}

void rotate(oa::formats::objects3d::FixedVector3& value, RotationWords words) {
    // The game's pair order and aliasing.
    rotate_pair(value.x, value.y, words.xy);
    rotate_pair(value.y, value.z, words.yz);
    rotate_pair(value.x, value.z, words.xz);
}

int32_t negate(int32_t value) {
    if (value == std::numeric_limits<int32_t>::min())
        throw std::overflow_error("3DO coordinate negation overflow");
    return -value;
}

} // namespace

oa::formats::objects3d::FixedVector3
rotate_vector(oa::formats::objects3d::FixedVector3 value, RotationWords words) {
    rotate(value, words);
    return value;
}

uint32_t count_linked_objects(const oa::formats::objects3d::Model& model, uint32_t object_index) {
    if (object_index >= model.objects.size())
        throw std::out_of_range("3DO object index is out of range");
    const auto& object = model.objects[object_index];
    // The count starts at 1, becomes child_count + 1 when the child link is
    // present, then adds the sibling chain.
    uint32_t count = 1;
    if (object.first_child != oa::formats::objects3d::kNoObject)
        count = count_linked_objects(model, object.first_child) + 1;
    if (object.next_sibling != oa::formats::objects3d::kNoObject)
        count += count_linked_objects(model, object.next_sibling);
    return count;
}

Instance make_instance(
    std::shared_ptr<const oa::formats::objects3d::Model> model,
    uintptr_t owner_token,
    std::span<const std::string_view> script_piece_names
) {
    if (!model || model->objects.empty())
        throw std::invalid_argument("3DO model is empty");

    // Public callers may construct Models without going through load_3do.
    const auto count = model->objects.size();
    auto valid_link = [count](uint32_t link) { return link == kNoPiece || link < count; };
    for (std::size_t i = 0; i < count; ++i) {
        const auto& object = model->objects[i];
        if (!valid_link(object.parent) || !valid_link(object.first_child) ||
            !valid_link(object.next_sibling))
            throw std::invalid_argument("3DO model contains an out-of-range hierarchy link");
        if (object.parent != kNoPiece && object.parent >= i)
            throw std::invalid_argument("3DO model hierarchy is not parent-before-child preorder");
    }

    struct Pending {
        uint32_t object;
        uint32_t expected_parent;
    };

    std::vector<Pending> pending{{0, kNoPiece}};
    std::vector<uint8_t> topology_seen(count);
    while (!pending.empty()) {
        const auto [current, expected_parent] = pending.back();
        pending.pop_back();
        if (topology_seen[current] != 0)
            throw std::invalid_argument("3DO model hierarchy contains a cycle or duplicate link");
        topology_seen[current] = 1;
        const auto& object = model->objects[current];
        if (object.parent != expected_parent)
            throw std::invalid_argument(
                "3DO model parent link disagrees with child/sibling topology"
            );
        if (object.next_sibling != kNoPiece)
            pending.push_back({object.next_sibling, expected_parent});
        if (object.first_child != kNoPiece)
            pending.push_back({object.first_child, current});
    }
    if (std::find(topology_seen.begin(), topology_seen.end(), 0) != topology_seen.end())
        throw std::invalid_argument("3DO model contains an unreachable object");

    Instance instance;
    instance.model_ = std::move(model);
    instance.owner_token_ = owner_token;
    instance.pieces_.reserve(instance.model_->objects.size());
    for (uint32_t oi = 0; oi < instance.model_->objects.size(); ++oi) {
        const auto& object = instance.model_->objects[oi];
        PieceState piece;
        piece.object_index = oi;
        piece.flags =
            static_cast<uint16_t>(PieceFlag::cached) | static_cast<uint16_t>(PieceFlag::shaded);
        if (object.vertices.size() >= 3)
            piece.flags |= static_cast<uint16_t>(PieceFlag::visible);
        piece.transformed_vertices.reserve(object.vertices.size());
        for (const auto& vertex : object.vertices)
            piece.transformed_vertices.push_back({negate(vertex.x), vertex.y, negate(vertex.z)});
        instance.pieces_.push_back(std::move(piece));
    }

    // Binding scans only from the current script slot onward and swaps the
    // whole piece state, making the first N entries use COB piece order.
    for (std::size_t script_index = 0; script_index < script_piece_names.size(); ++script_index) {
        if (script_index >= instance.pieces_.size())
            continue;
        for (std::size_t candidate = script_index; candidate < instance.pieces_.size();
             ++candidate) {
            const auto object_index = instance.pieces_[candidate].object_index;
            if (equal_fold_ascii(
                    script_piece_names[script_index], instance.model_->objects[object_index].name
                )) {
                if (candidate != script_index)
                    std::swap(instance.pieces_[candidate], instance.pieces_[script_index]);
                break;
            }
        }
    }

    // Links are rebuilt after the reorder by matching model objects.
    std::vector<uint32_t> piece_for_object(instance.model_->objects.size(), kNoPiece);
    for (uint32_t pi = 0; pi < instance.pieces_.size(); ++pi)
        piece_for_object[instance.pieces_[pi].object_index] = pi;
    for (auto& piece : instance.pieces_) {
        const auto& object = instance.model_->objects[piece.object_index];
        if (object.parent != oa::formats::objects3d::kNoObject)
            piece.parent = piece_for_object[object.parent];
        if (object.first_child != oa::formats::objects3d::kNoObject)
            piece.first_child = piece_for_object[object.first_child];
        if (object.next_sibling != oa::formats::objects3d::kNoObject)
            piece.next_sibling = piece_for_object[object.next_sibling];
    }
    instance.root_piece_ = piece_for_object[0];
    return instance;
}

PieceState& Instance::piece_for_script_index(uint32_t index) {
    if (index >= pieces_.size())
        throw std::out_of_range("COB piece index is out of range");
    return pieces_[index];
}

const PieceState& Instance::piece_for_script_index(uint32_t index) const {
    if (index >= pieces_.size())
        throw std::out_of_range("COB piece index is out of range");
    return pieces_[index];
}

oa::formats::objects3d::FixedVector3
Instance::attachment_position(uint32_t piece_index, RotationWords root_rotation) const {
    if (piece_index >= pieces_.size())
        throw std::out_of_range("3DO piece index is out of range");
    oa::formats::objects3d::FixedVector3 result{};
    auto current = piece_index;
    while (current != kNoPiece) {
        const auto& piece = pieces_[current];
        const auto& object = model_->objects[piece.object_index];
        result.x = checked(
            static_cast<int64_t>(result.x) + negate(object.offset_from_parent.x) +
                piece.translation.x,
            "3DO attachment X overflow"
        );
        result.y = checked(
            static_cast<int64_t>(result.y) + object.offset_from_parent.y + piece.translation.y,
            "3DO attachment Y overflow"
        );
        result.z = checked(
            static_cast<int64_t>(result.z) + negate(object.offset_from_parent.z) +
                piece.translation.z,
            "3DO attachment Z overflow"
        );
        if (piece.parent == kNoPiece)
            break;
        auto rotation = pieces_[piece.parent].rotation;
        if (pieces_[piece.parent].parent == kNoPiece) {
            rotation.xy = static_cast<int16_t>(rotation.xy + root_rotation.xy);
            rotation.xz = static_cast<int16_t>(rotation.xz + root_rotation.xz);
            rotation.yz = static_cast<int16_t>(rotation.yz + root_rotation.yz);
        }
        rotate(result, rotation);
        current = piece.parent;
    }
    result.z = negate(result.z);
    return result;
}

void Instance::rebuild_transforms(RotationWords root_rotation) {
    for (auto& piece : pieces_) {
        const auto& object = model_->objects[piece.object_index];
        piece.transformed_vertices.clear();
        piece.transformed_vertices.reserve(object.vertices.size());
        for (const auto& vertex : object.vertices)
            piece.transformed_vertices.push_back({negate(vertex.x), vertex.y, negate(vertex.z)});
        piece.transformed_origin = {};

        auto transform_by = [&](uint32_t transform_piece, bool root) {
            const auto& state = pieces_[transform_piece];
            const auto& source = model_->objects[state.object_index];
            auto rotation = state.rotation;
            if (root) {
                rotation.xy = static_cast<int16_t>(rotation.xy + root_rotation.xy);
                rotation.xz = static_cast<int16_t>(rotation.xz + root_rotation.xz);
                rotation.yz = static_cast<int16_t>(rotation.yz + root_rotation.yz);
            }
            auto apply = [&](oa::formats::objects3d::FixedVector3& point) {
                rotate(point, rotation);
                point.x = checked(
                    static_cast<int64_t>(point.x) + negate(source.offset_from_parent.x) +
                        state.translation.x,
                    "3DO transformed X overflow"
                );
                point.y = checked(
                    static_cast<int64_t>(point.y) + source.offset_from_parent.y +
                        state.translation.y,
                    "3DO transformed Y overflow"
                );
                point.z = checked(
                    static_cast<int64_t>(point.z) + negate(source.offset_from_parent.z) +
                        state.translation.z,
                    "3DO transformed Z overflow"
                );
            };
            apply(piece.transformed_origin);
            for (auto& vertex : piece.transformed_vertices)
                apply(vertex);
        };

        auto current = static_cast<uint32_t>(&piece - pieces_.data());
        auto top = current;
        while (pieces_[top].parent != kNoPiece)
            top = pieces_[top].parent;
        // The transform walk starts at the instance root. Root-level siblings
        // are reset but are outside that traversal.
        if (top != root_piece_) {
            piece.transform_marker = 0;
            continue;
        }
        while (current != kNoPiece) {
            const bool root = pieces_[current].parent == kNoPiece;
            transform_by(current, root);
            current = pieces_[current].parent;
        }
        piece.transform_marker = 0;
    }
    transforms_dirty_ = false;
}

} // namespace oa::sim::model_runtime
