// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings dialog: where its parts lie, its sections and rows, switches,
// sliders and their stops, the level strip, pointer and key events, OK,
// Cancel and Restore defaults, the locks and their texts, and what it draws.
// A section of the test's own, taller than the view under the heading,
// checks scrolling: the view and its limit, the wheel, the scroll bar, the
// scroll keys, the focus brought into view, rows cut by the view, the
// control numbers, and the two forms of a locked switch. With --data, its
// fonts from the installed game and every text fitting its place.

#include "oa/ui/engine_settings/dialog.hpp"

#include "geometry.hpp"

#include "oa/test/game_assets.hpp"
#include "oa/test/game_data.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (condition)
        return;
    std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
    ++failures;
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

namespace settings = oa::ui::engine_settings;
namespace renderer = oa::ui::frontend_renderer;
namespace geometry = oa::ui::engine_settings::geometry;

using settings::DialogAction;
using settings::DialogKey;
using settings::Lock;
using settings::Page;
using settings::Setting;

constexpr std::array<Page, 5> kPages{
    Page::path_search,
    Page::controls,
    Page::gameplay,
    Page::graphics,
    Page::developer,
};

/// Every lock state the dialog opens with.
std::vector<settings::Locks> lock_states() {
    std::vector<settings::Locks> states;
    for (const bool in_game : {false, true}) {
        for (const bool shared : {false, true}) {
            for (const bool replay : {false, true}) {
                for (const bool command_line : {false, true}) {
                    if (!in_game && (shared || replay))
                        continue;
                    states.push_back(
                        settings::settings_locks(
                            settings::GameState{in_game, shared, replay, command_line}
                        )
                    );
                }
            }
        }
    }
    return states;
}

settings::Dialog opened(Page page, const settings::Locks& locks = {}) {
    settings::Dialog dialog;
    settings::open_dialog(
        dialog, settings::EngineSettings{}, settings::EngineSettings{}, locks, "v0.2.0", page
    );
    return dialog;
}

bool overlap(const renderer::SourceRect& a, const renderer::SourceRect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
}

bool inside(const renderer::SourceRect& inner, const renderer::SourceRect& outer) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

/// The part with a text, or a control's first part.
const settings::LayoutPart*
find_part(const std::vector<settings::LayoutPart>& parts, std::string_view text, int32_t control) {
    for (const auto& part : parts) {
        if (!text.empty() && part.text == text)
            return &part;
        if (text.empty() && part.control == control)
            return &part;
    }
    return nullptr;
}

struct Point {
    int32_t x{};
    int32_t y{};
};

Point centre(const renderer::SourceRect& rect) {
    return {rect.x + rect.width / 2, rect.y + rect.height / 2};
}

DialogAction click(settings::Dialog& dialog, Point point) {
    static_cast<void>(settings::dialog_pointer_move(dialog, point.x, point.y));
    static_cast<void>(settings::dialog_pointer_down(dialog, point.x, point.y));
    return settings::dialog_pointer_up(dialog, point.x, point.y);
}

/// Every setting's whole state at each AA level, so each hint shows.
std::vector<settings::EngineSettings> level_states() {
    std::vector<settings::EngineSettings> states;
    for (const auto level : settings::anti_aliasing_levels) {
        settings::EngineSettings state{};
        state.anti_aliasing = level;
        states.push_back(state);
    }
    return states;
}

void opening_shows_the_settings_in_effect() {
    settings::EngineSettings current{};
    current.frame_stats = true;
    settings::Dialog dialog;
    settings::open_dialog(dialog, current, {}, {}, "v0.0.0", Page::graphics);
    CHECK(dialog.opened == current);
    CHECK(dialog.chosen == current);
    CHECK(dialog.page == Page::graphics);
    CHECK(!dialog.restored);
    CHECK(dialog.focused == settings::no_control);
}

void every_part_lies_inside_the_dialog_and_apart() {
    const renderer::SourceRect face{
        geometry::edge,
        geometry::edge,
        settings::dialog_width - 2 * geometry::edge,
        settings::dialog_height - 2 * geometry::edge,
    };
    const renderer::SourceRect section{
        geometry::content_left,
        geometry::body_top,
        geometry::content_width,
        geometry::footer_rule_row - geometry::body_top,
    };
    for (const Page page : kPages) {
        for (const auto& locks : lock_states()) {
            for (const auto& state : level_states()) {
                settings::Dialog dialog = opened(page, locks);
                dialog.chosen = state;
                const auto parts = settings::dialog_layout(dialog);
                CHECK(!parts.empty());
                for (std::size_t a = 0; a < parts.size(); ++a) {
                    CHECK(parts[a].rect.width > 0 && parts[a].rect.height > 0);
                    CHECK(inside(parts[a].rect, face));
                    for (std::size_t b = a + 1; b < parts.size(); ++b) {
                        if (overlap(parts[a].rect, parts[b].rect)) {
                            std::cerr << "overlap: '" << parts[a].text << "' and '" << parts[b].text
                                      << "'\n";
                            CHECK(!overlap(parts[a].rect, parts[b].rect));
                        }
                    }
                }
                // The open section stays within its columns, above the footer.
                const auto rows = geometry::place_rows(page, locks);
                CHECK(rows.rows.size() == settings::page_settings(page).size());
                CHECK(rows.bottom < geometry::footer_rule_row);
                for (std::size_t index = 0; index < rows.rows.size(); ++index) {
                    const auto& row = rows.rows[index];
                    CHECK(inside(row.label, section));
                    CHECK(inside(row.control_area, section));
                    // A focus outline two pixels out stays clear of the label and hints.
                    const renderer::SourceRect focus{
                        row.control_area.x - 2,
                        row.control_area.y - 2,
                        row.control_area.width + 4,
                        row.control_area.height + 4,
                    };
                    CHECK(!overlap(focus, row.label));
                    for (std::size_t line = 0; line < row.hint_lines; ++line)
                        CHECK(!overlap(focus, row.hints[line]));
                }
            }
        }
    }
    // The section list's entries and the footer's buttons keep apart too.
    for (const Page page : kPages)
        CHECK(!overlap(geometry::list_item(page), geometry::list_divider()));
    CHECK(!overlap(geometry::restore_button, geometry::cancel_button));
    CHECK(geometry::cancel_button.x + geometry::cancel_button.width < geometry::ok_button.x);
}

void each_section_shows_its_rows() {
    CHECK(settings::page_settings(Page::path_search).size() == 1);
    CHECK(settings::page_settings(Page::path_search)[0] == settings::Setting::path_search);
    CHECK(settings::page_settings(Page::controls).size() == 3);
    CHECK(settings::page_settings(Page::controls)[0] == settings::Setting::wheel_zoom);
    CHECK(settings::page_settings(Page::controls)[1] == settings::Setting::escape_opens_menu);
    CHECK(settings::page_settings(Page::controls)[2] == settings::Setting::switch_alt);
    CHECK(settings::page_settings(Page::gameplay)[0] == settings::Setting::unit_limit);
    CHECK(settings::page_settings(Page::graphics)[0] == settings::Setting::max_frame_rate);
    CHECK(settings::page_settings(Page::graphics)[1] == settings::Setting::anti_aliasing);
    CHECK(settings::page_settings(Page::graphics).size() == 3);
    CHECK(settings::page_settings(Page::graphics)[2] == settings::Setting::screen_size);
    CHECK(settings::page_settings(Page::developer)[0] == settings::Setting::frame_stats);
    // Graphics' three rows lie above the footer.
    const auto graphics = geometry::place_rows(Page::graphics, {});
    CHECK(graphics.rows.size() == 3);
    CHECK(graphics.bottom <= geometry::footer_rule_row);

    const auto parts = settings::dialog_layout(opened(Page::controls));
    for (const std::string_view text :
         {"OPEN ANNIHILATION",
          "SETTINGS",
          "v0.2.0",
          "AI & Pathfinding",
          "Controls & Input",
          "Gameplay",
          "Graphics",
          "Developer",
          "CONTROLS & INPUT",
          "Mouse wheel zoom",
          "Scroll to zoom the battlefield in and out.",
          "Escape opens the game menu",
          "The first press clears the selection,",
          "the second opens the menu.",
          "Select groups without Alt",
          "A number key selects its group on its own.",
          "OFF",
          "ON",
          "RESTORE DEFAULTS",
          "CANCEL",
          "OK"})
        CHECK(find_part(parts, text, settings::no_control) != nullptr);
    CHECK(find_part(parts, "Shared game - still running", settings::no_control) == nullptr);
}

void a_click_on_an_entry_shows_its_section() {
    settings::Dialog dialog = opened(Page::path_search);
    const auto parts = settings::dialog_layout(dialog);
    const auto* entry = find_part(parts, "Graphics", settings::no_control);
    CHECK(entry != nullptr && entry->control == settings::page_control(Page::graphics));
    CHECK(click(dialog, centre(geometry::list_item(Page::graphics))) == DialogAction::redraw);
    CHECK(dialog.page == Page::graphics);
    // A release away from the press does nothing.
    const Point developer = centre(geometry::list_item(Page::developer));
    static_cast<void>(settings::dialog_pointer_down(dialog, developer.x, developer.y));
    static_cast<void>(settings::dialog_pointer_up(dialog, 300, 200));
    CHECK(dialog.page == Page::graphics);
}

void every_control_is_pressed_where_it_is_drawn() {
    for (const Page page : kPages) {
        settings::Dialog dialog = opened(page);
        for (const auto& part : settings::dialog_layout(dialog)) {
            if (part.control == settings::no_control)
                continue;
            const Point point = centre(part.rect);
            static_cast<void>(settings::dialog_pointer_move(dialog, point.x, point.y));
            CHECK(dialog.hovered == part.control);
        }
        static_cast<void>(settings::dialog_pointer_move(dialog, 300, 40));
        CHECK(dialog.hovered == settings::no_control);
    }
}

