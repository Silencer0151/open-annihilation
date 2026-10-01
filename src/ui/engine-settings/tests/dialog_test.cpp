// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings dialog: where its parts lie, its sections and rows, switches,
// sliders and their stops, the level strip, pointer and key events, OK,
// Cancel and Restore defaults, the locks and their texts, and what it draws.
// With --data, its fonts from the installed game and every text fitting its
// place.

#include "oa/ui/engine_settings/dialog.hpp"

#include "geometry.hpp"

#include "oa/test/game_assets.hpp"
#include "oa/test/game_data.hpp"

#include <cstddef>
#include <iostream>
#include <string>
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
                CHECK(rows.count == settings::page_settings(page).size());
                CHECK(rows.bottom < geometry::footer_rule_row);
                for (std::size_t index = 0; index < rows.count; ++index) {
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
    CHECK(settings::page_settings(Page::developer)[0] == settings::Setting::frame_stats);

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
          Expected{settings::Setting::max_frame_rate, 17}}) {
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
    geometry::set_stop(state, settings::Setting::max_frame_rate, 0);
    CHECK(state.max_frame_rate == 40);
    geometry::set_stop(state, settings::Setting::max_frame_rate, 16);
    CHECK(state.max_frame_rate == 120);
    // A limit off the slider's steps, such as an installation's 21, shows at
    // the nearest stop and keeps its value until moved.
    state.unit_limit = 21;
    CHECK(geometry::stop_of(state, settings::Setting::unit_limit) == 0);
    CHECK(geometry::value_text(settings::Setting::unit_limit, state) == "21 per player");
    state.unit_limit = 275;
    CHECK(geometry::stop_of(state, settings::Setting::unit_limit) == 5);
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
        the_dialog_draws_its_faces_and_accents(settings::DialogFonts{});
    }
    if (failures != 0)
        return 1;
    std::cout << "settings dialog: ok\n";
    return 0;
}
