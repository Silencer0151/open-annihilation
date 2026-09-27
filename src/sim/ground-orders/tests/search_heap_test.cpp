// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/ground_orders/search_heap.hpp"

#include <bit>
#include <cassert>
#include <cstdint>

using oa::sim::ground_orders::SearchHeap;

namespace {
SearchHeap::Payload item(uint32_t identity, int32_t score) {
    return {identity, identity ^ 0x55aa55aaU, std::bit_cast<uint32_t>(score), identity + 7};
}
} // namespace

int main() {
    SearchHeap heap;
    const auto first = heap.insert(item(1, 10));
    const auto equal_a = heap.insert(item(2, 10));
    const auto equal_b = heap.insert(item(3, 10));
    assert(first == 0 && equal_a == 1 && equal_b == 2);
    assert((heap.heap_order() == std::vector<SearchHeap::Handle>{0, 1, 2}));

    const auto low = heap.insert(item(4, 5));
    assert(low == 3 && heap.heap_order().front() == low);
    assert(heap.peek()[0] == 4);
    heap.pop();
    assert(heap.deferred_pop() && heap.size() == 4);

    // Insertion replaces the deferred root in its physical slot and repairs
    // downward; it does not allocate a fifth node.
    const auto replaced = heap.insert(item(5, 20));
    assert(replaced == low && heap.allocated_slots() == 4 && !heap.deferred_pop());
    assert(heap.peek()[0] == 1);

    heap.pop();
    heap.payload(equal_b)[2] = std::bit_cast<uint32_t>(-7);
    heap.decrease_key(equal_b);
    assert(!heap.deferred_pop());
    assert(heap.size() == 3 && heap.peek()[0] == 3);
    assert(heap.free_head() == static_cast<int32_t>(first));

    const auto reused = heap.insert(item(6, 15));
    assert(reused == first);

    SearchHeap growth;
    for (uint32_t i = 0; i < 17; ++i) {
        [[maybe_unused]] const auto handle = growth.insert(item(i, static_cast<int32_t>(i)));
    }
    assert(growth.capacity() == 40);
    assert(growth.allocated_slots() == 17);

    const auto kept_capacity = growth.capacity();
    growth.reset_open_set();
    assert(growth.exhausted() && growth.size() == 0 && growth.allocated_slots() == 0);
    assert(growth.free_head() == -1 && !growth.deferred_pop());
    assert(growth.capacity() == kept_capacity);
    const auto reset_handle = growth.insert(item(99, 1));
    assert(reset_handle == 0 && growth.allocated_slots() == 1);
}
