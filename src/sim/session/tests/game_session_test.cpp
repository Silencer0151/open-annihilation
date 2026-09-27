// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/session.hpp"

#include "oa/data/campaign/campaign_file.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using namespace oa::sim::session;

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

struct Recorder {
    std::vector<SessionStep> steps;
    std::vector<AppMode> modes;
    std::vector<std::pair<uint32_t, int32_t>> commanders;
    uint32_t active_mask = 0;
    int32_t unit_limit = 250;
    int32_t random_value = 0;
    bool shuffled = false;
};

Recorder* rec(void* context) {
    return static_cast<Recorder*>(context);
}

SessionHost recording_host(Recorder& r) {
    SessionHost host{};
    host.context = &r;
    host.step = [](void* c, SessionStep s) { rec(c)->steps.push_back(s); };
    host.set_app_mode = [](void* c, AppMode m) { rec(c)->modes.push_back(m); };
    host.read_setting = [](void* c, const char*, int32_t) { return rec(c)->unit_limit; };
    host.random = [](void* c) { return rec(c)->random_value; };
    host.shuffle = [](void* c, int32_t* values, int32_t count) {
        rec(c)->shuffled = true;
        std::reverse(values, values + count);
    };
    host.spawn_commander = [](void* c, uint32_t player, int32_t start) {
        rec(c)->commanders.emplace_back(player, start);
    };
    host.slot_active = [](void* c, uint32_t player) {
        return (rec(c)->active_mask >> player & 1u) != 0;
    };
    return host;
}

bool before(const Recorder& r, SessionStep a, SessionStep b) {
    const auto first = std::find(r.steps.begin(), r.steps.end(), a);
    const auto second = std::find(r.steps.begin(), r.steps.end(), b);
    return first != r.steps.end() && second != r.steps.end() && first < second;
}

bool has(const Recorder& r, SessionStep s) {
    return std::find(r.steps.begin(), r.steps.end(), s) != r.steps.end();
}

void init_tests() {
    Recorder r;
    const auto host = recording_host(r);
    Session session{};
    session.world = oa::world_create();
    session.world->game.outcome_flags = 0xffff;
    r.unit_limit = 900;
    session_init(&session, &host);
    expect(
        session.unit_limit == kMaxUnitLimit && session.world->game.max_units_setting == 500,
        "unit limit clamps high"
    );
    expect((session.world->game.outcome_flags & 3) == 0, "outcome bits cleared");
    expect(session.skirmish_info != nullptr, "skirmish info allocated");
    expect(
        before(r, SessionStep::query_display_size, SessionStep::load_gui_fonts) &&
            before(r, SessionStep::load_gui_fonts, SessionStep::clear_state_buffer),
        "init order"
    );
    expect(r.modes.size() == 1 && r.modes[0] == AppMode::boot, "boot mode");
    r = {};
    r.unit_limit = 3;
    session_init(&session, &host);
    expect(session.unit_limit == kMinUnitLimit, "unit limit clamps low");
    session_shutdown(&session, &host);
    expect(
        session.skirmish_info == nullptr && r.steps.back() == SessionStep::free_subsystem_object,
        "shutdown frees skirmish info last"
    );
    oa::world_destroy(session.world);
}