void switches_take_a_click_on_either_half_and_keys() {
    settings::Dialog dialog = opened(Page::controls);
    const auto rows = geometry::place_rows(Page::controls, {});
    const auto& zoom = rows.rows[0].control_area;
    const Point off{zoom.x + 4, zoom.y + zoom.height / 2};
    const Point on{zoom.x + zoom.width - 4, zoom.y + zoom.height / 2};
    CHECK(dialog.chosen.wheel_zoom);
    CHECK(click(dialog, off) == DialogAction::changed);
    CHECK(!dialog.chosen.wheel_zoom);
    CHECK(click(dialog, off) == DialogAction::redraw);
    CHECK(!dialog.chosen.wheel_zoom);
    CHECK(click(dialog, on) == DialogAction::changed);
    CHECK(dialog.chosen.wheel_zoom);

    // Keys: the first moves the focus to the first row; Space flips it,
    // Left sets Off and Right On.
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_row_control);
    CHECK(dialog.chosen.wheel_zoom);
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::changed);
    CHECK(!dialog.chosen.wheel_zoom);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::changed);
    CHECK(dialog.chosen.wheel_zoom);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_row_control + 1);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::changed);
    CHECK(dialog.chosen.escape_opens_menu);
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::changed);
    CHECK(dialog.chosen.switch_alt);
    CHECK(dialog.chosen != dialog.opened);

    settings::Dialog developer = opened(Page::developer);
    const auto stats = geometry::place_rows(Page::developer, {}).rows[0].control_area;
    CHECK(click(developer, {stats.x + stats.width - 4, stats.y + 4}) == DialogAction::changed);
    CHECK(developer.chosen.frame_stats);
}

void every_stop_maps_to_its_value_and_back() {
    struct Expected {
        settings::Setting setting;
        int32_t stops;
    };

    for (const Expected expected :
         {Expected{settings::Setting::path_search, 8},
          Expected{settings::Setting::unit_limit, 30},
          Expected{settings::Setting::max_frame_rate, 19},
          Expected{settings::Setting::screen_size, 5}}) {
        CHECK(geometry::slider_of(expected.setting).stops == expected.stops);
        const renderer::SourceRect track{100, 50, 189, geometry::slider_line_height};
        for (int32_t stop = 0; stop < expected.stops; ++stop) {
            settings::EngineSettings state{};
            geometry::set_stop(state, expected.setting, stop);
            CHECK(geometry::stop_of(state, expected.setting) == stop);
            const int32_t column = geometry::knob_column(track, stop, expected.stops);
            CHECK(geometry::stop_at(track, column, expected.stops) == stop);
        }
    }
    settings::EngineSettings state{};
    geometry::set_stop(state, settings::Setting::path_search, 0);
    CHECK(state.path_search_nodes == settings::base_path_search_nodes);
    geometry::set_stop(state, settings::Setting::path_search, 7);
    CHECK(state.path_search_nodes == 8 * settings::base_path_search_nodes);
    geometry::set_stop(state, settings::Setting::path_search, 99);
    CHECK(state.path_search_nodes == 8 * settings::base_path_search_nodes);
    geometry::set_stop(state, settings::Setting::unit_limit, 0);
    CHECK(state.unit_limit == 50);
    geometry::set_stop(state, settings::Setting::unit_limit, 4);
    CHECK(state.unit_limit == 250);
    geometry::set_stop(state, settings::Setting::unit_limit, 29);
    CHECK(state.unit_limit == 1500);
    // The frame rate from 30, a frame for each tick, to 120 in steps of 5.
    geometry::set_stop(state, settings::Setting::max_frame_rate, 0);
    CHECK(state.max_frame_rate == 30);
    CHECK(geometry::value_text(settings::Setting::max_frame_rate, state) == "30 fps");
    geometry::set_stop(state, settings::Setting::max_frame_rate, 2);
    CHECK(state.max_frame_rate == 40);
    geometry::set_stop(state, settings::Setting::max_frame_rate, 18);
    CHECK(state.max_frame_rate == 120);
    // A limit off the slider's steps, such as an installation's 21, shows at
    // the nearest stop and keeps its value until moved.
    state.unit_limit = 21;
    CHECK(geometry::stop_of(state, settings::Setting::unit_limit) == 0);
    CHECK(geometry::value_text(settings::Setting::unit_limit, state) == "21 per player");
    state.unit_limit = 275;
    CHECK(geometry::stop_of(state, settings::Setting::unit_limit) == 5);
    // The screen sizes from the desktop's up, each shown as it is.
    geometry::set_stop(state, settings::Setting::screen_size, 0);
    CHECK(state.screen_size == settings::desktop_screen_size);
    CHECK(geometry::value_text(settings::Setting::screen_size, state) == "Desktop");
    geometry::set_stop(state, settings::Setting::screen_size, 2);
    CHECK((state.screen_size == settings::ScreenSize{800, 600}));
    CHECK(geometry::value_text(settings::Setting::screen_size, state) == "800 x 600");
    geometry::set_stop(state, settings::Setting::screen_size, 99);
    CHECK((state.screen_size == settings::ScreenSize{1280, 1024}));
    CHECK(geometry::value_text(settings::Setting::screen_size, state) == "1280 x 1024");
}

void sliders_follow_the_pointer_and_the_arrows() {
    settings::Dialog dialog = opened(Page::path_search);
    const auto track = geometry::place_rows(Page::path_search, {}).rows[0].control_area;
    const int32_t row = track.y + track.height / 2;
    CHECK(
        settings::dialog_pointer_down(dialog, track.x + track.width - 1, row) ==
        DialogAction::changed
    );
    CHECK(dialog.dragging);
    CHECK(dialog.chosen.path_search_nodes == 8 * settings::base_path_search_nodes);
    CHECK(settings::dialog_pointer_move(dialog, track.x - 40, row + 30) == DialogAction::changed);
    CHECK(dialog.chosen.path_search_nodes == settings::base_path_search_nodes);
    CHECK(
        settings::dialog_pointer_move(dialog, geometry::knob_column(track, 2, 8), row) ==
        DialogAction::changed
    );
    CHECK(dialog.chosen.path_search_nodes == 3 * settings::base_path_search_nodes);
    CHECK(settings::dialog_pointer_up(dialog, 0, 0) == DialogAction::redraw);
    CHECK(!dialog.dragging);
    CHECK(settings::dialog_pointer_move(dialog, track.x - 40, row) != DialogAction::changed);
    CHECK(dialog.chosen.path_search_nodes == 3 * settings::base_path_search_nodes);

    const auto parts = settings::dialog_layout(dialog);
    CHECK(find_part(parts, "3x", settings::no_control) != nullptr);

    // Arrows move one stop and stop at the ends.
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_row_control);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::changed);
    CHECK(dialog.chosen.path_search_nodes == 2 * settings::base_path_search_nodes);
    for (int32_t press = 0; press < 10; ++press)
        static_cast<void>(settings::dialog_key(dialog, DialogKey::right));
    CHECK(dialog.chosen.path_search_nodes == 8 * settings::base_path_search_nodes);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::redraw);

    settings::Dialog graphics = opened(Page::graphics);
    CHECK(settings::dialog_key(graphics, DialogKey::down) == DialogAction::redraw);
    CHECK(settings::dialog_key(graphics, DialogKey::left) == DialogAction::changed);
    CHECK(graphics.chosen.max_frame_rate == 115);
    CHECK(find_part(settings::dialog_layout(graphics), "115 fps", settings::no_control) != nullptr);

    settings::Dialog gameplay = opened(Page::gameplay);
    CHECK(settings::dialog_key(gameplay, DialogKey::down) == DialogAction::redraw);
    CHECK(settings::dialog_key(gameplay, DialogKey::right) == DialogAction::changed);
    CHECK(gameplay.chosen.unit_limit == 300);
    CHECK(
        find_part(settings::dialog_layout(gameplay), "300 per player", settings::no_control) !=
        nullptr
    );
}

void the_level_strip_picks_a_level() {
    settings::Dialog dialog = opened(Page::graphics);
    std::vector<const settings::LayoutPart*> levels;
    const auto parts = settings::dialog_layout(dialog);
    for (const auto& part : parts) {
        if (part.control == settings::first_row_control + 1)
            levels.push_back(&part);
    }
    CHECK(levels.size() == settings::anti_aliasing_levels.size());
    if (levels.size() != settings::anti_aliasing_levels.size())
        return;
    CHECK(levels[0]->text == "Off");
    CHECK(levels[1]->text == "2x");
    CHECK(levels[5]->text == "16x");
    for (std::size_t index = settings::anti_aliasing_levels.size(); index-- > 0;) {
        const auto expected = settings::anti_aliasing_levels[index];
        CHECK(click(dialog, centre(levels[index]->rect)) == DialogAction::changed);
        CHECK(dialog.chosen.anti_aliasing == expected);
    }
    CHECK(
        find_part(
            settings::dialog_layout(dialog),
            "Units drawn at higher resolution and scaled down",
            settings::no_control
        ) != nullptr
    );
    dialog.chosen.anti_aliasing = settings::AntiAliasing::x8;
    CHECK(
        find_part(
            settings::dialog_layout(dialog),
            "Units drawn at 8x and scaled down.",
            settings::no_control
        ) != nullptr
    );
    dialog.chosen.anti_aliasing = settings::AntiAliasing::x16;
    const auto demanding = settings::dialog_layout(dialog);
    CHECK(
        find_part(demanding, "Units drawn at 16x and scaled down.", settings::no_control) != nullptr
    );
    CHECK(find_part(demanding, "Needs a fast CPU.", settings::no_control) != nullptr);

    // Keys step along the strip and stop at its ends.
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_row_control + 1);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::changed);
    CHECK(dialog.chosen.anti_aliasing == settings::AntiAliasing::x8);
}

void enter_keeps_and_escape_cancels() {
    settings::Dialog dialog = opened(Page::developer);
    dialog.chosen.frame_stats = true;
    CHECK(settings::dialog_key(dialog, DialogKey::enter) == DialogAction::accepted);
    CHECK(dialog.chosen.frame_stats);
    CHECK(settings::dialog_key(dialog, DialogKey::escape) == DialogAction::cancelled);
    CHECK(dialog.chosen == dialog.opened);
}

