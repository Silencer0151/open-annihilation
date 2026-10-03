// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"

#include "oa/data/defs/unit_catalog.hpp"
#include "oa/ui/hud/order_panel.hpp"
#include "oa/test/game_assets.hpp"
#include "oa/base/text.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace oa;
using namespace oa::ui::hud;

namespace {

constexpr uint32_t page_bits(uint32_t page) {
    return page << kUnitBuildPageShift;
}

struct FakeControls {
    std::vector<std::string> names;
    std::vector<std::string> disabled;
    std::vector<std::pair<std::string, int32_t>> group_values;
    std::vector<std::pair<std::string, int32_t>> values;

    static int32_t find(void* user, const char* name) {
        auto& self = *static_cast<FakeControls*>(user);
        for (std::size_t i = 0; i < self.names.size(); ++i)
            if (self.names[i] == name)
                return static_cast<int32_t>(i);
        return -1;
    }

    static void set_group(void* user, int32_t index, int32_t value) {
        auto& self = *static_cast<FakeControls*>(user);
        self.group_values.emplace_back(self.names[static_cast<std::size_t>(index)], value);
    }

    static void set(void* user, int32_t index, int32_t value) {
        auto& self = *static_cast<FakeControls*>(user);
        self.values.emplace_back(self.names[static_cast<std::size_t>(index)], value);
    }

    static void disable(void* user, int32_t index) {
        auto& self = *static_cast<FakeControls*>(user);
        self.disabled.push_back(self.names[static_cast<std::size_t>(index)]);
    }

    PanelControls controls() { return {this, find, set_group, set, disable, nullptr}; }

    bool is_disabled(const char* name) const {
        for (const auto& entry : disabled)
            if (entry == name)
                return true;
        return false;
    }

    int32_t group_value(const char* name) const {
        int32_t found = -99;
        for (const auto& [entry, value] : group_values)
            if (entry == name)
                found = value;
        return found;
    }
};

struct Recorder {
    std::vector<std::string> sounds;
    std::vector<std::pair<std::string, int32_t>> orders;

    static void play(void* user, const char* name) {
        static_cast<Recorder*>(user)->sounds.emplace_back(name);
    }

    static void order(void* user, const char* tag, int32_t value) {
        static_cast<Recorder*>(user)->orders.emplace_back(tag, value);
    }

    HudEvents events() { return {this, play, order}; }
};

struct Pool {
    std::vector<Unit> units = std::vector<Unit>(8);
    std::vector<UnitDef> defs = std::vector<UnitDef>(4);

