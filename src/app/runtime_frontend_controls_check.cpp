// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// MAINMENU.GUI's buttons under the pointer and held down, then the setup
// screens' options clicked through the SDL presenter as a player does: each
// click changes its setting and what the screen shows for it before Start,
// the help line follows the pointer, and the chosen options tab shows pressed.
// Alt+Enter switches the window to full screen and back on a menu and in a
// match.
#include "oa/app/runtime.hpp"
#include "oa/ui/frontend_dialogs.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace oa::app {

namespace {

// Captions of SKIRMISH.GUI's and NEWGAME.GUI's Difficulty stages.
constexpr std::array<std::string_view, 3> kDifficultyCaptions{"Easy", "Medium", "Hard"};
// The alliance image a row shows: frame 10 for an empty group, alliance*2+1
// for a lone member, alliance*2 for a shared group.
constexpr std::size_t kEmptyAllianceFrame = 10;
constexpr auto kLeft = static_cast<uint8_t>(SDL_BUTTON_LEFT);
constexpr auto kRight = static_cast<uint8_t>(SDL_BUTTON_RIGHT);
// A canvas point no gadget of the setup screens covers.
constexpr int32_t kParkX = 0;
constexpr int32_t kParkY = 0;
// The width of the message box Alt+Enter is pressed over, in canvas pixels.
constexpr int32_t kMessageBoxWidth = 300;
// A text list's rows start this many pixels below its top.
constexpr int32_t kListRowsTop = 2;

[[noreturn]] void fail(std::string_view what) {
    throw std::runtime_error("frontend controls check: " + std::string(what));
}

void require(bool condition, std::string_view what) {
    if (!condition)
        fail(what);
}

// <stem>-<step>.ppm beside --snapshot.
fs::path step_snapshot(const fs::path& snapshot, std::string_view step) {
    return snapshot.parent_path() / (snapshot.stem().string() + '-' + std::string(step) + ".ppm");
}

// Pixels that differ between two frames inside a canvas rectangle.
std::size_t changed_pixels(
    const renderer::Surface& before,
    const renderer::Surface& after,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height
) {
    if (before.width != after.width || before.height != after.height)
        return static_cast<std::size_t>(std::max(width, 0)) *
               static_cast<std::size_t>(std::max(height, 0));
    std::size_t changed = 0;
    for (int32_t row = std::max(y, 0); row < std::min<int32_t>(y + height, before.height); ++row)
        for (int32_t column = std::max(x, 0); column < std::min<int32_t>(x + width, before.width);
             ++column) {
            const auto offset =
                (static_cast<std::size_t>(row) * before.width + static_cast<std::size_t>(column)) *
                3U;
            if (!std::equal(
                    before.rgb.begin() + static_cast<std::ptrdiff_t>(offset),
                    before.rgb.begin() + static_cast<std::ptrdiff_t>(offset + 3U),
                    after.rgb.begin() + static_cast<std::ptrdiff_t>(offset)
                ))
                ++changed;
        }
    return changed;
}

std::string_view controller_caption(int32_t controller) {
    switch (controller) {
    case entry::controller::disabled:
        return "Open";
    case entry::controller::human:
        return "Player";
    case entry::controller::computer:
        return "Computer";
    default:
        return {};
    }
}

} // namespace

