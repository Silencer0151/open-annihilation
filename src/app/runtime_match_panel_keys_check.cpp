// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The headless check of the keys the in-game menu's and the tab menu's
// panels take, over the navigation check's skirmish.
#include "oa/app/runtime.hpp"
#include "oa/data/defs/layout.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oa::app {

void Runtime::check_match_panel_keys() {
    const auto require = [](bool ok, const std::string& what) {
        if (!ok)
            throw std::runtime_error("match panel key check: " + what);
    };
    require(
        screen_ == Screen::match && match_ && !match_finished_ && !match_paused_,
        "needs a running match with no menu open"
    );
    bool running = true;
    const auto press = [&](SDL_Keycode code, SDL_Scancode scancode, SDL_Keymod mod) {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = code;
        event.key.scancode = scancode;
        event.key.mod = mod;
        event.key.down = true;
        dispatch_event(event, running);
        require(running, "a key ended the run");
    };
    const auto showing = [&](std::string_view layout) {
        return screen_ == Screen::match && match_paused_ && match_hud_ &&
               match_hud_panel_ == layout;
    };
    const auto menu = oa::data::defs::gui_path("ARMOPT.GUI");
    const auto exit_menu = oa::data::defs::gui_path("EXITMENU.GUI");

    // The in-game menu gives its panels the keyboard. A key with Ctrl down
    // types no character and presses nothing; a quick key presses its
    // button in either case.
    show_match_pause_menu();
    require(showing(menu) && match_panels_keyboard_, "the in-game menu did not take the keyboard");
    press(SDLK_E, SDL_SCANCODE_E, SDL_KMOD_LCTRL);
    require(showing(menu), "Ctrl+E pressed the in-game menu's Exit");
    press(SDLK_E, SDL_SCANCODE_E, SDL_KMOD_NONE);
    require(showing(exit_menu), "'e' did not press the in-game menu's Exit");
    // EXITMENU.GUI names no Escape default; the loader gives it CANCEL, which
    // goes back to the in-game menu.
    press(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, SDL_KMOD_NONE);
    require(showing(menu), "Escape did not press the exit menu's Cancel");
    press(SDLK_E, SDL_SCANCODE_E, SDL_KMOD_LSHIFT);
    require(showing(exit_menu), "'E' did not press the in-game menu's Exit");
    press(SDLK_R, SDL_SCANCODE_R, SDL_KMOD_NONE);
    require(
        showing(oa::data::defs::gui_path("RESTART.GUI")),
        "'r' did not press the exit menu's Restart"
    );
    // RESTART.GUI's Enter default is CANCEL.
    press(SDLK_RETURN, SDL_SCANCODE_RETURN, SDL_KMOD_NONE);
    require(showing(menu), "Enter did not press the restart dialog's Cancel");

    // The surrender confirmation takes its quick keys, and Space presses its
    // focused No.
    press(SDLK_E, SDL_SCANCODE_E, SDL_KMOD_NONE);
    activate_pause_gadget("EXITGAME");
    require(showing(oa::data::defs::gui_path("YESORNO.GUI")), "EXITGAME did not ask to surrender");
    press(SDLK_N, SDL_SCANCODE_N, SDL_KMOD_NONE);
    require(showing(menu) && !exit_requested_, "'n' did not answer the confirmation as No");
    press(SDLK_E, SDL_SCANCODE_E, SDL_KMOD_NONE);
    activate_pause_gadget("EXITGAME");
    press(SDLK_SPACE, SDL_SCANCODE_SPACE, SDL_KMOD_NONE);
    require(showing(menu) && !exit_requested_, "Space did not press the confirmation's No");

    // ARMOPT.GUI's Enter and Escape defaults are OK, Resume, as is 'r'.
    press(SDLK_KP_ENTER, SDL_SCANCODE_KP_ENTER, SDL_KMOD_NONE);
    require(!match_paused_, "Enter did not press the in-game menu's Resume");
    show_match_pause_menu();
    press(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, SDL_KMOD_NONE);
    require(!match_paused_, "Escape did not press the in-game menu's Resume");
    show_match_pause_menu();
    press(SDLK_O, SDL_SCANCODE_O, SDL_KMOD_NONE);
    require(
        showing(oa::data::defs::gui_path("PREFS.GUI")),
        "'o' did not press the in-game menu's Options"
    );
    press(SDLK_C, SDL_SCANCODE_C, SDL_KMOD_NONE);
    require(showing(menu), "'c' did not press the preferences' Cancel");
    press(SDLK_R, SDL_SCANCODE_R, SDL_KMOD_NONE);
    require(!match_paused_, "'r' did not press the in-game menu's Resume");

    // The tab menu of a multiplayer match gives its panels the keyboard too.
    const Extension saved_extension = extension_;
    try {
        extension_.state = [](void*, const Runtime&) -> uint32_t {
            return extension_state::multiplayer | extension_state::shared_match;
        };
        toggle_team_menu();
        require(
            showing(oa::data::defs::gui_path("TABMENU.GUI")) && match_panels_keyboard_,
            "the tab menu did not take the keyboard"
        );
        press(SDLK_A, SDL_SCANCODE_A, SDL_KMOD_NONE);
        require(
            showing(oa::data::defs::gui_path("ALLIES.GUI")),
            "'a' did not press the tab menu's Allies"
        );
        // ALLIES.GUI's Enter default is OK.
        press(SDLK_RETURN, SDL_SCANCODE_RETURN, SDL_KMOD_NONE);
        require(!team_panel_open() && !match_paused_, "Enter did not press ALLIES.GUI's OK");
        toggle_team_menu();
        press(SDLK_S, SDL_SCANCODE_S, SDL_KMOD_NONE);
        require(
            showing(oa::data::defs::gui_path("SHARE.GUI")), "'s' did not press the tab menu's Share"
        );
        // SHARE.GUI's Escape default is CANCEL.
        press(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, SDL_KMOD_NONE);
        require(!team_panel_open() && !match_paused_, "Escape did not press SHARE.GUI's Cancel");
    } catch (...) {
        if (match_paused_ && !match_finished_)
            resume_match_pause();
        extension_ = saved_extension;
        throw;
    }
    extension_ = saved_extension;
    std::cout << "match panel key check: quick keys, Enter, Space and Escape on ARMOPT.GUI, "
                 "EXITMENU.GUI, RESTART.GUI, YESORNO.GUI, PREFS.GUI, TABMENU.GUI, ALLIES.GUI "
                 "and SHARE.GUI\n";
}

} // namespace oa::app
