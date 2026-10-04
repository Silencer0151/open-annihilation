// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The headless navigation check over game data with no skirmish map, such as
// the Total Annihilation demo (1997): the notices, the entries the data cannot
// open, and the campaign's way in and out.
#include "oa/app/runtime.hpp"
#include "oa/data/defs/layout.hpp"
#include "web_link_state.hpp"
#include "oa/ui/frontend_dialogs.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace oa::app {

namespace {

namespace dialogs = oa::ui::frontend_dialogs;

void expect(bool condition, std::string_view what) {
    if (!condition)
        throw std::runtime_error("navigation check without maps: " + std::string(what));
}

} // namespace

void Runtime::check_navigation_without_maps(const fs::path& report_directory) {
    const auto grayed = [this](std::string_view name) {
        const auto* gadget = widget(name);
        const auto* button = gadget != nullptr
                                 ? std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget->fields)
                                 : nullptr;
        return button != nullptr && button->grayed_out;
    };
    const auto shown = [this](std::string_view name) {
        const auto* gadget = widget(name);
        return gadget != nullptr && gadget->common.active != 0;
    };
    // A press and release at a gadget's centre, as the player's pointer gives them.
    const auto press = [this](std::string_view name) {
        const auto* gadget = widget(name);
        expect(gadget != nullptr, "the screen lacks " + std::string(name));
        bool running = true;
        SDL_Event event{};
        event.button.x = static_cast<float>(gadget->common.x + gadget->common.width / 2);
        event.button.y = static_cast<float>(gadget->common.y + gadget->common.height / 2);
        event.button.button = SDL_BUTTON_LEFT;
        event.button.clicks = 1;
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        handle_sdl_event(event, running);
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        handle_sdl_event(event, running);
    };
    // The notice shows when the data can draw it; otherwise the message box stands in.
    const auto notice_open = [] {
        return dialogs::dialog_kind() == dialogs::DialogKind::notice ||
               dialogs::dialog_kind() == dialogs::DialogKind::message_box;
    };
    const auto close_with = [this](const char* button) {
        auto context = screen_context();
        expect(dialogs::dialog_click(&context, button), std::string("the notice lacks ") + button);
        run_pending_notice_return();
    };
    const auto snapshot = [&](const char* name) {
        rebuild_surface();
        write_ppm(report_directory / name, surface_);
    };

    // The main menu: INTRO and Credits play movies, and the version label
    // names the data's revision.
    snapshot("native-main.ppm");
    const bool movies = offers_movies();
    expect(
        grayed("INTRO") == !movies, "INTRO is grayed out exactly when the game offers no movies"
    );
    expect(shown("Credits") == movies, "Credits shows exactly when the game offers movies");
    expect(
        shown("DebugString") ==
            (assets_.file_size(
                 oa::data::defs::data_path(oa::data::defs::DataDirectory::gamedata, "version.tdf")
             ) != 0),
        "the version label shows exactly when the data names its revision"
    );
    press("INTRO");
    expect(screen_ == Screen::main_menu || movies, "a grayed-out INTRO took a press");

    // MULTI tells the player there are no multiplayer maps; OK stays on the main menu.
    exercise_click(menu::resource_name(menu::Button::multiplayer));
    expect(notice_open(), "MULTI did not tell the player multiplayer is not available");
    const bool notice_drawn = dialogs::dialog_kind() == dialogs::DialogKind::notice;
    snapshot("native-multiplayer-notice.ppm");
    close_with("OK");
    expect(dialogs::dialog_count() == 0 && screen_ == Screen::main_menu, "OK left the main menu");

    // SINGLE.GUI offers only what the data can open.
    exercise_click(menu::resource_name(menu::Button::single_player));
    expect(screen_ == Screen::single_player, "SINGLE did not open SINGLE.GUI");
    snapshot("native-single.ppm");
    expect(grayed("LoadGame") == !offers_saved_games(), "Load Game does not follow LOADGAME.GUI");
    expect(!shown("AnyMsn") || offers_any_mission(), "Any Mission shows without its screen");
    if (!offers_saved_games()) {
        press("LoadGame");
        expect(screen_ == Screen::single_player, "a grayed-out Load Game took a press");
    }

    // Skirmish tells the player there are no skirmish maps; the website
    // button asks the seam for the project's site and closes the notice.
    exercise_click(entry::resource_name(entry::Button::skirmish));
    expect(notice_open(), "Skirmish did not tell the player skirmish is not available");
    snapshot("native-skirmish-notice.ppm");
    if (notice_drawn) {
        // The website button names the address it opens.
        const oa::ui::gui_layout::ButtonFields* caption = nullptr;
        if (const auto* notice = dialogs::dialog_resources())
            for (const auto& gadget : notice->layout.gadgets)
                if (gadget.common.name == "GotoWebsite")
                    caption = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget.fields);
        expect(
            caption != nullptr && caption->text == web_link_caption(project_website_address),
            "the website button does not name " + std::string(project_website_address)
        );
        const auto& requests = web_links_->requests;
        const auto asked = requests.size();
        close_with("GotoWebsite");
        expect(
            requests.size() == asked + 1 && requests.back() == project_website_address,
            "the website button did not ask for " + std::string(project_website_address)
        );
        expect(dialogs::dialog_count() == 0, "the website button left the notice open");
        expect(screen_ == Screen::single_player, "the website button left SINGLE.GUI");
        exercise_click(entry::resource_name(entry::Button::skirmish));
    }
    close_with("OK");
    expect(
        dialogs::dialog_count() == 0 &&
            screen_ == (notice_drawn ? Screen::main_menu : Screen::single_player),
        "OK did not close the notice as it should"
    );
    if (screen_ == Screen::main_menu)
        exercise_click(menu::resource_name(menu::Button::single_player));

    // New Campaign opens on a side with a campaign; a side without one is grayed out.
    exercise_click(entry::resource_name(entry::Button::new_campaign));
    expect(screen_ == Screen::new_campaign, "New Campaign did not open NEWGAME.GUI");
    snapshot("native-newcamp.ppm");
    for (uint32_t side = 0; side < 2; ++side) {
        const auto* button = side == 0 ? "Side0" : "Side1";
        expect(
            grayed(button) == !side_has_campaign(side),
            std::string(button) + " is not grayed out exactly when its side has no campaign"
        );
    }
    expect(side_has_campaign(preferences_.side), "New Campaign opened on a side with no campaign");
    const auto missions = campaign_mission_files_.size();
    expect(missions != 0, "New Campaign found no mission");
    exercise_click("Start");
    expect(screen_ == Screen::briefing, "Start did not open the first mission's briefing");
    snapshot("native-newcamp-briefing.ppm");

    // Winning the last mission ends the campaign; without movies the notice
    // says so over the main menu, and OK leaves it there.
    if (!movies) {
        selected_mission_index_ = missions - 1;
        show_mission_briefing();
        start_campaign_mission();
        expect(
            screen_ == Screen::match && match_ && campaign_mission_,
            "the last mission did not start"
        );
        match_outcome_ = sim::scenario::Outcome::victory;
        match_->state().game.outcome_flags |= sim::scenario::outcome_flag::won;
        match_finished_ = true;
        finish_match_outcome();
        expect(screen_ == Screen::campaign_end, "the won last mission did not end");
        expect(step_endgame_until_left(), "the end screen did not leave for the main menu");
        expect(screen_ == Screen::main_menu, "the campaign's end did not return to the main menu");
        expect(notice_open(), "the campaign's end showed no notice");
        snapshot("native-campaign-complete.ppm");
        close_with("OK");
        expect(
            dialogs::dialog_count() == 0 && screen_ == Screen::main_menu, "OK left the main menu"
        );
    }
    std::cout << "navigation check without maps: " << (notice_drawn ? "DEMOMSG.GUI" : "message box")
              << " notices, " << missions << " campaign missions"
              << (movies ? "" : ", campaign complete notice") << '\n';
}

