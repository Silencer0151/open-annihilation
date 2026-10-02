// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace oa::sim::ground_orders {

// Path search open-set heap, a binary min-heap on payload word 2.
// Handles are physical node slots. They remain valid across heap reordering and
// storage growth, and are recycled last in, first out through a free-slot chain. A
// pop is deferred: the root stays in place until the next insert, peek or
// decrease_key settles it.
class SearchHeap {
  public:

    using Payload = std::array<uint32_t, 4>;
    using Handle = uint32_t;

    /// The handle insert returns when every handle is taken.
    static constexpr Handle no_handle = 0xffff'ffffU;
    /// The position position() returns for a free or unallocated slot.
    static constexpr std::size_t no_position = static_cast<std::size_t>(-1);

    /// Inserts a node, or reuses the root of a deferred pop for it.
    ///
    /// Grows the storage by the capacity + capacity/2 + 16 rule when full. A
    /// pending pop that emptied the heap is dropped, and the node starts a new
    /// root.
    ///
    /// @param value node payload; word 2 is the signed search score
    /// @return the node's handle; no_handle, with nothing inserted, when every handle is taken
    [[nodiscard]] Handle insert(const Payload& value);
    /// Moves a node up after its caller lowered its score.
    ///
    /// With a pop pending, the deferred root is removed once another node displaces
    /// it. A handle that is not live changes nothing.
    ///
    /// @param handle node whose score decreased
    void decrease_key(Handle handle);
    /// Returns the lowest-score payload, first completing a pending pop.
    ///
    /// @return the root payload; a zero payload for an empty heap
    [[nodiscard]] const Payload& peek();
    /// Pops the root; the first pop is deferred until the next heap operation.
    ///
    /// An empty heap is left as it is.
    void pop();

    /// Returns a node's payload for update before decrease_key.
    ///
    /// Payload word 2 is compared as a signed 32-bit search score.
    ///
    /// @param handle node slot
    /// @return the payload; for an unallocated slot, a spare payload outside the
    ///         heap, cleared at each such call
    [[nodiscard]] Payload& payload(Handle handle) noexcept;
    /// Returns a node's payload.
    ///
    /// @param handle node slot
    /// @return the payload; a zero payload for an unallocated slot
    [[nodiscard]] const Payload& payload(Handle handle) const noexcept;

    /// Returns the number of nodes in the heap, a deferred root included.
    [[nodiscard]] std::size_t size() const noexcept { return heap_.size(); }

    /// Returns whether the heap holds no nodes.
    [[nodiscard]] bool empty() const noexcept { return heap_.empty(); }

    /// Returns the reserved node capacity.
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /// Returns the number of node slots ever allocated.
    [[nodiscard]] std::size_t allocated_slots() const noexcept { return nodes_.size(); }

    /// Returns the first free slot, or -1.
    [[nodiscard]] int32_t free_head() const noexcept { return free_head_; }

    /// Returns whether a pop is pending on the root.
    [[nodiscard]] bool deferred_pop() const noexcept { return deferred_pop_; }

    /// Returns a live node's position in the heap array.
    ///
    /// @param handle node slot
    /// @return the heap position; no_position for a free or unallocated slot
    [[nodiscard]] std::size_t position(Handle handle) const noexcept;

    /// Returns the heap array of node slots.
    [[nodiscard]] const std::vector<Handle>& heap_order() const noexcept { return heap_; }

    /// Drops live nodes and the free-slot chain while keeping capacity.
    void reset_open_set();

    /// Tests whether no unpopped open nodes remain.
    ///
    /// The live node count equals the deferred-pop flag (0 or 1).
    ///
    /// @return true when the heap is empty or holds only a deferred root
    [[nodiscard]] bool exhausted() const noexcept {
        return heap_.size() == static_cast<std::size_t>(deferred_pop_);
    }

    /// Reserves node capacity.
    ///
    /// A request below the current capacity (or negative) selects the game's
    /// capacity + capacity/2 + 16 growth rule.
    ///
    /// @param requested_capacity nodes to reserve
    /// @return false, with the capacity unchanged, when the growth rule would
    ///         overflow or the capacity would fall below the allocated slots
    bool reserve(int32_t requested_capacity);

  private:

    struct Node {
        int32_t heap_position_or_next_free{-1};
        Payload value{};
    };

    static_assert(sizeof(Node) == 20);

    /// Takes a node slot from the free chain, or a new one.
    ///
    /// @return the slot, or no_handle when every handle is taken
    [[nodiscard]] Handle allocate_slot();
    /// Removes a live node, pushes its slot on the free chain and refills its position from the last node.
    ///
    /// @param handle node to remove; one that is not live is left alone
    void remove_slot(Handle handle);
    /// Moves the node at a heap position up while its score is lower than its parent's.
    ///
    /// @param position_value heap position
    void sift_up(std::size_t position_value);
    /// Moves the node at a heap position down while a child has a lower score.
    ///
    /// @param position_value heap position
    /// @quirk The left child is taken when both children's scores are equal.
    void sift_down(std::size_t position_value);
    /// Returns a node's search score, payload word 2 read as signed.
    ///
    /// @param value node
    /// @return the score
    [[nodiscard]] static int32_t score(const Node& value) noexcept;
    /// Returns the node in a slot.
    ///
    /// @param handle node slot
    /// @return the node; spare_, cleared, for an unallocated slot
    [[nodiscard]] Node& node(Handle handle) noexcept;
    /// Returns the node in a slot.
    ///
    /// @param handle node slot
    /// @return the node; a free node for an unallocated slot
    [[nodiscard]] const Node& node(Handle handle) const noexcept;

    std::vector<Node> nodes_;
    Node spare_{}; // what node() gives for an unallocated slot
    std::vector<Handle> heap_;
    int32_t free_head_{-1};
    std::size_t capacity_{};
    bool deferred_pop_{};
};

} // namespace oa::sim::ground_orders
