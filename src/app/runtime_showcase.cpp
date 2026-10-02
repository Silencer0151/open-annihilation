// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The scripted showcases --showcase plays: pointer and key events sent
// through dispatch_event() as SDL delivers the player's, on the application
// loop's frames and clock, so that a video capture shows them as a player
// sees them (docs/capture.md).
#include "oa/app/runtime.hpp"
#include "oa/present/world_renderer/world_camera.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {

namespace {

// Seconds the showcase gives each step, so that a viewer can take it in.
constexpr double kMenuSeconds = 3.0;         // the main menu before SINGLE
constexpr double kScreenSeconds = 1.5;       // a menu screen before its next click
constexpr double kBriefingSeconds = 9.0;     // the briefing and its narration
constexpr double kMissionStartSeconds = 3.0; // the mission's opening view
constexpr double kOrderSeconds = 1.0;        // between the orders
constexpr double kGlamourSeconds = 5.0;      // the victory picture before a click
constexpr double kScoreSeconds = 6.0;        // the score screen at the end
constexpr double kGlideSeconds = 0.6;        // the pointer's move to what it clicks
constexpr double kPressSeconds = 0.15;       // a button held down, drawn pressed
// Longest waits: for a screen to open, and for the march to the gate.
constexpr double kScreenTimeoutSeconds = 60.0;
constexpr double kMarchTimeoutSeconds = 900.0;
constexpr double kNanosecondsPerSecond = 1e9;
// The camera changes to a unit nearer the goal only when it is this many map
// pixels nearer, so that it does not jump between neighbours, and looks for
// the nearest this often, in seconds.
constexpr double kFollowMarginPixels = 160.0;
constexpr double kFollowSeconds = 1.0;
// The standing move order Hold Position, and how many settings MOVE ORDERS
// cycles through.
constexpr uint32_t kHoldPosition = 0;
constexpr int kMoveOrderSettings = 3;
// 16.16 world units to whole map pixels.
constexpr int32_t kFixedShift = 16;
// The campaign the first Arm mission belongs to.
constexpr std::string_view kArmCampaign = "Arm Campaign";
// --showcase skirmish-battle: the seconds the battle plays once its armies
// stand, and the units each side brings without --combat.
constexpr double kBattleSeconds = 60.0;
constexpr std::size_t kBattleUnitsPerSide = 50;
// A battle frame longer than five ticks at normal speed, the most a frame
// catches up, costs the match ticks; the showcase lists the first few.
constexpr double kSlowFrameSeconds = 5.0 / 30.0;
constexpr std::size_t kSlowFramesShown = 12;
// The skirmish's commander death rule in the game's preferences, and its
// value for a game that continues after a commander's death.
constexpr std::string_view kCommanderDeathKey = "SkirmishCommanderDeath";
constexpr const char* kGameContinues = "0";

// Throws the showcase's error.
[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("showcase: " + what);
}

// Prints one "showcase:" line; the terminal follows the run.
void report(const std::string& what) {
    std::printf("showcase: %s\n", what.c_str());
    std::fflush(stdout);
}

// The unit's standing move order.
uint32_t move_order(const oa::Unit& unit) {
    return (unit.flags & OA_UNIT_FLAG_MOVE_ORDER_MASK) >> OA_UNIT_FLAG_MOVE_ORDER_SHIFT;
}

} // namespace