void Runtime::check_frontend_controls() {
    namespace dialogs = oa::ui::frontend_dialogs;
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        fail("needs the SDL renderer");
    bool running = true;
    std::vector<std::string> problems;
    const auto expect = [&](bool condition, std::string what) {
        if (!condition) {
            std::cerr << "frontend controls check: " << what << '\n';
            problems.push_back(std::move(what));
        }
    };
    const auto snapshot = [&](std::string_view step) {
        if (!options_.snapshot.empty())
            write_ppm(step_snapshot(options_.snapshot, step), frame_without_cursor());
    };
    const auto send = [&](SDL_EventType type,
                          int32_t x,
                          int32_t y,
                          uint8_t button,
                          uint8_t clicks = 1) {
        float window_x = 0.0F;
        float window_y = 0.0F;
        require(
            SDL_RenderCoordinatesToWindow(
                sdl_.renderer, static_cast<float>(x), static_cast<float>(y), &window_x, &window_y
            ),
            SDL_GetError()
        );
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
            event.button.clicks = clicks;
            event.button.x = window_x;
            event.button.y = window_y;
        }
        dispatch_event(event, running);
    };
    // The canvas point of a layout point: the map modal is centred.
    const auto canvas_origin = [&]() -> std::pair<int32_t, int32_t> {
        if (screen_ != Screen::map_selection || resources_.layout.gadgets.empty())
            return {0, 0};
        const auto& root = resources_.layout.gadgets.front().common;
        return {(kCanvasWidth - root.width) / 2, (kCanvasHeight - root.height) / 2};
    };
    const auto gadget_named = [&](std::string_view name) -> const oa::ui::gui_layout::Gadget& {
        const auto* gadget = widget(name);
        if (gadget == nullptr)
            fail(
                "screen " + std::to_string(static_cast<int>(screen_)) + " has no " +
                std::string(name)
            );
        return *gadget;
    };

    // The gadget's rectangle on the canvas.
    struct Box {
        int32_t x{};
        int32_t y{};
        int32_t width{};
        int32_t height{};
    };

    const auto box_of = [&](std::string_view name) {
        const auto& gadget = gadget_named(name);
        const auto [x, y] = canvas_origin();
        return Box{
            x + gadget.common.x, y + gadget.common.y, gadget.common.width, gadget.common.height
        };
    };
    const auto park_pointer = [&] {
        send(SDL_EVENT_MOUSE_MOTION, kParkX, kParkY, 0);
        require(!hovered_, "the parking point is over a gadget");
    };
    // The frame with nothing hovered, as the screen shows its settings.
    const auto frame = [&] {
        park_pointer();
        return frame_without_cursor();
    };
    // Clicks a gadget; the pointer stays over it.
    const auto click = [&](std::string_view name, uint8_t button = kLeft) {
        const auto box = box_of(name);
        const auto x = box.x + box.width / 2;
        const auto y = box.y + box.height / 2;
        send(SDL_EVENT_MOUSE_MOTION, x, y, 0);
        send(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y, button);
        send(SDL_EVENT_MOUSE_BUTTON_UP, x, y, button);
        idle_tick();
    };
    // Moves the pointer onto a gadget without clicking.
    const auto hover = [&](std::string_view name) {
        const auto box = box_of(name);
        send(SDL_EVENT_MOUSE_MOTION, box.x + box.width / 2, box.y + box.height / 2, 0);
        require(
            hovered_ && resources_.layout.gadgets[*hovered_].common.name == name,
            "the pointer is not over " + std::string(name)
        );
    };
    // Whether a button's status is set, as the chosen member of its group.
    const auto pressed = [&](std::string_view name) {
        const auto* fields =
            std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget_named(name).fields);
        return fields != nullptr && fields->status != 0;
    };
    // The caption a button shows: its caption's segment for its stage.
    const auto caption = [&](std::string_view name) {
        const auto& gadget = gadget_named(name);
        const auto* fields = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget.fields);
        if (fields == nullptr)
            fail(std::string(name) + " is not a button");
        const auto stage = widget_text_stages_.find(std::string(name));
        return std::string(
            renderer::staged_caption(
                fields->text, stage == widget_text_stages_.end() ? 0U : stage->second
            )
        );
    };
    const auto label_text = [&](std::string_view name) {
        const auto& gadget = gadget_named(name);
        if (const auto* fields = std::get_if<oa::ui::gui_layout::LabelFields>(&gadget.fields))
            return fields->text;
        if (const auto* fields = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget.fields))
            return fields->text;
        fail(std::string(name) + " has no text");
    };
    const auto shown_frame = [&](std::string_view name) -> std::optional<std::size_t> {
        const auto found = widget_gaf_frames_.find(std::string(name));
        if (found == widget_gaf_frames_.end())
            return std::nullopt;
        return found->second;
    };
    const auto repainted = [&](const renderer::Surface& before,
                               const renderer::Surface& after,
                               std::string_view name) {
        const auto box = box_of(name);
        return changed_pixels(before, after, box.x, box.y, box.width, box.height);
    };
    // Clicks a staged button round its whole cycle: every click must show
    // another caption and repaint it, and the last one the first caption again.
    const auto cycle_captions = [&](std::string_view name) {
        const auto& gadget = gadget_named(name);
        const auto* fields = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget.fields);
        if (fields == nullptr || fields->stages < 2)
            fail(std::string(name) + " is not a staged button");
        const auto stages = fields->stages;
        const auto first = caption(name);
        auto before = frame();
        for (int8_t step = 0; step < stages; ++step) {
            const auto shown = caption(name);
            click(name);
            const auto now = caption(name);
            auto after = frame();
            const auto pixels = repainted(before, after, name);
            std::cout << "frontend controls check: " << name << " '" << shown << "' -> '" << now
                      << "', " << pixels << " pixels repainted\n";
            expect(now != shown, std::string(name) + " still shows '" + shown + "' after a click");
            expect(pixels != 0, std::string(name) + " was not repainted after a click");
            before = std::move(after);
        }
        expect(
            caption(name) == first,
            std::string(name) + " did not come back to '" + first + "' after " +
                std::to_string(stages) + " clicks"
        );
    };

    // MAINMENU.GUI's buttons under the pointer: each is drawn as it is without
    // it. A press held over a button sinks it; it rises while the pointer is
    // off it and sinks again when the pointer comes back, and a release away
    // from it chooses nothing.
    require(screen_ == Screen::main_menu, "did not start on MAINMENU.GUI");
    // The frame as shown, the pointer drawn, for <stem>-<step>.ppm.
    const auto shown_snapshot = [&](std::string_view step) {
        if (options_.snapshot.empty())
            return;
        rebuild_surface();
        write_ppm(step_snapshot(options_.snapshot, step), surface_);
    };
    const auto idle_menu = frame();
    std::vector<std::string> menu_buttons;
    for (const auto& gadget : resources_.layout.gadgets) {
        const auto* fields = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget.fields);
        if (gadget.common.type == oa::ui::gui_layout::GadgetType::button && fields != nullptr &&
            !fields->grayed_out)
            menu_buttons.push_back(gadget.common.name);
    }
    require(!menu_buttons.empty(), "MAINMENU.GUI has no buttons");
    for (const auto& name : menu_buttons) {
        hover(name);
        const auto pixels = repainted(idle_menu, frame_without_cursor(), name);
        std::cout << "frontend controls check: over " << name << ", " << pixels
                  << " pixels of it redrawn\n";
        expect(pixels == 0, name + " changes under the pointer");
        if (name == menu_buttons.front())
            shown_snapshot("menu-hover");
    }
    {
        const auto& name = menu_buttons.front();
        const auto box = box_of(name);
        const auto x = box.x + box.width / 2;
        const auto y = box.y + box.height / 2;
        send(SDL_EVENT_MOUSE_MOTION, x, y, 0);
        send(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y, kLeft);
        const auto held = repainted(idle_menu, frame_without_cursor(), name);
        shown_snapshot("menu-held");
        send(SDL_EVENT_MOUSE_MOTION, kParkX, kParkY, 0);
        const auto held_off = repainted(idle_menu, frame_without_cursor(), name);
        shown_snapshot("menu-held-off");
        send(SDL_EVENT_MOUSE_MOTION, x, y, 0);
        const auto held_back = repainted(idle_menu, frame_without_cursor(), name);
        send(SDL_EVENT_MOUSE_MOTION, kParkX, kParkY, 0);
        send(SDL_EVENT_MOUSE_BUTTON_UP, kParkX, kParkY, kLeft);
        idle_tick();
        std::cout << "frontend controls check: " << name << " held " << held
                  << " pixels redrawn, off it " << held_off << ", back on it " << held_back << '\n';
        expect(held != 0, name + " held under the pointer is not drawn pressed");
        expect(held_off == 0, name + " stays pressed with the pointer off it");
        expect(held_back != 0, name + " is not drawn pressed when the pointer comes back");
        require(screen_ == Screen::main_menu, name + " released away from it was chosen");
        expect(repainted(idle_menu, frame(), name) == 0, name + " stays pressed after the release");
    }

    // SKIRMISH.GUI.
    exercise_click(menu::resource_name(menu::Button::single_player));
    require(screen_ == Screen::single_player, "did not reach SINGLE.GUI");
    click(entry::resource_name(entry::Button::skirmish));
    require(screen_ == Screen::skirmish, "Skirmish did not open SKIRMISH.GUI");
    snapshot("skirmish-start");
    auto& rules = preferences_.skirmish;

    // The rule buttons, each with the SKIRMISH.GUI caption its setting shows.
    struct Rule {
        std::string_view name;
        std::string (*caption)(const init::Preferences&);
    };

    const std::array<Rule, 5> rule_buttons{{
        {"Difficulty",
         [](const init::Preferences& p) {
             return std::string(p.difficulty <= 2 ? kDifficultyCaptions[p.difficulty] : "");
         }},
        {"Mapping",
         [](const init::Preferences& p) {
             return std::string(p.skirmish.mapping == 0 ? "Mapped" : "Unmapped");
         }},
        {"CommanderDeath",
         [](const init::Preferences& p) {
             return std::string(p.skirmish.commander_death == 0 ? "Continues" : "Game ends");
         }},
        {"StartLocation",
         [](const init::Preferences& p) {
             return std::string(p.skirmish_location == 0 ? "Random" : "Fixed");
         }},
        {"LineOfSight", [](const init::Preferences& p) {
             return std::string(
                 p.skirmish.line_of_sight == 0 ? "Permanent"
                 : p.skirmish.los_type == 1    ? "True"
                                               : "Circular"
             );
         }},
    }};
    for (const auto& rule : rule_buttons)
        expect(
            caption(rule.name) == rule.caption(preferences_),
            std::string(rule.name) + " opens showing '" + caption(rule.name) +
                "' for a setting that reads '" + rule.caption(preferences_) + "'"
        );
    const auto settings_word = [&] {
        return std::to_string(preferences_.difficulty) + '/' +
               std::to_string(preferences_.skirmish_difficulty) + ' ' +
               std::to_string(rules.mapping) + ' ' + std::to_string(rules.commander_death) + ' ' +
               std::to_string(preferences_.skirmish_location) + ' ' +
               std::to_string(rules.line_of_sight) + '/' + std::to_string(rules.los_type);
    };
    // A map chosen on SELMAP.GUI, then one click of each rule button.
    auto before = frame();
    const auto map_before = skirmish_settings_.map_name;
    click(skirmish::resource_name(skirmish::Button::select_map));
    require(
        screen_ == Screen::map_selection && !bound_map_names_.empty(),
        "Select Map did not open SELMAP.GUI with maps"
    );
    require(bound_map_names_.size() > 1, "SELMAP.GUI lists a single map");
    const auto current = static_cast<std::size_t>(std::max<int16_t>(0, modal_map_index_));
    const auto chosen_index = (current + 1U) % bound_map_names_.size();
    const auto chosen = bound_map_names_[chosen_index];
    preview_map_index(chosen_index);
    // The map's summary names its players in the game's language, in the word
    // gamedata\translate.tdf gives for "Players": "Spieler" in German.
    const auto summary = label_text("SIZE");
    load_translations("German");
    preview_map_index(chosen_index);
    const auto german_summary = label_text("SIZE");
    load_translations(command_line::launch_language(options_.launch));
    preview_map_index(chosen_index);
    std::cout << "frontend controls check: the map summary reads '" << summary << "', in German '"
              << german_summary << "'\n";
    expect(
        german_summary.find("  Spieler: ") != std::string::npos,
        "the map summary reads '" + german_summary + "' in German, without \"Spieler\""
    );
    expect(
        label_text("SIZE") == summary,
        "the map summary did not return to '" + summary + "' in the game's own language"
    );
    click(map_modal::resource_name(map_modal::Button::load));
    require(screen_ == Screen::skirmish, "Load did not return to SKIRMISH.GUI");
    auto after_map = frame();
    std::cout << "frontend controls check: map '" << map_before << "' -> '"
              << skirmish_settings_.map_name << "', MapName shows '" << label_text("MapName")
              << "', " << repainted(before, after_map, "MapName") << " pixels repainted\n";
    expect(skirmish_settings_.map_name == chosen, "Load did not choose " + chosen);
    expect(
        label_text("MapName") == chosen,
        "MapName shows '" + label_text("MapName") + "' for the map " + chosen
    );
    expect(repainted(before, after_map, "MapName") != 0, "MapName was not repainted");
    for (const auto& rule : rule_buttons)
        expect(
            caption(rule.name) == rule.caption(preferences_),
            std::string(rule.name) + " shows '" + caption(rule.name) +
                "' after the map for a setting that reads '" + rule.caption(preferences_) + "'"
        );
    snapshot("skirmish-map");
    before = std::move(after_map);
    for (const auto& rule : rule_buttons) {
        const auto settings_before = settings_word();
        const auto help_before = gadget_named(rule.name).common.runtime_help;
        click(rule.name);
        const auto help = gadget_named(rule.name).common.runtime_help;
        const auto shown_help = label_text("HELPTEXT");
        if (rule.name == "Mapping")
            snapshot("skirmish-help");
        auto after = frame();
        const auto shown = caption(rule.name);
        const auto pixels = repainted(before, after, rule.name);
        std::cout << "frontend controls check: " << rule.name << " settings " << settings_before
                  << " -> " << settings_word() << ", shows '" << shown << "', help '" << shown_help
                  << "', " << pixels << " pixels repainted\n";
        expect(
            settings_word() != settings_before,
            std::string(rule.name) + " did not change its setting"
        );
        expect(
            shown == rule.caption(preferences_),
            std::string(rule.name) + " shows '" + shown + "' for a setting that reads '" +
                rule.caption(preferences_) + "'"
        );
        expect(pixels != 0, std::string(rule.name) + " was not repainted after a click");
        expect(
            preferences_.skirmish_difficulty == preferences_.difficulty,
            "Difficulty did not store the skirmish difficulty"
        );
        if (rule.name != "Difficulty") {
            expect(help != help_before, std::string(rule.name) + " kept its help after a click");
            expect(
                shown_help == help,
                "HELPTEXT shows '" + shown_help + "' over " + std::string(rule.name) +
                    " instead of its help '" + help + "'"
            );
        }
        before = std::move(after);
        if (rule.name == "Difficulty")
            snapshot("skirmish-difficulty");
    }
    snapshot("skirmish-rules");
    for (const auto& rule : rule_buttons)
        cycle_captions(rule.name);

    // The first rows' controls: each click changes the row's setting, and the
    // text or frame shown for it.
    before = frame();
    const auto slot_step = [&](std::string_view name,
                               uint8_t button,
                               auto&& setting,
                               auto&& shown_matches,
                               std::string_view what) {
        const auto value_before = setting();
        click(name, button);
        auto after = frame();
        const auto pixels = repainted(before, after, name);
        std::cout << "frontend controls check: " << name << ' ' << what << ' ' << value_before
                  << " -> " << setting() << ", " << pixels << " pixels repainted\n";
        expect(
            setting() != value_before, std::string(name) + " did not change " + std::string(what)
        );
        expect(shown_matches(), std::string(name) + " does not show " + std::string(what));
        expect(pixels != 0, std::string(name) + " was not repainted after a click");
        before = std::move(after);
    };
    auto& slots = skirmish_settings_.slots;
    for (int i = 0; i < 2; ++i)
        slot_step(
            "Player1",
            kLeft,
            [&] { return slots[1].controller; },
            [&] { return label_text("Player1") == controller_caption(slots[1].controller); },
            "its controller"
        );
    for (int i = 0; i < 2; ++i)
        slot_step(
            "Side0",
            kLeft,
            [&] { return slots[0].side; },
            [&] { return shown_frame("Side0") == static_cast<std::size_t>(slots[0].side); },
            "its side"
        );
    for (const auto button : {kLeft, kRight})
        slot_step(
            "Color0",
            button,
            [&] { return slots[0].color; },
            [&] { return shown_frame("Color0") == static_cast<std::size_t>(slots[0].color); },
            "its colour"
        );
    slot_step(
        "Allies0",
        kLeft,
        [&] { return slots[0].alliance; },
        [&] {
            const auto alliance = slots[0].alliance;
            const auto members = skirmish::alliance_members(skirmish_settings_, alliance);
            const auto expected = members == 0   ? kEmptyAllianceFrame
                                  : members == 1 ? static_cast<std::size_t>(alliance) * 2U + 1U
                                                 : static_cast<std::size_t>(alliance) * 2U;
            return shown_frame("Allies0") == expected;
        },
        "its allegiance"
    );
    snapshot("skirmish-rows");
    for (const std::string_view name : {"Metal0", "Energy0"}) {
        auto& amount = name == "Metal0" ? slots[0].metal : slots[0].energy;
        const auto start = amount;
        // Up then down, or down then up from the top.
        const bool up_first = amount < skirmish::resource_maximum;
        for (const bool up : {up_first, !up_first})
            slot_step(
                name,
                up ? kLeft : kRight,
                [&] { return amount; },
                [&] { return label_text(name) == std::to_string(amount); },
                "its amount"
            );
        expect(
            amount == start, std::string(name) + " did not come back to " + std::to_string(start)
        );
    }
    // The help line under the pointer: the allegiance and resource hints over
    // their row controls, nothing over the others. Each control without help
    // follows one with help, so its hover has to clear the line.
    constexpr std::array<std::pair<std::string_view, std::string_view>, 6> row_help{{
        {"Allies0", "Click to select an allegiance symbol."},
        {"Player0", ""},
        {"Metal0", "Left click to increase metal. Right click to decrease metal."},
        {"Side0", ""},
        {"Energy0", "Left click to increase energy. Right click to decrease energy."},
        {"Color0", ""},
    }};
    park_pointer();
    for (const auto& [name, help] : row_help) {
        hover(name);
        const auto shown_help = label_text("HELPTEXT");
        std::cout << "frontend controls check: over " << name << " HELPTEXT '" << shown_help
                  << "'\n";
        expect(
            shown_help == help,
            "HELPTEXT shows '" + shown_help + "' over " + std::string(name) + " instead of '" +
                std::string(help) + "'"
        );
        if (name == "Allies0")
            snapshot("skirmish-row-help");
    }

    // NEWGAME.GUI's difficulty and side.
    click(skirmish::resource_name(skirmish::Button::previous_menu));
    require(screen_ == Screen::single_player, "Previous Menu did not return to SINGLE.GUI");
    click(entry::resource_name(entry::Button::new_campaign));
    require(screen_ == Screen::new_campaign, "New Campaign did not open NEWGAME.GUI");
    expect(
        caption("Difficulty") == kDifficultyCaptions[preferences_.difficulty % 3U],
        "NEWGAME.GUI opens showing '" + caption("Difficulty") + "' for difficulty " +
            std::to_string(preferences_.difficulty)
    );
    cycle_captions("Difficulty");
    for (std::size_t step = 0; step < kDifficultyCaptions.size(); ++step) {
        click("Difficulty");
        expect(
            caption("Difficulty") == kDifficultyCaptions[preferences_.difficulty % 3U],
            "NEWGAME.GUI shows '" + caption("Difficulty") + "' for difficulty " +
                std::to_string(preferences_.difficulty)
        );
    }

    // The side buttons: the chosen side's Arm/Core button and emblem show
    // pressed, the other side's raised.
    struct SideChoice {
        std::string_view button;
        std::string_view emblem;
        uint32_t side;
    };

    constexpr std::array<SideChoice, 2> sides{{{"Arm", "Side0", 0}, {"Core", "Side1", 1}}};
    const auto shows_side = [&](uint32_t side) {
        const auto& chosen = sides[side];
        const auto& other = sides[1U - side];
        return pressed(chosen.button) && pressed(chosen.emblem) && !pressed(other.button) &&
               !pressed(other.emblem);
    };
    expect(
        preferences_.side <= 1U && shows_side(preferences_.side),
        "NEWGAME.GUI opens without showing side " + std::to_string(preferences_.side)
    );
    snapshot("newgame-start");
    before = frame();
    for (const auto side : {1U - (preferences_.side & 1U), preferences_.side & 1U}) {
        click(sides[side].button);
        auto after = frame();
        const auto pixels = repainted(before, after, sides[side].emblem) +
                            repainted(before, after, sides[1U - side].emblem);
        std::cout << "frontend controls check: " << sides[side].button << " side "
                  << preferences_.side << ", " << pixels << " emblem pixels repainted\n";
        expect(
            preferences_.side == side, std::string(sides[side].button) + " did not choose its side"
        );
        expect(
            shows_side(side),
            "NEWGAME.GUI does not show " + std::string(sides[side].button) + " chosen"
        );
        expect(
            repainted(before, after, sides[side].emblem) != 0 &&
                repainted(before, after, sides[1U - side].emblem) != 0,
            "the side emblems were not repainted after " + std::string(sides[side].button)
        );
        snapshot(side == 1U ? "newgame-core" : "newgame-arm");
        before = std::move(after);
    }

    // The Campaign list of the chosen side's campaigns: a click on another
    // campaign's row selects it and shows it selected without leaving the
    // screen, Down and Up move the selection a row, and a double-click on a
    // row opens that campaign's first briefing, as Start does.
    const auto campaign_rows = [&]() -> const oa::ui::gui_input::ScrollList& {
        auto* scrolls = frontend_scrolls();
        const auto index =
            static_cast<std::size_t>(&gadget_named("Campaign") - resources_.layout.gadgets.data());
        const auto* bound =
            scrolls != nullptr ? renderer::find_layout_list(*scrolls, index) : nullptr;
        if (bound == nullptr)
            fail("NEWGAME.GUI's Campaign list is not bound");
        return bound->list;
    };
    // The canvas point in the middle of a shown row of the Campaign list.
    const auto campaign_row_point = [&](std::size_t row) {
        const auto& rows = campaign_rows();
        const auto pitch =
            oa::ui::gui_input::scroll_list_pitch(rows, frontend_scrolls()->line_height);
        const auto box = box_of("Campaign");
        const auto shown = static_cast<int32_t>(row) - rows.first;
        return std::pair{box.x + box.width / 2, box.y + kListRowsTop + shown * pitch + pitch / 2};
    };
    const auto click_campaign_row = [&](std::size_t row, uint8_t clicks) {
        const auto [x, y] = campaign_row_point(row);
        send(SDL_EVENT_MOUSE_MOTION, x, y, 0);
        for (uint8_t click = 1; click <= clicks; ++click) {
            send(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y, kLeft, click);
            send(SDL_EVENT_MOUSE_BUTTON_UP, x, y, kLeft, click);
        }
        idle_tick();
    };
    const auto press_key = [&](SDL_Keycode code, SDL_Scancode scancode) {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.windowID = SDL_GetWindowID(sdl_.window);
        event.key.key = code;
        event.key.scancode = scancode;
        event.key.down = true;
        dispatch_event(event, running);
        idle_tick();
    };
    const auto campaigns = campaign_labels_.size();
    std::cout << "frontend controls check: NEWGAME.GUI lists " << campaigns << " campaigns:";
    for (const auto& label : campaign_labels_)
        std::cout << " '" << label << "'";
    std::cout << '\n';
    require(campaigns > 2, "NEWGAME.GUI does not list the installed campaigns");
    // The first shown row that names another campaign than the selected row.
    const auto page_rows = static_cast<std::size_t>(
        oa::ui::gui_input::scroll_list_page_rows(campaign_rows(), frontend_scrolls()->line_height)
    );
    const auto first_shown = static_cast<std::size_t>(std::max<int16_t>(0, campaign_rows().first));
    const auto shown_end = std::min(campaigns, first_shown + page_rows);
    auto picked = first_shown;
    while (picked < shown_end &&
           tdf_names_equal(campaign_labels_[picked], campaign_labels_[selected_campaign_index_]))
        ++picked;
    require(
        picked < shown_end && picked + 1 < campaigns,
        "the Campaign list shows no other campaign with a row below it"
    );
    const auto shows_campaign = [&](std::size_t row) {
        return selected_campaign_index_ == row &&
               campaign_rows().selection == static_cast<int16_t>(row);
    };
    const auto& picked_label = campaign_labels_[picked];
    before = frame();
    click_campaign_row(picked, 1);
    auto after = frame();
    const auto list_pixels = repainted(before, after, "Campaign");
    std::cout << "frontend controls check: a click on '" << picked_label << "' selects row "
              << selected_campaign_index_ << ", " << list_pixels << " list pixels repainted\n";
    expect(screen_ == Screen::new_campaign, "a click on '" + picked_label + "' left NEWGAME.GUI");
    expect(shows_campaign(picked), "a click on '" + picked_label + "' did not select it");
    expect(list_pixels != 0, "the Campaign list was not repainted after a click on another row");
    snapshot("newgame-campaign-picked");
    press_key(SDLK_DOWN, SDL_SCANCODE_DOWN);
    expect(shows_campaign(picked + 1), "Down did not select the campaign below the selected one");
    press_key(SDLK_UP, SDL_SCANCODE_UP);
    expect(shows_campaign(picked), "Up did not select the campaign above the selected one");
    // A pressed button takes the focus from the list: Down after Difficulty
    // leaves the chosen campaign alone. Three presses bring the difficulty
    // back round.
    for (std::size_t step = 0; step < kDifficultyCaptions.size(); ++step) {
        click("Difficulty");
        press_key(SDLK_DOWN, SDL_SCANCODE_DOWN);
        expect(shows_campaign(picked), "Down after Difficulty moved the Campaign list");
    }
    // CampaignKnob, shown when the campaigns overflow the list, scrolls it
    // with its forward arrow, a knob position at a time, and a click on the
    // row that brings into view selects that campaign.
    if (const auto knob = check_scroll_bar("CampaignKnob").bar; knob.active) {
        const auto first = campaign_rows().first;
        for (int32_t step = 0; step < knob.range && campaign_rows().first == first; ++step)
            click_check_arrow("CampaignKnob", true);
        idle_tick();
        const auto last_shown = std::min(
            campaigns - 1, static_cast<std::size_t>(campaign_rows().first) + page_rows - 1
        );
        std::cout << "frontend controls check: CampaignKnob's forward arrow shows row "
                  << campaign_rows().first << " first, '" << campaign_labels_[last_shown]
                  << "' last\n";
        expect(
            campaign_rows().first == first + 1,
            "CampaignKnob's forward arrow did not scroll the Campaign list"
        );
        click_campaign_row(last_shown, 1);
        expect(
            shows_campaign(last_shown),
            "a click on '" + campaign_labels_[last_shown] + "' scrolled into view did not select it"
        );
        snapshot("newgame-campaign-scrolled");
    }
    const auto& started_label = campaign_labels_[picked + 1];
    click_campaign_row(picked + 1, 2);
    const char* loaded = oa::data::campaign::campaign_name_if_loaded(&campaign_object());
    std::cout << "frontend controls check: a double-click on '" << started_label
              << "' shows screen " << static_cast<int>(screen_) << " for '"
              << (loaded != nullptr ? loaded : "") << "'\n";
    require(
        screen_ == Screen::briefing,
        "a double-click on '" + started_label + "' did not open its briefing"
    );
    expect(
        loaded != nullptr && tdf_names_equal(loaded, started_label),
        "a double-click on '" + started_label + "' did not start that campaign"
    );
    snapshot("newgame-campaign-briefing");
    click("PrevMenu");
    require(
        screen_ == Screen::new_campaign, "the briefing's PrevMenu did not go back to NEWGAME.GUI"
    );
    click("PrevMenu");
    require(screen_ == Screen::single_player, "Previous Menu did not leave NEWGAME.GUI");

    // The options tabs and the sub-panels' staged buttons; CANCEL puts the
    // options back. The sound and music panels keep their buttons' authored
    // foreground colour and draw them lit through the light table, so they are
    // exercised here too.
    click(entry::resource_name(entry::Button::options));
    require(screen_ == Screen::options, "Options did not open STARTOPT.GUI");
    constexpr std::array<std::string_view, 4> tabs{"SOUND", "SPEEDS", "VISUALS", "MUSIC"};
    const auto shows_tab = [&](std::string_view tab, std::string_view when) {
        for (const auto other : tabs)
            expect(
                pressed(other) == (other == tab),
                std::string(other) + (pressed(other) ? " shows pressed" : " shows raised") + ' ' +
                    std::string(when)
            );
    };
    shows_tab("", "before a tab is chosen");
    // The pixels a button's pressed status changes: the frame as shown
    // against the same frame drawn with the button raised.
    const auto pressed_pixels = [&](std::string_view name) {
        auto* gadget = widget(name);
        auto* fields = gadget != nullptr
                           ? std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget->fields)
                           : nullptr;
        if (fields == nullptr)
            fail(std::string(name) + " is not a button");
        const auto shown = frame();
        const auto status = fields->status;
        fields->status = 0;
        const auto raised = frame();
        fields->status = status;
        rebuild_surface();
        return repainted(raised, shown, name);
    };
    const std::array<std::pair<std::string_view, std::vector<std::string_view>>, 4> panels{{
        {"SOUND", {"SPEECH", "MODE"}},
        {"SPEEDS", {"LEFTCLICK", "UNITCHAT"}},
        {"VISUALS", {"SHADING", "ANTI", "BSHADOWS"}},
        {"MUSIC", {"NOTRAK", "TRACKTYPE", "TRACKMODE"}},
    }};
    // Whether the panel on screen has merged an art sequence by name.
    const auto merged_art = [&](std::string_view name) {
        return std::any_of(
            resources_.sprites.sequences.begin(),
            resources_.sprites.sequences.end(),
            [name](const oa::formats::gaf::Sequence& sequence) { return sequence.name == name; }
        );
    };
    for (const auto& [tab, buttons] : panels) {
        click(tab);
        // The music panel brings its own GAF, so its transport buttons draw
        // MUSIC.GAF's frames rather than the shared fallback frame.
        if (tab == "MUSIC") {
            expect(
                merged_art("CDPLAY") && merged_art("CDNEXT"),
                "MUSIC did not merge MUSIC.GAF's transport art"
            );
            // A transport button is hit where its frame is drawn: the record
            // takes the frame's size, and the pointer on the frame's right
            // edge, beyond the 16x16 the panel authors, is over the button.
            const auto sequence = std::find_if(
                resources_.sprites.sequences.begin(),
                resources_.sprites.sequences.end(),
                [](const oa::formats::gaf::Sequence& art) { return art.name == "CDPLAY"; }
            );
            const auto play = box_of("CDPLAY");
            int32_t drawn_width = play.width;
            if (sequence != resources_.sprites.sequences.end() && !sequence->frames.empty()) {
                const auto& art = sequence->frames.front();
                drawn_width = art.width;
                expect(
                    play.width == art.width && play.height == art.height,
                    "CDPLAY is hit as " + std::to_string(play.width) + 'x' +
                        std::to_string(play.height) + " but drawn " + std::to_string(art.width) +
                        'x' + std::to_string(art.height)
                );
            }
            send(SDL_EVENT_MOUSE_MOTION, play.x + drawn_width - 2, play.y + play.height / 2, 0);
            expect(
                hovered_ && resources_.layout.gadgets[*hovered_].common.name == "CDPLAY",
                "the right edge of CDPLAY's frame does not hit CDPLAY"
            );
            std::cout << "frontend controls check: CDPLAY is hit at " << play.x << ',' << play.y
                      << ' ' << play.width << 'x' << play.height << '\n';
        }
        const auto pixels = pressed_pixels(tab);
        std::cout << "frontend controls check: tab " << tab << " pressed "
                  << (pressed(tab) ? "yes" : "no") << ", " << pixels << " pixels drawn pressed\n";
        shows_tab(tab, "after " + std::string(tab) + " was clicked");
        expect(pixels != 0, std::string(tab) + " is not drawn pressed after it was clicked");
        park_pointer();
        snapshot("options-" + std::string(tab));
        for (const auto button : buttons)
            cycle_captions(button);
        shows_tab(tab, "after the " + std::string(tab) + " panel's buttons were clicked");
    }
    click("CANCEL");
    require(screen_ == Screen::single_player, "CANCEL did not leave the options");

    // Alt+Enter switches the window to full screen and back, on a menu and
    // in a match: Return and keypad Enter alike, a held key's repeats
    // switching nothing and reaching no screen, and the screen following the
    // window's new size.
    const auto key = [&](SDL_EventType type, SDL_Keycode code, SDL_Keymod mod, bool repeat) {
        SDL_Event event{};
        event.type = type;
        event.key.windowID = SDL_GetWindowID(sdl_.window);
        event.key.key = code;
        event.key.scancode =
            code == SDLK_KP_ENTER ? SDL_SCANCODE_KP_ENTER : SDL_GetScancodeFromKey(code, nullptr);
        event.key.mod = mod;
        event.key.repeat = repeat;
        event.key.down = type == SDL_EVENT_KEY_DOWN;
        dispatch_event(event, running);
    };
    // The window's own events (its new size, full screen entered or left)
    // reach the game as its loop pumps them.
    const auto settle = [&] {
        (void)SDL_SyncWindow(sdl_.window);
        SDL_Event event{};
        while (SDL_PollEvent(&event))
            dispatch_event(event, running);
    };
    // Alt+Enter pressed, held for some repeats and released.
    const auto alt_enter = [&](SDL_Keycode code, int repeats) {
        key(SDL_EVENT_KEY_DOWN, code, SDL_KMOD_LALT, false);
        for (int repeat = 0; repeat < repeats; ++repeat)
            key(SDL_EVENT_KEY_DOWN, code, SDL_KMOD_LALT, true);
        key(SDL_EVENT_KEY_UP, code, SDL_KMOD_LALT, false);
        settle();
    };
    const auto full_screen = [&] {
        return (SDL_GetWindowFlags(sdl_.window) & SDL_WINDOW_FULLSCREEN) != 0;
    };
    const auto window_size = [&] {
        int width = 0;
        int height = 0;
        (void)SDL_GetWindowSizeInPixels(sdl_.window, &width, &height);
        return std::to_string(width) + 'x' + std::to_string(height);
    };
    // The screen is laid out at the window's size: a match's frame takes
    // the window's pixels, and a menu is drawn to the whole window.
    const auto follows_window = [&] {
        int width = 0;
        int height = 0;
        int output_width = 0;
        int output_height = 0;
        (void)SDL_GetWindowSizeInPixels(sdl_.window, &width, &height);
        (void)SDL_GetRenderOutputSize(sdl_.renderer, &output_width, &output_height);
        if (output_width != width || output_height != height)
            return false;
        return screen_ != Screen::match ||
               (match_layout_.width == width && match_layout_.height == height &&
                output_texture_w_ == width && output_texture_h_ == height);
    };
    // The window's picture as it is presented, at the window's size.
    const auto snapshot_window = [&](std::string_view step) {
        if (options_.snapshot.empty())
            return;
        renderer::Surface presented;
        capture_frame_ = &presented;
        render();
        capture_frame_ = nullptr;
        write_ppm(step_snapshot(options_.snapshot, step), presented);
    };
    const auto switches_twice = [&](std::string_view where, Screen screen) {
        const bool started_full_screen = full_screen();
        const auto started_size = window_size();
        alt_enter(SDLK_RETURN, 3);
        const auto switched_size = window_size();
        expect(
            full_screen() != started_full_screen,
            "Alt+Enter did not switch the window's mode " + std::string(where)
        );
        expect(
            follows_window(),
            "the screen does not follow the window's size " + switched_size + ' ' +
                std::string(where)
        );
        const std::string step = where == "on a menu" ? "alt-enter-menu" : "alt-enter-match";
        snapshot_window(step + "-switched");
        alt_enter(SDLK_KP_ENTER, 3);
        expect(
            full_screen() == started_full_screen,
            "Alt+keypad Enter did not switch the window back " + std::string(where)
        );
        expect(
            window_size() == started_size,
            "the window came back at " + window_size() + ", not " + started_size + ' ' +
                std::string(where)
        );
        expect(
            follows_window(),
            "the screen does not follow the window's size " + window_size() + " back " +
                std::string(where)
        );
        expect(
            screen_ == screen, "Alt+Enter left the screen it was pressed on " + std::string(where)
        );
        snapshot_window(step + "-back");
        std::cout << "frontend controls check: Alt+Enter " << where << " switched to "
                  << (started_full_screen ? "a window" : "full screen") << " (" << started_size
                  << " to " << switched_size << ") and back\n";
    };
    switches_twice("on a menu", Screen::single_player);
    // Over a message box, a held Alt+Enter switches the window and leaves the
    // box open: its repeats never press OK, the box's Enter default.
    show_frontend_message("Alt+Enter", kMessageBoxWidth, 1, 0);
    require(dialogs::dialog_count() == 1, "the message box did not open");
    const bool before_box_switch = full_screen();
    alt_enter(SDLK_RETURN, 3);
    expect(full_screen() != before_box_switch, "Alt+Enter did not switch over a message box");
    expect(dialogs::dialog_count() == 1, "a held Alt+Enter pressed the message box's OK");
    snapshot_window("alt-enter-box");
    // Alt let go of before Enter: Enter's repeats and its release still
    // belong to the switch, and press no OK either.
    key(SDL_EVENT_KEY_DOWN, SDLK_RETURN, SDL_KMOD_LALT, false);
    for (int repeat = 0; repeat < 3; ++repeat)
        key(SDL_EVENT_KEY_DOWN, SDLK_RETURN, SDL_KMOD_NONE, true);
    key(SDL_EVENT_KEY_UP, SDLK_RETURN, SDL_KMOD_NONE, false);
    settle();
    expect(
        full_screen() == before_box_switch,
        "a second Alt+Enter over a message box did not switch back"
    );
    expect(
        dialogs::dialog_count() == 1,
        "Enter held on after Alt was let go pressed the message box's OK"
    );
    key(SDL_EVENT_KEY_DOWN, SDLK_RETURN, SDL_KMOD_NONE, false);
    key(SDL_EVENT_KEY_UP, SDLK_RETURN, SDL_KMOD_NONE, false);
    expect(dialogs::dialog_count() == 0, "Enter did not close the message box");
    require(screen_ == Screen::single_player, "the message box left SINGLE.GUI");

    click(entry::resource_name(entry::Button::skirmish));
    require(screen_ == Screen::skirmish, "Skirmish did not open SKIRMISH.GUI");
    exercise_click(skirmish::resource_name(skirmish::Button::start));
    require(screen_ == Screen::match && match_, "Start did not enter a match");
    apply_output_mode();
    switches_twice("in a match", Screen::match);
    expect(!chat_composing_, "a held Alt+Enter opened the chat line in a match");
    // With the chat line open, Alt+Enter switches the window and leaves the
    // line as it was.
    key(SDL_EVENT_KEY_DOWN, SDLK_RETURN, SDL_KMOD_NONE, false);
    key(SDL_EVENT_KEY_UP, SDLK_RETURN, SDL_KMOD_NONE, false);
    require(chat_composing_, "Enter did not open the chat line");
    const bool before_chat_switch = full_screen();
    alt_enter(SDLK_RETURN, 3);
    expect(full_screen() != before_chat_switch, "Alt+Enter did not switch over the chat line");
    expect(
        chat_composing_ && chat_buffer_.empty(), "Alt+Enter submitted or changed the open chat line"
    );
    alt_enter(SDLK_RETURN, 0);
    expect(
        full_screen() == before_chat_switch,
        "a second Alt+Enter over the chat line did not switch back"
    );
    key(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_KMOD_NONE, false);
    expect(!chat_composing_, "Escape did not close the chat line");

    if (!problems.empty()) {
        std::string report;
        for (const auto& problem : problems)
            report += "\n  " + problem;
        fail(
            std::to_string(problems.size()) +
            " controls do not show their setting, help or choice:" + report
        );
    }
    std::cout << "frontend controls check: every control shows its setting, help and choice\n";
}

} // namespace oa::app