void Runtime::check_saved_games_unavailable() {
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    const auto grayed_in = [](const renderer::ScreenResources& resources, std::string_view name) {
        for (const auto& gadget : resources.layout.gadgets)
            if (gadget.common.name == name)
                if (const auto* button =
                        std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget.fields))
                    return button->grayed_out;
        return false;
    };

    // Single Player's Load Game is grayed out and a press through SDL opens nothing.
    exercise_click(menu::resource_name(menu::Button::single_player));
    expect(screen_ == Screen::single_player, "SINGLE did not open SINGLE.GUI");
    expect(grayed_in(resources_, "LoadGame"), "Load Game is not grayed out");
    const auto* load_game = widget("LoadGame");
    expect(load_game != nullptr, "SINGLE.GUI has no Load Game");
    float window_x = static_cast<float>(load_game->common.x + load_game->common.width / 2);
    float window_y = static_cast<float>(load_game->common.y + load_game->common.height / 2);
    if (sdl_.renderer != nullptr &&
        !frame_to_window(sdl_.renderer, window_x, window_y, &window_x, &window_y))
        throw std::runtime_error(std::string("SDL_RenderCoordinatesToWindow: ") + SDL_GetError());
    bool running = true;
    for (const auto type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
        SDL_Event press{};
        press.button.type = type;
        press.button.windowID = sdl_.window != nullptr ? SDL_GetWindowID(sdl_.window) : 0;
        press.button.button = SDL_BUTTON_LEFT;
        press.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        press.button.clicks = 1;
        press.button.x = window_x;
        press.button.y = window_y;
        dispatch_event(press, running);
    }
    expect(screen_ == Screen::single_player, "a grayed-out Load Game took a press");
    rebuild_surface();
    write_ppm(report_directory / "native-saved-games-single.ppm", surface_);

    // The paused first campaign mission grays its save and load buttons, and
    // a press on either leaves the options panel up.
    discover_campaigns();
    expect(!campaign_mission_files_.empty(), "the data holds no campaign mission");
    selected_mission_index_ = 0;
    show_mission_briefing();
    start_campaign_mission();
    expect(
        screen_ == Screen::match && match_ && campaign_mission_, "the first mission did not start"
    );
    show_match_pause_menu();
    expect(match_hud_.has_value() && match_paused_, "the options panel did not open");
    for (const std::string_view name : {"SAVEGAME", "LOADGAME"}) {
        expect(grayed_in(*match_hud_, name), std::string(name) + " is not grayed out");
        const auto& gadgets = match_hud_->layout.gadgets;
        const auto found = std::find_if(gadgets.begin(), gadgets.end(), [name](const auto& gadget) {
            return gadget.common.name == name;
        });
        activate_match_hud(static_cast<std::size_t>(found - gadgets.begin()));
        expect(
            screen_ == Screen::match && match_paused_ && grayed_in(*match_hud_, name),
            "a grayed-out " + std::string(name) + " took a press"
        );
    }
    renderer::Surface paused;
    compose_match_frame(paused);
    write_ppm(report_directory / "native-saved-games-paused.ppm", paused);
    std::cout << "load/save check: the data has no save and load dialog; Load Game, SAVEGAME "
                 "and LOADGAME are grayed out and take no press\n";
}

} // namespace oa::app
