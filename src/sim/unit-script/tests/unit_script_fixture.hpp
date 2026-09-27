// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/formats/cob.hpp"
#include "oa/sim/unit_script.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace oa::sim::unit_script::test {

inline void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

// Piece state and callback log for a script bound to one unit.
struct RecordingHost final : sim::script_vm::Host {
    explicit RecordingHost(std::size_t pieces)
        : positions(pieces * 3), angles(pieces * 3), flags(pieces * 3) {}

    std::vector<int32_t> positions;
    std::vector<uint32_t> angles;
    std::vector<uint32_t> flags; // visible, cached, shaded per piece
    std::size_t effects = 0;
    std::size_t explosions = 0;
    std::vector<int32_t> unit_value_selectors;
    std::vector<int32_t> set_value_selectors;
    uint32_t random_state = 1;

    int32_t piece_position(uint32_t piece, uint32_t axis) const override {
        return positions.at(piece * 3 + axis);
    }

    int32_t piece_angle(uint32_t piece, uint32_t axis) const override {
        return static_cast<int32_t>(angles.at(piece * 3 + axis));
    }

    uint32_t piece_visible(uint32_t piece) const override { return flags.at(piece * 3); }

    uint32_t piece_cached(uint32_t piece) const override { return flags.at(piece * 3 + 1); }

    uint32_t piece_shaded(uint32_t piece) const override { return flags.at(piece * 3 + 2); }

    void set_piece_position(uint32_t piece, uint32_t axis, int32_t value) override {
        positions.at(piece * 3 + axis) = value;
    }

    void set_piece_angle(uint32_t piece, uint32_t axis, uint32_t value) override {
        angles.at(piece * 3 + axis) = value;
    }

    void set_piece_visible(uint32_t piece, uint32_t value) override { flags.at(piece * 3) = value; }

    void set_piece_cached(uint32_t piece, uint32_t value) override {
        flags.at(piece * 3 + 1) = value;
    }

    void set_piece_shaded(uint32_t piece, uint32_t value) override {
        flags.at(piece * 3 + 2) = value;
    }

    void emit_sfx(uint32_t, int32_t) override { ++effects; }

    void explode(uint32_t, int32_t) override { ++explosions; }

    int32_t get_unit_value(int32_t selector, int32_t, int32_t, int32_t, int32_t) override {
        unit_value_selectors.push_back(selector);
        return 0;
    }

    void set_unit_value(int32_t selector, int32_t) override {
        set_value_selectors.push_back(selector);
    }

    void attach_unit(int32_t, int32_t, int32_t) override {}

    void drop_unit(int32_t) override {}

    void ignored_piece_op(uint32_t, uint32_t, uint32_t) override {}

    void dont_shadow(uint32_t) override {}

    uint32_t is_carrying_unit(uint32_t) override { return 0; }

    uint32_t carrier_unit_id() override { return 0; }

    uint32_t random_bounded(uint32_t bound) override {
        random_state = random_state * 1103515245U + 12345U;
        return bound < 2 ? 0 : (random_state >> 8) % bound;
    }
};

// A World of `slots` unit slots whose slot 1 is live.
struct WorldFixture {
    explicit WorldFixture(uint32_t slots = 8) : world(world_create()) {
        const WorldCapacity capacity{slots, 4, 1};
        require(world != nullptr && world_alloc_tables(world, &capacity) != 0, "world allocation");
        for (uint32_t i = 1; i < slots; ++i) {
            world->units[i].id = static_cast<uint16_t>(i);
            world->units[i].type_index = 1;
        }
        world->units[1].flags = OA_UNIT_FLAG_LIVE;
        world->units[1].def = 1;
        world->unit_defs[0].max_damage = 400;
    }

    ~WorldFixture() { world_destroy(world); }

    WorldFixture(const WorldFixture&) = delete;
    WorldFixture& operator=(const WorldFixture&) = delete;

    Unit* unit(uint32_t slot) { return &world->units[slot]; }

    World* world;
};

inline sim::script_vm::Program vm_program(const formats::cob::CobProgram& cob) {
    return {cob.code, cob.entry_points, cob.header.static_variable_count, cob.piece_names.size()};
}

} // namespace oa::sim::unit_script::test
