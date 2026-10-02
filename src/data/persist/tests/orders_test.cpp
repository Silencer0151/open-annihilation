// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"

#include "oa/data/persist/save_orders.hpp"
#include "oa/base/text.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace oa;
using namespace oa::data::persist;

namespace {

// Mission indices of the sorted registration table.
constexpr uint8_t building_build = 12, follow_ground = 18, mobile_build = 25, move_ground = 26,
                  patrol = 29, self_repair = 40, standby = 41, standing_fire_order = 43,
                  vtol_mobile_build = 54;

struct ScopedBank {
    Bank bank{};

    ScopedBank() { bank_reset(&bank); }

    ~ScopedBank() { bank_destroy(&bank); }

    Bank* get() { return &bank; }
};

// A unit table with names and flags; slot 0 is reserved.
struct Types {
    std::unique_ptr<World> world = std::make_unique<World>();
    std::vector<UnitDef> defs;

    explicit Types(std::initializer_list<std::pair<const char*, uint32_t>> types) {
        std::memset(world.get(), 0, sizeof(World));
        defs.resize(types.size() + 1);
        std::memset(defs.data(), 0, defs.size() * sizeof(UnitDef));
        uint16_t index = 1;
        for (const auto& [name, flags] : types) {
            oa::base::text::copy_padded(
                defs[index].unit_name, name, sizeof(defs[index].unit_name) - 1
            );
            defs[index].flags = flags;
            defs[index].type_id = index;
            ++index;
        }
        world->unit_defs = defs.data();
        world->unit_def_count = static_cast<uint32_t>(defs.size());
    }
};

Unit unit_with_id(uint16_t id) {
    Unit unit;
    std::memset(&unit, 0, sizeof(unit));
    unit.id = id;
    return unit;
}

std::vector<uint8_t> read_blob(Bank* bank, const char* name) {
    bank_open_account(bank, "Units");
    if (!bank_open_blob_name(bank, name))
        return {};
    std::vector<uint8_t> out(static_cast<std::size_t>(bank_blob_size(bank)));
    bank_blob_seek(bank, 0);
    bank_blob_read(bank, out.data(), static_cast<uint32_t>(out.size()));
    return out;
}

void put_blob(Bank* bank, const char* name, const uint8_t* bytes, uint32_t size) {
    bank_open_account(bank, "Units");
    bank_open_blob_name(bank, name);
    bank_blob_seek(bank, 0);
    bank_blob_write(bank, bytes, size);
}

// An order blob laid out byte for byte as 3.1c saves one: a Move_Ground of
// unit 7 on unit 9 with a circle goal.
std::vector<uint8_t> move_order_blob() {
    std::vector<uint8_t> blob(order_blob_bytes);
    const auto put16 = [&](std::size_t at, uint16_t v) {
        blob[at] = static_cast<uint8_t>(v);
        blob[at + 1] = static_cast<uint8_t>(v >> 8);
    };
    const auto put32 = [&](std::size_t at, uint32_t v) {
        for (std::size_t i = 0; i < 4; ++i)
            blob[at + i] = static_cast<uint8_t>(v >> (8 * i));
    };
    put16(0x00, 7);           // owner id
    put16(0x02, 9);           // target id
    put32(0x04, 4);           // goal kind: circle
    blob[0x08] = move_ground; // kind
    blob[0x09] = 2;           // phase
    put32(0x0a, 0x60);        // wait events
    put32(0x0e, 1234);        // wake tick
    put32(0x12, 0x00400000);  // point X
    put32(0x16, 0x00050000);  // point Y
    put32(0x1a, 0x00600000);  // point Z
    put16(0x1e, 0x0012);      // anchor X
    put16(0x20, 0xfffe);      // anchor Z
    put32(0x22, 0x11223344);  // seen cell
    put32(0x26, 4);           // parameter 1: the tolerance
    put32(0x2a, 0xfffffffb);  // parameter 2
    put32(0x2e, 77);          // parameter 3
    blob[0x32] = 0x03;        // preserve flags
    blob[0x33] = 0x24;        // command flags
    blob[0x34] = 0x00;        // flags
    blob[0x35] = 0x00;        // mission byte
    put32(0x36, 0x20);        // raised events
    return blob;
}

void order_blob_layout() {
    const auto blob = move_order_blob();
    SavedOrder order;
    save_decode_order(blob.data(), &order);
    CHECK(order.owner_id == 7 && order.target_id == 9 && order.goal_kind == 4);
    CHECK(order.kind == move_ground && order.phase == 2);
    CHECK(order.wait_events == 0x60 && order.wake_tick == 1234);
    CHECK(order.point[0] == 0x400000 && order.point[1] == 0x50000 && order.point[2] == 0x600000);
    CHECK(order.anchor[0] == 0x12 && order.anchor[1] == -2);
    CHECK(
        order.seen_cell == 0x11223344u && order.parameter_1 == 4 && order.parameter_2 == -5 &&
        order.parameter_3 == 77
    );
    CHECK(
        order.preserve_flags == 3 && order.command_flags == 0x24 && order.flags == 0 &&
        order.mission_byte == 0
    );
    CHECK(order.raised_events == 0x20);
    uint8_t again[order_blob_bytes];
    save_encode_order(&order, again);
    CHECK(std::memcmp(again, blob.data(), order_blob_bytes) == 0);
}

void write_and_read_move() {
    Types types({{"ARMCOM", 0}});
    ScopedBank bank;
    bank_open_account(bank.get(), "Units");
    const Unit owner = unit_with_id(7);
    SavedOrder order;
    save_decode_order(move_order_blob().data(), &order);
    SavedGoal goal;
    goal.kind = static_cast<int32_t>(SavedGoalKind::circle);
    goal.circle = {{-3, 40}, 0x40, 16};
    CHECK(save_write_order(types.world.get(), &owner, &order, &goal, bank.get(), "u0007m0000"));
    CHECK(read_blob(bank.get(), "u0007m0000") == move_order_blob());
    CHECK(std::strcmp(bank_get_text(bank.get(), "u0007m0000_name", ""), "Move_Ground") == 0);
    // The circle goal: zero word, cell X/Z, tolerance, radius squared.
    const std::vector<uint8_t> circle{0, 0, 0, 0, 0xfd, 0xff, 40, 0, 0x40, 0, 0, 0, 16, 0, 0, 0};
    CHECK(read_blob(bank.get(), "u0007m0000g") == circle);
    // No type name for a move.
    CHECK(!bank_has_field(bank.get(), "UTYPENAME   0"));

    SavedOrder loaded;
    SavedGoal loaded_goal;
    bank_open_account(bank.get(), "Units");
    CHECK(
        save_read_order(types.world.get(), &owner, bank.get(), "u0007m0000", &loaded, &loaded_goal)
    );
    uint8_t a[order_blob_bytes], b[order_blob_bytes];
    save_encode_order(&order, a);
    save_encode_order(&loaded, b);
    CHECK(std::memcmp(a, b, order_blob_bytes) == 0);
    CHECK(
        loaded_goal.kind == goal.kind && loaded_goal.circle.cell[0] == -3 &&
        loaded_goal.circle.cell[1] == 40 && loaded_goal.circle.tolerance == 0x40 &&
        loaded_goal.circle.radius_squared == 16
    );

    // Another unit's order is neither written nor read.
    const Unit other = unit_with_id(8);
    CHECK(!save_write_order(types.world.get(), &other, &order, &goal, bank.get(), "u0008m0000"));
    CHECK(read_blob(bank.get(), "u0008m0000").empty());
    bank_open_account(bank.get(), "Units");
    CHECK(
        !save_read_order(types.world.get(), &other, bank.get(), "u0007m0000", &loaded, &loaded_goal)
    );
    // Names past 31 characters are refused.
    CHECK(!save_write_order(
        types.world.get(), &owner, &order, &goal, bank.get(), "u0007m0000u0007m0000u0007m000000"
    ));
    CHECK(!save_read_order(
        types.world.get(),
        &owner,
        bank.get(),
        "u0007m0000u0007m0000u0007m000000",
        &loaded,
        &loaded_goal
    ));
    // A missing or short blob restores nothing.
    CHECK(
        !save_read_order(types.world.get(), &owner, bank.get(), "u0007m0001", &loaded, &loaded_goal)
    );
    const uint8_t short_blob[order_blob_bytes - 1] = {7};
    put_blob(bank.get(), "u0007m0002", short_blob, sizeof(short_blob));
    CHECK(
        !save_read_order(types.world.get(), &owner, bank.get(), "u0007m0002", &loaded, &loaded_goal)
    );
    // An unknown mission name restores nothing.
    put_blob(bank.get(), "u0007m0003", move_order_blob().data(), order_blob_bytes);
    bank_set_text(bank.get(), "u0007m0003_name", "NotAMission");
    CHECK(
        !save_read_order(types.world.get(), &owner, bank.get(), "u0007m0003", &loaded, &loaded_goal)
    );
    // A goal without its blob is dropped; the order still restores.
    put_blob(bank.get(), "u0007m0004", move_order_blob().data(), order_blob_bytes);
    bank_set_text(bank.get(), "u0007m0004_name", "Patrol");
    CHECK(
        save_read_order(types.world.get(), &owner, bank.get(), "u0007m0004", &loaded, &loaded_goal)
    );
    CHECK(loaded.kind == patrol && loaded_goal.kind == 0);
}

void build_type_names() {
    Types saving({{"ARMLAB", 0}, {"ARMPW", 0}, {"ARMSOLAR", 0}});
    ScopedBank bank;
    bank_open_account(bank.get(), "Units");
    const Unit builder = unit_with_id(3);
    SavedOrder build{.owner_id = 3, .kind = mobile_build, .parameter_1 = 3};
    const SavedGoal none{};
    CHECK(save_write_order(saving.world.get(), &builder, &build, &none, bank.get(), "u0003m0000"));
    CHECK(std::strcmp(bank_get_text(bank.get(), "UTYPENAME   3", ""), "ARMSOLAR") == 0);
    // The first name written for a type stays.
    oa::base::text::copy_terminated(saving.defs[3].unit_name, "RENAMED");
    SavedOrder factory{.owner_id = 3, .kind = building_build, .parameter_1 = 3, .parameter_2 = 5};
    CHECK(
        save_write_order(saving.world.get(), &builder, &factory, &none, bank.get(), "u0003m0001")
    );
    CHECK(std::strcmp(bank_get_text(bank.get(), "UTYPENAME   3", ""), "ARMSOLAR") == 0);
    SavedOrder air{.owner_id = 3, .kind = vtol_mobile_build, .parameter_1 = 2};
    CHECK(save_write_order(saving.world.get(), &builder, &air, &none, bank.get(), "u0003m0002"));
    CHECK(std::strcmp(bank_get_text(bank.get(), "UTYPENAME   2", ""), "ARMPW") == 0);
    // Type 0 and types past the table get no name.
    SavedOrder unnamed{.owner_id = 3, .kind = building_build, .parameter_1 = 9};
    CHECK(
        save_write_order(saving.world.get(), &builder, &unnamed, &none, bank.get(), "u0003m0003")
    );
    CHECK(!bank_has_field(bank.get(), "UTYPENAME   9"));
    // Other missions keep parameter_1 as it is.
    SavedOrder follow{.owner_id = 3, .kind = follow_ground, .parameter_1 = 1};
    CHECK(save_write_order(saving.world.get(), &builder, &follow, &none, bank.get(), "u0003m0004"));
    CHECK(!bank_has_field(bank.get(), "UTYPENAME   1"));

    // Loading into a table with another type before them remaps by name.
    Types loading({{"ARMAAA", 0}, {"ARMLAB", 0}, {"ARMPW", 0}, {"ARMSOLAR", 0}});
    SavedOrder loaded;
    SavedGoal goal;
    bank_open_account(bank.get(), "Units");
    CHECK(save_read_order(loading.world.get(), &builder, bank.get(), "u0003m0000", &loaded, &goal));
    CHECK(loaded.kind == mobile_build && loaded.parameter_1 == 4);
    CHECK(save_read_order(loading.world.get(), &builder, bank.get(), "u0003m0001", &loaded, &goal));
    CHECK(loaded.kind == building_build && loaded.parameter_1 == 4 && loaded.parameter_2 == 5);
    CHECK(save_read_order(loading.world.get(), &builder, bank.get(), "u0003m0002", &loaded, &goal));
    CHECK(loaded.parameter_1 == 3);
    CHECK(save_read_order(loading.world.get(), &builder, bank.get(), "u0003m0004", &loaded, &goal));
    CHECK(loaded.kind == follow_ground && loaded.parameter_1 == 1);
    // A name the table lacks resolves to type 0.
    bank_set_text(bank.get(), "UTYPENAME   2", "CORGONE");
    CHECK(save_read_order(loading.world.get(), &builder, bank.get(), "u0003m0002", &loaded, &goal));
    CHECK(loaded.parameter_1 == 0);
}

// Saves without mission or type names number missions and types by position.
void unnamed_positions() {
    // Type 2 is downloadable and not counted.
    Types types({{"ARMA", 0}, {"ARMB", OA_UNIT_DEF_FLAG_DOWNLOADABLE}, {"ARMC", 0}, {"ARMD", 0}});
    ScopedBank bank;
    const Unit owner = unit_with_id(5);
    SavedOrder order;
    SavedGoal goal;
    const auto unnamed = [&](uint8_t position, int32_t parameter_1) {
        uint8_t blob[order_blob_bytes] = {};
        blob[order_blob::owner_id] = 5;
        blob[order_blob::kind] = position;
        blob[order_blob::parameter_1] = static_cast<uint8_t>(parameter_1);
        put_blob(bank.get(), "u0005m0000", blob, sizeof(blob));
        return save_read_order(types.world.get(), &owner, bank.get(), "u0005m0000", &order, &goal);
    };
    CHECK(unnamed(move_ground, 0) && order.kind == move_ground);
    // SelfRepair (40) and Standby_Mine (42) are not counted.
    CHECK(unnamed(self_repair, 0) && order.kind == standby);
    CHECK(unnamed(41, 0) && order.kind == standing_fire_order);
    CHECK(unnamed(65, 0) && order.kind == 67);
    CHECK(!unnamed(66, 0));
    // Position 0 is the unnamed mission and restores nothing.
    CHECK(!unnamed(0, 0));
    // Numbered types are 1, 3 and 4; position n resolves to the type before
    // the n-th of them.
    CHECK(unnamed(mobile_build, 0) && order.parameter_1 == 0);
    CHECK(unnamed(mobile_build, 1) && order.parameter_1 == 2);
    CHECK(unnamed(mobile_build, 2) && order.parameter_1 == 3);
    CHECK(unnamed(mobile_build, 3) && order.parameter_1 == 0);
    CHECK(unnamed(patrol, 2) && order.parameter_1 == 2);
}

void goal_layouts() {
    ScopedBank bank;
    bank_open_account(bank.get(), "Units");
    // Ring: zero word, cell, inner range, outer range, inner
    // and outer radius squared.
    const SavedRingGoal ring{{10, -20}, 0x30, 0x90, 1, 25};
    save_write_ring_goal(&ring, bank.get(), "ring");
    const std::vector<uint8_t> ring_bytes{0,    0, 0, 0, 10, 0, 0xec, 0xff, 0x30, 0, 0, 0,
                                          0x90, 0, 0, 0, 1,  0, 0,    0,    25,   0, 0, 0};
    CHECK(read_blob(bank.get(), "ring") == ring_bytes);
    SavedRingGoal ring_back;
    CHECK(save_read_ring_goal(bank.get(), "ring", &ring_back));
    CHECK(
        ring_back.cell[0] == 10 && ring_back.cell[1] == -20 && ring_back.inner_range == 0x30 &&
        ring_back.outer_range == 0x90 && ring_back.inner_radius_squared == 1 &&
        ring_back.outer_radius_squared == 25
    );

    // Outline: zero word, left, right, top, bottom.
    const SavedOutlineGoal outline{3, 7, -1, 9};
    save_write_outline_goal(&outline, bank.get(), "outline");
    const auto outline_bytes = read_blob(bank.get(), "outline");
    CHECK(
        outline_bytes.size() == outline_goal_blob_bytes && outline_bytes[4] == 3 &&
        outline_bytes[8] == 7 && outline_bytes[0xc] == 0xff && outline_bytes[0xf] == 0xff &&
        outline_bytes[0x10] == 9
    );
    SavedOutlineGoal outline_back;
    CHECK(save_read_outline_goal(bank.get(), "outline", &outline_back));
    CHECK(
        outline_back.left == 3 && outline_back.right == 7 && outline_back.top == -1 &&
        outline_back.bottom == 9
    );

    // Air target: unit id at +08, zero bytes, target id at +1a,
    // flags, arrival radius, altitude, bearing, query point, point, stand-off.
    SavedAirTargetGoal target;
    target.unit_id = 0x21;
    target.target_id = 0x34;
    target.flags = 0x0081;
    target.arrival_radius = 16;
    target.altitude = -8;
    target.bearing = 0xc000;
    target.query_point = -1;
    target.point[0] = 0x10000;
    target.point[1] = 0x20000;
    target.point[2] = 0x30000;
    target.stand_off = 0x640000;
    save_write_air_target_goal(&target, bank.get(), "target");
    const auto target_bytes = read_blob(bank.get(), "target");
    CHECK(target_bytes.size() == air_target_goal_blob_bytes);
    CHECK(target_bytes[0x08] == 0x21 && target_bytes[0x1a] == 0x34 && target_bytes[0x1c] == 0x81);
    CHECK(target_bytes[0x1e] == 16 && target_bytes[0x20] == 0xf8 && target_bytes[0x23] == 0xc0);
    CHECK(
        target_bytes[0x24] == 0xff && target_bytes[0x25] == 0xff && target_bytes[0x28] == 1 &&
        target_bytes[0x2c] == 2 && target_bytes[0x30] == 3 && target_bytes[0x34] == 0x64
    );
    for (std::size_t i = 0x0a; i < 0x1a; ++i)
        CHECK(target_bytes[i] == 0);
    SavedAirTargetGoal target_back;
    CHECK(save_read_air_target_goal(bank.get(), "target", &target_back));
    CHECK(
        target_back.unit_id == 0x21 && target_back.target_id == 0x34 && target_back.flags == 0x81 &&
        target_back.arrival_radius == 16 && target_back.altitude == -8 &&
        target_back.bearing == 0xc000 && target_back.query_point == -1 &&
        target_back.point[0] == 0x10000 && target_back.point[1] == 0x20000 &&
        target_back.point[2] == 0x30000 && target_back.stand_off == 0x640000
    );

    // Air seek: unit id at +08, flags, point, step, the word after step,
    // heading and the word after heading.
    SavedAirSeekGoal seek;
    seek.unit_id = 0x21;
    seek.flags = 1;
    seek.point[0] = 0x50000;
    seek.step[2] = -0x10000;
    seek.reserved_after_step = 0;
    seek.heading = 0x4000;
    seek.reserved_after_heading = 0x1234;
    save_write_air_seek_goal(&seek, bank.get(), "seek");
    const auto seek_bytes = read_blob(bank.get(), "seek");
    CHECK(
        seek_bytes.size() == air_seek_goal_blob_bytes && seek_bytes[0x08] == 0x21 &&
        seek_bytes[0x0a] == 1
    );
    CHECK(seek_bytes[0x0e] == 5 && seek_bytes[0x22] == 0xff && seek_bytes[0x23] == 0xff);
    CHECK(seek_bytes[0x27] == 0x40 && seek_bytes[0x28] == 0x34 && seek_bytes[0x29] == 0x12);
    SavedAirSeekGoal seek_back;
    CHECK(save_read_air_seek_goal(bank.get(), "seek", &seek_back));
    CHECK(
        seek_back.unit_id == 0x21 && seek_back.flags == 1 && seek_back.point[0] == 0x50000 &&
        seek_back.point[1] == 0 && seek_back.step[2] == -0x10000 &&
        seek_back.reserved_after_step == 0 && seek_back.heading == 0x4000 &&
        seek_back.reserved_after_heading == 0x1234
    );

    // A short goal blob leaves the goal as it was.
    const uint8_t short_blob[3] = {1, 2, 3};
    put_blob(bank.get(), "short", short_blob, sizeof(short_blob));
    SavedCircleGoal circle{{1, 2}, 3, 4};
    CHECK(!save_read_circle_goal(bank.get(), "short", &circle));
    CHECK(circle.cell[0] == 1 && circle.tolerance == 3);
}

} // namespace

int main() {
    order_blob_layout();
    write_and_read_move();
    build_type_names();
    unnamed_positions();
    goal_layouts();
    return oa::data::persist::test::finish("persist-orders");
}