void the_footer_buttons_restore_cancel_and_keep() {
    settings::EngineSettings defaults{};
    defaults.escape_opens_menu = true;
    settings::EngineSettings current{};
    current.wheel_zoom = false;
    current.unit_limit = 600;
    current.anti_aliasing = settings::AntiAliasing::x4;
    settings::Dialog dialog;
    settings::open_dialog(dialog, current, defaults, {}, "v0.2.0", Page::controls);
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.restored);
    CHECK(dialog.chosen == defaults);
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::redraw);
    CHECK(click(dialog, centre(geometry::cancel_button)) == DialogAction::cancelled);
    CHECK(dialog.chosen == current);
    settings::open_dialog(dialog, current, defaults, {}, "v0.2.0", Page::controls);
    CHECK(click(dialog, centre(geometry::ok_button)) == DialogAction::accepted);
    CHECK(dialog.chosen == current);

    // Left and Right move along the footer; Space presses.
    settings::open_dialog(dialog, current, defaults, {}, "v0.2.0", Page::path_search);
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(dialog.focused == settings::restore_control);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::none);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::redraw);
    CHECK(dialog.focused == settings::cancel_control);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::redraw);
    CHECK(dialog.focused == settings::ok_control);
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::accepted);
}

void the_focus_moves_round_every_control() {
    settings::Dialog dialog = opened(Page::controls);
    const std::vector<int32_t> expected{
        settings::first_row_control,
        settings::first_row_control + 1,
        settings::first_row_control + 2,
        settings::restore_control,
        settings::cancel_control,
        settings::ok_control,
        settings::page_control(Page::path_search),
        settings::page_control(Page::controls),
        settings::page_control(Page::gameplay),
        settings::page_control(Page::graphics),
        settings::page_control(Page::developer),
    };
    for (const int32_t control : expected) {
        CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
        CHECK(dialog.focused == control);
    }
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(dialog.focused == expected.front());
    CHECK(settings::dialog_key(dialog, DialogKey::up) == DialogAction::redraw);
    CHECK(dialog.focused == expected.back());
    CHECK(settings::dialog_key(dialog, DialogKey::back_tab) == DialogAction::redraw);
    CHECK(dialog.focused == settings::page_control(Page::graphics));
    // Space on an entry shows its section.
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::redraw);
    CHECK(dialog.page == Page::graphics);
    // Up from nothing lands on the last control.
    settings::Dialog fresh = opened(Page::controls);
    CHECK(settings::dialog_key(fresh, DialogKey::up) == DialogAction::redraw);
    CHECK(fresh.focused == settings::page_control(Page::developer));
}

void locks_show_their_text_and_hold_their_settings() {
    const auto in_game = settings::settings_locks(settings::GameState{true, false, false, false});
    settings::Dialog dialog = opened(Page::path_search, in_game);
    auto parts = settings::dialog_layout(dialog);
    CHECK(find_part(parts, "Locked during a game", settings::no_control) != nullptr);
    CHECK(find_part(parts, "Shared game - still running", settings::no_control) == nullptr);
    const auto track = geometry::place_rows(Page::path_search, in_game).rows[0].control_area;
    CHECK(
        settings::dialog_pointer_down(dialog, track.x + track.width - 1, track.y + 4) ==
        DialogAction::none
    );
    CHECK(dialog.chosen.path_search_nodes == settings::base_path_search_nodes);
    // The focus passes over the locked slider.
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(dialog.focused == settings::restore_control);
    // A locked slider takes no part a press can find.
    for (const auto& part : parts)
        CHECK(part.control != settings::first_row_control);

    const auto shared = settings::settings_locks(settings::GameState{true, true, false, false});
    parts = settings::dialog_layout(opened(Page::gameplay, shared));
    CHECK(find_part(parts, "Set by the host", settings::no_control) != nullptr);
    CHECK(find_part(parts, "Shared game - still running", settings::no_control) != nullptr);
    const auto replay = settings::settings_locks(settings::GameState{true, false, true, false});
    parts = settings::dialog_layout(opened(Page::path_search, replay));
    CHECK(find_part(parts, "Set by the host", settings::no_control) != nullptr);
    CHECK(find_part(parts, "Shared game - still running", settings::no_control) == nullptr);

    const auto command_line =
        settings::settings_locks(settings::GameState{false, false, false, true});
    parts = settings::dialog_layout(opened(Page::graphics, command_line));
    CHECK(find_part(parts, "Set on the command line", settings::no_control) != nullptr);
    parts = settings::dialog_layout(opened(Page::graphics));
    CHECK(find_part(parts, "Set on the command line", settings::no_control) == nullptr);
    CHECK(find_part(parts, "Locked during a game", settings::no_control) == nullptr);

    // Restore defaults leaves what is locked as it is.
    settings::EngineSettings current{};
    current.path_search_nodes = 4 * settings::base_path_search_nodes;
    current.unit_limit = 900;
    current.max_frame_rate = 60;
    current.frame_stats = true;
    auto all_locked = in_game;
    all_locked.max_frame_rate = Lock::command_line;
    settings::open_dialog(dialog, current, {}, all_locked, "v0.2.0", Page::developer);
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.chosen.path_search_nodes == current.path_search_nodes);
    CHECK(dialog.chosen.unit_limit == current.unit_limit);
    CHECK(dialog.chosen.max_frame_rate == current.max_frame_rate);
    CHECK(!dialog.chosen.frame_stats);
}

/// A section of the test's own, shown in Graphics' place: its rows, and the
/// locks and status hints it gives them.
struct Section {
    std::vector<Setting> rows;
    std::vector<std::pair<Setting, Lock>> locks;
    std::vector<Setting> status;
};

/// Returns the test section's rows for Graphics, and the dialog's own for
/// the other sections (SectionHooks::settings).
std::span<const Setting> section_settings(void* context, Page page) {
    const auto& section = *static_cast<const Section*>(context);
    if (page != Page::graphics)
        return settings::page_settings(page);
    return section.rows;
}

/// Returns the lock the test section gives a setting, else the dialog's
/// (SectionHooks::lock).
Lock section_lock(void* context, Setting setting, Lock lock) {
    const auto& section = *static_cast<const Section*>(context);
    for (const auto& [locked, kind] : section.locks)
        if (locked == setting)
            return kind;
    return lock;
}

/// Tells whether the test section makes a setting's hint lines its status
/// (SectionHooks::hint_is_status).
bool section_status(void* context, Setting setting) {
    const auto& section = *static_cast<const Section*>(context);
    return std::find(section.status.begin(), section.status.end(), setting) != section.status.end();
}

/// Returns five rows as tall as the Graphics page the scrolling is for: a
/// slider with one hint line (65 rows), the level strip (59), a slider with
/// two (77), a switch with two (59) and a switch with one (47).
Section five_rows() {
    return {
        {Setting::max_frame_rate,
         Setting::anti_aliasing,
         Setting::screen_size,
         Setting::escape_opens_menu,
         Setting::frame_stats},
        {},
        {},
    };
}

/// Returns nine rows in one section: 543 rows from the first row's line to
/// the line under the last.
Section nine_rows() {
    return {
        {Setting::path_search,
         Setting::wheel_zoom,
         Setting::escape_opens_menu,
         Setting::switch_alt,
         Setting::unit_limit,
         Setting::max_frame_rate,
         Setting::anti_aliasing,
         Setting::screen_size,
         Setting::frame_stats},
        {},
        {},
    };
}

/// A dialog open on Graphics with a section of the test's own in its place.
struct Scrolling {
    Section section;
    settings::SectionHooks hooks{};
    settings::Dialog dialog;

    /// Opens the dialog on Graphics with a section of the test's own in its place.
    explicit Scrolling(
        Section shown,
        const settings::EngineSettings& current = {},
        const settings::Locks& locks = {}
    )
        : section(std::move(shown)) {
        hooks.context = &section;
        hooks.settings = section_settings;
        hooks.lock = section_lock;
        hooks.hint_is_status = section_status;
        open(current, locks);
    }

    Scrolling(const Scrolling&) = delete;
    Scrolling& operator=(const Scrolling&) = delete;

    /// Opens the dialog again on Graphics, with the test's section in its place.
    void open(const settings::EngineSettings& current = {}, const settings::Locks& locks = {}) {
        settings::open_dialog(dialog, current, {}, locks, "v0.2.0", Page::graphics);
        dialog.section_hooks = &hooks;
    }

    /// Returns Graphics' stored offset.
    int32_t scroll() const { return dialog.scroll[static_cast<std::size_t>(Page::graphics)]; }

    /// Returns the open section's rows, placed at its offset.
    geometry::ScrolledRows rows() const { return geometry::open_rows(dialog); }
};