void Runtime::run_battle_showcase() {
    bool running = true;
    const auto now = [] { return static_cast<double>(SDL_GetTicksNS()) / kNanosecondsPerSecond; };
    // One pass of the application loop, paced as run() paces it; the time it
    // worked, without the wait, is added to `work`.
    double work = 0.0;
    const auto frame = [&] {
        const double start = now();
        run_frame(running);
        work += now() - start;
        if (!running || exit_requested_)
            fail("the game was closed");
        pace_next_frame(running);
    };
    const double menu_end = now() + kMenuSeconds;
    while (now() < menu_end)
        frame();
    // The battle plays on when a commander falls: the skirmish screen reads
    // the game continuing after a commander's death, and the player's own
    // choice is put back once the skirmish has started.
    const auto commander_key =
        preference_key(oa::ui::frontend_state::initialization::general_section, kCommanderDeathKey);
    const auto kept_value = preference_values_.find(commander_key);
    const std::optional<std::string> kept =
        kept_value != preference_values_.end() ? std::optional{kept_value->second} : std::nullopt;
    const auto kept_rule = preferences_.skirmish.commander_death;
    preference_values_[commander_key] = kGameContinues;
    start_benchmark_skirmish();
    if (kept)
        preference_values_[commander_key] = *kept;
    else
        preference_values_.erase(commander_key);
    preferences_.skirmish.commander_death = kept_rule;
    preferences_dirty_ = true;
    flush_preferences();
    const auto units = options_.combat_units != 0 ? options_.combat_units : kBattleUnitsPerSide;
    // The busy combat's units, away from the armies, keep the player in the
    // game when the army beside the commander falls.
    options_.busy_combat = true;
    spawn_combat_armies(units);
    const uint32_t drawing_threads = draw_pool_ ? draw_pool_->threads() : 1U;
    report(
        "the battle started at " + std::to_string(match_layout_.width) + "x" +
        std::to_string(match_layout_.height) + " with " + std::to_string(units) +
        " units a side and the busy combat's, at most " +
        std::to_string(options_.max_frames_per_second) + " frames a second, on " +
        std::to_string(drawing_threads) + " drawing thread" + (drawing_threads > 1 ? "s" : "")
    );
    phase_times_ = {};
    work = 0.0;
    const uint32_t first_tick = match_timing_.tick;
    const double start = now();
    double previous = start;
    double longest = 0.0;
    // The longest frame's ticks and the time they, its drawing and its
    // presenting took.
    uint32_t longest_ticks = 0;
    PhaseTimes longest_phases{};
    std::size_t frames = 0;
    // Frames too long for the clock to catch up within the most ticks a
    // frame runs, which the match loses.
    std::size_t slow_frames = 0;
    while (previous - start < kBattleSeconds) {
        const auto phases = phase_times_;
        const uint32_t tick = match_timing_.tick;
        frame();
        const double finished = now();
        const PhaseTimes spent{
            phase_times_.simulation - phases.simulation,
            phase_times_.compose - phases.compose,
            0,
            0,
            phase_times_.upload - phases.upload,
            phase_times_.present - phases.present,
        };
        if (finished - previous > kSlowFrameSeconds) {
            ++slow_frames;
            if (slow_frames <= kSlowFramesShown) {
                char slow[160];
                std::snprintf(
                    slow,
                    sizeof slow,
                    "slow frame at tick %u: %.1f ms (%u ticks in %.1f ms, draw %.1f ms, "
                    "present %.1f ms)",
                    tick,
                    (finished - previous) * 1000.0,
                    match_timing_.tick - tick,
                    static_cast<double>(spent.simulation) / 1.0e6,
                    static_cast<double>(spent.compose) / 1.0e6,
                    static_cast<double>(spent.upload + spent.present) / 1.0e6
                );
                report(slow);
            }
        }
        if (finished - previous > longest) {
            longest = finished - previous;
            longest_ticks = match_timing_.tick - tick;
            longest_phases = spent;
        }
        previous = finished;
        ++frames;
        if (screen_ != Screen::match) {
            char ended[64];
            std::snprintf(
                ended, sizeof ended, "the match ended %.1f s into the battle", previous - start
            );
            report(ended);
            break;
        }
    }
    const double seconds = previous - start;
    const uint32_t ticks = match_timing_.tick - first_tick;
    const auto per_frame_ms = [&](double total_seconds) {
        return frames == 0 ? 0.0 : total_seconds * 1000.0 / static_cast<double>(frames);
    };
    const auto ns_to_seconds = [](int64_t ns) {
        return static_cast<double>(ns) / kNanosecondsPerSecond;
    };
    char line[640];
    std::snprintf(
        line,
        sizeof line,
        "battle %.1f s: %u ticks (%.1f a second), %zu frames (%.1f a second), game speed %d "
        "of %d; frame %.1f ms mean, %.1f longest (%u ticks in %.1f ms, draw %.1f ms, present "
        "%.1f ms); %zu frames slower than %.0f ms; work %.1f ms a frame; tick %.2f ms mean; "
        "draw %.1f ms and present %.1f ms a frame",
        seconds,
        ticks,
        seconds > 0.0 ? ticks / seconds : 0.0,
        frames,
        seconds > 0.0 ? static_cast<double>(frames) / seconds : 0.0,
        static_cast<int>(match_timing_.actual_rate),
        static_cast<int>(match_timing_.requested_rate),
        per_frame_ms(seconds),
        longest * 1000.0,
        longest_ticks,
        ns_to_seconds(longest_phases.simulation) * 1000.0,
        ns_to_seconds(longest_phases.compose) * 1000.0,
        ns_to_seconds(longest_phases.upload + longest_phases.present) * 1000.0,
        slow_frames,
        kSlowFrameSeconds * 1000.0,
        per_frame_ms(work),
        ticks == 0 ? 0.0 : ns_to_seconds(phase_times_.simulation) * 1000.0 / ticks,
        per_frame_ms(ns_to_seconds(phase_times_.compose)),
        per_frame_ms(ns_to_seconds(phase_times_.upload + phase_times_.present))
    );
    report(line);
    print_memory_status();
}

