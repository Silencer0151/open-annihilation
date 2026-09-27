// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The order page's FIRE ORDERS, MOVE ORDERS, ON/OFF and CLOAK buttons in a
// live match, clicked through the SDL presenter.
#include "oa/app/runtime.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {

namespace {

// Standing orders and ARMGEN.GUI art frames. The FBI orders of ARMPW
// (StandingFireOrder=2, StandingMoveOrder=1) start the check; ARMSOLAR takes
// neither but is onoffable. ARMCOM.FBI and CORCOM.FBI give the commander
// StandingFireOrder=2 and StandingMoveOrder=0.
constexpr uint32_t hold_fire = 0;
constexpr uint32_t return_fire = 1;
constexpr uint32_t fire_at_will = 2;
constexpr uint32_t mixed_orders = 3; // the "FIRE ORDERS" / "MOVE ORDERS" frame
constexpr uint32_t hold_position = 0;
constexpr uint32_t maneuver = 1;
constexpr uint32_t roam = 2;
// Ticks for a sighting refresh and a weapon sweep to bring a unit in range
// under a fire-at-will unit's guns.
constexpr int kTargetTicks = 240;

uint32_t fire_order(const oa::Unit& unit) {
    return (unit.flags & OA_UNIT_FLAG_FIRE_ORDER_MASK) >> OA_UNIT_FLAG_FIRE_ORDER_SHIFT;
}

uint32_t move_order(const oa::Unit& unit) {
    return (unit.flags & OA_UNIT_FLAG_MOVE_ORDER_MASK) >> OA_UNIT_FLAG_MOVE_ORDER_SHIFT;
}

bool targets(const oa::Unit& shooter, uint16_t target) {
    for (const auto& weapon : shooter.weapons)
        if (weapon.target_b == OA_UNIT_TARGET_IS_UNIT &&
            weapon.target_a == static_cast<int16_t>(target))
            return true;
    return false;
}

void require(bool condition, const std::string& what) {
    if (!condition)
        throw std::runtime_error("match order check: " + what);
}

} // namespace

