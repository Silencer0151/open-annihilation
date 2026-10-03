// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings dialog: where its parts lie, its sections and rows, switches,
// sliders and their stops, the level strip, pointer and key events, OK,
// Cancel and Restore defaults, the locks and their texts, and what it draws.
// The Graphics page's five rows scroll: Hardware acceleration, a strip of
// Off, Basic and Full whose hint lines are its status, and Vertical sync,
// each locked in its own form.
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
using settings::HardwareAcceleration;
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
                    for (int32_t renderer = 0; renderer < 8; ++renderer) {
                        settings::GameState state{in_game, shared, replay, command_line};
                        state.renderer_from_command_line = (renderer & 1) != 0;
                        state.acceleration_unavailable = (renderer & 2) != 0;
                        state.vertical_sync_unavailable = (renderer & 4) != 0;
                        states.push_back(settings::settings_locks(state));
                    }
                }
            }
        }
    }
    return states;
}

/// Every status Hardware acceleration's row shows: each state at each reach,
/// in a replay, with Basic or Full asked for.
std::vector<settings::AccelerationStatus> acceleration_statuses() {
    std::vector<settings::AccelerationStatus> statuses;
    for (int32_t state = 0; state <= static_cast<int32_t>(settings::AccelerationState::in_use);
         ++state)
        for (int32_t reach = 0;
             reach <= static_cast<int32_t>(settings::AccelerationReach::nearest_none);
             ++reach)
            for (const bool replay : {false, true})
                for (const auto asked : {HardwareAcceleration::basic, HardwareAcceleration::full})
                    statuses.push_back(
                        settings::AccelerationStatus{
                            static_cast<settings::AccelerationState>(state),
                            static_cast<settings::AccelerationReach>(reach),
                            replay,
                            asked,
                        }
                    );
    return statuses;
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
                // The open section stays within its columns; at its end, above
                // the footer.
                const auto rows = geometry::place_rows(page, locks);
                CHECK(rows.rows.size() == settings::page_settings(page).size());
                const auto open = geometry::open_rows(dialog);
                CHECK(
                    rows.bottom - geometry::scroll_limit(open.content_height) <
                    geometry::footer_rule_row
                );
                // Each row in its columns, at the offset that shows it.
                const auto in_columns = [&section](const renderer::SourceRect& rect) {
                    return rect.x >= section.x && rect.x + rect.width <= section.x + section.width;
                };
                for (std::size_t index = 0; index < rows.rows.size(); ++index) {
                    const auto& row = rows.rows[index];
                    CHECK(in_columns(row.label));
                    // A locked row whose hint lines are its status has no control.
                    if (row.control_area.width == 0) {
                        CHECK(row.hint_is_status && row.lock != Lock::none);
                        continue;
                    }
                    CHECK(in_columns(row.control_area));
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
    CHECK(settings::page_settings(Page::graphics).size() == 5);
    CHECK(settings::page_settings(Page::graphics)[2] == settings::Setting::screen_size);
    CHECK(settings::page_settings(Page::graphics)[3] == settings::Setting::hardware_acceleration);
    CHECK(settings::page_settings(Page::graphics)[4] == settings::Setting::vertical_sync);
    CHECK(settings::page_settings(Page::developer)[0] == settings::Setting::frame_stats);
    // Graphics' five rows are taller than the view, by 80 rows.
    const auto graphics = geometry::place_rows(Page::graphics, {});
    CHECK(graphics.rows.size() == 5);
    CHECK(geometry::scroll_limit(geometry::content_height(graphics, 0)) == 80);

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
    // While frames are drawn in Full the hint says what the level does
    // there, at the factor in use, and never asks for a fast processor.
    settings::AccelerationStatus full = dialog.acceleration;
    full.full_supersample = 4;
    CHECK(settings::set_acceleration_status(dialog, full) == DialogAction::redraw);
    const auto in_full = settings::dialog_layout(dialog);
    CHECK(
        find_part(
            in_full, "In Full the graphics card draws the view 4x finer", settings::no_control
        ) != nullptr
    );
    CHECK(
        find_part(in_full, "and scales it down for smoother edges.", settings::no_control) !=
        nullptr
    );
    CHECK(find_part(in_full, "Needs a fast CPU.", settings::no_control) == nullptr);
    full.full_supersample = 2;
    CHECK(settings::set_acceleration_status(dialog, full) == DialogAction::redraw);
    CHECK(
        find_part(
            settings::dialog_layout(dialog),
            "In Full the graphics card draws the view 2x finer",
            settings::no_control
        ) != nullptr
    );
    full.full_supersample = 1;
    CHECK(settings::set_acceleration_status(dialog, full) == DialogAction::redraw);
    CHECK(
        find_part(
            settings::dialog_layout(dialog),
            "In Full the graphics card draws the view 1:1;",
            settings::no_control
        ) != nullptr
    );
    full.full_supersample = 0;
    CHECK(settings::set_acceleration_status(dialog, full) == DialogAction::redraw);
    CHECK(
        find_part(
            settings::dialog_layout(dialog),
            "Units drawn at 16x and scaled down.",
            settings::no_control
        ) != nullptr
    );

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
    CHECK(dialog.forget_renderer_failures == 1);
    // Every press asks the host to act, even when no setting moves.
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.chosen == defaults);
    CHECK(dialog.forget_renderer_failures == 2);
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
    // Graphics' five rows are 316 and scroll.
    const std::array<int32_t, 5> content{74, 162, 86, 316, 56};
    for (std::size_t index = 0; index < kPages.size(); ++index) {
        const Page page = kPages[index];
        if (page == Page::graphics) {
            for (const auto& locks : lock_states()) {
                const auto at_top = geometry::place_rows(page, locks);
                CHECK(geometry::content_height(at_top, 0) == content[index]);
                CHECK(geometry::scroll_limit(content[index]) == 80);
            }
            continue;
        }
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

/// Checks a dialog open on Graphics at one offset: every part inside the
/// dialog and apart, every part over the view wholly in it, the scroll bar
/// once and right of every focus outline, and every listed control pressed
/// where it is drawn.
void check_dialog_layout_at(settings::Dialog& dialog, int32_t scroll) {
    const renderer::SourceRect face{
        geometry::edge,
        geometry::edge,
        settings::dialog_width - 2 * geometry::edge,
        settings::dialog_height - 2 * geometry::edge,
    };
    dialog.scroll[static_cast<std::size_t>(Page::graphics)] = scroll;
    const auto parts = settings::dialog_layout(dialog);
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
    for (const auto& row : geometry::open_rows(dialog).rows.rows)
        CHECK(
            row.control_area.x + row.control_area.width + geometry::focus_inset <
            geometry::scroll_well.x
        );
    for (const auto& part : parts) {
        if (part.control == settings::no_control)
            continue;
        const Point point = centre(part.rect);
        static_cast<void>(settings::dialog_pointer_move(dialog, point.x, point.y));
        CHECK(dialog.hovered == part.control);
    }
    CHECK(dialog.scroll[static_cast<std::size_t>(Page::graphics)] == scroll);
}

/// Checks the layout of a section of the test's own at one offset
/// (check_dialog_layout_at).
void check_layout_at(Scrolling& scrolling, int32_t scroll) {
    check_dialog_layout_at(scrolling.dialog, scroll);
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
                    std::string(
                        geometry::hint_line(
                            setting, scrolling.dialog.chosen, scrolling.dialog.acceleration, line
                        )
                    )
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
    CHECK(click(five.dialog, centre(geometry::restore_button)) == DialogAction::changed);
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

/// Returns a dialog open on the Graphics page with its own rows.
settings::Dialog graphics_page(
    const settings::EngineSettings& current = {},
    const settings::Locks& locks = {},
    const settings::AccelerationStatus& acceleration = {}
) {
    settings::Dialog dialog;
    settings::open_dialog(dialog, current, {}, locks, "v0.2.0", Page::graphics, acceleration);
    return dialog;
}

/// Returns Graphics' stored offset.
int32_t graphics_scroll(const settings::Dialog& dialog) {
    return dialog.scroll[static_cast<std::size_t>(Page::graphics)];
}

void the_graphics_page_scrolls_its_five_rows() {
    settings::Dialog dialog = graphics_page();
    const auto open = geometry::open_rows(dialog);
    CHECK(open.rows.rows.size() == 5);
    const std::array<int32_t, 5> tops{54, 119, 178, 255, 314};
    const std::array<int32_t, 5> heights{65, 59, 77, 59, 47};
    for (std::size_t index = 0; index < open.rows.rows.size() && index < tops.size(); ++index) {
        const auto& row = open.rows.rows[index];
        CHECK(row.top == tops[index]);
        CHECK(row.height == heights[index]);
        CHECK(row.control == settings::first_row_control + static_cast<int32_t>(index));
    }
    CHECK(open.rows.bottom == 361);
    CHECK(open.limit == 80);
    // Hardware acceleration: a strip of Off, Basic and Full, 34 columns a
    // level inside its border, with two status lines; Vertical sync a
    // switch with one hint line.
    const auto& acceleration = open.rows.rows[3];
    CHECK(acceleration.setting == Setting::hardware_acceleration);
    CHECK(acceleration.hint_is_status);
    CHECK(same_rect(acceleration.label, {158, 264, 197, 16}));
    CHECK(same_rect(acceleration.control_area, {363, 264, 104, 16}));
    CHECK(acceleration.hint_lines == 2);
    CHECK(acceleration.hints[0].y == 282 && acceleration.hints[1].y == 294);
    const auto& vsync = open.rows.rows[4];
    CHECK(vsync.setting == Setting::vertical_sync);
    CHECK(!vsync.hint_is_status);
    CHECK(same_rect(vsync.label, {158, 323, 249, 16}));
    CHECK(same_rect(vsync.control_area, {415, 323, 52, 16}));
    CHECK(vsync.hint_lines == 1 && vsync.hints[0].y == 341);

    // At the top the first three rows keep their places and Hardware
    // acceleration's label and strip show whole; at the end the closing
    // line is at 281.
    const auto top = settings::dialog_layout(dialog);
    CHECK(find_part(top, "Hardware acceleration", settings::no_control) != nullptr);
    CHECK(find_part(top, "Vertical sync", settings::no_control) == nullptr);
    CHECK(find_part(top, {}, settings::first_row_control + 3) != nullptr);
    dialog.scroll[static_cast<std::size_t>(Page::graphics)] = 500;
    const auto end = geometry::open_rows(dialog);
    CHECK(end.scroll == 80);
    CHECK(end.rows.bottom == 281);
    CHECK(end.rows.rows[1].control_area.y == 48);
    CHECK(end.rows.rows[3].label.y == 184);
    CHECK(end.rows.rows[4].label.y == 243);
    const auto at_end = settings::dialog_layout(dialog);
    CHECK(find_part(at_end, "Vertical sync", settings::no_control) != nullptr);
    CHECK(
        find_part(at_end, "Each frame waits for the display: no tearing.", settings::no_control) !=
        nullptr
    );
    CHECK(
        find_part(at_end, "Off: the processor draws and scales the view.", settings::no_control) !=
        nullptr
    );

    // Tab from no focus: the first three rows at 0, Hardware acceleration at
    // 25, Vertical sync at the end; back up, Enhanced anti-aliasing at 65.
    settings::Dialog keys = graphics_page();
    const std::array<std::pair<int32_t, int32_t>, 5> forward{{
        {settings::first_row_control, 0},
        {settings::first_row_control + 1, 0},
        {settings::first_row_control + 2, 0},
        {settings::first_row_control + 3, 25},
        {settings::first_row_control + 4, 80},
    }};
    for (const auto& [control, expected] : forward) {
        CHECK(settings::dialog_key(keys, DialogKey::tab) == DialogAction::redraw);
        CHECK(keys.focused == control);
        CHECK(graphics_scroll(keys) == expected);
    }
    const std::array<std::pair<int32_t, int32_t>, 4> back{{
        {settings::first_row_control + 3, 80},
        {settings::first_row_control + 2, 80},
        {settings::first_row_control + 1, 65},
        {settings::first_row_control, 0},
    }};
    for (const auto& [control, expected] : back) {
        CHECK(settings::dialog_key(keys, DialogKey::back_tab) == DialogAction::redraw);
        CHECK(keys.focused == control);
        CHECK(graphics_scroll(keys) == expected);
    }

    // Every part apart and in its place at every offset, under every lock.
    for (const auto& locks : lock_states()) {
        settings::Dialog locked = graphics_page({}, locks);
        for (int32_t scroll = 0; scroll <= 80; scroll += 5)
            check_dialog_layout_at(locked, scroll);
    }
}

void every_switch_reads_and_sets_through_one_table() {
    int32_t switches = 0;
    for (int32_t value = 0; value <= static_cast<int32_t>(Setting::vertical_sync); ++value) {
        const auto setting = static_cast<Setting>(value);
        const settings::EngineSettings before{};
        settings::EngineSettings state = before;
        if (!geometry::is_switch(setting)) {
            // A slider or the level strip is no switch, and set_switch leaves it.
            geometry::set_switch(state, setting, true);
            CHECK(state == before);
            CHECK(!geometry::switch_on(state, setting));
            continue;
        }
        ++switches;
        // Each switch reads and sets its own value, whichever its default.
        const bool was = geometry::switch_on(state, setting);
        geometry::set_switch(state, setting, !was);
        CHECK(geometry::switch_on(state, setting) == !was);
        CHECK(state != before);
        geometry::set_switch(state, setting, was);
        CHECK(geometry::switch_on(state, setting) == was);
        CHECK(state == before);
    }
    CHECK(switches == 5);
    // Hardware acceleration is a strip of Off, Basic and Full, no switch:
    // set_switch leaves it, and the strip reads and sets it by level, as
    // Enhanced anti-aliasing's strip does.
    settings::EngineSettings both{};
    geometry::set_switch(both, Setting::hardware_acceleration, true);
    CHECK(both.hardware_acceleration == HardwareAcceleration::off);
    CHECK(geometry::is_strip(Setting::hardware_acceleration));
    CHECK(geometry::is_strip(Setting::anti_aliasing));
    CHECK(!geometry::is_strip(Setting::vertical_sync));
    CHECK(geometry::strip_of(Setting::hardware_acceleration).levels == 3);
    CHECK(
        geometry::strip_of(Setting::hardware_acceleration).level_width ==
        geometry::acceleration_level_width
    );
    CHECK(
        geometry::strip_of(Setting::anti_aliasing).levels == settings::anti_aliasing_levels.size()
    );
    CHECK(geometry::strip_of(Setting::vertical_sync).levels == 0);
    CHECK(geometry::strip_caption(Setting::hardware_acceleration, 0) == "Off");
    CHECK(geometry::strip_caption(Setting::hardware_acceleration, 1) == "Basic");
    CHECK(geometry::strip_caption(Setting::hardware_acceleration, 2) == "Full");
    CHECK(geometry::strip_caption(Setting::hardware_acceleration, 3).empty());
    CHECK(geometry::strip_caption(Setting::anti_aliasing, 1) == "2x");
    geometry::set_strip_level(both, Setting::hardware_acceleration, 2);
    CHECK(both.hardware_acceleration == HardwareAcceleration::full);
    CHECK(geometry::strip_level(both, Setting::hardware_acceleration) == 2);
    geometry::set_strip_level(both, Setting::hardware_acceleration, 9);
    CHECK(both.hardware_acceleration == HardwareAcceleration::full);
    geometry::set_strip_level(both, Setting::hardware_acceleration, 1);
    CHECK(both.hardware_acceleration == HardwareAcceleration::basic);
    CHECK(geometry::strip_level(both, Setting::hardware_acceleration) == 1);
    geometry::set_strip_level(both, Setting::vertical_sync, 1);
    CHECK(!both.vertical_sync);
    geometry::set_switch(both, Setting::vertical_sync, true);
    CHECK(both.hardware_acceleration == HardwareAcceleration::basic && both.vertical_sync);
    // The column under each level, and the nearest end outside the strip.
    const renderer::SourceRect strip_area{363, 264, 104, 16};
    const auto strip = geometry::strip_of(Setting::hardware_acceleration);
    CHECK(geometry::level_at(strip_area, strip, 364) == 0);
    CHECK(geometry::level_at(strip_area, strip, 397) == 0);
    CHECK(geometry::level_at(strip_area, strip, 398) == 1);
    CHECK(geometry::level_at(strip_area, strip, 431) == 1);
    CHECK(geometry::level_at(strip_area, strip, 432) == 2);
    CHECK(geometry::level_at(strip_area, strip, 466) == 2);
    CHECK(geometry::level_at(strip_area, strip, 0) == 0);
    CHECK(geometry::level_at(strip_area, strip, 900) == 2);

    // Vertical sync's switch takes a click on either half and the keys;
    // Hardware acceleration's strip a click on each level.
    settings::Dialog dialog = graphics_page();
    dialog.scroll[static_cast<std::size_t>(Page::graphics)] = 80;
    const auto rows = geometry::open_rows(dialog).rows.rows;
    const auto on_half = [](const renderer::SourceRect& area) {
        return Point{area.x + area.width - 4, area.y + area.height / 2};
    };
    const auto off_half = [](const renderer::SourceRect& area) {
        return Point{area.x + 4, area.y + area.height / 2};
    };
    const auto level_centre = [](const renderer::SourceRect& area, int32_t level) {
        return Point{
            area.x + 1 + level * geometry::acceleration_level_width +
                geometry::acceleration_level_width / 2,
            area.y + area.height / 2
        };
    };
    CHECK(click(dialog, on_half(rows[4].control_area)) == DialogAction::changed);
    CHECK(dialog.chosen.vertical_sync);
    CHECK(click(dialog, off_half(rows[4].control_area)) == DialogAction::changed);
    CHECK(!dialog.chosen.vertical_sync);
    CHECK(dialog.forget_renderer_failures == 0);

    // Hardware acceleration passing to a higher level, Off to Basic or
    // Full or Basic to Full, asks for the graphics card to be tried afresh,
    // once each time; a level kept, and a lower one, keep the count.
    CHECK(click(dialog, level_centre(rows[3].control_area, 1)) == DialogAction::changed);
    CHECK(dialog.chosen.hardware_acceleration == HardwareAcceleration::basic);
    CHECK(dialog.forget_renderer_failures == 1);
    CHECK(click(dialog, level_centre(rows[3].control_area, 1)) == DialogAction::redraw);
    CHECK(dialog.forget_renderer_failures == 1);
    CHECK(click(dialog, level_centre(rows[3].control_area, 2)) == DialogAction::changed);
    CHECK(dialog.chosen.hardware_acceleration == HardwareAcceleration::full);
    CHECK(dialog.forget_renderer_failures == 2);
    CHECK(click(dialog, level_centre(rows[3].control_area, 1)) == DialogAction::changed);
    CHECK(dialog.chosen.hardware_acceleration == HardwareAcceleration::basic);
    CHECK(dialog.forget_renderer_failures == 2);
    CHECK(click(dialog, level_centre(rows[3].control_area, 0)) == DialogAction::changed);
    CHECK(dialog.chosen.hardware_acceleration == HardwareAcceleration::off);
    CHECK(dialog.forget_renderer_failures == 2);
    CHECK(click(dialog, level_centre(rows[3].control_area, 2)) == DialogAction::changed);
    CHECK(dialog.chosen.hardware_acceleration == HardwareAcceleration::full);
    CHECK(dialog.forget_renderer_failures == 3);
    CHECK(click(dialog, level_centre(rows[3].control_area, 0)) == DialogAction::changed);
    CHECK(dialog.chosen.hardware_acceleration == HardwareAcceleration::off);
    // Through the keys: Tab to it, Right a level up to Full and no further,
    // Left back down to Off, Space nothing on a strip.
    for (int32_t press = 0; press < 4; ++press)
        static_cast<void>(settings::dialog_key(dialog, DialogKey::tab));
    CHECK(dialog.focused == settings::first_row_control + 3);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::changed);
    CHECK(dialog.chosen.hardware_acceleration == HardwareAcceleration::basic);
    CHECK(dialog.forget_renderer_failures == 4);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::changed);
    CHECK(dialog.chosen.hardware_acceleration == HardwareAcceleration::full);
    CHECK(dialog.forget_renderer_failures == 5);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::redraw);
    CHECK(dialog.chosen.hardware_acceleration == HardwareAcceleration::full);
    CHECK(dialog.forget_renderer_failures == 5);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::changed);
    CHECK(dialog.chosen.hardware_acceleration == HardwareAcceleration::basic);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::changed);
    CHECK(dialog.chosen.hardware_acceleration == HardwareAcceleration::off);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::none);
    CHECK(dialog.chosen.hardware_acceleration == HardwareAcceleration::off);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::changed);
    CHECK(dialog.forget_renderer_failures == 6);
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::changed);
    CHECK(dialog.chosen.vertical_sync);
    CHECK(dialog.forget_renderer_failures == 6);
    // Cancel puts both back.
    CHECK(settings::dialog_key(dialog, DialogKey::escape) == DialogAction::cancelled);
    CHECK(
        dialog.chosen.hardware_acceleration == HardwareAcceleration::off &&
        !dialog.chosen.vertical_sync
    );

    // Restore defaults with the player's own defaults, Hardware acceleration
    // Full: every press asks once more, and reports a change though none
    // moved.
    settings::EngineSettings own{};
    own.hardware_acceleration = HardwareAcceleration::full;
    settings::open_dialog(dialog, own, own, {}, "v0.2.0", Page::graphics);
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.chosen == own);
    CHECK(dialog.forget_renderer_failures == 1);
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.forget_renderer_failures == 2);
    // From Off to its default Full, Restore defaults asks once; from Basic
    // it asks too, since each press has the card tried afresh.
    settings::open_dialog(dialog, {}, own, {}, "v0.2.0", Page::graphics);
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.chosen.hardware_acceleration == HardwareAcceleration::full);
    CHECK(dialog.forget_renderer_failures == 1);
    settings::EngineSettings basic{};
    basic.hardware_acceleration = HardwareAcceleration::basic;
    settings::open_dialog(dialog, basic, own, {}, "v0.2.0", Page::graphics);
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.chosen.hardware_acceleration == HardwareAcceleration::full);
    CHECK(dialog.forget_renderer_failures == 1);
}

