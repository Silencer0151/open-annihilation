// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The option fields kept only in the Game block, which the
// runtime also holds in its preferences between matches: a match starts from
// the preferences and hands its values back before they are saved or another
// screen reads them.
#include "oa/app/runtime.hpp"
#include "oa/ui/engine_settings.hpp"

#include "oa/platform/preferences.hpp"
#include "oa/ui/console/game_fields.hpp"

#include <SDL3/SDL.h>
#include <charconv>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace oa::app {

namespace console = oa::ui::console;

void Runtime::seed_match_options(oa::Game& game) const {
    game.interface_type = preferences_.interface_type;
    game.scroll_speed = preferences_.scroll_speed;
    game.graphics_flags = preferences_.graphics_flags;
    game.screen_chat = preferences_.screen_chat;
    game.gamma = preferences_.gamma;
    game.sound_flags = preferences_.sound_flags;
    // The capture directory and rate (Film, FilmSpeed, MakePoster) with their
    // change flags clear; take_match_options moves pending changes out first.
    std::snprintf(
        game.output_directory,
        sizeof game.output_directory,
        "%s",
        preferences_.image_output_directory.c_str()
    );
    game.capture_rate = static_cast<int32_t>(preferences_.movie_output_rate);
}

void Runtime::take_match_options(oa::Game& game) {
    take_console_capture_options(game);
    preferences_.interface_type = game.interface_type;
    preferences_.scroll_speed = game.scroll_speed;
    preferences_.graphics_flags = game.graphics_flags;
    preferences_.screen_chat = game.screen_chat;
    preferences_.gamma = game.gamma;
    preferences_.display_flags = console::console_flags(game);
    preferences_.sound_flags = game.sound_flags;
}

int64_t Runtime::saved_general_number(const char* key) const {
    const auto values = oa::platform::preferences::load(preference_path_);
    const auto found = values.find(preference_key(init::general_section, key));
    if (found == values.end())
        return -1;
    // A value that is not a number reads as no value.
    int64_t value = -1;
    const char* first = found->second.data();
    if (std::from_chars(first, first + found->second.size(), value).ec != std::errc{})
        return -1;
    return value;
}

