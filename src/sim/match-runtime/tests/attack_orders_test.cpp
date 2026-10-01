// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Wait mission's steps for an order with no unit to wait for: phase 0
// sets the timer the order wakes on, phase 1 finishes it.
#include "oa/sim/match_runtime/attack_orders.hpp"
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(                                                              \
                std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #x                   \
            );                                                                                     \
    } while (false)

int main() {
    try {
        uint32_t waits = 0;
        uint32_t wake = 0;
        CHECK(oa::sim::match_runtime::wait_order(0, 12, waits, wake, 100) == 1);
        CHECK(waits == 1 && wake == 112);
        CHECK(oa::sim::match_runtime::wait_order(1, 12, waits, wake, 100) == 5);
        CHECK(oa::sim::match_runtime::wait_order(4, 12, waits, wake, 100) == 7);
    } catch (const std::exception& error) {
        std::cerr << "attack orders: " << error.what() << '\n';
        return 1;
    }
    std::cout << "attack orders passed\n";
    return 0;
}