void the_new_rows_lock_in_their_own_forms() {
    // Either flag locks Hardware acceleration; nothing that could help locks
    // it Not available here; a game never does.
    settings::GameState state{};
    state.renderer_from_command_line = true;
    state.acceleration_unavailable = true;
    CHECK(settings::settings_locks(state).hardware_acceleration == Lock::command_line);
    state.renderer_from_command_line = false;
    CHECK(settings::settings_locks(state).hardware_acceleration == Lock::unavailable);
    for (const bool shared : {false, true})
        for (const bool replay : {false, true}) {
            const auto locks = settings::settings_locks(settings::GameState{true, shared, replay});
            CHECK(locks.hardware_acceleration == Lock::none);
            CHECK(locks.vertical_sync == (shared || replay ? Lock::in_game : Lock::none));
        }
    state = {};
    state.vertical_sync_unavailable = true;
    CHECK(settings::settings_locks(state).vertical_sync == Lock::unavailable);
    state.in_game = true;
    state.shared_game = true;
    CHECK(settings::settings_locks(state).vertical_sync == Lock::unavailable);

    // Hardware acceleration locked: its lock where the strip was, no strip,
    // its label 153 wide. Vertical sync locked: its switch kept, the lock 8
    // columns left of it and its label 93 wide.
    settings::Locks locks{};
    locks.hardware_acceleration = Lock::unavailable;
    locks.vertical_sync = Lock::unavailable;
    settings::EngineSettings current{};
    current.hardware_acceleration = HardwareAcceleration::basic;
    current.vertical_sync = true;
    settings::Dialog dialog =
        graphics_page(current, locks, {settings::AccelerationState::no_usable_card, {}, false});
    const auto rows = geometry::open_rows(dialog).rows.rows;
    CHECK(same_rect(rows[3].lock_area, {319, 264, 148, 16}));
    CHECK(same_rect(rows[3].label, {158, 264, 153, 16}));
    CHECK(rows[3].control_area.width == 0);
    CHECK(same_rect(rows[4].control_area, {415, 323, 52, 16}));
    CHECK(same_rect(rows[4].lock_area, {259, 323, 148, 16}));
    CHECK(same_rect(rows[4].label, {158, 323, 93, 16}));
    CHECK(geometry::open_rows(dialog).limit == 80);

    // At the end: both lock texts, the status, the kept switch's captions
    // with no control, none of the strip's, and no control for either row.
    dialog.scroll[static_cast<std::size_t>(Page::graphics)] = 80;
    const auto parts = settings::dialog_layout(dialog);
    int32_t lock_texts = 0;
    for (const auto& part : parts) {
        lock_texts += part.text == "Not available here" ? 1 : 0;
        CHECK(part.control != settings::first_row_control + 3);
        CHECK(part.control != settings::first_row_control + 4);
    }
    CHECK(lock_texts == 2);
    CHECK(
        find_part(parts, "Not in use: no usable graphics card was found.", settings::no_control) !=
        nullptr
    );
    CHECK(find_part(parts, "Basic", settings::no_control) == nullptr);
    // A press where either control is, and every key, leaves them.
    CHECK(click(dialog, {460, 190}) == DialogAction::none);
    CHECK(click(dialog, {380, 190}) == DialogAction::none);
    CHECK(click(dialog, {460, 250}) == DialogAction::none);
    CHECK(dialog.chosen == current);
    for (const int32_t expected :
         {settings::first_row_control,
          settings::first_row_control + 1,
          settings::first_row_control + 2,
          settings::restore_control}) {
        CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
        CHECK(dialog.focused == expected);
    }
    // Restore defaults keeps both locked values.
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(
        dialog.chosen.hardware_acceleration == HardwareAcceleration::basic &&
        dialog.chosen.vertical_sync
    );

    // Under a flag the lock says so, and in a shared game Vertical sync is
    // locked during the game while Hardware acceleration can still be set.
    settings::GameState flagged{true, true, false, false};
    flagged.renderer_from_command_line = true;
    settings::Dialog under_flag = graphics_page({}, settings::settings_locks(flagged));
    under_flag.scroll[static_cast<std::size_t>(Page::graphics)] = 80;
    const auto flag_parts = settings::dialog_layout(under_flag);
    CHECK(find_part(flag_parts, "Set on the command line", settings::no_control) != nullptr);
    CHECK(find_part(flag_parts, "Locked during a game", settings::no_control) != nullptr);
    settings::Dialog shared =
        graphics_page({}, settings::settings_locks(settings::GameState{true, true, false, false}));
    shared.scroll[static_cast<std::size_t>(Page::graphics)] = 80;
    const auto shared_rows = geometry::open_rows(shared).rows.rows;
    CHECK(shared_rows[3].lock == Lock::none);
    CHECK(shared_rows[4].lock == Lock::in_game);
    CHECK(
        click(shared, {shared_rows[3].control_area.x + 48, shared_rows[3].control_area.y + 8}) ==
        DialogAction::changed
    );
    CHECK(shared.chosen.hardware_acceleration == HardwareAcceleration::basic);
    CHECK(
        click(shared, {shared_rows[3].control_area.x + 90, shared_rows[3].control_area.y + 8}) ==
        DialogAction::changed
    );
    CHECK(shared.chosen.hardware_acceleration == HardwareAcceleration::full);
}

