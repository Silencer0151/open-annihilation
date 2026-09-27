// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Build pages driven through synthetic SDL input: a download page with its
// linked buttons and a missile silo's stockpile button.
#include "oa/app/runtime.hpp"
#include "oa/data/mission_types.hpp"
#include "oa/ui/hud/order_panel.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace oa::app {
namespace {

// Ticks the lab gets to build one Flea at full resources (about 610).
constexpr int kBuildTickLimit = 1600;

[[noreturn]] void fail(std::string_view what, std::string_view how = {}) {
    throw std::runtime_error(
        "download build check: " + std::string(what) + (how.empty() ? "" : " ") + std::string(how)
    );
}

} // namespace

void Runtime::check_download_builds() {
    namespace hud = oa::ui::hud;
    namespace orders = oa::sim::match_runtime;
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        fail("needs the SDL renderer");
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    start_benchmark_skirmish();
    apply_output_mode();
    const auto type_of = [&](const char* name) {
        const auto type = oa::sim::unit_spawn::find_type_index(spawn_type_names_, name);
        if (type == 0 || type >= spawn_types_.size())
            fail("lacks", name);
        return type;
    };
    const auto lab = type_of("ARMLAB");
    const auto silo = type_of("ARMSILO");
    const auto flea = type_of("ARMFLEA");
    const auto warrior = type_of("ARMWAR");
    auto& slots = match_->world().slots;
    uint16_t commander = 0;
    for (const auto& slot : slots)
        if (slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == match_local_player_) {
            commander = slot.unit_index;
            break;
        }
    if (commander == 0)
        fail("found no local commander");

    const auto place = [&](uint16_t type) -> uint16_t {
        const auto* placed = place_finished_structure(type, commander);
        if (placed == nullptr)
            fail("could not place", spawn_type_names_[type]);
        return placed->unit_index;
    };
    const auto factory = place(lab);
    const auto missile_silo = place(silo);

    auto& player = match_->world().players[match_local_player_];
    const auto tick = [&] {
        player.metal = player.metal_cap;
        player.energy = player.energy_cap;
        step_match_simulation();
    };
    bool running = true;
    const auto send = [&](SDL_EventType type, uint8_t button, float x, float y) {
        float window_x = 0.0F;
        float window_y = 0.0F;
        if (!SDL_RenderCoordinatesToWindow(sdl_.renderer, x, y, &window_x, &window_y))
            fail(SDL_GetError());
        SDL_Event event{};
        event.type = type;
        if (type == SDL_EVENT_MOUSE_MOTION) {
            event.motion.windowID = SDL_GetWindowID(sdl_.window);
            event.motion.x = window_x;
            event.motion.y = window_y;
        } else {
            event.button.windowID = SDL_GetWindowID(sdl_.window);
            event.button.button = button;
            event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.clicks = 1;
            event.button.x = window_x;
            event.button.y = window_y;
        }
        dispatch_event(event, running);
    };
    const auto click = [&](float x, float y, uint8_t button) {
        send(SDL_EVENT_MOUSE_MOTION, 0, x, y);
        send(SDL_EVENT_MOUSE_BUTTON_DOWN, button, x, y);
        send(SDL_EVENT_MOUSE_BUTTON_UP, button, x, y);
    };
    const auto gadget_index = [&](std::string_view name) -> std::size_t {
        if (!match_hud_)
            fail("has no order panel");
        const auto& gadgets = match_hud_->layout.gadgets;
        for (std::size_t index = 1; index < gadgets.size(); ++index)
            if (gadgets[index].common.name == name)
                return index;
        fail("found no button", name);
    };
    const auto click_gadget = [&](std::string_view name, uint8_t button) {
        const auto& common = match_hud_->layout.gadgets[gadget_index(name)].common;
        const auto point = oa::ui::display_layout::source_to_canvas(
            match_layout_, common.x + common.width / 2, common.y + common.height / 2
        );
        click(static_cast<float>(point.x), static_cast<float>(point.y), button);
    };
    const auto select = [&](uint16_t unit) {
        center_camera_on_unit(unit);
        const auto viewport = live_viewport(
            static_cast<uint32_t>(std::max(0, match_camera_x_)),
            static_cast<uint32_t>(std::max(0, match_camera_z_))
        );
        const auto on_screen = project_match_point(viewport, slots[unit].unit->position);
        click(static_cast<float>(on_screen.x), static_cast<float>(on_screen.y), SDL_BUTTON_LEFT);
        if (selected_match_unit_ != unit)
            fail("did not select", spawn_type_names_[slots[unit].record.type_index]);
    };
    const auto snapshot = [&](const char* name) {
        render_match_surface();
        renderer::Surface frame;
        compose_match_frame(frame);
        write_ppm(report_directory / name, frame);
    };
    const auto queue = [&](uint16_t unit, bool secondary) {
        std::vector<std::pair<int32_t, int32_t>> entries; // type, count
        std::array<orders::Match::OrderRecordView, 8> records{};
        const auto count = match_->queue_records(unit, secondary, records.data(), records.size());
        for (std::size_t index = 0; index < count; ++index)
            entries.emplace_back(records[index].parameter_1, records[index].parameter_2);
        return entries;
    };
    using Queue = std::vector<std::pair<int32_t, int32_t>>;

    select(factory);
    if (match_hud_panel_ != "guis/ARMLAB1.GUI" || match_build_page_ != 1)
        fail("did not open ARMLAB1.GUI for the lab");
    click_gadget("ARMNEXT", SDL_BUTTON_LEFT);
    if (match_hud_panel_ != "guis/ARMDL.GUI" || match_build_page_ != 2)
        fail("did not open the download page ARMDL.GUI on NEXT");
    for (const auto& [slot, name] : {std::pair{4U, "ARMWAR"}, std::pair{5U, "ARMFLEA"}}) {
        const auto& gadget = match_hud_->layout.gadgets.at(slot);
        const auto* button = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget.fields);
        if (gadget.common.name != name || !gadget.common.gaf_file || button == nullptr ||
            button->grayed_out ||
            static_cast<uint8_t>(gadget.common.common_attributes) != hud::kCommonUnitButton)
            fail("did not link a live button into the patch for", name);
        if (gaf_sequence(match_hud_->sprites, name) == nullptr)
            fail("did not load the _gadget art of", name);
    }
    for (std::size_t slot = 6; slot < 10; ++slot) {
        const auto* button = std::get_if<oa::ui::gui_layout::ButtonFields>(
            &match_hud_->layout.gadgets.at(slot).fields
        );
        if (match_hud_->layout.gadgets[slot].common.name != "IGPATCH" || button == nullptr ||
            !button->grayed_out)
            fail("linked more than the lab's two download buttons");
    }
    snapshot("native-download-page.ppm");

    click_gadget("ARMFLEA", SDL_BUTTON_LEFT);
    click_gadget("ARMWAR", SDL_BUTTON_LEFT);
    click_gadget("ARMFLEA", SDL_BUTTON_LEFT);
    if (queue(factory, false) != Queue{{flea, 1}, {warrior, 1}, {flea, 1}})
        fail("did not queue Flea, Warrior, Flea from the download page");
    refresh_build_page(false);
    if (build_captions_.at(4) != "+1" || build_captions_.at(5) != "+2")
        fail("did not count the queued builds on the download buttons");
    snapshot("native-download-queued.ppm");
    click_gadget("ARMWAR", SDL_BUTTON_RIGHT);
    if (queue(factory, false) != Queue{{flea, 1}, {flea, 1}})
        fail("did not take the Warrior out of the middle of the queue");
    click_gadget("ARMFLEA", SDL_BUTTON_RIGHT);
    if (queue(factory, false) != Queue{{flea, 1}})
        fail("did not take the last Flea off the queue");
    uint16_t product = 0;
    for (int step = 0; step < kBuildTickLimit && product == 0; ++step) {
        tick();
        for (const auto& slot : slots)
            if (slot.unit != nullptr && slot.record.type_index == flea &&
                slot.record.owner_index == match_local_player_ &&
                slot.record.build_remaining == 0.0F)
                product = slot.unit_index;
    }
    if (product == 0)
        fail("saw no Flea come out of the lab");

    select(missile_silo);
    if (match_hud_panel_ != "guis/ARMGEN.GUI")
        fail("did not open the order page for the silo");
    click_gadget("ARMBUILD", SDL_BUTTON_LEFT);
    if (match_hud_panel_ != "guis/ARMSILO1.GUI")
        fail("did not open ARMSILO1.GUI on BUILD");
    const auto build_weapon = oa::data::mission_types::index_for_name("BUILDWEAPON");
    click_gadget("ARMMAKENUKE", SDL_BUTTON_LEFT);
    const auto missiles = queue(missile_silo, true);
    if (match_->queued_build_count(missile_silo, 0) != 1 || missiles != Queue{{0, 1}})
        fail("did not queue one missile with MAKENUKE");
    std::array<orders::Match::OrderRecordView, 1> head{};
    if (match_->queue_records(missile_silo, true, head.data(), head.size()) != 1 ||
        head[0].kind != build_weapon)
        fail("queued the missile as something other than BuildWeapon");
    refresh_build_page(false);
    if (build_captions_.at(gadget_index("ARMMAKENUKE")) != " +1")
        fail("did not count the queued missile on MAKENUKE");
    snapshot("native-download-silo.ppm");
    click_gadget("ARMMAKENUKE", SDL_BUTTON_RIGHT);
    if (match_->queued_build_count(missile_silo, 0) != 0 || !queue(missile_silo, true).empty())
        fail("did not take the missile off with a right click");
    std::cout << "download build check: ARMDL.GUI linked the lab's Warrior and Flea, their "
                 "queue took and lost orders from the middle and the end and built a Flea, and "
                 "MAKENUKE queued and dropped a missile\n";
}

} // namespace oa::app