void Runtime::check_console_option_commands(const std::function<void(const char*)>& enter_line) {
    oa::Game& game = match_->state().game;
    const auto require = [](bool ok, const char* what) {
        if (!ok)
            throw std::runtime_error(std::string("console option check: ") + what);
    };
    const auto interface_type = [&] { return game.interface_type; };
    const auto switch_alt = [&] {
        return (game.graphics_flags & console::graphics_flag::switch_alt) != 0;
    };
    const auto saved = [&](const char* key) { return saved_general_number(key); };
    require(
        game.scroll_speed == preferences_.scroll_speed &&
            interface_type() == preferences_.interface_type &&
            game.graphics_flags == preferences_.graphics_flags,
        "the match did not start from the saved scroll speed, interface and graphics options"
    );

    const auto speed = game.scroll_speed;
    enter_line("+scrollspeed 300");
    require(
        game.scroll_speed == 44 && saved("scrollspeed") == 44,
        "+scrollspeed 300 did not set and save scroll speed 44"
    );
    enter_line(("+scrollspeed " + std::to_string(speed)).c_str());
    require(saved("scrollspeed") == speed, "+scrollspeed did not save the restored speed");

    // The options save writes ackfx, buildfx and speechfx from the match's
    // sound word (Game.sound_flags), which the settings load filled.
    require(
        game.sound_flags == preferences_.sound_flags,
        "the match did not start from the saved sound flags"
    );
    const auto sound = game.sound_flags;
    const auto speech = (sound & init::preference_flags::speech_fx) != 0 ? 1 : 0;
    game.sound_flags = static_cast<uint16_t>(sound ^ init::preference_flags::speech_fx);
    enter_line(("+scrollspeed " + std::to_string(speed)).c_str());
    require(saved("speechfx") == 1 - speech, "a save did not take speechfx from the match");
    game.sound_flags = sound;
    enter_line(("+scrollspeed " + std::to_string(speed)).c_str());
    require(
        saved("speechfx") == speech && preferences_.sound_flags == sound,
        "a save did not put speechfx back"
    );

    const auto scheme = interface_type();
    enter_line("+iface 1");
    require(
        interface_type() == 1 && saved("Interface Type") == 1,
        "+iface 1 did not set and save the right-click interface"
    );
    enter_line(("+iface " + std::to_string(scheme)).c_str());
    require(
        interface_type() == scheme && saved("Interface Type") == scheme,
        "+iface did not restore the interface"
    );

    const bool alt_before = switch_alt();
    const auto alt_saved = saved("SwitchAlt");
    enter_line("+switchalt 0");
    require(!switch_alt() && saved("SwitchAlt") == alt_saved, "+switchalt 0 saved or left the bit");
    require(
        !engine_settings().switch_alt, "+switchalt 0 left the Select groups without Alt setting"
    );
    enter_line("+switchalt");
    require(switch_alt() && saved("SwitchAlt") == 1, "+switchalt did not set and save the bit");
    require(engine_settings().switch_alt, "+switchalt left the Select groups without Alt setting");

    uint16_t commander = 0;
    for (const auto& slot : match_->world().slots)
        if (commander == 0 && slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == match_local_player_)
            commander = slot.unit_index;
    require(commander != 0, "no local unit for the squad keys");
    const auto& unit = *match_->world().slots[commander].unit;
    const auto selected = [&] { return (unit.flags & OA_UNIT_FLAG_SELECTED) != 0; };
    const auto kept_camera_x = match_camera_x_;
    const auto kept_camera_z = match_camera_z_;
    const auto kept_selection = selected_match_unit_;
    const auto digit = [&](SDL_Keycode code, SDL_Scancode scancode, SDL_Keymod mods) {
        bool running = true;
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = code;
        event.key.scancode = scancode;
        event.key.mod = mods;
        SDL_SetModState(mods);
        handle_sdl_event(event, running);
        SDL_SetModState(SDL_KMOD_NONE);
    };
    clear_local_selection();
    adopt_selection(commander);
    digit(SDLK_1, SDL_SCANCODE_1, SDL_KMOD_LCTRL);
    require(unit.squad == 1, "Ctrl+1 did not put the selected unit in squad 1");
    clear_local_selection();
    digit(SDLK_1, SDL_SCANCODE_1, SDL_KMOD_LALT);
    require(!selected(), "Alt+1 picked squad 1 with SwitchAlt on");
    digit(SDLK_1, SDL_SCANCODE_1, SDL_KMOD_NONE);
    require(selected(), "1 did not pick squad 1 with SwitchAlt on");

    enter_line("+switchalt");
    require(!switch_alt() && saved("SwitchAlt") == 0, "+switchalt did not clear and save the bit");
    require(!engine_settings().switch_alt, "+switchalt left the Select groups without Alt setting");
    digit(SDLK_2, SDL_SCANCODE_2, SDL_KMOD_NONE);
    require(match_build_page_ == 1, "2 did not show build page 1 with SwitchAlt off");
    digit(SDLK_1, SDL_SCANCODE_1, SDL_KMOD_NONE);
    require(match_build_page_ == 0, "1 did not show the order page with SwitchAlt off");
    clear_local_selection();
    digit(SDLK_1, SDL_SCANCODE_1, SDL_KMOD_NONE);
    require(!selected(), "1 picked squad 1 with SwitchAlt off");
    digit(SDLK_1, SDL_SCANCODE_1, SDL_KMOD_LALT);
    require(selected(), "Alt+1 did not pick squad 1 with SwitchAlt off");

    enter_line("+switchalt 3");
    require(switch_alt() && saved("SwitchAlt") == 0, "+switchalt 3 saved or left the bit clear");
    enter_line("+switchalt 2");
    require(!switch_alt() && saved("SwitchAlt") == 0, "+switchalt 2 saved or left the bit set");
    if (alt_before)
        enter_line("+switchalt");

    clear_local_selection();
    adopt_selection(kept_selection);
    apply_match_hud_for_selection();
    match_camera_x_ = kept_camera_x;
    match_camera_z_ = kept_camera_z;
    squad_double_tap_ = 0;

    // +stats alone shows or hides the frame statistics and saves the Show
    // performance statistics setting; with an argument it saves nothing.
    const auto saved_stats = [&] {
        const auto values = oa::platform::preferences::load(preference_path_);
        const auto found = values.find(std::string(oa::ui::engine_settings::key::frame_stats));
        return found != values.end() ? found->second : std::string();
    };
    show_frame_stats(false);
    const auto stats_saved = saved_stats();
    enter_line("+stats 1");
    require(frame_stats_shown_ && saved_stats() == stats_saved, "+stats 1 saved or hid the panel");
    require(engine_settings().frame_stats, "+stats 1 left the Show performance statistics setting");
    enter_line("+stats");
    require(!frame_stats_shown_ && saved_stats() == "0", "+stats did not hide and save");
    enter_line("+stats");
    require(frame_stats_shown_ && saved_stats() == "1", "+stats did not show and save");
    require(engine_settings().frame_stats, "+stats left the Show performance statistics setting");
    enter_line("+stats 0");
    require(!frame_stats_shown_ && saved_stats() == "1", "+stats 0 saved or showed the panel");
    enter_line("+stats");
    enter_line("+stats");
    require(!frame_stats_shown_ && saved_stats() == "0", "+stats did not hide and save again");
    std::cout << "console option check: +scrollspeed and +iface set and save their options, "
                 "a save takes the match's sound flags, +switchalt toggles and saves (with an "
                 "argument sets without saving) and the digit keys and the settings follow it, "
                 "and +stats toggles and saves (with an argument sets without saving)\n";
}

} // namespace oa::app
