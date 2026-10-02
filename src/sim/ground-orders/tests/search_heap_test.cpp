// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/ground_orders/search_heap.hpp"
#include "oa/test/check.hpp"

#include <bit>
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
    OA_CHECK(first == 0 && equal_a == 1 && equal_b == 2);
    OA_CHECK((heap.heap_order() == std::vector<SearchHeap::Handle>{0, 1, 2}));

    const auto low = heap.insert(item(4, 5));
    OA_CHECK(low == 3 && heap.heap_order().front() == low);
    OA_CHECK(heap.peek()[0] == 4);
    heap.pop();
    OA_CHECK(heap.deferred_pop() && heap.size() == 4);

    // Insertion replaces the deferred root in its physical slot and repairs
    // downward; it does not allocate a fifth node.
    const auto replaced = heap.insert(item(5, 20));
    OA_CHECK(replaced == low && heap.allocated_slots() == 4 && !heap.deferred_pop());
    OA_CHECK(heap.peek()[0] == 1);

    heap.pop();
    heap.payload(equal_b)[2] = std::bit_cast<uint32_t>(-7);
    heap.decrease_key(equal_b);
    OA_CHECK(!heap.deferred_pop());
    OA_CHECK(heap.size() == 3 && heap.peek()[0] == 3);
    OA_CHECK(heap.free_head() == static_cast<int32_t>(first));

    const auto reused = heap.insert(item(6, 15));
    OA_CHECK(reused == first);

    SearchHeap growth;
    for (uint32_t i = 0; i < 17; ++i) {
        [[maybe_unused]] const auto handle = growth.insert(item(i, static_cast<int32_t>(i)));
    }
    OA_CHECK(growth.capacity() == 40);
    OA_CHECK(growth.allocated_slots() == 17);

    const auto kept_capacity = growth.capacity();
    growth.reset_open_set();
    OA_CHECK(growth.exhausted() && growth.size() == 0 && growth.allocated_slots() == 0);
    OA_CHECK(growth.free_head() == -1 && !growth.deferred_pop());
    OA_CHECK(growth.capacity() == kept_capacity);
    const auto reset_handle = growth.insert(item(99, 1));
    OA_CHECK(reset_handle == 0 && growth.allocated_slots() == 1);
    return oa::test::check_exit_status();
}
