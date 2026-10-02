// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/sqsh.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace oa::formats::sqsh {
namespace {
constexpr std::size_t maximum_match_bytes = lz77::token_length_mask + lz77::minimum_match_bytes;
constexpr uint16_t no_node = 0;
constexpr uint16_t tree_root = lz77::dictionary_bytes;
constexpr unsigned tokens_per_flag_byte = 8;

struct TreeNode {
    uint16_t parent{}, lower{}, upper{};
};

// The tree shape is part of the encoded representation: equal-length matches
// replace the previous candidate, and comparisons use signed input bytes.
class MatchTree {
  public:

    std::array<uint8_t, lz77::dictionary_bytes> dictionary{};

    /// Creates the tree with the root holding the first window position as its only child.
    MatchTree() {
        nodes_[tree_root].upper = lz77::initial_write_offset;
        nodes_[lz77::initial_write_offset].parent = tree_root;
    }

    /// Unlinks a window position from the tree.
    ///
    /// A node with two children is replaced by the rightmost node of its
    /// lower subtree.
    ///
    /// @param index window position; one not in the tree is ignored
    void remove(uint16_t index) {
        auto& node = nodes_[index];
        if (node.parent == no_node)
            return;
        if (node.upper != no_node && node.lower != no_node) {
            auto predecessor = node.lower;
            while (nodes_[predecessor].upper != no_node)
                predecessor = nodes_[predecessor].upper;
            remove(predecessor);
            replace(index, predecessor);
            return;
        }
        const auto child = node.upper != no_node ? node.upper : node.lower;
        nodes_[child].parent = node.parent; // with no child: sentinel node 0
        auto& parent = nodes_[node.parent];
        if (parent.upper == index)
            parent.upper = child;
        else
            parent.lower = child;
        node.parent = no_node;
    }

    /// Inserts a window position and finds its longest earlier match.
    ///
    /// A full-length match takes over the candidate's node. Comparisons use
    /// signed bytes and a later equal-length candidate wins.
    ///
    /// @param index window position to insert; 0 is ignored
    /// @param[out] match_offset window position of the best match
    /// @return the match length, at most 17 bytes
    std::size_t insert(uint16_t index, uint16_t& match_offset) {
        if (index == no_node)
            return 0;
        auto candidate = nodes_[tree_root].upper;
        std::size_t best_length = 0;
        for (;;) {
            std::size_t length = 0;
            int difference = 0;
            for (; length < maximum_match_bytes; ++length) {
                const auto current =
                    std::bit_cast<int8_t>(dictionary[(index + length) & lz77::dictionary_mask]);
                const auto previous =
                    std::bit_cast<int8_t>(dictionary[(candidate + length) & lz77::dictionary_mask]);
                difference = int(current) - int(previous);
                if (difference != 0)
                    break;
            }
            if (best_length <= length) {
                best_length = length;
                match_offset = candidate;
                if (length == maximum_match_bytes) {
                    replace(candidate, index);
                    return length;
                }
            }
            auto& child = difference < 0 ? nodes_[candidate].lower : nodes_[candidate].upper;
            if (child != no_node) {
                candidate = child;
                continue;
            }
            child = index;
            nodes_[index] = {candidate, no_node, no_node};
            return best_length;
        }
    }

  private:

    std::array<TreeNode, lz77::dictionary_bytes + 1> nodes_{};

    /// Moves a node's links to another window position and unlinks the old one.
    ///
    /// @param old_index window position whose place is taken
    /// @param new_index window position that takes it
    void replace(uint16_t old_index, uint16_t new_index) {
        auto& old_node = nodes_[old_index];
        auto& parent = nodes_[old_node.parent];
        if (parent.lower == old_index)
            parent.lower = new_index;
        else
            parent.upper = new_index;
        nodes_[new_index] = old_node;
        nodes_[nodes_[new_index].lower].parent = new_index;
        nodes_[nodes_[new_index].upper].parent = new_index;
        old_node.parent = no_node;
    }
};
} // namespace

base::bytes::Decoded<std::vector<uint8_t>>
encode_lz77(std::span<const uint8_t> input, std::size_t output_limit) {
    MatchTree tree;
    std::size_t cursor = 0;
    auto available = std::min(input.size(), maximum_match_bytes);
    for (; cursor < available; ++cursor)
        tree.dictionary[lz77::initial_write_offset + cursor] = input[cursor];
    uint16_t position = lz77::initial_write_offset;
    std::size_t match_length = 0;
    uint16_t match_offset = 0;
    // The scratch arrays are indexed by bitmask and reused across groups. They
    // start zeroed and keep their values between groups, so the extra byte
    // after the terminator is always defined.
    constexpr unsigned scratch_entries = (1u << tokens_per_flag_byte) + 1;
    std::array<uint8_t, scratch_entries> literals{};
    std::array<uint16_t, scratch_entries> matches{};
    unsigned next_bit = 1;
    uint8_t flags = 0;
    std::vector<uint8_t> output;
    bool overflowed = false;
    const auto emit = [&](uint8_t byte) {
        if (output.size() == output_limit) {
            overflowed = true;
            return;
        }
        output.push_back(byte);
    };
    const auto too_long = [&] {
        return base::bytes::DecodeError{
            base::bytes::DecodeCode::limit_exceeded, cursor, "LZ77 output limit exceeded"
        };
    };
    const auto flush = [&](unsigned count) {
        emit(flags);
        for (unsigned token = 0; token < count; ++token) {
            const auto bit = 1u << token;
            if ((flags & bit) == 0)
                emit(literals[bit]);
            else {
                emit(static_cast<uint8_t>(matches[bit]));
                emit(static_cast<uint8_t>(matches[bit] >> 8));
            }
        }
    };
    while (available != 0) {
        match_length = std::min(match_length, available);
        std::size_t consumed;
        if (match_length < lz77::minimum_match_bytes) {
            consumed = 1;
            literals[next_bit] = tree.dictionary[position];
        } else {
            consumed = match_length;
            flags |= static_cast<uint8_t>(next_bit);
            matches[next_bit] = static_cast<uint16_t>(
                (match_offset << lz77::token_length_bits) |
                ((match_length - lz77::minimum_match_bytes) & lz77::token_length_mask)
            );
        }
        next_bit <<= 1;
        if (next_bit == (1u << tokens_per_flag_byte)) {
            flush(tokens_per_flag_byte);
            if (overflowed)
                return too_long();
            flags = 0;
            next_bit = 1;
        }
        for (std::size_t count = 0; count < consumed; ++count) {
            const auto incoming =
                static_cast<uint16_t>((position + maximum_match_bytes) & lz77::dictionary_mask);
            tree.remove(incoming);
            if (cursor < input.size())
                tree.dictionary[incoming] = input[cursor++];
            else
                --available;
            position = static_cast<uint16_t>((position + 1) & lz77::dictionary_mask);
            if (available != 0)
                match_length = tree.insert(position, match_offset);
        }
    }
    matches[next_bit] = 0;
    flags |= static_cast<uint8_t>(next_bit);
    // The count starts at 1 before the mask bits are counted, so one literal
    // beyond the terminating match token is emitted.
    flush(1 + std::bit_width(next_bit));
    if (overflowed)
        return too_long();
    return output;
}
} // namespace oa::formats::sqsh
