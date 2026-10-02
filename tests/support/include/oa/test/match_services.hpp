// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What a test match asks of the application, done the plain way: services
// that take every call quietly, services on which every call fails the
// test, and scenario definitions with no keys. A test derives from them and
// overrides only the calls it watches. Link oa-test-match.
#pragma once

#include "oa/sim/match_runtime.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oa::test {

/// A match's OfflineServices that take every call and do nothing: no
/// speech or sound plays, no panel redraws, no script effect shows and no
/// movement map changes.
struct QuietServices : sim::match_runtime::OfflineServices {
    /// Plays no command speech.
    void command_sound(sim::unit_spawn::Slot&, uint32_t) override {}

    /// Plays no activation sound.
    void activation_sound(sim::unit_spawn::Slot&, sim::unit_activation::Sound) override {}

    /// Tells no attachment interface.
    void attachment_notification(sim::unit_spawn::Slot&, uint32_t) override {}

    /// Redraws no order panel.
    void refresh_selected_unit(sim::unit_spawn::Slot&) override {}

    /// Shows no EMIT-SFX effect.
    void emit_sfx(sim::unit_spawn::Slot&, uint32_t, int32_t) override {}

    /// Explodes no piece.
    void explode_piece(sim::unit_spawn::Slot&, uint32_t, int32_t) override {}

    /// Attaches no unit.
    void attach_unit(sim::unit_spawn::Slot&, int32_t, int32_t, int32_t) override {}

    /// Drops no unit.
    void drop_unit(sim::unit_spawn::Slot&, int32_t) override {}

    /// Recomputes no plot heights.
    void refresh_plot_height_range(std::array<int16_t, 2>, std::array<int16_t, 2>) override {}

    /// Refreshes no movement map.
    void notify_object_footprint_removed(sim::spatial_state::Unit&, uint32_t) override {}

    /// Refreshes no movement map.
    void notify_footprint_changed(std::array<int16_t, 2>, std::array<int16_t, 2>) override {}
};

/// Fails a test on a call it did not expect.
///
/// @param call the call's name
/// @throws std::runtime_error always, saying "unexpected <call>"
[[noreturn]] inline void unexpected_call(std::string_view call) {
    throw std::runtime_error("unexpected " + std::string(call));
}

/// A match's OfflineServices on which every call fails the test
/// (unexpected_call), for a test that expects the match to ask nothing of
/// its application.
struct StrictServices : sim::match_runtime::OfflineServices {
    /// Fails the test.
    void command_sound(sim::unit_spawn::Slot&, uint32_t) override {
        unexpected_call("command_sound");
    }

    /// Fails the test.
    void activation_sound(sim::unit_spawn::Slot&, sim::unit_activation::Sound) override {
        unexpected_call("activation_sound");
    }

    /// Fails the test.
    void attachment_notification(sim::unit_spawn::Slot&, uint32_t) override {
        unexpected_call("attachment_notification");
    }

    /// Fails the test.
    void refresh_selected_unit(sim::unit_spawn::Slot&) override {
        unexpected_call("refresh_selected_unit");
    }

    /// Fails the test.
    void emit_sfx(sim::unit_spawn::Slot&, uint32_t, int32_t) override {
        unexpected_call("emit_sfx");
    }

    /// Fails the test.
    void explode_piece(sim::unit_spawn::Slot&, uint32_t, int32_t) override {
        unexpected_call("explode_piece");
    }

    /// Fails the test.
    void attach_unit(sim::unit_spawn::Slot&, int32_t, int32_t, int32_t) override {
        unexpected_call("attach_unit");
    }

    /// Fails the test.
    void drop_unit(sim::unit_spawn::Slot&, int32_t) override { unexpected_call("drop_unit"); }

    /// Fails the test.
    void refresh_plot_height_range(std::array<int16_t, 2>, std::array<int16_t, 2>) override {
        unexpected_call("refresh_plot_height_range");
    }

    /// Fails the test.
    void notify_object_footprint_removed(sim::spatial_state::Unit&, uint32_t) override {
        unexpected_call("notify_object_footprint_removed");
    }

    /// Fails the test.
    void notify_footprint_changed(std::array<int16_t, 2>, std::array<int16_t, 2>) override {
        unexpected_call("notify_footprint_changed");
    }
};

/// Scenario definitions whose GlobalHeader holds no keys.
struct EmptyScenario : sim::scenario::DefinitionHost {
    /// Reads no key.
    ///
    /// @return the fallback
    int32_t integer(std::string_view, int32_t fallback) override { return fallback; }

    /// Reads no key.
    ///
    /// @return nullopt
    std::optional<std::string> text(std::string_view) override { return std::nullopt; }
};

} // namespace oa::test