    UnitTable table() {
        return {
            units.data(),
            static_cast<uint16_t>(units.size()),
            defs.data(),
            static_cast<int32_t>(defs.size())
        };
    }
};

void set_name(UnitDef& def, const char* name) {
    std::memset(def.unit_name, 0, sizeof def.unit_name);
    oa::base::text::copy_padded(def.unit_name, name, sizeof def.unit_name - 1);
}

void test_paging_values() {
    // ARMCOM ships ARMCOM1..4.GUI, so its page count is 5.
    constexpr uint8_t count = 5;
    uint32_t flags = kUnitFlagBuildMenu | page_bits(1);
    const uint32_t expect_forward[] = {2, 3, 4, 1, 2};
    for (const auto page : expect_forward) {
        flags = build_menu_forward(flags, count, false);
        CHECK(build_page(flags) == page);
        CHECK((flags & kUnitFlagBuildMenu) != 0);
    }
    flags = page_bits(1);
    CHECK(build_page(build_menu_back(flags, count, false)) == 4);
    CHECK(build_page(build_menu_back(page_bits(3), count, false)) == 2);
    CHECK((build_menu_back(page_bits(3), count, false) & kUnitFlagBuildMenu) != 0);
    // Page 0 wraps to the last page as well.
    CHECK(build_page(build_menu_back(0, count, false)) == 4);

    // The cycling variants walk through the order page.
    constexpr uint32_t fire_order_bit = 1u << kUnitFireOrderShift;
    flags = fire_order_bit; // unrelated fire-order bits must survive
    flags = build_menu_forward(flags, count, true);
    CHECK((flags & kUnitFlagBuildMenu) != 0 && build_page(flags) == 1);
    for (uint32_t page = 2; page <= 4; ++page) {
        flags = build_menu_forward(flags, count, true);
        CHECK(build_page(flags) == page && (flags & kUnitFlagBuildMenu) != 0);
    }
    flags = build_menu_forward(flags, count, true);
    CHECK((flags & kUnitFlagBuildMenu) == 0 && build_page(flags) == 4);
    CHECK((flags & fire_order_bit) != 0);

    flags = build_menu_back(page_bits(2), count, true);
    CHECK((flags & kUnitFlagBuildMenu) != 0 && build_page(flags) == 4);
    flags = build_menu_back(kUnitFlagBuildMenu | page_bits(1), count, true);
    CHECK((flags & kUnitFlagBuildMenu) == 0 && build_page(flags) == 1);
    flags = build_menu_back(kUnitFlagBuildMenu | page_bits(3), count, true);
    CHECK((flags & kUnitFlagBuildMenu) != 0 && build_page(flags) == 2);

    // Selecting a page.
    flags = build_menu_select(page_bits(3), count, 2);
    CHECK(build_page(flags) == 2 && (flags & kUnitFlagBuildMenu) != 0);
    flags = build_menu_select(kUnitFlagBuildMenu | page_bits(3), count, 0);
    CHECK(build_page(flags) == 3 && (flags & kUnitFlagBuildMenu) == 0);
    CHECK(build_menu_select(page_bits(3), count, 5) == page_bits(3));
}

std::string lower(std::string text) {
    for (auto& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// The commanders' build pages and the ARM general page among the installed
// game's GUI files.
void test_installed_build_lists(const AssetStore& assets) {
    const auto has_gui = [&](const char* name) {
        return assets.file_size("guis/" + lower(name)) != 0;
    };
    for (const char* unit : {"ARMCOM", "CORCOM"}) {
        UnitDef def{};
        set_name(def, unit);
        uint8_t count = 1;
        char name[64];
        while (true) {
            format_build_page_name(name, sizeof name, def, count);
            if (!has_gui(name))
                break;
            ++count;
        }
        CHECK(count == 5);
        def.gui_page_count = count;
        // One full forward cycle visits every page file, then the order page.
        uint32_t flags = 0;
        for (int step = 0; step < 4; ++step) {
            flags = build_menu_forward(flags, count, true);
            format_build_page_name(name, sizeof name, def, build_page(flags));
            CHECK(has_gui(name));
        }
        CHECK((build_menu_forward(flags, count, true) & kUnitFlagBuildMenu) == 0);
    }
    char general[64];
    format_general_page_name(general, sizeof general, "ARM");
    CHECK(std::strcmp(general, "ARMGEN.GUI") == 0);
    CHECK(has_gui(general));
}

void test_panel_paging_and_requests() {
    Pool pool;
    pool.defs[1].gui_page_count = 3;
    pool.units[2].type_index = 1;
    pool.units[2].id = 2;
    pool.units[2].flags = kUnitFlagBuildMenu | page_bits(1);
    Recorder recorder;
    OrderPanelState state{2, 1, 0, 0, 0};
    order_panel_page_forward(state, pool.table(), false, recorder.events());
    CHECK(build_page(pool.units[2].flags) == 2);
    CHECK((state.frame_flags & kFrameRedrawBuildMenu) != 0);
    CHECK(recorder.sounds.size() == 1 && recorder.sounds[0] == "nextbuildmenu");

    state.frame_flags = kFramePageBack;
    order_panel_handle_requests(state, pool.table(), recorder.events());
    CHECK(build_page(pool.units[2].flags) == 1);
    CHECK((state.frame_flags & kFramePageBack) == 0);

    state.frame_flags = kFrameBuildMenuOff;
    order_panel_handle_requests(state, pool.table(), recorder.events());
    CHECK((pool.units[2].flags & kUnitFlagBuildMenu) == 0);
    state.frame_flags = kFrameBuildMenuOn;
    order_panel_handle_requests(state, pool.table(), recorder.events());
    CHECK((pool.units[2].flags & kUnitFlagBuildMenu) != 0);

    // No loaded unit: the sound still plays.
    OrderPanelState empty{};
    order_panel_page_back(empty, pool.table(), false, recorder.events());
    CHECK(recorder.sounds.back() == "nextbuildmenu");
    CHECK(empty.frame_flags == 0);
}

void test_selection_summary() {
    Pool pool;
    pool.defs[1].abilities = kAbilityFireOrders | kAbilityMoveOrders | kAbilityMove | kAbilityStop;
    pool.defs[2].abilities = kAbilityFireOrders | kAbilityCloak | kAbilityBlast | kAbilityAttack;
    pool.units[1].type_index = 1;
    pool.units[1].flags =
        kUnitFlagPanelSelected | (1u << kUnitFireOrderShift) | (2u << kUnitMoveOrderShift);
    pool.units[2].type_index = 2;
    pool.units[2].flags = kUnitFlagPanelSelected | (2u << kUnitFireOrderShift) | 0x800u;
    pool.units[3].type_index = 1; // not selected
    OrderPanelState state{};
    auto summary = summarize_selection(state, pool.table(), 1, 3);
    CHECK(summary.count == 2 && summary.first == &pool.units[1]);
    CHECK(((summary.frame_flags >> kFrameFireOrderShift) & 7u) == kStandingMixed);
    CHECK((summary.order_flags & 7u) == 2);
    CHECK(((summary.order_flags >> kOrderCloakShift) & 3u) == 1);
    CHECK(((summary.order_flags >> kOrderOnOffShift) & 3u) == kToggleNone);
    CHECK(
        (summary.order_flags & (kOrderCanMove | kOrderCanStop | kOrderCanAttack)) ==
        (kOrderCanMove | kOrderCanStop | kOrderCanAttack)
    );
    CHECK((summary.order_flags & kOrderCanPatrol) == 0);
    CHECK((summary.order_flags2 & kOrder2CanBlast) != 0);

    // Two cloakers always read as mixed.
    pool.units[1].type_index = 2;
    pool.units[1].flags = kUnitFlagPanelSelected | 0x800u;
    summary = summarize_selection(state, pool.table(), 1, 2);
    CHECK(((summary.order_flags >> kOrderCloakShift) & 3u) == kToggleMixed);
}

void test_order_buttons() {
    FakeControls fake;
    fake.names = {
        "BUILD",
        "ORDERS",
        "CLOAK",
        "ONOFF",
        "MOVEORD",
        "FIREORD",
        "MOVE",
        "STOP",
        "ATTACK",
        "DEFEND",
        "PATROL",
        "RECLAIM",
        "REPAIR",
        "CAPTURE",
        "LOAD",
        "UNLOAD",
        "BLAST"
    };
    OrderPanelState state{};
    state.order_flags = static_cast<uint16_t>(
        1u | (kToggleNone << kOrderCloakShift) | (0u << kOrderOnOffShift) | kOrderCanMove |
        kOrderCanAttack
    );
    state.frame_flags = static_cast<uint16_t>(kStandingNone << kFrameFireOrderShift);
    Unit unit{};
    unit.flags = kUnitFlagBuildMenu;
    UnitDef def{};
    def.gui_page_count = 3;
    refresh_order_buttons(fake.controls(), state, &unit, &def);
    CHECK(fake.group_value("BUILD") == 1 && fake.group_value("ORDERS") == 0);
    CHECK(fake.is_disabled("CLOAK") && !fake.is_disabled("ONOFF"));
    CHECK(fake.group_value("ONOFF") == 0 && fake.group_value("MOVEORD") == 1);
    CHECK(fake.is_disabled("FIREORD"));
    CHECK(!fake.is_disabled("MOVE") && !fake.is_disabled("ATTACK"));
    CHECK(fake.is_disabled("STOP") && fake.is_disabled("PATROL") && fake.is_disabled("CAPTURE"));
    CHECK(fake.is_disabled("UNLOAD") && fake.is_disabled("BLAST"));

    FakeControls none;
    none.names = fake.names;
    refresh_order_buttons(none.controls(), state, nullptr, nullptr);
    CHECK(none.is_disabled("BUILD") && none.is_disabled("ORDERS"));

    UnitDef single{};
    single.gui_page_count = 1;
    FakeControls pages;
    pages.names = {"ARMPREV", "ARMNEXT"};
    disable_page_buttons(pages.controls(), single, "ARM");
    CHECK(pages.values.size() == 2 && pages.values[0].first == "ARMPREV");
}

void test_order_toggles() {
    Pool pool;
    FakeControls fake;
    fake.names = {"MOVEORD", "FIREORD", "ONOFF", "CLOAK"};
    Recorder recorder;
    OrderPanelState state{};
    const uint32_t moves[] = {1, 2, 0, 1};
    for (const auto expect : moves) {
        CHECK(order_panel_toggle(
            state, pool.table(), fake.controls(), 0, "MOVEORD", recorder.events()
        ));
        CHECK((state.order_flags & 7u) == expect);
        CHECK(recorder.orders.back().first == "STANDING_MOVEORDER");
        CHECK(recorder.orders.back().second == static_cast<int32_t>(expect));
        CHECK(recorder.sounds.back() == "setmoveorders");
    }
    state.frame_flags = static_cast<uint16_t>(kStandingMixed << kFrameFireOrderShift);
    CHECK(
        order_panel_toggle(state, pool.table(), fake.controls(), 1, "FIREORD", recorder.events())
    );
    CHECK(((state.frame_flags >> kFrameFireOrderShift) & 7u) == 0);
    CHECK(recorder.orders.back().second == 0);

    // A move summary of "none" plays the sound but issues no order.
    const auto issued = recorder.orders.size();
    state.order_flags = static_cast<uint16_t>((state.order_flags & ~7u) | kStandingNone);
    CHECK(
        order_panel_toggle(state, pool.table(), fake.controls(), 0, "MOVEORD", recorder.events())
    );
    CHECK(recorder.orders.size() == issued);

    CHECK(order_panel_toggle(state, pool.table(), fake.controls(), 2, "ONOFF", recorder.events()));
    CHECK(recorder.orders.back().first == "ACTIVATE");
    CHECK(((state.order_flags >> kOrderOnOffShift) & 3u) == 1);
    CHECK(order_panel_toggle(state, pool.table(), fake.controls(), 2, "ONOFF", recorder.events()));
    CHECK(recorder.orders.back().first == "DEACTIVATE");
    CHECK(order_panel_toggle(state, pool.table(), fake.controls(), 3, "CLOAK", recorder.events()));
    CHECK(recorder.orders.back().first == "CLOAK_ON");
    CHECK(order_panel_toggle(state, pool.table(), fake.controls(), 3, "CLOAK", recorder.events()));
    CHECK(recorder.orders.back().first == "CLOAK_OFF");
    CHECK(recorder.sounds.back() == "specialorders");
    CHECK(
        !order_panel_toggle(state, pool.table(), fake.controls(), 0, "ATTACK", recorder.events())
    );
}

// ARMPW takes both standing orders, ARMLLT (firestandorders only) the fire
// order and ARMSOLAR (onoffable) neither; the toggle tags come in capitals
// and the order table's names in mixed case.
void test_group_order_reach() {
    UnitDef peewee{};
    peewee.abilities = kAbilityFireOrders | kAbilityMoveOrders | kAbilityStop | kAbilityAttack;
    UnitDef tower{};
    tower.abilities = kAbilityFireOrders | kAbilityStop | kAbilityAttack;
    UnitDef solar{};
    solar.abilities = kAbilityOnOff;
    CHECK(group_order_reaches("STANDING_FIREORDER", peewee));
    CHECK(group_order_reaches("STANDING_MOVEORDER", peewee));
    CHECK(group_order_reaches("Standing_FireOrder", tower));
    CHECK(!group_order_reaches("Standing_MoveOrder", tower));
    CHECK(!group_order_reaches("STANDING_FIREORDER", solar));
    CHECK(!group_order_reaches("STANDING_MOVEORDER", solar));
    for (const char* tag : {"ACTIVATE", "DEACTIVATE", "CLOAK_ON", "CLOAK_OFF"}) {
        CHECK(group_order_reaches(tag, solar));
        CHECK(group_order_reaches(tag, peewee));
    }
    CHECK(!group_order_reaches(nullptr, peewee));
}

struct FakeLoader {
    std::vector<std::string> loaded;
    std::string current;

    static bool close(void*) { return true; }

    static bool is_loaded(void* user, const char* name) {
        return static_cast<FakeLoader*>(user)->current == name;
    }

    static bool load(void* user, const char* name, const Unit*, int32_t) {
        auto& self = *static_cast<FakeLoader*>(user);
        self.loaded.emplace_back(name);
        self.current = name;
        return true;
    }

    PanelLoader loader() { return {this, close, is_loaded, load}; }
};

void test_update_order_panel() {
    Pool pool;
    set_name(pool.defs[1], "ARMCOM");
    pool.defs[1].gui_page_count = 5;
    pool.units[1].type_index = 1;
    pool.units[1].id = 1;
    pool.units[1].flags = kUnitFlagPanelSelected | kUnitFlagBuildMenu | page_bits(2);
    FakeControls fake;
    FakeLoader loader;
    OrderPanelState state{};
    update_order_panel(state, pool.table(), 1, 7, 0, "ARM", fake.controls(), loader.loader());
    CHECK(loader.loaded.size() == 1 && loader.loaded[0] == "ARMCOM2.GUI");
    CHECK(state.unit_id == 1 && state.unit_type == 1);
    CHECK((state.frame_flags & kFrameReloadOrders) == 0);

    // A second unit in the selection switches to the general page.
    pool.units[3].type_index = 1;
    pool.units[3].id = 3;
    pool.units[3].flags = kUnitFlagPanelSelected;
    state.unit_id = 0;
    update_order_panel(state, pool.table(), 1, 7, 0, "ARM", fake.controls(), loader.loader());
    CHECK(loader.loaded.back() == "ARMGEN.GUI");
    CHECK(state.unit_id == 0);

    // A modal HUD state defers the reload.
    const auto count = loader.loaded.size();
    state.unit_id = 0;
    state.frame_flags = kFrameSharePanelOpen;
    update_order_panel(state, pool.table(), 1, 7, 0, "ARM", fake.controls(), loader.loader());
    CHECK(loader.loaded.size() == count);
    CHECK((state.frame_flags & kFrameRedrawBuildMenu) != 0);

    // An unfinished unit never loads its build page.
    pool.units[3].flags = 0;
    pool.units[1].build_remaining = 0.5F;
    state = {};
    update_order_panel(state, pool.table(), 1, 7, 0, "ARM", fake.controls(), loader.loader());
    CHECK(loader.loaded.size() == count);
}

uint16_t type_for(void*, const char* name) {
    return std::strcmp(name, "ARMLAB") == 0 ? 7 : 0;
}

void test_build_queue_and_scroll() {
    Recorder recorder;
    Unit builder{};
    builder.owner_index = 1;
    auto change =
        classify_build_queue_change("ARMLAB", builder, 1, 1, type_for, nullptr, recorder.events());
    CHECK(change.kind == BuildQueueKind::building && change.type == 7);
    CHECK(recorder.sounds.back() == "addbuild");
    builder.movement = 1;
    change =
        classify_build_queue_change("ARMLAB", builder, 1, -1, type_for, nullptr, recorder.events());
    CHECK(change.kind == BuildQueueKind::mobile && recorder.sounds.back() == "subbuild");
    const auto sounds = recorder.sounds.size();
    change = classify_build_queue_change(
        "ARMSILO_MAKENUKE", builder, 0, 1, type_for, nullptr, recorder.events()
    );
    CHECK(change.kind == BuildQueueKind::weapon && change.type == 0);
    CHECK(std::strcmp(change.tag, "BUILDWEAPON") == 0);
    CHECK(recorder.sounds.size() == sounds);
    change = classify_build_queue_change(
        "ARMMAKEANTI", builder, 0, -1, type_for, nullptr, recorder.events()
    );
    CHECK(change.kind == BuildQueueKind::weapon && std::strcmp(change.tag, "BUILDWEAPON") == 0);
    change =
        classify_build_queue_change("NOTHING", builder, 0, 1, type_for, nullptr, recorder.events());
    CHECK(change.kind == BuildQueueKind::none);

    auto game = std::make_unique<Game>();
    game->scroll_speed = 9;
    game->camera_x = 123;
    game->radar_blink_countdown = 4;
    clear_scroll_state(*game);
    CHECK(game->scroll_speed == 9 && game->camera_x == 0 && game->radar_blink_countdown == 4);

    OrderPanelState state{5, 6, 7, 8, 9};
    order_panel_store(*game, state);
    const auto loaded = order_panel_load(*game);
    CHECK(loaded.unit_id == 5 && loaded.unit_type == 6 && loaded.frame_flags == 7);
    CHECK(loaded.order_flags == 8 && loaded.order_flags2 == 9);
    CHECK(game->frame_flags == 7);
}

// AC02's use-only list leaves ARMCOM ARMLAB, ARMLLT, ARMMEX and ARMSOLAR.
uint16_t ac02_type(void*, const char* name) {
    static const char* const kAllowed[] = {"ARMCOM", "ARMLAB", "ARMLLT", "ARMMEX", "ARMSOLAR"};
    for (std::size_t index = 0; index < sizeof kAllowed / sizeof kAllowed[0]; ++index)
        if (std::strcmp(name, kAllowed[index]) == 0)
            return static_cast<uint16_t>(index + 1);
    return 0;
}

int32_t queued_weapons = 0;

int32_t queued_for(void*, const Unit&, uint16_t type) {
    if (type == 0)
        return queued_weapons;
    return type == 5 ? 3 : 0; // three ARMSOLAR
}

BuildPageRecord record(const char* name, uint8_t type, uint8_t attributes) {
    BuildPageRecord result{};
    result.name = name;
    result.gadget_type = type;
    result.common_attributes = attributes;
    return result;
}

// ARMCOM1.GUI's records: the page, ARMFONT, PREV/NEXT, six unit buttons and
// the order buttons, with a unit button last to show the unvisited record.
void test_build_page_buttons() {
    BuildPageRecord records[] = {
        record("ARMCOM1.GUI", 0, 0),
        record("ARMFONT", 7, 0),
        record("ARMPREV", kGadgetTypeButton, 0),
        record("ARMSOLAR", kGadgetTypeButton, kCommonUnitButton),
        record("ARMWIN", kGadgetTypeButton, kCommonUnitButton),
        record("ARMMEX", kGadgetTypeButton, kCommonUnitButton),
        record("ARMMAKR", kGadgetTypeButton, kCommonUnitButton),
        record("ARMMAKENUKE", kGadgetTypeButton, kCommonWeaponButton),
        record("ARMORDERS", kGadgetTypeButton, 0),
        record("ARMESTOR", kGadgetTypeButton, kCommonUnitButton),
    };
    const int32_t gadgets = static_cast<int32_t>(sizeof records / sizeof records[0]) - 1;
    std::snprintf(records[4].caption, kBuildCaptionBytes, "old");
    std::snprintf(records[5].caption, kBuildCaptionBytes, "+1");
    Unit builder{};
    builder.weapons[0].stockpile = 2;
    queued_weapons = 1;
    format_build_counts(records, gadgets, builder, ac02_type, queued_for, nullptr);
    CHECK(std::strcmp(records[3].caption, "+3") == 0);
    CHECK(std::strcmp(records[4].caption, "old") == 0);
    CHECK(records[5].caption[0] == '\0');
    CHECK(std::strcmp(records[7].caption, "2 +1") == 0);
    builder.weapons[0].stockpile = 0;
    queued_weapons = 0;
    format_build_counts(records, gadgets, builder, ac02_type, queued_for, nullptr);
    CHECK(records[7].caption[0] == '\0');

    update_build_button_validity(records, gadgets, ac02_type, nullptr);
    CHECK(!records[3].grayed && !records[5].grayed);
    CHECK(records[4].grayed && records[6].grayed);
    CHECK(!records[2].grayed && !records[7].grayed && !records[8].grayed);
    CHECK(!records[9].grayed);
    records[4].name = "ARMLAB";
    update_build_button_validity(records, gadgets, ac02_type, nullptr);
    CHECK(!records[4].grayed);
}

// A loader over a GUI directory holding ARMLAB1.GUI and ARMDL.GUI only.
struct PageLoader {
    std::vector<std::string> loaded;
    std::vector<std::pair<int32_t, std::string>> links;
    int32_t redraws = 0;
    int32_t counts = 0;
    int32_t validity = 0;

    static bool close(void*) { return true; }

    static bool is_loaded(void*, const char*) { return false; }

    static bool load(void* user, const char* name, const Unit*, int32_t) {
        static_cast<PageLoader*>(user)->loaded.emplace_back(name);
        return true;
    }

    static bool exists(void*, const char* name) {
        return std::strcmp(name, "ARMLAB1.GUI") == 0 || std::strcmp(name, "ARMDL") == 0;
    }

    static void link(void* user, int32_t gadget, const char* unit_name) {
        static_cast<PageLoader*>(user)->links.emplace_back(gadget, unit_name);
    }

    static void redraw(void* user) { ++static_cast<PageLoader*>(user)->redraws; }

    static void format(void* user, const Unit&) { ++static_cast<PageLoader*>(user)->counts; }

    static void check(void* user) { ++static_cast<PageLoader*>(user)->validity; }
};

void set_entry(
    data::defs::DownloadMenuEntry& entry,
    uint16_t builder,
    uint8_t menu,
    uint8_t button,
    const char* unit_name
) {
    entry.builder_index = builder;
    entry.menu = menu;
    entry.button = button;
    std::snprintf(entry.unit_name, sizeof entry.unit_name, "%s", unit_name);
}

// download\armwar.tdf and armflea.tdf give ARMLAB MENU=3 BUTTON=0 and 1,
// which raise its page count from 2 (ARMLAB1.GUI) to 3: page 2 has no GUI
// file, so it is the side's download page with the two buttons linked.
void test_download_build_page() {
    Pool pool;
    set_name(pool.defs[2], "ARMLAB");
    pool.defs[2].type_id = 2;
    pool.defs[2].gui_page_count = 3;
    pool.units[4].type_index = 2;
    pool.units[4].id = 4;
    pool.units[4].flags = OA_UNIT_FLAG_BUILDING;
    data::defs::DownloadMenuGroup groups[3]{};
    groups[0].count = 1;
    set_entry(groups[0].entries[0], 2, 3, 0, "ARMWAR");
    groups[1].count = 2;
    set_entry(groups[1].entries[0], 3, 3, 1, "ARMFLAK");
    set_entry(groups[1].entries[1], 2, 3, 1, "ARMFLEA");
    groups[2].count = 1;
    set_entry(groups[2].entries[0], 2, 2, 5, "ARMSPY");
    PageLoader pages;
    PanelLoader loader{&pages, PageLoader::close, PageLoader::is_loaded, PageLoader::load};
    loader.downloads = groups;
    loader.download_count = 3;
    loader.link_button = PageLoader::link;
    loader.redraw = PageLoader::redraw;
    loader.exists = PageLoader::exists;
    loader.format_counts = PageLoader::format;
    loader.check_validity = PageLoader::check;
    FakeControls fake;
    fake.names = {"HEADER", "ARMFONT", "ARMPREV", "ARMNEXT", "ONOFF"};
    OrderPanelState state{};

    char name[64];
    format_build_page_name(name, sizeof name, pool.defs[2], 2);
    load_build_page(state, pool.units[4], pool.defs[2], name, 2, "ARM", fake.controls(), loader);
    CHECK(pages.loaded.size() == 1 && pages.loaded[0] == "ARMDL");
    CHECK(pages.links.size() == 2);
    CHECK(pages.links[0].first == 4 && pages.links[0].second == "ARMWAR");
    CHECK(pages.links[1].first == 5 && pages.links[1].second == "ARMFLEA");
    CHECK(pages.redraws == 1 && pages.counts == 1 && pages.validity == 1);
    CHECK(state.unit_id == 4 && state.unit_type == 2);

    // Page 1 has its own GUI and a MENU=2 entry; nothing else links there.
    format_build_page_name(name, sizeof name, pool.defs[2], 1);
    load_build_page(state, pool.units[4], pool.defs[2], name, 1, "ARM", fake.controls(), loader);
    CHECK(pages.loaded.back() == "ARMLAB1.GUI");
    CHECK(
        pages.links.size() == 3 && pages.links[2].first == 9 && pages.links[2].second == "ARMSPY"
    );
    CHECK(pages.redraws == 2 && pages.validity == 2);

    // Page 0 skips the validity pass; an unfinished unit loads nothing.
    load_build_page(
        state, pool.units[4], pool.defs[2], "ARMLAB0.GUI", 0, "COR", fake.controls(), loader
    );
    CHECK(pages.loaded.back() == "CORDL" && pages.validity == 2 && pages.counts == 3);
    pool.units[4].build_remaining = 0.25F;
    load_build_page(
        state, pool.units[4], pool.defs[2], "ARMLAB1.GUI", 1, "ARM", fake.controls(), loader
    );
    CHECK(pages.loaded.size() == 3);
}

struct ClickHost {
    std::vector<std::pair<std::string, int32_t>> changes;
    int32_t counts = 0;
    bool shift = false;
    bool stockpile = false;
    bool order = false;

    static uint16_t type_for(void*, const char* name) {
        if (std::strcmp(name, "ARMSOLAR") == 0)
            return 1;
        return std::strcmp(name, "ARMPW") == 0 ? 3 : 0;
    }

    static bool order_click(void* user, const char*) {
        return static_cast<ClickHost*>(user)->order;
    }

    static bool shift_down(void* user) { return static_cast<ClickHost*>(user)->shift; }

    static void change(void* user, const char* name, Unit&, int32_t count) {
        static_cast<ClickHost*>(user)->changes.emplace_back(name, count);
    }

    static bool stockpiles(void* user, const Unit&) {
        return static_cast<ClickHost*>(user)->stockpile;
    }

    static void format(void* user, const Unit&) { ++static_cast<ClickHost*>(user)->counts; }

    BuildPanelHost host() {
        return {this, type_for, order_click, shift_down, change, stockpiles, format};
    }
};

void test_build_panel_clicks() {
    Pool pool;
    pool.defs[1].bm_code = 0; // ARMSOLAR, a building
    pool.defs[3].bm_code = 1; // ARMPW
    pool.units[2].type_index = 2;
    pool.units[2].id = 2;
    pool.units[2].flags = kUnitFlagPanelSelected | OA_UNIT_FLAG_BUILDING;
    ClickHost clicks;
    Recorder recorder;
    OrderPanelState state{2, 2, 0, 0, 0};
    const auto click = [&](const char* name, bool left) {
        return on_build_panel_click(
            state, pool.table(), name, left, clicks.host(), recorder.events()
        );
    };

    CHECK(click("ARMPREV", true).action == BuildPanelClick::page_back);
    CHECK(state.frame_flags == kFramePageBack);
    CHECK(click("ARMNEXT", false).action == BuildPanelClick::page_forward);
    CHECK((state.frame_flags & kFramePageForward) != 0);
    CHECK(click("ARMORDERS", true).action == BuildPanelClick::orders);
    CHECK(
        (state.frame_flags & kFrameBuildMenuOff) != 0 && recorder.sounds.back() == "ordersbutton"
    );
    CHECK(click("ARMBUILD", true).action == BuildPanelClick::build);
    CHECK((state.frame_flags & kFrameBuildMenuOn) != 0 && recorder.sounds.back() == "buildbutton");
    CHECK(clicks.changes.empty());

    // A building arms placement, with either button.
    const auto place = click("ARMSOLAR", false);
    CHECK(place.action == BuildPanelClick::place && place.type == 1);
    CHECK(recorder.sounds.back() == "addbuild" && clicks.changes.empty());

    // A mobile unit and a missile change the queue by one, or five with shift,
    // down with the right button; a building's counts are rewritten.
    CHECK(click("ARMPW", true).action == BuildPanelClick::queued);
    clicks.shift = true;
    CHECK(click("ARMPW", false).action == BuildPanelClick::queued);
    clicks.shift = false;
    CHECK(click("ARMMAKENUKE", false).action == BuildPanelClick::queued);
    CHECK(clicks.changes.size() == 3);
    CHECK(clicks.changes[0] == std::make_pair(std::string("ARMPW"), 1));
    CHECK(clicks.changes[1] == std::make_pair(std::string("ARMPW"), -5));
    CHECK(clicks.changes[2] == std::make_pair(std::string("ARMMAKENUKE"), -1));
    CHECK(clicks.counts == 3);

    // ui.selection-shortcuts: Ctrl with shift steps the queue by the host's step.
    auto hundred = clicks.host();
    hundred.shift_step = 100;
    clicks.shift = true;
    CHECK(
        on_build_panel_click(state, pool.table(), "ARMPW", false, hundred, recorder.events())
            .action == BuildPanelClick::queued
    );
    clicks.shift = false;
    CHECK(clicks.changes.back() == std::make_pair(std::string("ARMPW"), -100));
    clicks.changes.pop_back();
    --clicks.counts;

    // A mobile panel unit rewrites its counts only when it stockpiles.
    pool.units[2].flags = kUnitFlagPanelSelected;
    CHECK(click("ARMMAKEANTI", true).action == BuildPanelClick::queued && clicks.counts == 3);
    clicks.stockpile = true;
    CHECK(click("ARMMAKEANTI", true).action == BuildPanelClick::queued && clicks.counts == 4);

    // Order buttons the order handlers take, and an unselected panel unit,
    // leave the queue alone.
    clicks.order = true;
    CHECK(click("ARMSTOP", true).action == BuildPanelClick::none);
    clicks.order = false;
    pool.units[2].flags = 0;
    CHECK(click("ARMPW", true).action == BuildPanelClick::none);
    state.unit_id = 0;
    CHECK(click("ARMPW", true).action == BuildPanelClick::none);
    CHECK(clicks.changes.size() == 5);
}

// units.placement-by-builder: the panel unit's type decides between placement
// and the queue; a mobile builder places every type, a factory queues them.
void test_build_panel_placement_by_builder() {
    Pool pool;
    pool.defs[1].bm_code = 0; // ARMSOLAR, a building
    pool.defs[2].bm_code = 0; // the factory
    pool.defs[3].bm_code = 1; // ARMPW, mobile
    pool.units[2].type_index = 2;
    pool.units[2].id = 2;
    pool.units[2].flags = kUnitFlagPanelSelected | OA_UNIT_FLAG_BUILDING;
    ClickHost clicks;
    Recorder recorder;
    OrderPanelState state{2, 2, 0, 0, 0};
    auto host = clicks.host();
    const auto click = [&](const char* name) {
        return on_build_panel_click(state, pool.table(), name, true, host, recorder.events());
    };

    // 3.1c: the clicked item decides, whatever builds it.
    state.unit_type = 3;
    CHECK(click("ARMSOLAR").action == BuildPanelClick::place);
    CHECK(click("ARMPW").action == BuildPanelClick::queued);

    host.placement_by_builder = true;
    const auto mobile = click("ARMPW");
    CHECK(mobile.action == BuildPanelClick::place && mobile.type == 3);
    CHECK(recorder.sounds.back() == "addbuild");
    const auto building = click("ARMSOLAR");
    CHECK(building.action == BuildPanelClick::place && building.type == 1);

    // A factory queues every item, buildings too.
    state.unit_type = 2;
    const auto changes = clicks.changes.size();
    CHECK(click("ARMSOLAR").action == BuildPanelClick::queued);
    CHECK(click("ARMPW").action == BuildPanelClick::queued);
    CHECK(clicks.changes.size() == changes + 2);
    CHECK(clicks.changes.back() == std::make_pair(std::string("ARMPW"), 1));

    // A panel type past the table places nothing.
    state.unit_type = 9;
    CHECK(click("ARMPW").action == BuildPanelClick::queued);
    // Names that are no unit type keep their own handling.
    state.unit_type = 3;
    CHECK(click("ARMMAKENUKE").action == BuildPanelClick::queued);
    CHECK(click("ARMBUILD").action == BuildPanelClick::build);
}

} // namespace

int main(int argc, char** argv) {
    if (test::game_data_requested(argc, argv)) {
        test_installed_build_lists(test::require_game_assets("the installed build pages"));
        return 0;
    }
    test_paging_values();
    test_panel_paging_and_requests();
    test_selection_summary();
    test_order_buttons();
    test_order_toggles();
    test_group_order_reach();
    test_update_order_panel();
    test_build_queue_and_scroll();
    test_build_page_buttons();
    test_download_build_page();
    test_build_panel_clicks();
    test_build_panel_placement_by_builder();
    return 0;
}
