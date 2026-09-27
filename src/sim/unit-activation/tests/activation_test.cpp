// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_activation.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using namespace oa::sim::unit_activation;

struct Capture : Host {
    std::vector<std::string> events;
    uint16_t changed_unit{};
    uint8_t changed_flags{};
    uint8_t* flags{};
    bool mutate = false, simulated_here = true;

    void script(std::string_view name) override {
        events.emplace_back(name);
        if (mutate)
            *flags = 0x80;
    }

    void sound(Sound sound) override {
        events.push_back("sound" + std::to_string(unsigned(sound)));
    }

    void notify_attachments(uint32_t value) override {
        if (value != cloak_notification)
            throw std::runtime_error("attachment value");
        events.push_back("attachments");
    }

    void refresh_selected_unit() override { events.push_back("refresh"); }

    bool owner_simulates_here() override {
        events.push_back("simulated-here");
        return simulated_here;
    }

    void flags_changed(uint16_t unit, uint8_t changed) override {
        changed_unit = unit;
        changed_flags = changed;
        events.push_back("changed");
    }
};

// A unit its owner simulates here shares its new flags after the callbacks.
const std::vector<std::string> kShared{"simulated-here", "changed"};

std::vector<std::string> shared(std::vector<std::string> events) {
    events.insert(events.end(), kShared.begin(), kShared.end());
    return events;
}

int main() {
    uint8_t flags = 0;
    Capture host;
    host.flags = &flags;
    change(flags, 0x1234, 13, true, host);
    if (host.events !=
        shared({"Activate", "sound3", "StartBuilding", "sound14", "attachments", "refresh"}))
        throw std::runtime_error("activation order");
    if (host.changed_unit != 0x1234 || host.changed_flags != 13)
        throw std::runtime_error("activation shared flags");
    host.events.clear();
    change(flags, 0x1234, 13, true, host);
    if (!host.events.empty())
        throw std::runtime_error("unchanged activation");
    change(flags, 0x1234, 13, false, host);
    if (host.events != shared({"Deactivate", "sound4", "StopBuilding", "sound15", "refresh"}))
        throw std::runtime_error("deactivation order");
    host.events.clear();
    host.simulated_here = false;
    host.changed_flags = 0xff;
    change(flags, 0x1234, 1, true, host);
    if (host.events !=
            std::vector<std::string>{"Activate", "sound3", "refresh", "simulated-here"} ||
        host.changed_flags != 0xff)
        throw std::runtime_error("mirrored unit shared its flags");
    change(flags, 0x1234, 1, false, host);
    host.simulated_here = true;
    host.mutate = true;
    change(flags, 0x1234, 13, true, host);
    if (flags != 128)
        throw std::runtime_error("callback state reload");
    if (host.changed_flags != 128)
        throw std::runtime_error("callback state reload shared");

    std::array<ListedUnit, 4> units{{
        ListedUnit{1, 0x80},
        ListedUnit{2, 0x123456b0u},
        ListedUnit{2, 0xffffffffu},
        ListedUnit{0xff, 0x10},
    }};
    const std::array<uint16_t, 4> selected{0, 1, 1, 3};
    mark_owned_selection(units, selected, 2);
    if (units[0].flags != 0x80 || units[0].owner != 1)
        throw std::runtime_error("foreign selection");
    if (units[1].flags != 0x12345670u)
        throw std::runtime_error("owned selection bits");
    if (units[2].flags != 0xffffffffu)
        throw std::runtime_error("unlisted owner");
    if (units[3].flags != 0x10)
        throw std::runtime_error("other owner in list");
    mark_owned_selection(units, {}, 2);
    if (units[1].flags != 0x12345670u)
        throw std::runtime_error("empty selection");
    units[0].owner = 0;
    units[0].flags = 0;
    const std::array<uint16_t, 1> slot0{0};
    mark_owned_selection(units, slot0, 0);
    if (units[0].flags != owned_selection_bit)
        throw std::runtime_error("slot zero");
    units[3].flags = 0x80;
    const std::array<uint16_t, 1> high{3};
    mark_owned_selection(units, high, 0xff);
    if (units[3].flags != owned_selection_bit)
        throw std::runtime_error("owner 0xff");

    const std::array<std::pair<uint32_t, uint32_t>, 11> samples{{
        {0, 0},
        {0x3fu, 0x3fu},
        {0x40u, 0},
        {0x80u, 0},
        {0xc0u, 0},
        {0x10u, 0x10u},
        {0xffu, 0x3fu},
        {0x1c0u, 0x100u},
        {0x123456b0u, 0x12345630u},
        {0xffffffffu, 0xffffff3fu},
        {0xffffffc0u, 0xffffff00u},
    }};
    for (const auto& [before, expect] : samples) {
        ListedUnit unit{0xab, before};
        clear_cycle_marks(std::span<ListedUnit>(&unit, 1));
        if (unit.flags != expect || unit.owner != 0xab)
            throw std::runtime_error("byte mask 0x3f");
    }
    std::array<ListedUnit, 3> range{{
        ListedUnit{1, 0xffu},
        ListedUnit{2, 0x800000c0u},
        ListedUnit{3, 0x80u},
    }};
    clear_cycle_marks({});
    if (range[0].flags != 0xffu || range[2].flags != 0x80u)
        throw std::runtime_error("empty range");
    clear_cycle_marks(std::span<ListedUnit>(range).subspan(1, 1));
    if (range[0].flags != 0xffu || range[0].owner != 1 || range[1].flags != 0x80000000u ||
        range[1].owner != 2 || range[2].flags != 0x80u || range[2].owner != 3)
        throw std::runtime_error("single end unit");
    clear_cycle_marks(std::span<ListedUnit>(range).subspan(0, 2));
    if (range[0].flags != 0x3fu || range[1].flags != 0x80000000u || range[2].flags != 0x80u)
        throw std::runtime_error("prefix range");
}