void Runtime::run_showcase() {
    if (sdl_.window == nullptr || sdl_.renderer == nullptr)
        fail("needs the game's window");
    if (options_.showcase == Showcase::skirmish_battle) {
        run_battle_showcase();
        return;
    }
    bool running = true;
    const auto now = [] { return static_cast<double>(SDL_GetTicksNS()) / kNanosecondsPerSecond; };
    // One pass of the application loop, paced as run() paces it.
    const auto frame = [&] {
        run_frame(running);
        if (!running || exit_requested_)
            fail("the game was closed");
        pace_next_frame(running);
    };
    const auto hold = [&](double seconds) {
        const auto end = now() + seconds;
        while (now() < end)
            frame();
    };
    const auto wait_for = [&](const std::function<bool()>& done, const std::string& what) {
        const auto end = now() + kScreenTimeoutSeconds;
        while (!done()) {
            if (now() > end)
                fail(
                    "no " + what + " after " + std::to_string(std::lround(kScreenTimeoutSeconds)) +
                    " s"
                );
            frame();
        }
    };

    // The pointer, in window coordinates, and the events that move and press
    // it, as SDL delivers them.
    int window_width = 0;
    int window_height = 0;
    if (!SDL_GetWindowSize(sdl_.window, &window_width, &window_height))
        fail(std::string("cannot read the window's size: ") + SDL_GetError());
    float pointer_x = static_cast<float>(window_width) / 2.0F;
    float pointer_y = static_cast<float>(window_height) / 2.0F;
    const auto send = [&](SDL_EventType type, uint8_t button) {
        SDL_Event event{};
        event.type = type;
        if (type == SDL_EVENT_MOUSE_MOTION) {
            event.motion.windowID = SDL_GetWindowID(sdl_.window);
            event.motion.x = pointer_x;
            event.motion.y = pointer_y;
        } else {
            event.button.windowID = SDL_GetWindowID(sdl_.window);
            event.button.button = button;
            event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.clicks = 1;
            event.button.x = pointer_x;
            event.button.y = pointer_y;
        }
        dispatch_event(event, running);
    };
    // Glides the pointer to a point of the current screen's canvas, easing in
    // and out, one step a frame.
    const auto glide = [&](float canvas_x, float canvas_y) {
        float to_x = 0.0F;
        float to_y = 0.0F;
        if (!SDL_RenderCoordinatesToWindow(sdl_.renderer, canvas_x, canvas_y, &to_x, &to_y))
            fail(std::string("SDL_RenderCoordinatesToWindow: ") + SDL_GetError());
        const float from_x = pointer_x;
        const float from_y = pointer_y;
        const auto start = now();
        for (;;) {
            const double t = std::min(1.0, (now() - start) / kGlideSeconds);
            const auto eased = static_cast<float>(t * t * (3.0 - 2.0 * t));
            pointer_x = from_x + (to_x - from_x) * eased;
            pointer_y = from_y + (to_y - from_y) * eased;
            send(SDL_EVENT_MOUSE_MOTION, 0);
            if (t >= 1.0)
                return;
            frame();
        }
    };
    // A left click where the pointer is, held long enough to show pressed.
    const auto press_here = [&] {
        send(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT);
        hold(kPressSeconds);
        send(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT);
    };
    const auto click = [&](float canvas_x, float canvas_y) {
        glide(canvas_x, canvas_y);
        press_here();
    };
    // A key pressed and released with the modifiers held.
    const auto press_key = [&](SDL_Keycode key, SDL_Scancode scancode, SDL_Keymod modifiers) {
        SDL_SetModState(modifiers);
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.windowID = SDL_GetWindowID(sdl_.window);
        event.key.key = key;
        event.key.scancode = scancode;
        event.key.mod = modifiers;
        event.key.down = true;
        dispatch_event(event, running);
        event.type = SDL_EVENT_KEY_UP;
        event.key.down = false;
        dispatch_event(event, running);
        SDL_SetModState(SDL_KMOD_NONE);
    };
    // A gadget of the frontend screen, clicked at its centre.
    const auto click_gadget = [&](std::string_view name) {
        const auto* gadget = widget(name);
        if (gadget == nullptr)
            fail("the screen has no " + std::string(name));
        const auto origin = panel_origin();
        click(
            static_cast<float>(origin.x + gadget->common.x) +
                static_cast<float>(gadget->common.width) / 2.0F,
            static_cast<float>(origin.y + gadget->common.y) +
                static_cast<float>(gadget->common.height) / 2.0F
        );
        report("clicked " + std::string(name));
    };
    const auto shows = [this](Screen screen) {
        return [this, screen] { return screen_ == screen; };
    };
    // A button of the match's side panel, by its action (ARMMOVEORD's is MOVEORD).
    const auto hud_button = [&](std::string_view action) -> const oa::ui::gui_layout::Gadget* {
        if (!match_hud_)
            return nullptr;
        for (const auto& gadget : match_hud_->layout.gadgets)
            if (match_hud_action(gadget.common.name) == action)
                return &gadget;
        return nullptr;
    };
    const auto click_hud = [&](std::string_view action) {
        const auto* gadget = hud_button(action);
        if (gadget == nullptr)
            fail("the side panel has no " + std::string(action));
        const auto rect = oa::ui::display_layout::source_rect_to_canvas(
            match_layout_,
            gadget->common.x,
            gadget->common.y,
            gadget->common.width,
            gadget->common.height
        );
        click(
            static_cast<float>(rect.x) + static_cast<float>(rect.width) / 2.0F,
            static_cast<float>(rect.y) + static_cast<float>(rect.height) / 2.0F
        );
        report("clicked " + std::string(action));
    };

    // The main menu, SINGLE, and New Campaign on the Arm side.
    send(SDL_EVENT_MOUSE_MOTION, 0);
    hold(kMenuSeconds);
    click_gadget(menu::resource_name(menu::Button::single_player));
    wait_for(shows(Screen::single_player), "SINGLE.GUI");
    hold(kScreenSeconds);
    click_gadget(entry::resource_name(entry::Button::new_campaign));
    wait_for(shows(Screen::new_campaign), "NEWGAME.GUI");
    hold(kScreenSeconds);
    click_gadget("Arm");
    if (preferences_.side != 0)
        fail("Arm did not choose the Arm side");
    if (selected_campaign_index_ >= campaign_labels_.size() ||
        campaign_labels_[selected_campaign_index_] != kArmCampaign)
        fail("NEWGAME.GUI does not offer the " + std::string(kArmCampaign) + " first");
    report("NEWGAME.GUI: " + std::string(kArmCampaign) + ", mission 1");
    hold(kScreenSeconds);

    // The first mission's briefing, and its Start.
    click_gadget("Start");
    wait_for(shows(Screen::briefing), "briefing");
    hold(kBriefingSeconds);
    click_gadget("Start");
    wait_for([this] { return screen_ == Screen::match && match_ != nullptr; }, "mission");
    report("the mission started");
    hold(kMissionStartSeconds);

    // Every unit selected, holding position, and sent to the goal on the radar.
    const auto goal = mission_move_goal();
    if (!goal)
        fail("the mission has no MoveUnitToRadius victory condition");
    // The goal's map pixel on the screen plane, where the radar shows it.
    const oa::FixedVec3 goal_position{goal->point[0], goal->point[1], goal->point[2]};
    const auto goal_x = oa::present::world_renderer::world_screen_x(goal_position);
    const auto goal_y = oa::present::world_renderer::world_screen_y(goal_position);
    report(
        "the goal is at map pixel (" + std::to_string(goal_x) + ", " + std::to_string(goal_y) + ")"
    );
    press_key(SDLK_A, SDL_SCANCODE_A, SDL_KMOD_LCTRL);
    std::vector<uint16_t> group;
    for (const auto id : selected_local_ids())
        if (match_->takes_move_order(id))
            group.push_back(id);
    if (group.empty())
        fail("Ctrl+A selected no unit that moves");
    report("Ctrl+A selected " + std::to_string(group.size()) + " units that move");
    hold(kOrderSeconds);
    const auto present = [this](uint16_t id) { return match_unit_present(id); };
    const auto unit_of = [this](uint16_t id) -> const oa::Unit& {
        return match_->world().slots[id].record;
    };
    const auto holding = [&] {
        return std::all_of(group.begin(), group.end(), [&](uint16_t id) {
            return !present(id) || move_order(unit_of(id)) == kHoldPosition;
        });
    };
    if (hud_button("MOVEORD") == nullptr) {
        click_hud("ORDERS");
        hold(kOrderSeconds);
    }
    for (int clicks = 0; !holding(); ++clicks) {
        if (clicks == kMoveOrderSettings)
            fail("MOVE ORDERS did not give every unit Hold Position");
        click_hud("MOVEORD");
        hold(kOrderSeconds);
    }
    report("MOVE ORDERS: Hold Position");
    if (hud_button("MOVE") != nullptr)
        click_hud("MOVE");
    // radar_world_point() takes radar pixel p to map pixel p * map / radar;
    // the pointer goes to the middle of the pixel nearest the map pixel.
    const auto radar_pixel = [](int32_t map, int32_t picture, int32_t map_size) {
        constexpr float kPixelMiddle = 0.5F;
        return static_cast<float>(
                   std::lround(static_cast<double>(map) * picture / static_cast<double>(map_size))
               ) +
               kPixelMiddle;
    };
    if (radar_map_w_ <= 0 || radar_map_h_ <= 0)
        fail("the radar shows no map");
    click(
        static_cast<float>(radar_picture_.x) +
            radar_pixel(goal_x, radar_picture_.width, radar_map_w_),
        static_cast<float>(radar_picture_.y) +
            radar_pixel(goal_y, radar_picture_.height, radar_map_h_)
    );
    report("clicked the goal on the radar: " + status_);
    // The pointer rests over the top bar, off the battlefield.
    glide(
        static_cast<float>(match_layout_.width) / 2.0F, static_cast<float>(match_layout_.top) / 2.0F
    );

    // The march: the camera follows the unit of the group nearest the goal.
    const auto distance = [&](uint16_t id) {
        const auto& unit = unit_of(id);
        const auto dx =
            static_cast<double>((unit.position.x >> kFixedShift) - (goal->point[0] >> kFixedShift));
        const auto dz =
            static_cast<double>((unit.position.z >> kFixedShift) - (goal->point[2] >> kFixedShift));
        return std::sqrt(dx * dx + dz * dz);
    };
    const auto follow_front = [&] {
        uint16_t front = 0;
        double nearest = std::numeric_limits<double>::max();
        for (const auto id : group)
            if (present(id) && distance(id) < nearest) {
                front = id;
                nearest = distance(id);
            }
        if (front == 0)
            fail("every unit of the group was destroyed");
        const bool following =
            match_tracking_ && present(tracked_match_unit_) &&
            std::find(group.begin(), group.end(), tracked_match_unit_) != group.end();
        if (following && (tracked_match_unit_ == front ||
                          distance(tracked_match_unit_) - nearest < kFollowMarginPixels))
            return;
        // T follows the next selected unit; it is pressed until the front one.
        const auto selected = selected_local_ids().size();
        for (std::size_t presses = 0;
             presses <= selected && !(match_tracking_ && tracked_match_unit_ == front);
             ++presses)
            press_key(SDLK_T, SDL_SCANCODE_T, SDL_KMOD_NONE);
        report(
            "the camera follows " + unit_info_name(front) + " (unit " + std::to_string(front) +
            "), " + std::to_string(std::lround(nearest)) + " pixels from the goal"
        );
    };
    const auto give_up = now() + kMarchTimeoutSeconds;
    auto next_look = now();
    while (screen_ == Screen::match && !match_finished_) {
        if (now() > give_up)
            fail("the units did not reach the goal in time");
        if (now() >= next_look || !match_tracking_) {
            follow_front();
            next_look = now() + kFollowSeconds;
        }
        frame();
    }
    // The finished match's Game block, which the end screen shows, holds
    // its outcome.
    const auto* finished = endgame_world();
    if (finished == nullptr ||
        (finished->game.outcome_flags & sim::scenario::outcome_flag::won) == 0)
        fail("the mission was lost");
    report("victory");

    // The end screen: the darkened last frame, the glamour picture, which a
    // click advances once it has shown a while, and the score screen.
    const auto end_step = [this] {
        const auto* world = endgame_world();
        return world != nullptr ? world->game.endgame_state : 0;
    };
    wait_for(
        [&] {
            const auto* world = endgame_world();
            return world != nullptr && world->game.endgame_state == OA_ENDGAME_GLAMOUR &&
                   world->game.endgame_fade_done != 0;
        },
        "glamour picture"
    );
    report("the glamour picture");
    hold(kGlamourSeconds);
    press_here();
    wait_for([&] { return end_step() == OA_ENDGAME_PANEL; }, "score screen");
    report("the score screen");
    hold(kScoreSeconds);
    report("done");
}

} // namespace oa::app