void Runtime::check_match_orders() {
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    start_benchmark_skirmish();
    auto& slots = match_->world().slots;
    uint16_t commander = 0;
    for (const auto& slot : slots)
        if (slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == match_local_player_) {
            commander = slot.unit_index;
            break;
        }
    require(commander != 0, "no local commander");
    const auto enemy_player = static_cast<uint8_t>(match_local_player_ == 0 ? 1 : 0);
    const auto spawn = [&](std::string_view name, uint8_t player, int32_t dx, int32_t dz) {
        const auto type = oa::sim::unit_spawn::find_type_index(spawn_type_names_, name);
        require(type != 0, "no " + std::string(name));
        const auto x = static_cast<int32_t>(slots[commander].unit->position[0] >> 16) + dx;
        const auto z = static_cast<int32_t>(slots[commander].unit->position[2] >> 16) + dz;
        oa::sim::unit_spawn::Request request;
        request.player = player;
        request.type = type;
        request.finished = true;
        request.state = kGroundOccupancyState;
        request.position = {
            static_cast<uint32_t>(x) << 16,
            static_cast<uint32_t>(
                match_->map_height(static_cast<uint32_t>(x) << 16, static_cast<uint32_t>(z) << 16)
            ) << 16,
            static_cast<uint32_t>(z) << 16
        };
        auto* slot = match_->create(request);
        require(slot != nullptr && slot->unit != nullptr, "could not spawn " + std::string(name));
        return slot->unit_index;
    };
    const auto peewee = spawn("ARMPW", match_local_player_, -96, 0);
    const auto wingman = spawn("ARMPW", match_local_player_, -96, 160);
    const auto solar = spawn("ARMSOLAR", match_local_player_, 96, 96);
    center_camera_on_unit(peewee);
    const auto run_ticks = [&](int ticks) {
        for (int tick = 0; tick < ticks; ++tick)
            step_match_simulation();
    };
    run_ticks(1);

    const auto select = [&](std::initializer_list<uint16_t> ids) {
        clear_local_selection();
        for (const auto id : ids)
            adopt_selection(id);
        selected_match_unit_ = *ids.begin();
        apply_match_hud_for_selection();
    };
    const auto button = [&](std::string_view action) {
        for (std::size_t index = 0; index < match_hud_->layout.gadgets.size(); ++index)
            if (match_hud_action(match_hud_->layout.gadgets[index].common.name) == action)
                return index;
        throw std::runtime_error("match order check: the order page has no " + std::string(action));
    };
    // The button's rectangle in the HUD layer must hold frame `frame` of its art.
    const auto expect_frame = [&](std::string_view action,
                                  std::size_t frame,
                                  const std::string& what) {
        render_match_surface();
        const auto& gadget = match_hud_->layout.gadgets[button(action)];
        const auto* sequence = gaf_sequence(match_hud_->sprites, gadget.common.name);
        if (sequence == nullptr)
            sequence = gaf_sequence(match_hud_->shared_sprites, gadget.common.name);
        require(
            sequence != nullptr && frame < sequence->frames.size(),
            "no art for " + gadget.common.name
        );
        const auto rendered = oa::formats::gaf::render_normal(sequence->frames[frame]);
        require(rendered.ok(), "cannot render " + gadget.common.name);
        const auto& image = *rendered.frame;
        const auto& palette = match_hud_->background.palette ? *match_hud_->background.palette
                                                             : match_hud_->gui_palette;
        const auto& hud = match_hud_cpu_;
        std::size_t differing = 0;
        for (uint32_t row = 0; row < image.height; ++row)
            for (uint32_t column = 0; column < image.width; ++column) {
                const auto offset = static_cast<std::size_t>(row) * image.width + column;
                if (image.coverage[offset] == 0)
                    continue;
                const auto x = static_cast<std::size_t>(gadget.common.x) + column;
                const auto y = static_cast<std::size_t>(gadget.common.y) + row;
                const auto* shown = hud.rgb.data() + (y * hud.width + x) * 3U;
                const auto* colour =
                    palette.data() +
                    static_cast<std::size_t>(image.pixels[offset]) * oa::palette_entry_bytes;
                differing +=
                    shown[0] != colour[0] || shown[1] != colour[1] || shown[2] != colour[2] ? 1 : 0;
            }
        if (differing != 0) {
            write_ppm(report_directory / "native-match-orders-failed.ppm", hud);
            throw std::runtime_error(
                "match order check: " + what + ": " + gadget.common.name + " does not show frame " +
                std::to_string(frame) + " (" + std::to_string(differing) + " pixels differ)"
            );
        }
    };
    const auto last_frame = [&](std::string_view action) {
        const auto& gadget = match_hud_->layout.gadgets[button(action)];
        const auto* sequence = gaf_sequence(match_hud_->sprites, gadget.common.name);
        if (sequence == nullptr)
            sequence = gaf_sequence(match_hud_->shared_sprites, gadget.common.name);
        require(
            sequence != nullptr && !sequence->frames.empty(), "no art for " + gadget.common.name
        );
        return sequence->frames.size() - 1U;
    };
    // A left click at the button's presented position, as SDL delivers it.
    const auto click = [&](std::string_view action) {
        const auto& common = match_hud_->layout.gadgets[button(action)].common;
        const auto rect = oa::ui::display_layout::source_rect_to_canvas(
            match_layout_, common.x, common.y, common.width, common.height
        );
        float window_x = 0;
        float window_y = 0;
        if (!SDL_RenderCoordinatesToWindow(
                sdl_.renderer,
                static_cast<float>(rect.x) + static_cast<float>(rect.width) / 2.0F,
                static_cast<float>(rect.y) + static_cast<float>(rect.height) / 2.0F,
                &window_x,
                &window_y
            ))
            throw std::runtime_error(
                std::string("SDL_RenderCoordinatesToWindow: ") + SDL_GetError()
            );
        bool running = true;
        SDL_Event motion{};
        motion.motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.windowID = SDL_GetWindowID(sdl_.window);
        motion.motion.x = window_x;
        motion.motion.y = window_y;
        dispatch_event(motion, running);
        for (const auto type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
            SDL_Event press{};
            press.button.type = type;
            press.button.windowID = SDL_GetWindowID(sdl_.window);
            press.button.button = SDL_BUTTON_LEFT;
            press.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            press.button.clicks = 1;
            press.button.x = window_x;
            press.button.y = window_y;
            dispatch_event(press, running);
        }
    };
    const auto unit = [&](uint16_t id) -> const oa::Unit& { return slots[id].record; };
    const auto snapshot = [&](const char* name) {
        renderer::Surface presented;
        capture_frame_ = &presented;
        render();
        capture_frame_ = nullptr;
        write_ppm(report_directory / name, presented);
    };

    // The commander starts the skirmish with its FBI standing orders: fire at
    // will and hold position. Its panel opens on the build menu; ORDERS shows
    // them.
    require(
        fire_order(unit(commander)) == fire_at_will && move_order(unit(commander)) == hold_position,
        "the commander started with fire order " + std::to_string(fire_order(unit(commander))) +
            " and move order " + std::to_string(move_order(unit(commander))) +
            ", not its FBI standing orders"
    );
    select({commander});
    click("ORDERS");
    snapshot("native-match-orders-commander.ppm");
    expect_frame("FIREORD", fire_at_will, "commander selected");
    expect_frame("MOVEORD", hold_position, "commander selected");

    // The commander stands by the radar's spot; only the peewee may shoot.
    auto& commander_flags = slots[commander].record.flags;
    commander_flags = (commander_flags & ~OA_UNIT_FLAG_FIRE_ORDER_MASK) |
                      (hold_fire << OA_UNIT_FLAG_FIRE_ORDER_SHIFT);
    require(
        fire_order(unit(peewee)) == fire_at_will && move_order(unit(peewee)) == maneuver,
        "ARMPW did not start with its FBI standing orders"
    );
    select({peewee});
    snapshot("native-match-orders-peewee.ppm");
    expect_frame("FIREORD", fire_at_will, "ARMPW selected");
    expect_frame("MOVEORD", maneuver, "ARMPW selected");
    expect_frame("ONOFF", last_frame("ONOFF"), "ARMPW selected");
    expect_frame("CLOAK", last_frame("CLOAK"), "ARMPW selected");

    // Hold fire, then return fire: the radar spawned in range stays unharmed
    // and untargeted. Fire at will: the peewee shoots it.
    uint16_t target = 0;
    const auto radar_harmed = [&] {
        const auto& radar = slots[target];
        return (radar.record.flags & OA_UNIT_FLAG_LIVE) == 0 ||
               radar.record.health < static_cast<int32_t>(radar.unit->type->maximum_health);
    };
    for (const auto order : {hold_fire, return_fire, fire_at_will}) {
        click("FIREORD");
        expect_frame("FIREORD", order, "after a FIRE ORDERS click");
        run_ticks(1);
        require(
            fire_order(unit(peewee)) == order,
            "FIRE ORDERS gave fire order " + std::to_string(fire_order(unit(peewee))) + ", not " +
                std::to_string(order)
        );
        require(
            fire_order(unit(wingman)) == fire_at_will, "FIRE ORDERS reached an unselected unit"
        );
        if (target == 0)
            target = spawn("CORRAD", enemy_player, -96, -96);
        bool harmed = false;
        for (int tick = 0; tick < kTargetTicks && !harmed; ++tick) {
            run_ticks(1);
            harmed = targets(unit(peewee), target) || radar_harmed();
        }
        require(
            harmed == (order == fire_at_will),
            "fire order " + std::to_string(order) + (harmed ? " shot" : " did not shoot") +
                " the enemy radar in range"
        );
    }
    for (const auto order : {roam, hold_position, maneuver}) {
        click("MOVEORD");
        expect_frame("MOVEORD", order, "after a MOVE ORDERS click");
        run_ticks(1);
        require(
            move_order(unit(peewee)) == order,
            "MOVE ORDERS gave move order " + std::to_string(move_order(unit(peewee)))
        );
    }
    const auto state_before = unit(peewee).state_flags;
    click("ONOFF");
    run_ticks(1);
    expect_frame("ONOFF", last_frame("ONOFF"), "after a click on the grayed ON/OFF");
    require(unit(peewee).state_flags == state_before, "the grayed ON/OFF reached ARMPW");

    // Two peewees that disagree show the mixed frame; a click gives both hold fire.
    select({wingman});
    click("FIREORD");
    run_ticks(1);
    require(fire_order(unit(wingman)) == hold_fire, "FIRE ORDERS did not reach the second ARMPW");
    select({peewee, wingman});
    expect_frame("FIREORD", mixed_orders, "two ARMPW with different fire orders");
    click("FIREORD");
    expect_frame("FIREORD", hold_fire, "after a click on mixed fire orders");
    run_ticks(1);
    require(
        fire_order(unit(peewee)) == hold_fire && fire_order(unit(wingman)) == hold_fire,
        "mixed FIRE ORDERS did not give both ARMPW hold fire"
    );

    // The solar takes on/off orders only.
    select({solar});
    snapshot("native-match-orders-solar.ppm");
    expect_frame("FIREORD", last_frame("FIREORD"), "ARMSOLAR selected");
    expect_frame("MOVEORD", last_frame("MOVEORD"), "ARMSOLAR selected");
    expect_frame("CLOAK", last_frame("CLOAK"), "ARMSOLAR selected");
    const auto active = static_cast<uint32_t>(unit(solar).state_flags & OA_UNIT_STATE_ACTIVE);
    expect_frame("ONOFF", active, "ARMSOLAR selected");
    click("ONOFF");
    expect_frame("ONOFF", active ^ 1u, "after an ON/OFF click");
    run_ticks(1);
    require(
        static_cast<uint32_t>(unit(solar).state_flags & OA_UNIT_STATE_ACTIVE) == (active ^ 1u),
        "ON/OFF did not switch ARMSOLAR"
    );

    // With both selected, fire orders reach the peewee alone.
    const auto solar_fire = fire_order(unit(solar));
    select({peewee, solar});
    expect_frame("FIREORD", hold_fire, "ARMPW and ARMSOLAR selected");
    expect_frame("ONOFF", active ^ 1u, "ARMPW and ARMSOLAR selected");
    click("FIREORD");
    run_ticks(1);
    require(
        fire_order(unit(peewee)) == return_fire, "FIRE ORDERS did not reach ARMPW beside ARMSOLAR"
    );
    require(fire_order(unit(solar)) == solar_fire, "FIRE ORDERS reached ARMSOLAR");

    check_command_buttons(peewee, commander);
    check_unit_damage_bar(peewee);
    std::cout
        << "match order check: the commander starts with its FBI fire at will and hold "
           "position; FIRE ORDERS, MOVE ORDERS and ON/OFF frames and orders, grayed "
           "buttons blank, hold and return fire leave the enemy radar, fire at will takes it; "
           "command buttons light, go out and play their allsound.tdf entries; the damage bar "
           "draws in UI colours 10 and 4 and hides an enemy commander's damage\n";
}

