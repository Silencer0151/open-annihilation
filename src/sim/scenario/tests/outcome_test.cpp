// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/scenario/outcome.hpp"
#include <bit>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace oa::sim::scenario;

namespace {
struct Definitions : DefinitionHost {
    /// Returns every key's fallback.
    ///
    /// @param key key name
    /// @param fallback the value returned
    /// @return the fallback
    int32_t integer(std::string_view key, int32_t fallback) override {
        (void)key;
        return fallback;
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

/// Counts the victory cues played.
///
/// @param context the unsigned counter
void count_cue(void* context) {
    ++*static_cast<unsigned*>(context);
}

/// Throws when a check fails.
///
/// @param v whether the check held
void check(bool v) {
    if (!v)
        throw std::runtime_error("outcome assertion failed");
}
} // namespace

int main() {
    try {
        Definitions d;
        Controller c;
        register_conditions(c, d);
        OutcomeView v;
        unsigned announcements = 0;
        ConditionHost host;
        host.context = &announcements;
        host.victory_cue = count_cue;
        const auto world = std::make_unique<oa::World>();
        const QueryContext context{*world, v, 0, host};
        check(campaign_victory(c, context) && announcements == 1);
        check(campaign_victory(c, context) && announcements == 1);
        check(campaign_defeat(c, context));
        v.live_units[0] = 1;
        v.live_units[1] = 1;
        check(!offline_victory(c, v) && !offline_defeat(c, v));
        v.local_allies[1] = 1;
        check(offline_victory(c, v));
        disable(c);
        check(!offline_victory(c, v) && !offline_defeat(c, v));
        OutcomeState state;
        check(
            advance_outcome(state, false, true, true) == Outcome::ongoing && state.countdown == 4
        );
        check(
            advance_outcome(state, false, false, false) == Outcome::ongoing && state.countdown == 4
        );
        for (int i = 0; i < 4; ++i)
            check(advance_outcome(state, false, true, true) == Outcome::ongoing);
        check(advance_outcome(state, false, true, true) == Outcome::defeat && state.flags == 0x44);

        struct Random : DiagnosticRandom {
            /// Draws the highest 15-bit value.
            ///
            /// @return 32767
            uint16_t rand15() override { return 32767; }
        } random;

        construct(c);
        register_conditions(c, d);
        DiagnosticLoss loss{true, 0};
        check(!diagnostic_defeat(c, loss, 0, random) && loss.deadline == 17999);
        check(diagnostic_defeat(c, loss, 17999, random) && loss.deadline == 0);
        std::cout << "scenario outcomes passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