/// Tells whether two rectangles are the same.
bool same_rect(const renderer::SourceRect& a, const renderer::SourceRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

/// Turns the wheel at a point of the section.
DialogAction wheel(settings::Dialog& dialog, float notches, Point at = {300, 150}) {
    return settings::dialog_wheel(dialog, at.x, at.y, notches);
}

void the_view_and_the_scroll_bar_keep_their_places() {
    CHECK(same_rect(geometry::view, {158, 54, 309, 236}));
    CHECK(same_rect(geometry::view_clip, {156, 54, 313, 236}));
    CHECK(same_rect(geometry::scroll_well, {470, 54, 7, 236}));
    CHECK(same_rect(geometry::scroll_hit, {467, 54, 12, 236}));
    CHECK(geometry::wheel_step == 24);
    CHECK(geometry::page_step == 200);
    CHECK(geometry::least_thumb_height == 16);
    CHECK(geometry::end_gap == 8);
    // The view runs from the first row's line to the row above the footer's.
    CHECK(geometry::view.y == geometry::first_row_top);
    CHECK(geometry::view.y + geometry::view.height == geometry::footer_rule_row);
    // Columns 477 and 478 stay clear before the dark edge; the hit area ends there.
    CHECK(geometry::scroll_well.x + geometry::scroll_well.width == 477);
    CHECK(geometry::scroll_hit.x + geometry::scroll_hit.width == settings::dialog_width - 1);
}

void sections_that_fit_do_not_scroll() {
    // Each section's content: its rows, and the end gap under the last.
    const std::array<int32_t, 5> content{74, 162, 86, 210, 56};
    for (std::size_t index = 0; index < kPages.size(); ++index) {
        const Page page = kPages[index];
        for (const auto& locks : lock_states()) {
            settings::Dialog dialog = opened(page, locks);
            const auto at_top = geometry::place_rows(page, locks);
            CHECK(geometry::content_height(at_top, 0) == content[index]);
            CHECK(geometry::scroll_limit(content[index]) == 0);
            // A stored offset is clamped to the limit: the rows stay put.
            dialog.scroll[index] = 100;
            const auto open = geometry::open_rows(dialog);
            CHECK(open.scroll == 0 && open.limit == 0);
            CHECK(open.rows.rows.size() == at_top.rows.size());
            CHECK(open.rows.bottom == at_top.bottom);
            for (std::size_t row = 0; row < at_top.rows.size(); ++row) {
                CHECK(open.rows.rows[row].top == at_top.rows[row].top);
                CHECK(same_rect(open.rows.rows[row].control_area, at_top.rows[row].control_area));
                CHECK(same_rect(open.rows.rows[row].label, at_top.rows[row].label));
            }
            for (const auto& part : settings::dialog_layout(dialog))
                CHECK(part.control != settings::scroll_bar_control);
            // Neither the wheel, the keys nor a press in the margin scroll it.
            CHECK(wheel(dialog, -1.0F) == DialogAction::none);
            for (const DialogKey key :
                 {DialogKey::page_down, DialogKey::end, DialogKey::page_up, DialogKey::home})
                CHECK(settings::dialog_key(dialog, key) == DialogAction::none);
            CHECK(dialog.focused == settings::no_control);
            CHECK(settings::dialog_pointer_down(dialog, 473, 150) == DialogAction::none);
            CHECK(settings::dialog_pointer_up(dialog, 473, 150) == DialogAction::none);
            CHECK(dialog.hovered == settings::no_control);
            CHECK(geometry::open_rows(dialog).scroll == 0);
        }
    }
}

void a_long_section_scrolls_by_its_overflow() {
    Scrolling five(five_rows());
    const auto open = five.rows();
    CHECK(open.rows.rows.size() == 5);
    const std::array<int32_t, 5> tops{54, 119, 178, 255, 314};
    const std::array<int32_t, 5> heights{65, 59, 77, 59, 47};
    for (std::size_t index = 0; index < open.rows.rows.size() && index < tops.size(); ++index) {
        const auto& row = open.rows.rows[index];
        CHECK(row.top == tops[index]);
        CHECK(row.height == heights[index]);
        CHECK(row.control == settings::first_row_control + static_cast<int32_t>(index));
        // No row is taller than the view, so the focus can always show one whole.
        CHECK(row.height < geometry::view.height);
    }
    CHECK(open.rows.bottom == 361);
    CHECK(open.content_height == 316);
    CHECK(open.limit == 80);
    CHECK(open.scroll == 0);

    // At its end every row is 80 rows higher, the closing line at 281.
    five.dialog.scroll[static_cast<std::size_t>(Page::graphics)] = 500;
    const auto end = five.rows();
    CHECK(end.scroll == 80);
    CHECK(end.rows.bottom == 281);
    CHECK(end.rows.rows[0].top == -26);
    CHECK(end.rows.rows[1].control_area.y == 48);
    CHECK(end.rows.rows[4].top == 234);
    // Every part a row has moves with it; a part it lacks stays empty.
    const auto lifted = [](const renderer::SourceRect& at_end, const renderer::SourceRect& at_top) {
        if (at_top.width == 0 || at_top.height == 0)
            return same_rect(at_end, at_top);
        return same_rect(at_end, {at_top.x, at_top.y - 80, at_top.width, at_top.height});
    };
    for (std::size_t index = 0; index < open.rows.rows.size(); ++index) {
        const auto& top_row = open.rows.rows[index];
        const auto& end_row = end.rows.rows[index];
        CHECK(lifted(end_row.label, top_row.label));
        CHECK(lifted(end_row.lock_area, top_row.lock_area));
        for (std::size_t line = 0; line < top_row.hints.size(); ++line)
            CHECK(lifted(end_row.hints[line], top_row.hints[line]));
        CHECK(lifted(end_row.control_area, top_row.control_area));
        CHECK(lifted(end_row.value, top_row.value));
    }

    // The thumb: 174 rows of the well's 234, its top 55 at the top, 85
    // half-way and 115 at the end; a dragged top gives the offset back.
    CHECK(same_rect(geometry::scroll_thumb(0, 80, 316), {471, 55, 5, 174}));
    CHECK(geometry::scroll_thumb(40, 80, 316).y == 85);
    CHECK(geometry::scroll_thumb(80, 80, 316).y == 115);
    CHECK(geometry::scroll_at(55, 80, 316) == 0);
    CHECK(geometry::scroll_at(85, 80, 316) == 40);
    CHECK(geometry::scroll_at(115, 80, 316) == 80);
    CHECK(geometry::scroll_at(10, 80, 316) == 0);
    CHECK(geometry::scroll_at(400, 80, 316) == 80);
    // To the nearest row: two rows down the thumb's 60 is 2.67 of the 80.
    CHECK(geometry::scroll_at(57, 80, 316) == 3);
    CHECK(geometry::scroll_at(56, 80, 316) == 1);
    for (int32_t scroll = 0; scroll <= 80; ++scroll) {
        const auto thumb = geometry::scroll_thumb(scroll, 80, 316);
        CHECK(thumb.y >= 55 && thumb.y + thumb.height <= 289);
        // The offset a thumb's top gives puts the thumb back there.
        CHECK(geometry::scroll_thumb(geometry::scroll_at(thumb.y, 80, 316), 80, 316).y == thumb.y);
    }

    // Nine rows: 543 rows, a limit of 316 and a thumb of 100.
    Scrolling all(nine_rows());
    CHECK(all.rows().content_height == 552);
    CHECK(all.rows().limit == 316);
    CHECK(geometry::scroll_thumb(0, 316, 552).height == 100);
    // A section far taller than the view keeps the thumb's least height.
    CHECK(geometry::scroll_thumb(0, 10000, 10236).height == geometry::least_thumb_height);
}

void control_numbers_put_the_rows_after_every_fixed_control() {
    for (std::size_t index = 0; index < kPages.size(); ++index)
        CHECK(settings::page_control(kPages[index]) == static_cast<int32_t>(index));
    CHECK(settings::restore_control == 5);
    CHECK(settings::cancel_control == 6);
    CHECK(settings::ok_control == 7);
    CHECK(settings::scroll_bar_control == 8);
    CHECK(settings::first_row_control == 9);

    // Every row's number comes after every fixed control's, and each is its own.
    Scrolling all(nine_rows());
    std::set<int32_t> rows;
    for (const auto& row : all.rows().rows.rows) {
        CHECK(row.control > settings::scroll_bar_control);
        CHECK(rows.insert(row.control).second);
    }
    CHECK(rows.size() == 9);
    CHECK(*rows.rbegin() == settings::first_row_control + 8);
    // The focus moves through the rows, the footer's buttons and the
    // sections' entries, never the scroll bar.
    std::vector<int32_t> expected;
    for (int32_t row = 0; row < 9; ++row)
        expected.push_back(settings::first_row_control + row);
    for (const int32_t control :
         {settings::restore_control, settings::cancel_control, settings::ok_control})
        expected.push_back(control);
    for (const Page page : kPages)
        expected.push_back(settings::page_control(page));
    for (const int32_t control : expected) {
        CHECK(settings::dialog_key(all.dialog, DialogKey::tab) == DialogAction::redraw);
        CHECK(all.dialog.focused == control);
    }
    CHECK(settings::dialog_key(all.dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(all.dialog.focused == settings::first_row_control);
    // Space on the last row of nine flips its switch: it is a row, not a button.
    settings::Dialog& dialog = all.dialog;
    for (int32_t press = 0; press < 8; ++press)
        static_cast<void>(settings::dialog_key(dialog, DialogKey::down));
    CHECK(dialog.focused == settings::first_row_control + 8);
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::changed);
    CHECK(dialog.chosen.frame_stats);
}

/// Checks the layout at one offset: every part inside the dialog and apart,
/// every part over the view wholly in it, the scroll bar once and right of
/// every focus outline, and every listed control pressed where it is drawn.
void check_layout_at(Scrolling& scrolling, int32_t scroll) {
    const renderer::SourceRect face{
        geometry::edge,
        geometry::edge,
        settings::dialog_width - 2 * geometry::edge,
        settings::dialog_height - 2 * geometry::edge,
    };
    scrolling.dialog.scroll[static_cast<std::size_t>(Page::graphics)] = scroll;
    const auto parts = settings::dialog_layout(scrolling.dialog);
    int32_t bars = 0;
    for (std::size_t a = 0; a < parts.size(); ++a) {
        const auto& part = parts[a];
        CHECK(part.rect.width > 0 && part.rect.height > 0);
        CHECK(inside(part.rect, face));
        if (overlap(part.rect, geometry::view) && !inside(part.rect, geometry::view)) {
            std::cerr << "at " << scroll << ": '" << part.text << "' is cut by the view\n";
            CHECK(inside(part.rect, geometry::view));
        }
        for (std::size_t b = a + 1; b < parts.size(); ++b)
            CHECK(!overlap(part.rect, parts[b].rect));
        if (part.control == settings::scroll_bar_control) {
            ++bars;
            CHECK(same_rect(part.rect, geometry::scroll_well));
        }
    }
    CHECK(bars == 1);
    for (const auto& row : scrolling.rows().rows.rows)
        CHECK(
            row.control_area.x + row.control_area.width + geometry::focus_inset <
            geometry::scroll_well.x
        );
    for (const auto& part : parts) {
        if (part.control == settings::no_control)
            continue;
        const Point point = centre(part.rect);
        static_cast<void>(settings::dialog_pointer_move(scrolling.dialog, point.x, point.y));
        CHECK(scrolling.dialog.hovered == part.control);
    }
    CHECK(scrolling.scroll() == scroll);
}

void the_layout_lists_the_parts_wholly_in_the_view() {
    for (Section section : {five_rows(), nine_rows()}) {
        Scrolling scrolling(std::move(section));
        const int32_t limit = scrolling.rows().limit;
        std::set<std::string> seen;
        for (int32_t scroll = 0; scroll <= limit; ++scroll) {
            check_layout_at(scrolling, scroll);
            for (const auto& part : settings::dialog_layout(scrolling.dialog))
                seen.insert(part.text);
        }
        // Every row's label and hint lines are listed whole at some offset.
        for (const Setting setting : scrolling.section.rows) {
            CHECK(seen.contains(std::string(geometry::label_of(setting))));
            for (std::size_t line = 0; line < geometry::hint_line_count(setting); ++line)
                CHECK(seen.contains(
                    std::string(geometry::hint_line(setting, scrolling.dialog.chosen, line))
                ));
        }
    }
}

void the_wheel_scrolls_by_notches_and_carries_fractions() {
    Scrolling five(five_rows());
    // Towards the player scrolls down, 24 rows a notch, to the end in four.
    for (const int32_t expected : {24, 48, 72, 80}) {
        CHECK(wheel(five.dialog, -1.0F) == DialogAction::redraw);
        CHECK(five.scroll() == expected);
    }
    CHECK(wheel(five.dialog, -1.0F) == DialogAction::none);
    CHECK(five.scroll() == 80);
    CHECK(five.dialog.wheel_rows == 0.0F);
    // Away from the player scrolls up, and stops at the top.
    for (const int32_t expected : {56, 32, 8, 0}) {
        CHECK(wheel(five.dialog, 1.0F) == DialogAction::redraw);
        CHECK(five.scroll() == expected);
    }
    CHECK(wheel(five.dialog, 2.0F) == DialogAction::none);
    CHECK(five.scroll() == 0);
    // Fractions of a notch carry over until they make whole rows: 0.75 rows
    // a turn.
    const float fine = -1.0F / 32.0F;
    CHECK(wheel(five.dialog, fine) == DialogAction::none);
    CHECK(five.scroll() == 0 && five.dialog.wheel_rows == 0.75F);
    CHECK(wheel(five.dialog, fine) == DialogAction::redraw);
    CHECK(five.scroll() == 1 && five.dialog.wheel_rows == 0.5F);
    CHECK(wheel(five.dialog, fine) == DialogAction::redraw);
    CHECK(five.scroll() == 2 && five.dialog.wheel_rows == 0.25F);
    CHECK(wheel(five.dialog, fine) == DialogAction::redraw);
    CHECK(five.scroll() == 3 && five.dialog.wheel_rows == 0.0F);
    // A turn far larger than the section reaches its end, and the carry
    // towards that end is dropped there.
    CHECK(wheel(five.dialog, -1.0e9F) == DialogAction::redraw);
    CHECK(five.scroll() == 80 && five.dialog.wheel_rows == 0.0F);
    CHECK(wheel(five.dialog, fine) == DialogAction::none);
    CHECK(five.dialog.wheel_rows == 0.0F);
    // A carry is dropped when another section shows.
    CHECK(wheel(five.dialog, 1.0F / 32.0F) == DialogAction::none);
    CHECK(five.dialog.wheel_rows == -0.75F);
    CHECK(click(five.dialog, centre(geometry::list_item(Page::controls))) == DialogAction::redraw);
    CHECK(five.dialog.wheel_rows == 0.0F);
    CHECK(click(five.dialog, centre(geometry::list_item(Page::graphics))) == DialogAction::redraw);
    CHECK(five.scroll() == 80);

    // Anywhere over the dialog, the section list and the footer included;
    // nowhere outside it.
    CHECK(wheel(five.dialog, 1.0F, {20, 100}) == DialogAction::redraw);
    CHECK(wheel(five.dialog, 1.0F, {300, 310}) == DialogAction::redraw);
    CHECK(five.scroll() == 32);
    for (const Point outside :
         {Point{-1, 100},
          Point{settings::dialog_width, 100},
          Point{300, -1},
          Point{300, settings::dialog_height}})
        CHECK(wheel(five.dialog, 1.0F, outside) == DialogAction::none);
    CHECK(five.scroll() == 32);
    // Not a number, nor an endless turn, scrolls.
    CHECK(
        settings::dialog_wheel(five.dialog, 300, 150, std::numeric_limits<float>::quiet_NaN()) ==
        DialogAction::none
    );
    CHECK(
        settings::dialog_wheel(five.dialog, 300, 150, std::numeric_limits<float>::infinity()) ==
        DialogAction::none
    );
    CHECK(five.scroll() == 32);

    // Nothing moves while a press is held: on a footer button or a slider.
    const Point restore = centre(geometry::restore_button);
    CHECK(settings::dialog_pointer_down(five.dialog, restore.x, restore.y) == DialogAction::redraw);
    CHECK(wheel(five.dialog, -1.0F) == DialogAction::none);
    static_cast<void>(settings::dialog_pointer_up(five.dialog, 0, 0));
    const auto track = five.rows().rows.rows[2].control_area;
    const Point last_stop{track.x + track.width - 1, track.y + 4};
    CHECK(
        settings::dialog_pointer_down(five.dialog, last_stop.x, last_stop.y) ==
        DialogAction::changed
    );
    CHECK(five.dialog.dragging);
    CHECK(wheel(five.dialog, -1.0F) == DialogAction::none);
    static_cast<void>(settings::dialog_pointer_up(five.dialog, last_stop.x, last_stop.y));
    CHECK(five.scroll() == 32);
    // A scroll changes no setting.
    const auto chosen = five.dialog.chosen;
    CHECK(wheel(five.dialog, -1.0F) == DialogAction::redraw);
    CHECK(five.dialog.chosen == chosen);
}

void the_scroll_keys_scroll_whatever_has_the_focus() {
    Scrolling all(nine_rows());
    // Page Down and Page Up by 200 rows, stopping at the ends; End and Home.
    for (const auto& [key, expected] :
         {std::pair{DialogKey::page_down, 200},
          std::pair{DialogKey::page_down, 316},
          std::pair{DialogKey::page_up, 116},
          std::pair{DialogKey::page_up, 0},
          std::pair{DialogKey::end, 316},
          std::pair{DialogKey::home, 0}}) {
        CHECK(settings::dialog_key(all.dialog, key) == DialogAction::redraw);
        CHECK(all.scroll() == expected);
    }
    CHECK(settings::dialog_key(all.dialog, DialogKey::home) == DialogAction::none);
    CHECK(settings::dialog_key(all.dialog, DialogKey::page_up) == DialogAction::none);
    // They never show the focus, nor change a setting.
    CHECK(all.dialog.focused == settings::no_control);
    CHECK(all.dialog.chosen == settings::EngineSettings{});

    // With the focus on the unit limit's slider, Home and End scroll and
    // leave the slider and the focus alone.
    for (int32_t press = 0; press < 5; ++press)
        static_cast<void>(settings::dialog_key(all.dialog, DialogKey::tab));
    CHECK(all.dialog.focused == settings::first_row_control + 4);
    const auto chosen = all.dialog.chosen;
    CHECK(settings::dialog_key(all.dialog, DialogKey::end) == DialogAction::redraw);
    CHECK(all.scroll() == 316);
    CHECK(settings::dialog_key(all.dialog, DialogKey::home) == DialogAction::redraw);
    CHECK(all.scroll() == 0);
    CHECK(all.dialog.focused == settings::first_row_control + 4);
    CHECK(all.dialog.chosen == chosen);

    // One Page Down reaches the end of five rows.
    Scrolling five(five_rows());
    CHECK(settings::dialog_key(five.dialog, DialogKey::page_down) == DialogAction::redraw);
    CHECK(five.scroll() == 80);
    // Nothing moves while a press is held.
    const Point ok = centre(geometry::ok_button);
    static_cast<void>(settings::dialog_pointer_down(five.dialog, ok.x, ok.y));
    CHECK(settings::dialog_key(five.dialog, DialogKey::home) == DialogAction::none);
    CHECK(five.scroll() == 80);
}

void the_focus_scrolls_its_row_into_view() {
    Scrolling five(five_rows());
    // Tab from no focus: the first three rows at 0, the fourth at 25 (its
    // lower line at 289), the last at the end.
    const std::array<std::pair<int32_t, int32_t>, 6> forward{{
        {settings::first_row_control, 0},
        {settings::first_row_control + 1, 0},
        {settings::first_row_control + 2, 0},
        {settings::first_row_control + 3, 25},
        {settings::first_row_control + 4, 80},
        {settings::restore_control, 80},
    }};
    for (const auto& [control, expected] : forward) {
        CHECK(settings::dialog_key(five.dialog, DialogKey::tab) == DialogAction::redraw);
        CHECK(five.dialog.focused == control);
        CHECK(five.scroll() == expected);
    }
    // Back up: the third row shows whole at 80, the second scrolls to 65 and
    // the first to 0. A button or an entry never scrolls.
    const std::array<std::pair<int32_t, int32_t>, 5> back{{
        {settings::first_row_control + 4, 80},
        {settings::first_row_control + 3, 80},
        {settings::first_row_control + 2, 80},
        {settings::first_row_control + 1, 65},
        {settings::first_row_control, 0},
    }};
    for (const auto& [control, expected] : back) {
        CHECK(settings::dialog_key(five.dialog, DialogKey::back_tab) == DialogAction::redraw);
        CHECK(five.dialog.focused == control);
        CHECK(five.scroll() == expected);
    }
    CHECK(settings::dialog_key(five.dialog, DialogKey::up) == DialogAction::redraw);
    CHECK(five.dialog.focused == settings::page_control(Page::developer));
    CHECK(five.scroll() == 0);

    // Scrolling never moves the focus; a key that acts on the focused row
    // brings it back into view first.
    five.open();
    for (int32_t press = 0; press < 5; ++press)
        static_cast<void>(settings::dialog_key(five.dialog, DialogKey::down));
    CHECK(five.dialog.focused == settings::first_row_control + 4);
    CHECK(settings::dialog_key(five.dialog, DialogKey::home) == DialogAction::redraw);
    CHECK(five.dialog.focused == settings::first_row_control + 4);
    CHECK(five.scroll() == 0);
    CHECK(settings::dialog_key(five.dialog, DialogKey::right) == DialogAction::changed);
    CHECK(five.dialog.chosen.frame_stats);
    CHECK(five.scroll() == 80);
    // A key that only brings the row back, changing nothing, still redraws.
    CHECK(settings::dialog_key(five.dialog, DialogKey::home) == DialogAction::redraw);
    CHECK(settings::dialog_key(five.dialog, DialogKey::right) == DialogAction::redraw);
    CHECK(five.scroll() == 80);
    // Space on a slider does nothing but bring it into view.
    for (int32_t press = 0; press < 4; ++press)
        static_cast<void>(settings::dialog_key(five.dialog, DialogKey::up));
    CHECK(five.dialog.focused == settings::first_row_control);
    CHECK(five.scroll() == 0);
    CHECK(settings::dialog_key(five.dialog, DialogKey::end) == DialogAction::redraw);
    CHECK(settings::dialog_key(five.dialog, DialogKey::space) == DialogAction::redraw);
    CHECK(five.scroll() == 0);
    CHECK(settings::dialog_key(five.dialog, DialogKey::space) == DialogAction::none);
}

void the_scroll_bar_follows_a_drag() {
    Scrolling five(five_rows());
    // The pointer over the bar hovers it.
    CHECK(settings::dialog_pointer_move(five.dialog, 473, 150) == DialogAction::redraw);
    CHECK(five.dialog.hovered == settings::scroll_bar_control);
    CHECK(settings::dialog_pointer_move(five.dialog, 467, 289) == DialogAction::none);
    CHECK(settings::dialog_pointer_move(five.dialog, 478, 54) == DialogAction::none);
    // A press on the thumb grabs it where it is pressed and moves nothing.
    CHECK(settings::dialog_pointer_down(five.dialog, 473, 100) == DialogAction::redraw);
    CHECK(five.dialog.pressed == settings::scroll_bar_control && five.dialog.dragging);
    CHECK(five.dialog.scroll_grab == 45);
    CHECK(five.scroll() == 0);
    // The thumb follows the pointer's row only, wherever the pointer goes,
    // and the offset follows the thumb to the nearest row.
    CHECK(settings::dialog_pointer_move(five.dialog, 300, 102) == DialogAction::redraw);
    CHECK(five.scroll() == 3);
    CHECK(settings::dialog_pointer_move(five.dialog, 300, 130) == DialogAction::redraw);
    CHECK(five.scroll() == 40);
    CHECK(settings::dialog_pointer_move(five.dialog, 10, 130) == DialogAction::none);
    CHECK(settings::dialog_pointer_move(five.dialog, 10, 400) == DialogAction::redraw);
    CHECK(five.scroll() == 80);
    CHECK(settings::dialog_pointer_move(five.dialog, 600, -100) == DialogAction::redraw);
    CHECK(five.scroll() == 0);
    CHECK(five.dialog.chosen == settings::EngineSettings{});
    // While it is held the wheel and the scroll keys do nothing.
    CHECK(wheel(five.dialog, -1.0F) == DialogAction::none);
    CHECK(settings::dialog_key(five.dialog, DialogKey::end) == DialogAction::none);
    CHECK(five.scroll() == 0);
    // The release ends the drag, wherever it happens, and acts on nothing.
    CHECK(settings::dialog_pointer_up(five.dialog, 10, 10) == DialogAction::redraw);
    CHECK(!five.dialog.dragging && five.dialog.pressed == settings::no_control);
    CHECK(settings::dialog_pointer_move(five.dialog, 10, 400) == DialogAction::none);
    CHECK(five.scroll() == 0);

    // A press on the well below the thumb brings the thumb's middle to it,
    // and above it likewise; the drag starts there.
    CHECK(settings::dialog_pointer_down(five.dialog, 473, 260) == DialogAction::redraw);
    CHECK(five.scroll() == 80);
    CHECK(five.dialog.scroll_grab == 87);
    static_cast<void>(settings::dialog_pointer_up(five.dialog, 473, 260));
    CHECK(settings::dialog_pointer_down(five.dialog, 473, 70) == DialogAction::redraw);
    CHECK(five.scroll() == 0);
    CHECK(settings::dialog_pointer_move(five.dialog, 473, 172) == DialogAction::redraw);
    CHECK(five.scroll() == 40);
    static_cast<void>(settings::dialog_pointer_up(five.dialog, 473, 172));

    // A press on the bar leaves the keyboard focus where it was, and the
    // next Tab moves on from there: a press on the thumb,
    five.open();
    CHECK(settings::dialog_key(five.dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(five.dialog.focused == settings::first_row_control);
    static_cast<void>(settings::dialog_pointer_down(five.dialog, 473, 100));
    CHECK(five.dialog.focused == settings::first_row_control);
    static_cast<void>(settings::dialog_pointer_up(five.dialog, 473, 100));
    CHECK(five.dialog.focused == settings::first_row_control);
    CHECK(settings::dialog_key(five.dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(five.dialog.focused == settings::first_row_control + 1);
    // and a press on the well below it, which scrolls to the end.
    five.open();
    for (int32_t press = 0; press < 3; ++press)
        static_cast<void>(settings::dialog_key(five.dialog, DialogKey::tab));
    CHECK(five.dialog.focused == settings::first_row_control + 2);
    CHECK(five.scroll() == 0);
    CHECK(settings::dialog_pointer_down(five.dialog, 473, 260) == DialogAction::redraw);
    CHECK(five.scroll() == 80);
    CHECK(five.dialog.focused == settings::first_row_control + 2);
    static_cast<void>(settings::dialog_pointer_up(five.dialog, 473, 260));
    CHECK(five.dialog.focused == settings::first_row_control + 2);
    CHECK(settings::dialog_key(five.dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(five.dialog.focused == settings::first_row_control + 3);
    CHECK(five.scroll() == 80);
}

void a_cut_row_answers_only_where_it_shows() {
    Scrolling five(five_rows());
    // At 40 the last row's switch shows its top seven rows, 283 to 289: a
    // press there acts, a press under the view does not.
    five.dialog.scroll[static_cast<std::size_t>(Page::graphics)] = 40;
    const auto stats = five.rows().rows.rows[4].control_area;
    CHECK(stats.y == 283);
    const int32_t on = stats.x + stats.width - 4;
    CHECK(click(five.dialog, {on, 292}) == DialogAction::none);
    CHECK(!five.dialog.chosen.frame_stats);
    CHECK(click(five.dialog, {on, 286}) == DialogAction::changed);
    CHECK(five.dialog.chosen.frame_stats);
    // Its parts are drawn but not listed.
    for (const auto& part : settings::dialog_layout(five.dialog))
        CHECK(part.control != settings::first_row_control + 4);

    // At 50 the first row's slider line shows from 54: a press on its
    // visible part drags it, a press above the view does not.
    five.dialog.scroll[static_cast<std::size_t>(Page::graphics)] = 50;
    const auto track = five.rows().rows.rows[0].control_area;
    CHECK(track.y == 47);
    CHECK(settings::dialog_pointer_down(five.dialog, track.x + 1, 50) == DialogAction::none);
    CHECK(five.dialog.chosen.max_frame_rate == settings::highest_frame_rate);
    CHECK(settings::dialog_pointer_down(five.dialog, track.x + 1, 56) == DialogAction::changed);
    CHECK(five.dialog.chosen.max_frame_rate == settings::lowest_frame_rate);
    // The drag follows the pointer's column wherever the line is.
    CHECK(
        settings::dialog_pointer_move(five.dialog, track.x + track.width, 20) ==
        DialogAction::changed
    );
    CHECK(five.dialog.chosen.max_frame_rate == settings::highest_frame_rate);
    static_cast<void>(settings::dialog_pointer_up(five.dialog, 0, 0));

    // At the end the strip's top is cut at 54; it still answers below it.
    five.dialog.scroll[static_cast<std::size_t>(Page::graphics)] = 80;
    const auto strip = five.rows().rows.rows[1].control_area;
    CHECK(strip.y == 48);
    const int32_t second = strip.x + 1 + geometry::level_width + geometry::level_width / 2;
    CHECK(click(five.dialog, {second, 50}) == DialogAction::none);
    CHECK(click(five.dialog, {second, 58}) == DialogAction::changed);
    CHECK(five.dialog.chosen.anti_aliasing == settings::AntiAliasing::x2);
    // A press never scrolls.
    CHECK(five.scroll() == 80);
}

void offsets_are_kept_for_each_section_until_the_dialog_opens_again() {
    Scrolling five(five_rows());
    CHECK(wheel(five.dialog, -1.0F) == DialogAction::redraw);
    CHECK(five.scroll() == 24);
    CHECK(click(five.dialog, centre(geometry::list_item(Page::controls))) == DialogAction::redraw);
    CHECK(geometry::open_rows(five.dialog).scroll == 0);
    CHECK(click(five.dialog, centre(geometry::list_item(Page::graphics))) == DialogAction::redraw);
    CHECK(five.scroll() == 24);
    CHECK(geometry::open_rows(five.dialog).rows.rows[0].top == geometry::first_row_top - 24);
    // Restore defaults is no scroll: the offset stays.
    CHECK(click(five.dialog, centre(geometry::restore_button)) == DialogAction::redraw);
    CHECK(five.scroll() == 24);
    // Each opening starts every section at its top.
    five.open();
    CHECK(five.dialog.scroll == (std::array<int32_t, settings::page_count>{}));
    CHECK(five.scroll() == 0);
}

void the_hover_follows_the_rows_under_a_still_pointer() {
    Scrolling five(five_rows());
    // Over the first row's value: no control. A notch brings the level strip
    // under the pointer, and it is hovered without the pointer moving.
    CHECK(settings::dialog_pointer_move(five.dialog, 400, 104) == DialogAction::none);
    CHECK(five.dialog.hovered == settings::no_control);
    CHECK(wheel(five.dialog, -1.0F, {400, 104}) == DialogAction::redraw);
    CHECK(five.dialog.hovered == settings::first_row_control + 1);
    // The keys move the rows under it too.
    CHECK(settings::dialog_key(five.dialog, DialogKey::home) == DialogAction::redraw);
    CHECK(five.dialog.hovered == settings::no_control);
}

void locked_switch_rows_keep_their_value_in_sight() {
    // The fourth row, a switch whose hint lines are its status, locked
    // during a game; the fifth, any other switch, set on the command line.
    Section section = five_rows();
    section.locks = {
        {Setting::escape_opens_menu, Lock::in_game},
        {Setting::frame_stats, Lock::command_line},
    };
    section.status = {Setting::escape_opens_menu};
    settings::EngineSettings current{};
    current.escape_opens_menu = true;
    current.frame_stats = true;
    current.unit_limit = 900;
    Scrolling five(std::move(section), current);
    const auto rows = five.rows().rows.rows;
    // Its lock where the switch was, ending where the switch ended; no
    // switch; the label 153 columns wide, 8 short of the lock.
    const auto& status = rows[3];
    CHECK(status.hint_is_status);
    CHECK(status.control_area.width == 0);
    CHECK(same_rect(status.lock_area, {319, 264, 148, 16}));
    CHECK(same_rect(status.label, {158, 264, 153, 16}));
    // The switch kept, its lock 8 columns left of it and the label 93 wide.
    const auto& kept = rows[4];
    CHECK(!kept.hint_is_status);
    CHECK(same_rect(kept.control_area, {415, 323, 52, 16}));
    CHECK(same_rect(kept.lock_area, {259, 323, 148, 16}));
    CHECK(same_rect(kept.label, {158, 323, 93, 16}));
    for (const auto& row : {status, kept}) {
        CHECK(!overlap(row.label, row.lock_area));
        CHECK(!overlap(row.lock_area, row.control_area));
        CHECK(row.height == (row.hint_lines == 2 ? 59 : 47));
    }
    // A locked row is as tall as it is unlocked: the limit stays 80.
    CHECK(five.rows().limit == 80);

    // At the end both show whole: the padlock and lock text, the kept
    // switch's Off and On with no control, and no switch for the status row.
    five.dialog.scroll[static_cast<std::size_t>(Page::graphics)] = 80;
    const auto parts = settings::dialog_layout(five.dialog);
    CHECK(find_part(parts, "Locked during a game", settings::no_control) != nullptr);
    CHECK(find_part(parts, "Set on the command line", settings::no_control) != nullptr);
    int32_t status_captions = 0;
    int32_t kept_captions = 0;
    for (const auto& part : parts) {
        CHECK(part.control != settings::first_row_control + 3);
        CHECK(part.control != settings::first_row_control + 4);
        if (part.text != "OFF" && part.text != "ON")
            continue;
        status_captions += part.rect.y == 185 ? 1 : 0;
        if (part.rect.y == 244) {
            CHECK(part.control == settings::no_control);
            ++kept_captions;
        }
    }
    CHECK(status_captions == 0);
    CHECK(kept_captions == 2);
    bool status_padlock = false;
    bool kept_padlock = false;
    for (const auto& part : parts) {
        if (!part.text.empty() || part.control != settings::no_control)
            continue;
        status_padlock = status_padlock || same_rect(part.rect, {319, 184, 5, 16});
        kept_padlock = kept_padlock || same_rect(part.rect, {259, 243, 5, 16});
    }
    CHECK(status_padlock && kept_padlock);

    // Neither takes a press or the focus.
    CHECK(settings::dialog_pointer_down(five.dialog, 460, 250) == DialogAction::none);
    CHECK(settings::dialog_pointer_up(five.dialog, 460, 250) == DialogAction::none);
    CHECK(settings::dialog_pointer_down(five.dialog, 460, 190) == DialogAction::none);
    CHECK(settings::dialog_pointer_up(five.dialog, 460, 190) == DialogAction::none);
    CHECK(five.dialog.chosen == current);
    five.open(current);
    for (const int32_t expected :
         {settings::first_row_control,
          settings::first_row_control + 1,
          settings::first_row_control + 2,
          settings::restore_control}) {
        CHECK(settings::dialog_key(five.dialog, DialogKey::tab) == DialogAction::redraw);
        CHECK(five.dialog.focused == expected);
    }
    CHECK(five.scroll() == 0);

    // Restore defaults keeps every locked value, the switches' included.
    CHECK(click(five.dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(five.dialog.chosen.escape_opens_menu);
    CHECK(five.dialog.chosen.frame_stats);
    CHECK(five.dialog.chosen.unit_limit == settings::default_unit_limit);
}

struct Canvas {
    renderer::Surface surface;

    renderer::Rgb at(int32_t x, int32_t y) const {
        const std::size_t offset = (static_cast<std::size_t>(y) * surface.width + x) * 3;
        return {surface.rgb[offset], surface.rgb[offset + 1], surface.rgb[offset + 2]};
    }
};

Canvas blank(uint32_t width, uint32_t height) {
    Canvas canvas;
    canvas.surface.width = width;
    canvas.surface.height = height;
    canvas.surface.rgb.assign(std::size_t{width} * height * 3, 0);
    return canvas;
}

constexpr renderer::Rgb kPanel{0x1b, 0x1e, 0x19};
constexpr renderer::Rgb kBand{0x14, 0x16, 0x12};
constexpr renderer::Rgb kList{0x17, 0x1a, 0x15};
constexpr renderer::Rgb kAccent{0x9c, 0xcc, 0x3c};
constexpr renderer::Rgb kOffSelected{0x2c, 0x32, 0x26};

void the_dialog_draws_its_faces_and_accents(const settings::DialogFonts& fonts) {
    Canvas canvas = blank(settings::dialog_width, settings::dialog_height);
    settings::Dialog dialog = opened(Page::controls);
    dialog.chosen.escape_opens_menu = false;
    settings::draw_dialog(canvas.surface, {0, 0, 1}, dialog, fonts);
    CHECK(canvas.at(300, 3) == kBand);                           // the header
    CHECK(canvas.at(300, geometry::footer_top + 2) == kBand);    // the footer
    CHECK(canvas.at(4, 250) == kList);                           // the section list
    CHECK(canvas.at(geometry::content_left + 2, 270) == kPanel); // under the rows
    const auto marker = geometry::list_item(Page::controls);
    CHECK(
        canvas.at(marker.x + geometry::list_marker_offset, marker.y + marker.height / 2) == kAccent
    );
    CHECK(canvas.at(geometry::ok_button.x + 2, geometry::ok_button.y + 2) == kAccent);
    const auto rows = geometry::place_rows(Page::controls, {});
    const auto& zoom = rows.rows[0].control_area;
    CHECK(canvas.at(zoom.x + zoom.width - 3, zoom.y + 2) == kAccent); // On
    const auto& escape = rows.rows[1].control_area;
    CHECK(canvas.at(escape.x + 2, escape.y + 2) == kOffSelected); // Off

    // At twice the size, offset into a larger surface.
    Canvas larger = blank(1200, 800);
    settings::draw_dialog(larger.surface, {100, 50, 2}, dialog, fonts);
    CHECK(larger.at(99, 49) == (renderer::Rgb{0, 0, 0}));
    CHECK(larger.at(100 + 2 * 300, 50 + 2 * 3) == kBand);
    CHECK(
        larger.at(100 + 2 * (geometry::ok_button.x + 2), 50 + 2 * (geometry::ok_button.y + 2)) ==
        kAccent
    );

    // A slider's filled track runs to its knob.
    Canvas graphics = blank(settings::dialog_width, settings::dialog_height);
    settings::Dialog shown = opened(Page::graphics);
    settings::draw_dialog(graphics.surface, {0, 0, 1}, shown, fonts);
    const auto track = geometry::place_rows(Page::graphics, {}).rows[0].control_area;
    CHECK(graphics.at(track.x + 2, track.y + geometry::track_offset + 1) == kAccent);

    // The OA button's face.
    Canvas button = blank(settings::menu_button_side, settings::menu_button_side);
    settings::draw_oa_button(
        button.surface, {0, 0, 1}, settings::menu_button_side, settings::ButtonLook::idle, fonts
    );
    CHECK(button.at(3, 3) == kPanel);
    Canvas pressed = blank(settings::ingame_button_side, settings::ingame_button_side);
    settings::draw_oa_button(
        pressed.surface,
        {0, 0, 1},
        settings::ingame_button_side,
        settings::ButtonLook::pressed,
        fonts
    );
    CHECK(pressed.at(2, 2) == kBand);
}

constexpr renderer::Rgb kRule{0x2b, 0x30, 0x27};
constexpr renderer::Rgb kText{0xe7, 0xe8, 0xdf};
constexpr renderer::Rgb kHint{0x9a, 0xa1, 0x90};
constexpr renderer::Rgb kWell{0x12, 0x14, 0x10};
constexpr renderer::Rgb kControlBorder{0x3a, 0x40, 0x34};
constexpr renderer::Rgb kControlHover{0x5b, 0x63, 0x52};
constexpr renderer::Rgb kSwitchIdle{0x7d, 0x84, 0x74};
constexpr renderer::Rgb kLock{0xe0, 0xb0, 0x4f};

/// Returns a colour faded into the panel as a locked row is: 115 of 256
/// parts panel.
renderer::Rgb faded(renderer::Rgb color) {
    constexpr uint32_t fade = 115;
    renderer::Rgb mixed{};
    for (std::size_t channel = 0; channel < mixed.size(); ++channel)
        mixed[channel] = static_cast<uint8_t>(
            (color[channel] * (256U - fade) + kPanel[channel] * fade + 128U) / 256U
        );
    return mixed;
}

/// Returns a font whose every printable glyph is a solid block 3 columns
/// wide and 8 rows high, so that every text draws pixels in its whole colour.
settings::DialogFonts block_fonts() {
    renderer::TextFont font;
    font.font.nominal_height = 8;
    constexpr uint8_t ink_index = 1;
    for (int32_t byte = ' '; byte < 0x7f; ++byte) {
        const bool space = byte == ' ';
        font.font.glyphs[static_cast<std::size_t>(byte)] = oa::formats::fnt::Glyph{
            3,
            8,
            0,
            0,
            std::vector<uint8_t>(24, ink_index),
            std::vector<uint8_t>(24, space ? 0 : 1),
        };
    }
    font.ink[ink_index] = renderer::blend_opaque;
    return {font, font};
}

void the_dialog_draws_the_scroll_bar_and_clips_the_rows() {
    const auto fonts = block_fonts();
    // Sections that fit draw no scroll bar, whatever offset they hold.
    for (const Page page : kPages) {
        Canvas canvas = blank(settings::dialog_width, settings::dialog_height);
        settings::Dialog dialog = opened(page);
        settings::draw_dialog(canvas.surface, {0, 0, 1}, dialog, fonts);
        CHECK(canvas.at(473, 150) == kPanel);
        CHECK(canvas.at(300, geometry::first_row_top) == kRule);
        Canvas held = blank(settings::dialog_width, settings::dialog_height);
        dialog.scroll[static_cast<std::size_t>(page)] = 50;
        settings::draw_dialog(held.surface, {0, 0, 1}, dialog, fonts);
        CHECK(held.surface.rgb == canvas.surface.rgb);
    }

    // At the top: the well with its border, the thumb in its top 174 rows.
    // On a canvas taller than the dialog, nothing of the rows below the view
    // is drawn under the dialog.
    Scrolling five(five_rows());
    Canvas top = blank(settings::dialog_width, 400);
    settings::draw_dialog(top.surface, {0, 0, 1}, five.dialog, fonts);
    CHECK(top.at(470, 100) == kControlBorder);
    CHECK(top.at(476, 100) == kControlBorder);
    CHECK(top.at(473, 54) == kControlBorder);
    CHECK(top.at(473, 289) == kControlBorder);
    CHECK(top.at(473, 100) == kControlHover);
    CHECK(top.at(473, 228) == kControlHover);
    CHECK(top.at(473, 229) == kWell);
    CHECK(top.at(469, 100) == kPanel);
    CHECK(top.at(477, 100) == kPanel);
    CHECK(top.at(300, geometry::first_row_top) == kRule);
    CHECK(top.at(440, 330) == (renderer::Rgb{0, 0, 0}));
    // The fourth row's first status line is cut at 289: drawn above, not below.
    CHECK(top.at(159, 284) == kHint);
    CHECK(top.at(159, 291) != kHint);

    // Scrolled to 40 with the pointer on the bar: the bar lighter, the thumb
    // at 85, a fixed line at the view's top, and nothing of the rows above
    // the view drawn over the header or beside the heading.
    five.dialog.scroll[static_cast<std::size_t>(Page::graphics)] = 40;
    static_cast<void>(settings::dialog_pointer_move(five.dialog, 473, 150));
    Canvas scrolled = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(scrolled.surface, {0, 0, 1}, five.dialog, fonts);
    CHECK(scrolled.at(470, 100) == kControlHover);
    CHECK(scrolled.at(473, 84) == kWell);
    CHECK(scrolled.at(473, 85) == kSwitchIdle);
    CHECK(scrolled.at(473, 258) == kSwitchIdle);
    CHECK(scrolled.at(473, 259) == kWell);
    CHECK(scrolled.at(300, geometry::first_row_top) == kRule);
    CHECK(scrolled.at(300, 14) == kBand);
    CHECK(scrolled.at(160, 30) == kPanel);
    CHECK(scrolled.at(220, 45) == kPanel);
    // The first row's slider line shows from 57, its track filled to the knob.
    CHECK(scrolled.at(160, 57 + geometry::track_offset + 1) == kAccent);

    // Locked: the status row fades its label line only, its status at full
    // strength; the other switch fades whole, its On without the accent,
    // and each padlock is drawn over the fade.
    Section section = five_rows();
    section.locks = {
        {Setting::escape_opens_menu, Lock::in_game},
        {Setting::frame_stats, Lock::command_line},
    };
    section.status = {Setting::escape_opens_menu};
    settings::EngineSettings current{};
    current.frame_stats = true;
    Scrolling locked(std::move(section), current);
    locked.dialog.scroll[static_cast<std::size_t>(Page::graphics)] = 80;
    Canvas canvas = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(canvas.surface, {0, 0, 1}, locked.dialog, fonts);
    CHECK(canvas.at(159, 190) == faded(kText));         // the status row's label
    CHECK(canvas.at(159, 205) == kHint);                // its first status line
    CHECK(canvas.at(159, 217) == kHint);                // its second
    CHECK(canvas.at(400, 188) == kLock);                // its padlock, where the switch was
    CHECK(canvas.at(445, 185) == kPanel);               // no switch: neither its well
    CHECK(canvas.at(445, 199) == kPanel);               // nor its border
    CHECK(canvas.at(159, 249) == faded(kText));         // the other row's label
    CHECK(canvas.at(159, 264) == faded(kHint));         // its hint
    CHECK(canvas.at(444, 245) == faded(kControlHover)); // its On, without the accent
    CHECK(canvas.at(444, 245) != faded(kAccent));
    CHECK(canvas.at(415, 250) == faded(kControlBorder)); // its switch's border
    locked.dialog.chosen.frame_stats = false;
    Canvas off = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(off.surface, {0, 0, 1}, locked.dialog, fonts);
    CHECK(off.at(418, 245) == faded(kOffSelected)); // its Off, faded
    CHECK(off.at(444, 245) == faded(kWell));

    // With the focus shown on a row, a press held on the bar leaves its
    // outline drawn.
    Scrolling focused(five_rows());
    CHECK(settings::dialog_key(focused.dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(settings::dialog_pointer_down(focused.dialog, 473, 100) == DialogAction::redraw);
    Canvas held = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(held.surface, {0, 0, 1}, focused.dialog, fonts);
    const auto track = focused.rows().rows.rows[0].control_area;
    CHECK(held.at(track.x - geometry::focus_inset, track.y + 4) == kAccent);
    CHECK(held.at(470, 100) == kControlHover);
    CHECK(held.at(473, 100) == kSwitchIdle);
    // So does a press held on the well, which scrolled to the end: the third
    // row's outline is drawn where its slider now lies.
    Scrolling jumped(five_rows());
    for (int32_t press = 0; press < 3; ++press)
        static_cast<void>(settings::dialog_key(jumped.dialog, DialogKey::tab));
    CHECK(settings::dialog_pointer_down(jumped.dialog, 473, 260) == DialogAction::redraw);
    CHECK(jumped.scroll() == 80);
    Canvas well = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(well.surface, {0, 0, 1}, jumped.dialog, fonts);
    const auto size_track = jumped.rows().rows.rows[2].control_area;
    CHECK(size_track.y == 153);
    CHECK(well.at(size_track.x - geometry::focus_inset, size_track.y + 4) == kAccent);
    CHECK(well.at(473, 200) == kSwitchIdle);
}

void fonts_load_and_every_text_fits_its_place() {
    auto assets = oa::test::require_game_assets("the settings dialog's fonts");
    const auto fonts = settings::load_dialog_fonts(assets);
    CHECK(oa::formats::fnt::measure_text(fonts.regular.font, "OK") > 0);
    CHECK(oa::formats::fnt::measure_text(fonts.small.font, "OK") > 0);
    CHECK(fonts.small.font.nominal_height < fonts.regular.font.nominal_height);

    std::vector<settings::EngineSettings> states = level_states();
    settings::EngineSettings widest{};
    widest.unit_limit = settings::highest_unit_limit;
    widest.path_search_nodes = 8 * settings::base_path_search_nodes;
    states.push_back(widest);
    for (const Page page : kPages) {
        for (const auto& locks : lock_states()) {
            for (const auto& state : states) {
                settings::Dialog dialog = opened(page, locks);
                dialog.chosen = state;
                for (const auto& part : settings::dialog_layout(dialog)) {
                    if (part.text.empty())
                        continue;
                    const auto& font = part.font == settings::DialogFont::regular
                                           ? fonts.regular.font
                                           : fonts.small.font;
                    const auto width =
                        static_cast<int32_t>(oa::formats::fnt::measure_text(font, part.text)) +
                        part.tracking * static_cast<int32_t>(part.text.size() - 1);
                    if (width > part.rect.width || font.nominal_height > part.rect.height) {
                        std::cerr << "'" << part.text << "' is " << width << " wide in a box "
                                  << part.rect.width << " wide\n";
                        CHECK(width <= part.rect.width);
                        CHECK(font.nominal_height <= part.rect.height);
                    }
                }
            }
        }
    }
    the_dialog_draws_its_faces_and_accents(fonts);

    // A section that scrolls: every text it lists fits its place at every
    // offset, and each lock's text fits beside a kept switch as on a slider.
    Scrolling all(nine_rows());
    const int32_t limit = all.rows().limit;
    for (int32_t scroll = 0; scroll <= limit; ++scroll) {
        all.dialog.scroll[static_cast<std::size_t>(Page::graphics)] = scroll;
        for (const auto& part : settings::dialog_layout(all.dialog)) {
            if (part.text.empty())
                continue;
            const auto& font =
                part.font == settings::DialogFont::regular ? fonts.regular.font : fonts.small.font;
            const auto width =
                static_cast<int32_t>(oa::formats::fnt::measure_text(font, part.text));
            CHECK(width <= part.rect.width);
        }
    }
    for (const Lock lock : {Lock::in_game, Lock::set_by_host, Lock::command_line}) {
        Section section = five_rows();
        section.locks = {{Setting::escape_opens_menu, lock}, {Setting::frame_stats, lock}};
        section.status = {Setting::escape_opens_menu};
        Scrolling locked(std::move(section));
        locked.dialog.scroll[static_cast<std::size_t>(Page::graphics)] = 80;
        const auto parts = settings::dialog_layout(locked.dialog);
        int32_t lock_texts = 0;
        for (const auto& part : parts) {
            if (part.text != geometry::lock_text(lock))
                continue;
            ++lock_texts;
            const auto width =
                static_cast<int32_t>(oa::formats::fnt::measure_text(fonts.small.font, part.text));
            CHECK(width <= part.rect.width);
        }
        CHECK(lock_texts == 2);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv))
        fonts_load_and_every_text_fits_its_place();
    else {
        opening_shows_the_settings_in_effect();
        every_part_lies_inside_the_dialog_and_apart();
        each_section_shows_its_rows();
        a_click_on_an_entry_shows_its_section();
        every_control_is_pressed_where_it_is_drawn();
        switches_take_a_click_on_either_half_and_keys();
        every_stop_maps_to_its_value_and_back();
        sliders_follow_the_pointer_and_the_arrows();
        the_level_strip_picks_a_level();
        enter_keeps_and_escape_cancels();
        the_footer_buttons_restore_cancel_and_keep();
        the_focus_moves_round_every_control();
        locks_show_their_text_and_hold_their_settings();
        the_view_and_the_scroll_bar_keep_their_places();
        sections_that_fit_do_not_scroll();
        a_long_section_scrolls_by_its_overflow();
        control_numbers_put_the_rows_after_every_fixed_control();
        the_layout_lists_the_parts_wholly_in_the_view();
        the_wheel_scrolls_by_notches_and_carries_fractions();
        the_scroll_keys_scroll_whatever_has_the_focus();
        the_focus_scrolls_its_row_into_view();
        the_scroll_bar_follows_a_drag();
        a_cut_row_answers_only_where_it_shows();
        offsets_are_kept_for_each_section_until_the_dialog_opens_again();
        the_hover_follows_the_rows_under_a_still_pointer();
        locked_switch_rows_keep_their_value_in_sight();
        the_dialog_draws_its_faces_and_accents(settings::DialogFonts{});
        the_dialog_draws_the_scroll_bar_and_clips_the_rows();
    }
    if (failures != 0)
        return 1;
    std::cout << "settings dialog: ok\n";
    return 0;
}