void Runtime::check_command_buttons(uint16_t peewee, uint16_t commander) {
    std::vector<std::string> heard;
    heard_interface_sounds_ = &heard;

    struct StopListening {
        Runtime& runtime;

        ~StopListening() { runtime.heard_interface_sounds_ = nullptr; }
    } stop_listening{*this};

    const auto index_of = [&](std::string_view action) {
        for (std::size_t index = 0; index < match_hud_->layout.gadgets.size(); ++index)
            if (match_hud_action(match_hud_->layout.gadgets[index].common.name) == action)
                return index;
        throw std::runtime_error(
            "command button check: the order page has no " + std::string(action)
        );
    };
    bool running = true;
    const auto send_pointer = [&](float canvas_x, float canvas_y, bool press) {
        float window_x = 0;
        float window_y = 0;
        if (!SDL_RenderCoordinatesToWindow(sdl_.renderer, canvas_x, canvas_y, &window_x, &window_y))
            throw std::runtime_error(
                std::string("SDL_RenderCoordinatesToWindow: ") + SDL_GetError()
            );
        SDL_Event motion{};
        motion.motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.windowID = SDL_GetWindowID(sdl_.window);
        motion.motion.x = window_x;
        motion.motion.y = window_y;
        dispatch_event(motion, running);
        if (!press)
            return;
        for (const auto type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
            SDL_Event event{};
            event.button.type = type;
            event.button.windowID = SDL_GetWindowID(sdl_.window);
            event.button.button = SDL_BUTTON_LEFT;
            event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.clicks = 1;
            event.button.x = window_x;
            event.button.y = window_y;
            dispatch_event(event, running);
        }
    };
    // A click at the button, then the pointer over the top bar so the hover
    // does not draw the button pressed.
    const auto click = [&](std::string_view action) {
        const auto& common = match_hud_->layout.gadgets[index_of(action)].common;
        const auto rect = oa::ui::display_layout::source_rect_to_canvas(
            match_layout_, common.x, common.y, common.width, common.height
        );
        send_pointer(
            static_cast<float>(rect.x) + static_cast<float>(rect.width) / 2.0F,
            static_cast<float>(rect.y) + static_cast<float>(rect.height) / 2.0F,
            true
        );
        const auto away = oa::ui::display_layout::source_to_canvas(match_layout_, 300, 8);
        send_pointer(static_cast<float>(away.x), static_cast<float>(away.y), false);
    };
    // Whether the HUD layer shows frame `frame` of the button's art.
    const auto shows_frame = [&](std::size_t index, std::size_t frame) {
        const auto& gadget = match_hud_->layout.gadgets[index];
        const auto* sequence = gaf_sequence(match_hud_->sprites, gadget.common.name);
        if (sequence == nullptr)
            sequence = gaf_sequence(match_hud_->shared_sprites, gadget.common.name);
        require(
            sequence != nullptr && frame < sequence->frames.size(),
            "no art for " + gadget.common.name
        );
        const auto rendered = oa::formats::gaf::render_normal(sequence->frames[frame]);
        require(rendered.ok(), "cannot render " + gadget.common.name);
        const auto& image = *rendered.frame;
        const auto& palette = match_hud_->background.palette ? *match_hud_->background.palette
                                                             : match_hud_->gui_palette;
        for (uint32_t row = 0; row < image.height; ++row)
            for (uint32_t column = 0; column < image.width; ++column) {
                const auto offset = static_cast<std::size_t>(row) * image.width + column;
                if (image.coverage[offset] == 0)
                    continue;
                const auto x = static_cast<std::size_t>(gadget.common.x) + column;
                const auto y = static_cast<std::size_t>(gadget.common.y) + row;
                const auto* shown = match_hud_cpu_.rgb.data() + (y * match_hud_cpu_.width + x) * 3U;
                const auto entry = static_cast<std::size_t>(image.pixels[offset]);
                const auto* colour = palette.data() + entry * oa::palette_entry_bytes;
                if (shown[0] != colour[0] || shown[1] != colour[1] || shown[2] != colour[2])
                    return false;
            }
        return true;
    };
    // A lit button draws the second frame of its art, an unlit one the first.
    const auto lit = [&](std::string_view action) {
        render_match_surface();
        const auto index = index_of(action);
        const bool on = match_command_lit(index);
        if (!shows_frame(index, on ? 1U : 0U)) {
            write_ppm(
                fs::path("local/reports") / "native-match-orders-command-failed.ppm", match_hud_cpu_
            );
            require(
                false, std::string(action) + (on ? " is lit" : " is out") + " but not drawn so"
            );
        }
        return on;
    };
    const auto expect = [&](std::string_view action,
                            MatchCommand command,
                            std::string_view lit_action,
                            std::string_view sound,
                            const char* what) {
        click(action);
        require(match_command_ == command, std::string(what) + ": wrong order armed");
        require(
            !heard.empty() && heard.back() == sound,
            std::string(what) + ": did not play " + std::string(sound)
        );
        for (const std::string_view name :
             {"MOVE", "ATTACK", "PATROL", "DEFEND", "REPAIR", "RECLAIM", "CAPTURE"}) {
            bool shown_on_page = false;
            for (const auto& gadget : match_hud_->layout.gadgets)
                shown_on_page = shown_on_page || (match_hud_action(gadget.common.name) == name &&
                                                  gadget_command_available(gadget));
            if (!shown_on_page)
                continue;
            const bool shown = lit(name);
            require(
                shown == (name == lit_action),
                std::string(what) + ": " + std::string(name) + (shown ? " is lit" : " is not lit")
            );
        }
    };

    clear_local_selection();
    adopt_selection(peewee);
    selected_match_unit_ = peewee;
    apply_match_hud_for_selection();
    reset_match_command();
    expect("ATTACK", MatchCommand::attack, "ATTACK", "immediateorders", "ATTACK");
    expect("MOVE", MatchCommand::move, "MOVE", "immediateorders", "MOVE after ATTACK");
    expect("MOVE", MatchCommand::none, "", "immediateorders", "MOVE clicked again");
    expect("PATROL", MatchCommand::patrol, "PATROL", "immediateorders", "PATROL");
    expect("STOP", MatchCommand::none, "", "immediateorders", "STOP");

    clear_local_selection();
    adopt_selection(commander);
    selected_match_unit_ = commander;
    apply_match_hud_for_selection();
    show_match_orders_page();
    expect("REPAIR", MatchCommand::repair, "REPAIR", "specialorders", "REPAIR");
    expect("RECLAIM", MatchCommand::reclaim, "RECLAIM", "specialorders", "RECLAIM");
    expect("CAPTURE", MatchCommand::capture, "CAPTURE", "specialorders", "CAPTURE");
    expect("CAPTURE", MatchCommand::none, "", "specialorders", "CAPTURE clicked again");
    expect("DEFEND", MatchCommand::guard, "DEFEND", "immediateorders", "DEFEND");
    reset_match_command();
    require(!lit("DEFEND"), "the reset left DEFEND lit");

    // The names resolve through allsound.tdf to BUTTON5.
    for (const auto* name : {"immediateorders", "specialorders"}) {
        const auto* sound = audio_registry_.get(audio_registry_.find(name));
        require(
            sound != nullptr && sound->resource == "sounds/button5.wav",
            std::string(name) + " is not BUTTON5"
        );
    }
    clear_local_selection();
}