void campaign_mission_tests() {
    Recorder r;
    const auto host = recording_host(r);
    Session session{};
    session.world = oa::world_create();
    auto* campaign = new oa::data::campaign::CampaignFile;
    oa::data::campaign::campaign_file_init(campaign);
    campaign->kind = oa::data::campaign::SessionKind::campaign;
    session.campaign = campaign;
    session.capacity = {251, 64, 16};
    const int32_t record[4] = {0, 1, 1, 1};
    std::memcpy(session.world->game.session_record, record, sizeof(record));
    session.world->game.tick = 99;
    expect(begin_mission(&session, &host), "campaign mission begins");
    expect(session.world->game.tick == 0, "tick reset");
    expect(session.world->game.visibility_flags == 0x07, "session flags applied");
    expect(
        session.world->units != nullptr && session.world->unit_slot_count == 251 &&
            session.world->unit_def_count == 64 && session.world->projectiles != nullptr,
        "world tables allocated"
    );
    expect(
        before(r, SessionStep::load_unit_availability, SessionStep::init_mission_state),
        "restrictions first"
    );
    expect(
        before(r, SessionStep::create_mission_units, SessionStep::camera_to_start),
        "units then camera"
    );
    expect(r.commanders.empty(), "campaign spawns no commanders");
    expect(
        (session.world->game.load_flags & 2) != 0 &&
            r.steps.back() == SessionStep::dispatch_flagged_state,
        "mission started flag"
    );

    r = {};
    session.world->game.session_flags = 0x05;
    frontend_teardown(&session, &host);
    expect(
        has(r, SessionStep::report_quit_outcome) && has(r, SessionStep::close_multiplayer_session),
        "live game quit"
    );
    expect(
        session.world->units == nullptr && session.world->unit_slot_count == 0, "world tables freed"
    );
    expect(
        (session.world->game.session_flags & 4) == 0 && !has(r, SessionStep::finish_reload_sync),
        "campaign teardown"
    );
    expect(
        before(r, SessionStep::build_score_summary, SessionStep::free_unit_tables),
        "scores before free"
    );

    r = {};
    session.world->game.outcome_flags = 0x14;
    session.world->game.session_flags = 0x0d;
    enter_frontend_mode(&session, &host);
    expect(
        session.world->game.session_flags == 0 && session.world->game.outcome_flags == 0,
        "frontend flags"
    );
    expect(r.modes.size() == 1 && r.modes[0] == AppMode::frontend, "frontend mode");

    oa::data::campaign::campaign_file_free(campaign);
    delete campaign;
    oa::world_destroy(session.world);
}

void skirmish_tests() {
    Recorder r;
    const auto host = recording_host(r);
    Session session{};
    session.world = oa::world_create();
    auto* campaign = new oa::data::campaign::CampaignFile;
    oa::data::campaign::campaign_file_init(campaign);
    campaign->kind = oa::data::campaign::SessionKind::skirmish;
    session.campaign = campaign;
    session.capacity = {501, 8, 8};
    session.skirmish_info = static_cast<uint8_t*>(std::calloc(1, kSkirmishInfoBytes));
    r.active_mask = 0b1011; // players 0, 1, 3
    expect(begin_mission(&session, &host), "skirmish begins");
    expect(r.shuffled, "three players always shuffle");
    expect(
        r.commanders.size() == 3 && r.commanders[0] == std::make_pair(0u, 3) &&
            r.commanders[2] == std::make_pair(3u, 0),
        "commanders take shuffled starts"
    );
    expect(!has(r, SessionStep::create_mission_units), "no mission units in skirmish");

    r = {};
    r.active_mask = 0b11;
    r.random_value = 0x3fff; // 0x3fff * 2 / 0x8000 == 0: keep order
    begin_mission(&session, &host);
    expect(
        !r.shuffled && r.commanders[1] == std::make_pair(1u, 1),
        "two players keep order on a low roll"
    );
    r = {};
    r.active_mask = 0b11;
    r.random_value = 0x4000;
    begin_mission(&session, &host);
    expect(r.shuffled, "two players shuffle on a high roll");

    r = {};
    session.skirmish_info[kSkirmishInfoFixedStartsOffset] = 1;
    r.active_mask = 0b101;
    begin_mission(&session, &host);
    expect(
        !r.shuffled && r.commanders[1] == std::make_pair(2u, 2), "fixed starts use the slot index"
    );

    std::free(session.skirmish_info);
    oa::data::campaign::campaign_file_free(campaign);
    delete campaign;
    oa::world_destroy(session.world);
}

} // namespace

int main() {
    init_tests();
    campaign_mission_tests();
    skirmish_tests();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "game session tests passed\n";
    return 0;
}
