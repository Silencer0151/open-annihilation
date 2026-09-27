// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/scenario/outcome.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
using namespace oa::sim::scenario;

namespace {
struct Definitions : DefinitionHost {
    int32_t seconds{};
    std::string_view armed{};

    /// Returns the armed timer's seconds.
    ///
    /// @param key key name
    /// @param fallback value for any other key
    /// @return the value
    int32_t integer(std::string_view key, int32_t fallback) override {
        return key == armed ? seconds : fallback;
    }

    /// Names no text key.
    ///
    /// @param key key name
    /// @return nullopt
    std::optional<std::string> text(std::string_view key) override {
        (void)key;
        return {};
    }
};

/// Throws when a check fails.
///
/// @param v whether the check held
void check(bool v) {
    if (!v)
        throw std::runtime_error("timer assertion failed");
}

/// Registers a VictoryTimerRunsOut or DeathTimerRunsOut condition.
///
/// @param kind victory_timer or death_timer
/// @param seconds the key's value
/// @return the condition
Condition registered(Kind kind, int32_t seconds) {
    Definitions definitions;
    definitions.seconds = seconds;
    definitions.armed = kind == Kind::victory_timer ? "VictoryTimerRunsOut" : "DeathTimerRunsOut";
    Controller controller;
    register_conditions(controller, definitions);
    auto& entries = kind == Kind::victory_timer ? controller.victory : controller.defeat;
    check(entries[0] && entries[0]->kind == kind);
    return *entries[0];
}
} // namespace

int main() {
    try {
        auto victory = registered(Kind::victory_timer, 2);
        check(victory.deadline == 60);
        check(!timer_elapsed(victory, 59));
        check(timer_elapsed(victory, 60));
        check(timer_elapsed(victory, 0xffffffffu));
        const auto before = victory;
        check(!timer_elapsed(victory, 0));
        check(victory == before);

        auto defeat = registered(Kind::death_timer, 1);
        check(defeat.deadline == 30);
        check(!timer_elapsed(defeat, 29) && timer_elapsed(defeat, 30));

        constexpr uint32_t wrapping_seconds = 143165577u;
        auto wrapped = registered(Kind::victory_timer, static_cast<int32_t>(wrapping_seconds));
        check(wrapped.deadline == wrapping_seconds * 30u);
        check(!timer_elapsed(wrapped, wrapped.deadline - 1));
        check(timer_elapsed(wrapped, wrapped.deadline));

        victory.deadline = 0;
        check(timer_elapsed(victory, 0));
        victory.deadline = 0x80000001u;
        check(!timer_elapsed(victory, 0x80000000u));
        check(timer_elapsed(victory, 0x80000001u));

        // A condition never registered has a zero deadline, which has always elapsed.
        const Condition blank;
        check(timer_elapsed(blank, 0));
        std::cout << "scenario timer passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