void Runtime::check_unit_damage_bar(uint16_t peewee) {
    auto& world = match_->state();
    auto& game = world.game;
    require(
        game.ui_colors[4] == 213 && game.ui_colors[10] == 233 && game.ui_colors[12] == 211 &&
            game.ui_colors[14] == 194 && game.ui_colors[15] == 255,
        "the UI colour table is not guipal on PALETTE.PAL"
    );
    const auto& bar = side_hud_.damage_bar;
    require(bar.width > 1 && bar.height > 0, "SIDEDATA names no DAMAGEBAR");
    const auto colour_at = [&](int x, int y) {
        const auto offset =
            static_cast<std::size_t>(y) * match_hud_cpu_.width + static_cast<std::size_t>(x);
        const auto* pixel = match_hud_cpu_.rgb.data() + offset * 3U;
        return std::array<uint8_t, 3>{pixel[0], pixel[1], pixel[2]};
    };
    const auto palette_colour = [&](uint8_t index) {
        const auto at = static_cast<std::size_t>(index) * 4U;
        return std::array<uint8_t, 3>{
            match_palette_[at], match_palette_[at + 1], match_palette_[at + 2]
        };
    };
    const auto bar_row = [&] {
        std::vector<std::array<uint8_t, 3>> row;
        for (int x = bar.x; x < bar.x + bar.width; ++x)
            row.push_back(colour_at(x, bar.y));
        return row;
    };
    const auto show = [&](uint16_t id) {
        clear_local_selection();
        selected_match_unit_ = 0;
        hovered_match_unit_ = id;
        render_match_surface();
        return bar_row();
    };

    auto& peewee_unit = match_->world().slots[peewee].record;
    const auto* def = oa::world_unit_def_of(&world, &peewee_unit);
    require(def != nullptr && def->max_damage > 1, "ARMPW has no max_damage");
    const auto saved_health = peewee_unit.health;
    peewee_unit.health = static_cast<int16_t>(def->max_damage / 2U);
    const auto own = show(peewee);
    peewee_unit.health = saved_health;
    const auto maximum = static_cast<int>(def->max_damage);
    const auto split = (bar.width - 1) * (maximum / 2) / maximum;
    for (int column = 0; column < bar.width; ++column) {
        const int slot = column <= split ? 10 : 4;
        require(
            own[static_cast<std::size_t>(column)] == palette_colour(game.ui_colors[slot]),
            "damage bar column " + std::to_string(column) + " is not UI colour " +
                std::to_string(slot)
        );
    }

    uint16_t enemy_commander = 0;
    for (const auto& slot : match_->world().slots) {
        const auto* type =
            slot.unit != nullptr ? oa::world_unit_def_of(&world, &slot.record) : nullptr;
        if (type != nullptr && slot.record.owner_index != match_local_player_ &&
            (type->flags & OA_UNIT_DEF_FLAG_HIDE_DAMAGE) != 0 &&
            (slot.record.flags & OA_UNIT_FLAG_LIVE) != 0) {
            enemy_commander = slot.unit_index;
            break;
        }
    }
    require(enemy_commander != 0, "no enemy unit whose type hides damage");
    const auto nothing = show(0);
    const auto hidden = show(enemy_commander);
    require(hidden == nothing, "an enemy commander's damage bar was drawn");
    const auto viewpoint = game.viewpoint_player;
    game.viewpoint_player = match_->world().slots[enemy_commander].record.owner_index;
    const auto owned = show(enemy_commander);
    game.viewpoint_player = viewpoint;
    require(
        owned != nothing && owned.front() == palette_colour(game.ui_colors[10]),
        "the commander's owner does not see its damage bar"
    );
    hovered_match_unit_ = 0;
}

} // namespace oa::app