void hardware_acceleration_shows_its_status() {
    using settings::AccelerationReach;
    using settings::AccelerationState;

    struct Expected {
        AccelerationState state;
        std::string_view first;
        std::string_view second;
    };

    constexpr std::string_view off = "Off: the processor draws and scales the view.";
    constexpr std::string_view processor = "The processor draws and scales the view.";
    constexpr std::string_view retry = "Set it to Off and back, or restore defaults.";
    constexpr std::string_view needs_memory = "Not in use: it needs at least 2 GB of memory.";
    constexpr std::string_view takes_effect = "takes effect from the next game.";
    const std::array<Expected, 19> fixed{{
        {AccelerationState::off_driver_skipped, off, "A failed graphics driver is skipped."},
        {AccelerationState::needs_memory_driver_skipped,
         needs_memory,
         "A failed graphics driver is skipped."},
        {AccelerationState::needs_memory, needs_memory, processor},
        {AccelerationState::off_by_setting, off, "Basic lets the graphics card scale it evenly."},
        {AccelerationState::off_by_command_line, off, "For this run only. The setting is kept."},
        {AccelerationState::environment_driver,
         "Not in use: the environment names a driver.",
         processor},
        {AccelerationState::too_little_memory,
         "Not in use: there is too little memory.",
         processor},
        {AccelerationState::waiting_for_game_end,
         "Off for this game: in a shared game, Basic",
         "takes effect from the next game."},
        {AccelerationState::engine_error, "Not in use: an error stopped it for this run.", retry},
        {AccelerationState::driver_failed, "Not in use: the graphics driver failed.", retry},
        {AccelerationState::game_stopped, "Not in use: the game stopped while using it.", retry},
        {AccelerationState::no_usable_card,
         "Not in use: no usable graphics card was found.",
         processor},
        {AccelerationState::lacks_feature,
         "Not in use: the graphics card lacks a feature.",
         processor},
        {AccelerationState::cannot_save, "Not in use: the game cannot save its files.", processor},
        {AccelerationState::slow_frames, "Not in use for this run: frames were slow.", processor},
        {AccelerationState::next_start,
         "Takes effect from the next start.",
         "The processor draws and scales the view until then."},
        {AccelerationState::full_stopped, "Basic in use: Full stopped for this run.", retry},
        {AccelerationState::full_failed_before,
         "Basic in use: Full failed before on this driver.",
         retry},
        {AccelerationState::full_waiting_for_game_end,
         "Basic for this game: in a shared game, Full",
         takes_effect},
    }};
    for (const auto& expected : fixed) {
        for (const auto& status : acceleration_statuses()) {
            // The wait names the level asked for: Basic here, Full below.
            if (status.state != expected.state || status.replay ||
                status.asked == HardwareAcceleration::full)
                continue;
            CHECK(geometry::status_line(status, 0) == expected.first);
            CHECK(geometry::status_line(status, 1) == expected.second);
            CHECK(geometry::status_line(status, 2).empty());
        }
    }
    // In a replay the wait names it, and Full where Full was asked for.
    settings::AccelerationStatus replay{AccelerationState::waiting_for_game_end, {}, true};
    CHECK(geometry::status_line(replay, 0) == "Off for this game: in a replay, Basic");
    CHECK(geometry::status_line(replay, 1) == "takes effect from the next game.");
    settings::AccelerationStatus full_wait{
        AccelerationState::waiting_for_game_end, {}, false, HardwareAcceleration::full
    };
    CHECK(geometry::status_line(full_wait, 0) == "Off for this game: in a shared game, Full");
    CHECK(geometry::status_line(full_wait, 1) == "takes effect from the next game.");
    full_wait.replay = true;
    CHECK(geometry::status_line(full_wait, 0) == "Off for this game: in a replay, Full");
    CHECK(geometry::status_line(full_wait, 1) == "takes effect from the next game.");
    // Basic running while Full waits names the match too.
    settings::AccelerationStatus basic_wait{
        AccelerationState::full_waiting_for_game_end, {}, true, HardwareAcceleration::full
    };
    CHECK(geometry::status_line(basic_wait, 0) == "Basic for this game: in a replay, Full");
    CHECK(geometry::status_line(basic_wait, 1) == takes_effect);
    // Every other state reads the same whichever level was asked for.
    for (const auto& status : acceleration_statuses()) {
        if (status.state == AccelerationState::waiting_for_game_end ||
            status.state == AccelerationState::full_waiting_for_game_end)
            continue;
        settings::AccelerationStatus other = status;
        other.asked = HardwareAcceleration::off;
        CHECK(geometry::status_line(status, 0) == geometry::status_line(other, 0));
        CHECK(geometry::status_line(status, 1) == geometry::status_line(other, 1));
    }
    // While it is in use the first line names Basic, the tier that runs, and
    // the second says what it does here; Full, which the game cannot draw
    // yet, or which stopped, says Basic is in use in its place.
    const std::array<std::pair<AccelerationState, std::string_view>, 9> in_use{{
        {AccelerationState::full_cannot_save, "Basic in use: the game cannot save its files."},
        {AccelerationState::full_too_little_memory,
         "Basic in use: there is too little memory for Full."},
        {AccelerationState::full_slow_frames,
         "Basic in use for this run: Full's frames were slow."},
        {AccelerationState::full_lacks_feature,
         "Basic in use: the card lacks a feature Full needs."},
        {AccelerationState::full_not_built, "Full is not in this build: Basic is in use."},
        {AccelerationState::in_use_on_another_driver,
         "Basic in use, on another driver: one failed."},
        {AccelerationState::in_use_less_smoothing,
         "Basic in use, with less smoothing: frames were slow."},
        {AccelerationState::in_use_no_smoothing,
         "Basic in use; no smoothing when zoomed out here."},
        {AccelerationState::in_use, "Basic in use."},
    }};
    const std::array<std::pair<AccelerationReach, std::string_view>, 5> reaches{{
        {AccelerationReach::menus, "It scales the menus and the interface evenly."},
        {AccelerationReach::zoomed_in, "It scales the interface and zoomed-in view evenly."},
        {AccelerationReach::zoomed_out, "It scales evenly and smooths the zoomed-out view."},
        {AccelerationReach::nearest_zoomed_out, "It smooths the zoomed-out view."},
        {AccelerationReach::nearest_none, "Here the view is drawn as when it is off."},
    }};
    for (const auto& [state, first] : in_use)
        for (const auto& [reach, second] : reaches) {
            const settings::AccelerationStatus status{state, reach, false};
            CHECK(geometry::status_line(status, 0) == first);
            CHECK(geometry::status_line(status, 1) == second);
        }
    // Full in use names its anti-aliasing on the second line, whatever the
    // reach: none, 2x or 4x; a count between reads as the one below it.
    const std::array<std::pair<AccelerationState, std::string_view>, 2> full_in_use{{
        {AccelerationState::full_in_use, "Full in use: the graphics card draws the view."},
        {AccelerationState::full_in_use_less_anti_aliasing,
         "Full in use, less anti-aliasing: frames were slow."},
    }};
    const std::array<std::pair<uint8_t, std::string_view>, 5> anti_aliasing{{
        {1, "Smoothed at every zoom."},
        {2, "Smoothed at every zoom; anti-aliasing 2x."},
        {3, "Smoothed at every zoom; anti-aliasing 2x."},
        {4, "Smoothed at every zoom; anti-aliasing 4x."},
        {0, "Smoothed at every zoom."},
    }};
    for (const auto& [state, first] : full_in_use)
        for (const auto& [reach, second] : reaches)
            for (const auto& [supersample, line] : anti_aliasing) {
                settings::AccelerationStatus status{state, reach, false};
                status.supersample = supersample;
                CHECK(geometry::status_line(status, 0) == first);
                CHECK(geometry::status_line(status, 1) == line);
            }
    // Every status has two lines, none empty.
    for (const auto& status : acceleration_statuses()) {
        CHECK(!geometry::status_line(status, 0).empty());
        CHECK(!geometry::status_line(status, 1).empty());
        CHECK(
            geometry::hint_line(Setting::hardware_acceleration, {}, status, 0) ==
            geometry::status_line(status, 0)
        );
    }
    CHECK(geometry::hint_line_count(Setting::hardware_acceleration) == 2);
    CHECK(geometry::hint_line_count(Setting::vertical_sync) == 1);
    CHECK(geometry::hint_is_status(Setting::hardware_acceleration));
    CHECK(!geometry::hint_is_status(Setting::vertical_sync));
    CHECK(geometry::lock_text(Lock::unavailable) == "Not available here");

    // The host gives the status at opening and each frame after; only a
    // change asks for a redraw, and the row shows it.
    settings::Dialog dialog = graphics_page({}, {}, {AccelerationState::off_by_setting, {}, false});
    dialog.scroll[static_cast<std::size_t>(Page::graphics)] = 80;
    CHECK(
        settings::set_acceleration_status(
            dialog, {AccelerationState::off_by_setting, AccelerationReach::menus, false}
        ) == DialogAction::none
    );
    CHECK(
        settings::set_acceleration_status(
            dialog, {AccelerationState::in_use, AccelerationReach::zoomed_out, false}
        ) == DialogAction::redraw
    );
    const auto parts = settings::dialog_layout(dialog);
    CHECK(find_part(parts, "Basic in use.", settings::no_control) != nullptr);
    CHECK(
        find_part(
            parts, "It scales evenly and smooths the zoomed-out view.", settings::no_control
        ) != nullptr
    );
    CHECK(
        settings::set_acceleration_status(
            dialog, {AccelerationState::waiting_for_game_end, AccelerationReach::zoomed_out, true}
        ) == DialogAction::redraw
    );
    CHECK(
        find_part(
            settings::dialog_layout(dialog),
            "Off for this game: in a replay, Basic",
            settings::no_control
        ) != nullptr
    );
    CHECK(
        settings::set_acceleration_status(
            dialog,
            {AccelerationState::waiting_for_game_end,
             AccelerationReach::zoomed_out,
             true,
             HardwareAcceleration::full}
        ) == DialogAction::redraw
    );
    CHECK(
        find_part(
            settings::dialog_layout(dialog),
            "Off for this game: in a replay, Full",
            settings::no_control
        ) != nullptr
    );
    CHECK(
        settings::set_acceleration_status(
            dialog,
            {AccelerationState::full_not_built,
             AccelerationReach::menus,
             false,
             HardwareAcceleration::full}
        ) == DialogAction::redraw
    );
    const auto full_parts = settings::dialog_layout(dialog);
    CHECK(
        find_part(
            full_parts, "Full is not in this build: Basic is in use.", settings::no_control
        ) != nullptr
    );
    CHECK(
        find_part(
            full_parts, "It scales the menus and the interface evenly.", settings::no_control
        ) != nullptr
    );
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
        if (page == Page::graphics)
            continue;
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

void the_graphics_page_draws_its_locked_rows() {
    const auto fonts = block_fonts();
    // At the top: the scroll bar, Hardware acceleration's strip whole, Off
    // chosen with the accent and Basic in the well, and its status cut at
    // 289.
    settings::Dialog top =
        graphics_page({}, {}, {settings::AccelerationState::off_by_setting, {}, false});
    Canvas at_top = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(at_top.surface, {0, 0, 1}, top, fonts);
    CHECK(at_top.at(473, 100) == kControlHover);
    CHECK(at_top.at(366, 266) == kAccent);
    CHECK(at_top.at(400, 266) == kWell);
    CHECK(at_top.at(434, 266) == kWell);
    CHECK(at_top.at(159, 284) == kHint);
    CHECK(at_top.at(159, 291) != kHint);

    // At the end, both locked Not available here: Hardware acceleration's
    // label line faded and its status at full strength, no strip; Vertical
    // sync faded whole, its switch kept and On without the accent.
    settings::Locks locks{};
    locks.hardware_acceleration = Lock::unavailable;
    locks.vertical_sync = Lock::unavailable;
    settings::EngineSettings current{};
    current.hardware_acceleration = HardwareAcceleration::full;
    current.vertical_sync = true;
    settings::Dialog dialog =
        graphics_page(current, locks, {settings::AccelerationState::no_usable_card, {}, false});
    dialog.scroll[static_cast<std::size_t>(Page::graphics)] = 80;
    Canvas canvas = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(canvas.surface, {0, 0, 1}, dialog, fonts);
    CHECK(canvas.at(159, 190) == faded(kText));          // its label
    CHECK(canvas.at(159, 205) == kHint);                 // its first status line
    CHECK(canvas.at(159, 217) == kHint);                 // its second
    CHECK(canvas.at(445, 185) == kPanel);                // no strip: neither its well
    CHECK(canvas.at(445, 199) == kPanel);                // nor its border
    CHECK(canvas.at(159, 249) == faded(kText));          // Vertical sync's label
    CHECK(canvas.at(159, 264) == faded(kHint));          // its hint
    CHECK(canvas.at(444, 245) == faded(kControlHover));  // its On, without the accent
    CHECK(canvas.at(415, 250) == faded(kControlBorder)); // its switch's border
    // Unlocked, Full and On show the accent, Full's level alone of the strip.
    settings::Dialog open =
        graphics_page(current, {}, {settings::AccelerationState::in_use, {}, false});
    open.scroll[static_cast<std::size_t>(Page::graphics)] = 80;
    Canvas unlocked = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(unlocked.surface, {0, 0, 1}, open, fonts);
    CHECK(unlocked.at(434, 186) == kAccent);
    CHECK(unlocked.at(400, 186) == kWell);
    CHECK(unlocked.at(366, 186) == kWell);
    CHECK(unlocked.at(444, 245) == kAccent);
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
    // Every status line, for every state, reach and replay, fits a hint
    // line's 309 columns; Hardware acceleration's label fits the 153 beside
    // its lock, and Vertical sync's the 93 beside its lock and kept switch.
    const auto small_width = [&](std::string_view text) {
        return static_cast<int32_t>(oa::formats::fnt::measure_text(fonts.small.font, text));
    };
    for (const auto& status : acceleration_statuses())
        for (std::size_t line = 0; line < 2; ++line) {
            const auto text = geometry::status_line(status, line);
            if (small_width(text) > geometry::content_width) {
                std::cerr << "'" << text << "' is " << small_width(text) << " wide\n";
                CHECK(small_width(text) <= geometry::content_width);
            }
        }
    CHECK(
        static_cast<int32_t>(
            oa::formats::fnt::measure_text(fonts.regular.font, "Hardware acceleration")
        ) <= 153
    );
    CHECK(
        static_cast<int32_t>(oa::formats::fnt::measure_text(fonts.regular.font, "Vertical sync")) <=
        93
    );
    for (const std::string_view text :
         {"Not in use: the game cannot save its files.",
          "Not in use: it needs at least 2 GB of memory.",
          "Full is not in this build: Basic is in use.",
          "Basic in use, on another driver: one failed.",
          "Basic in use, with less smoothing: frames were slow.",
          "Basic in use; no smoothing when zoomed out here.",
          "Basic in use.",
          "Off for this game: in a shared game, Basic",
          "It smooths the zoomed-out view.",
          "Here the view is drawn as when it is off.",
          "Each frame waits for the display: no tearing.",
          "Not available here",
          "Off",
          "Basic",
          "Full"})
        std::cout << "'" << text << "' is " << small_width(text) << " columns\n";
    // The Graphics page at every offset, under every lock and with every
    // status: every text it lists fits its place.
    const auto fits = [&](const settings::Dialog& dialog) {
        for (const auto& part : settings::dialog_layout(dialog)) {
            if (part.text.empty())
                continue;
            const auto& font =
                part.font == settings::DialogFont::regular ? fonts.regular.font : fonts.small.font;
            const auto width =
                static_cast<int32_t>(oa::formats::fnt::measure_text(font, part.text)) +
                part.tracking * static_cast<int32_t>(part.text.size() - 1);
            if (width > part.rect.width) {
                std::cerr << "'" << part.text << "' is " << width << " wide in a box "
                          << part.rect.width << " wide\n";
                CHECK(width <= part.rect.width);
            }
        }
    };
    for (const auto& locks : lock_states()) {
        settings::Dialog dialog = graphics_page({}, locks);
        for (int32_t scroll = 0; scroll <= 80; ++scroll) {
            dialog.scroll[static_cast<std::size_t>(Page::graphics)] = scroll;
            fits(dialog);
        }
    }
    for (const auto& status : acceleration_statuses()) {
        settings::GameState unavailable{};
        unavailable.acceleration_unavailable = true;
        unavailable.vertical_sync_unavailable = true;
        for (const auto& locks : {settings::Locks{}, settings::settings_locks(unavailable)}) {
            settings::Dialog dialog = graphics_page({}, locks, status);
            dialog.scroll[static_cast<std::size_t>(Page::graphics)] = 80;
            fits(dialog);
        }
    }
    // Enhanced anti-aliasing's hint in Full, at each factor and each level.
    for (const uint8_t supersample : {uint8_t{1}, uint8_t{2}, uint8_t{4}})
        for (const auto& state : level_states()) {
            settings::AccelerationStatus in_full{};
            in_full.state = settings::AccelerationState::in_use;
            in_full.asked = HardwareAcceleration::full;
            in_full.full_supersample = supersample;
            settings::Dialog dialog = graphics_page(state, {}, in_full);
            for (const int32_t scroll : {0, 80}) {
                dialog.scroll[static_cast<std::size_t>(Page::graphics)] = scroll;
                fits(dialog);
            }
        }

    for (const Lock lock :
         {Lock::in_game, Lock::set_by_host, Lock::command_line, Lock::unavailable}) {
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
        the_graphics_page_scrolls_its_five_rows();
        every_switch_reads_and_sets_through_one_table();
        the_new_rows_lock_in_their_own_forms();
        hardware_acceleration_shows_its_status();
        the_dialog_draws_its_faces_and_accents(settings::DialogFonts{});
        the_dialog_draws_the_scroll_bar_and_clips_the_rows();
        the_graphics_page_draws_its_locked_rows();
    }
    if (failures != 0)
        return 1;
    std::cout << "settings dialog: ok\n";
    return 0;
}
