// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit speech through synthetic SDL input: the commander clicked says its
// select line and, clicked onto open ground, its order line, in a run's
// first skirmish and in the next one; "+Sing" sings the select line.
#include "oa/app/runtime.hpp"
#include "oa/audio/game_audio.hpp"
#include "oa/audio/unit_announcements.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace oa::app {
namespace {

using oa::audio::game_audio::UnitAnnouncementCategory;

// Ticks the speech queue keeps the sound device to itself after a line: the
// next line is heard only this many ticks after the last one.
constexpr int kSpeechWindowTicks = 30;
// Ticks the first skirmish plays before its commander is clicked, so that
// it speaks later in its game than the second skirmish's commander does.
constexpr int kFirstSkirmishTicks = 600;
// Ticks an order given with a click has to be acknowledged in.
constexpr int kAcknowledgeTicks = 15;
// Canvas pixels from the commander to the ground the order click tries.
constexpr std::array<std::pair<float, float>, 4> kGroundOffsets{
    {{120.0F, 0.0F}, {-120.0F, 0.0F}, {0.0F, 100.0F}, {0.0F, -100.0F}}
};

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("unit speech check: " + what);
}

} // namespace

void Runtime::check_unit_speech() {
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        fail("needs the SDL renderer");
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
    const auto click = [&](float x, float y) {
        send(SDL_EVENT_MOUSE_MOTION, 0, x, y);
        send(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, x, y);
        send(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, x, y);
    };
    // Lines presented while the match plays on; each tick presents one, as
    // a frame does.
    std::vector<oa::audio::game_audio::UnitAnnouncement> heard;
    const auto present = [&] {
        for (auto& line : offline_services_.pump_announcements())
            heard.push_back(std::move(line));
    };
    const auto play = [&](int ticks) {
        for (int tick = 0; tick < ticks; ++tick) {
            step_match_simulation();
            present();
        }
    };
    // The sound resources a unit may play for a category: its own from
    // SOUND.TDF, or the novelty voice's two while "+Sing" is on.
    const auto offered = [&](uint16_t unit, UnitAnnouncementCategory category) {
        std::vector<std::string> sounds;
        if (novelty_voice_ != 0) {
            for (const auto& name : novelty_sounds_)
                sounds.push_back(oa::audio::game_audio::sound_resource(name));
            return sounds;
        }
        const auto* definition = definition_for(unit);
        if (definition == nullptr)
            fail("the commander has no unit definition");
        const auto* choices = unit_sound_catalog_.choices(definition->sound_category, category);
        if (choices != nullptr)
            for (const auto& choice : *choices)
                sounds.push_back(oa::audio::game_audio::sound_resource(choice.sound));
        return sounds;
    };
    // The unit's line of the category heard since `from` must play one of
    // the sounds the unit offers for it.
    const auto said = [&](std::size_t from,
                          uint16_t unit,
                          UnitAnnouncementCategory category,
                          const std::string& what) {
        const auto sounds = offered(unit, category);
        if (sounds.empty())
            fail("the commander's sound category offers no sound for " + what);
        for (std::size_t index = from; index < heard.size(); ++index) {
            const auto& line = heard[index];
            if (line.unit_index != unit || line.category != category)
                continue;
            if (!line.sound_resource)
                fail(what + ": the line was presented without a sound");
            if (std::find(sounds.begin(), sounds.end(), *line.sound_resource) == sounds.end())
                fail(what + ": the line played " + *line.sound_resource);
            return;
        }
        fail(what + ": the line was not presented");
    };

    const auto skirmish = [&](std::string_view which, int ticks_before) {
        const std::string name(which);
        start_benchmark_skirmish();
        apply_output_mode();
        // --mute keeps the sound device closed; the queue still chooses the
        // sound each line plays.
        offline_services_.announcement_gates().play_audio = true;
        uint16_t commander = 0;
        for (const auto& slot : match_->world().slots)
            if (commander == 0 && slot.unit != nullptr && slot.record.type_index != 0 &&
                slot.record.owner_index == match_local_player_)
                commander = slot.unit_index;
        if (commander == 0)
            fail(name + ": found no local commander");
        // A line plays only once the window after the last one has passed,
        // and a match's first line no sooner than its first second.
        play(ticks_before + kSpeechWindowTicks);
        center_camera_on_unit(commander);
        const auto canvas_of_commander = [&] {
            const auto viewport = live_viewport(
                static_cast<uint32_t>(std::max(0, match_camera_x_)),
                static_cast<uint32_t>(std::max(0, match_camera_z_))
            );
            const auto point =
                project_match_point(viewport, match_->world().slots[commander].unit->position);
            return std::pair{static_cast<float>(point.x), static_cast<float>(point.y)};
        };

        auto [unit_x, unit_y] = canvas_of_commander();
        std::size_t from = heard.size();
        click(unit_x, unit_y);
        if (selected_match_unit_ != commander)
            fail(name + ": a click did not select the commander");
        present();
        said(from, commander, UnitAnnouncementCategory::select, name + ": the selected commander");

        play(kSpeechWindowTicks);
        std::tie(unit_x, unit_y) = canvas_of_commander();
        std::optional<std::pair<float, float>> ground;
        for (const auto& [dx, dy] : kGroundOffsets) {
            update_pointer(unit_x + dx, unit_y + dy);
            if (!ground && hovered_match_unit_ == 0 && match_world_point(unit_x + dx, unit_y + dy))
                ground = std::pair{unit_x + dx, unit_y + dy};
        }
        if (!ground)
            fail(name + ": found no open ground beside the commander");
        from = heard.size();
        click(ground->first, ground->second);
        play(kAcknowledgeTicks);
        said(
            from,
            commander,
            UnitAnnouncementCategory::acknowledge,
            name + ": the commander sent to open ground"
        );
        return commander;
    };

    skirmish("the first skirmish", kFirstSkirmishTicks);
    leave_match();
    load(Screen::main_menu);
    // What the first skirmish said, and when, must not silence the second.
    const auto commander = skirmish("the second skirmish", 0);

    // "+Sing": the select line sings instead.
    toggle_novelty_voice();
    play(kSpeechWindowTicks);
    const auto viewport = live_viewport(
        static_cast<uint32_t>(std::max(0, match_camera_x_)),
        static_cast<uint32_t>(std::max(0, match_camera_z_))
    );
    const auto point =
        project_match_point(viewport, match_->world().slots[commander].unit->position);
    const std::size_t from = heard.size();
    click(static_cast<float>(point.x), static_cast<float>(point.y));
    present();
    said(from, commander, UnitAnnouncementCategory::select, "\"+Sing\": the selected commander");
    toggle_novelty_voice();
    std::cout << "unit speech check: the commander said its select and order lines in two "
                 "skirmishes, and sang with +Sing\n";
}

} // namespace oa::app
