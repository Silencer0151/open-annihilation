// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings dialog: where its parts lie, its sections and rows, switches,
// sliders and their stops, the level strip, pointer and key events, OK,
// Cancel and Restore defaults, the locks and their texts, and what it draws.
// The Graphics page's five rows scroll: Hardware acceleration, a strip of
// Off, Basic and Full whose hint lines are its status, and Vertical sync,
// each locked in its own form. Language's four switches and its Text
// size slider set the text style the drawing reads; Text size is locked
// while the modern fonts are off.
// A section of the test's own, taller than the view under the heading,
// checks scrolling: the view and its limit, the wheel, the scroll bar, the
// scroll keys, the focus brought into view, rows cut by the view, the
// control numbers, and the two forms of a locked switch. The Developer
// section: its two rows over Developer Mode's list of the standard hacks,
// the list's areas and hacks opening and closing, static while Off, each
// kind of parameter's control, the overrides it makes, Restore profile
// values, Show Active Only, its scrolling and its focus. The Touch section,
// listed only with touch controls: its place in the list, the entries'
// numbers kept with and without it, its rows and their values, and a
// finger's press taking the nearest control within reach. The Game files
// section, listed only where the host says the platform brings game files
// in: its place in the list and its entry's number, its rows (what is
// installed with MANAGE…, the backups switch with the device's name, where
// the files are), MANAGE… asking the host, and the switch. The Language
// dialog: its one section, Restore defaults restoring that section alone,
// and every text drawn in the modern fonts when its fonts hold no glyphs.
// Mods: its rows in order, the mod played first, each row's badge, title,
// version and description, long texts cut, the list scrolling under fixed
// buttons, the Switch Mod question by pointer and keys, the locks during a
// game and by the command line, and OPEN MODS FOLDER.
// With --data, its fonts from the installed game and every text fitting its
// place.

#include "oa/data/languages.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/ui/engine_settings/dialog.hpp"

#include "geometry.hpp"

#include "oa/data/mod_profile/overrides.hpp"
#include "oa/data/mod_profile/registry.hpp"
#include "oa/present/game_text.hpp"
#include "oa/ui/engine_settings/notice.hpp"
#include "oa/test/game_assets.hpp"
#include "oa/test/game_data.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
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

/// No icon: the dialog's header and the OA button draw the OA mark.
const renderer::RgbaPicture kNoIcon{};

using settings::DialogAction;
using settings::DialogKey;
using settings::HardwareAcceleration;
using settings::Lock;
using settings::Page;
using settings::Setting;

/// The engine's sections, in the order the list shows them.
constexpr std::array<Page, 6> kPages{
    Page::mods,
    Page::controls,
    Page::common_tweaks,
    Page::language,
    Page::graphics,
    Page::developer,
};

/// The engine's sections while the game has touch controls: Touch between
/// Graphics and Developer.
constexpr std::array<Page, 7> kTouchPages{
    Page::mods,
    Page::controls,
    Page::common_tweaks,
    Page::language,
    Page::graphics,
    Page::touch,
    Page::developer,
};

/// The engine's sections in the main menu's dialog of a game that brings
/// game files in: Game files between Graphics and Developer.
constexpr std::array<Page, 7> kGameFilesPages{
    Page::mods,
    Page::controls,
    Page::common_tweaks,
    Page::language,
    Page::graphics,
    Page::game_files,
    Page::developer,
};

/// The engine's sections with Touch and Game files both listed.
constexpr std::array<Page, 8> kTouchGameFilesPages{
    Page::mods,
    Page::controls,
    Page::common_tweaks,
    Page::language,
    Page::graphics,
    Page::touch,
    Page::game_files,
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

/// Returns a dialog of the engine's settings opened on a section, with or
/// without touch controls.
settings::Dialog opened_with_touch(Page page, bool touch = true) {
    settings::Dialog dialog;
    settings::open_dialog(
        dialog,
        settings::EngineSettings{},
        settings::EngineSettings{},
        {},
        "v0.2.0",
        page,
        {},
        settings::highest_unit_limit,
        {},
        {},
        nullptr,
        touch
    );
    return dialog;
}

/// Returns a dialog of the engine's settings opened on a section with the
/// Game files section listed, with or without touch controls, its rows
/// filled as a host fills them.
settings::Dialog opened_with_game_files(Page page, bool touch = false) {
    settings::Dialog dialog;
    settings::open_dialog(
        dialog,
        settings::EngineSettings{},
        settings::EngineSettings{},
        {},
        "v0.2.0",
        page,
        {},
        settings::highest_unit_limit,
        {},
        {},
        nullptr,
        touch,
        true
    );
    dialog.game_files_summary = "3.1c · Core Contingency · Battle Tactics · music";
    dialog.game_files_sizes = "1.1 GB · 37 GB free on this tablet";
    dialog.game_files_location = "In the file manager: Open Annihilation › Total Annihilation";
    dialog.game_files_device = "tablet";
    return dialog;
}

/// The list's entries as the dialog lists them: each entry's control and
/// its rectangle, top to bottom.
std::vector<std::pair<int32_t, renderer::SourceRect>> list_entries(const settings::Dialog& dialog) {
    std::vector<std::pair<int32_t, renderer::SourceRect>> entries;
    for (const auto& part : settings::dialog_layout(dialog))
        if (part.control >= settings::first_page_control &&
            part.control < settings::restore_control)
            entries.emplace_back(part.control, part.rect);
    return entries;
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

/// Tells whether two rectangles are the same.
bool same_entry(const renderer::SourceRect& a, const renderer::SourceRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
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
    // The list names the sections in order, Developer last under a divider.
    const auto engine = settings::dialog_pages(settings::DialogKind::engine);
    CHECK(std::equal(engine.begin(), engine.end(), kPages.begin(), kPages.end()));
    // Each entry's number is its place in the list with Touch, which keeps
    // its number listed or not: Developer is 6.
    for (std::size_t index = 0; index < kPages.size(); ++index)
        CHECK(
            settings::page_control(kPages[index]) ==
            static_cast<int32_t>(kPages[index] == Page::developer ? index + 1 : index)
        );
    for (std::size_t index = 1; index < kPages.size(); ++index) {
        const auto above = geometry::list_item(kPages[index - 1]);
        CHECK(above.y + above.height <= geometry::list_item(kPages[index]).y);
    }
    const auto divider = geometry::list_divider();
    const auto graphics_entry = geometry::list_item(Page::graphics);
    CHECK(graphics_entry.y + graphics_entry.height <= divider.y);
    CHECK(divider.y + divider.height <= geometry::list_item(Page::developer).y);

    CHECK(settings::page_settings(Page::mods).size() == 1);
    CHECK(settings::page_settings(Page::mods)[0] == Setting::mod);
    CHECK(settings::page_settings(Page::controls).size() == 3);
    CHECK(settings::page_settings(Page::controls)[0] == Setting::wheel_zoom);
    CHECK(settings::page_settings(Page::controls)[1] == Setting::escape_opens_menu);
    CHECK(settings::page_settings(Page::controls)[2] == Setting::switch_alt);
    // Common Tweaks: Your files first, then the unit limit and pathfinding.
    CHECK(settings::page_settings(Page::common_tweaks).size() == 3);
    CHECK(settings::page_settings(Page::common_tweaks)[0] == Setting::user_folder);
    CHECK(settings::page_settings(Page::common_tweaks)[1] == Setting::unit_limit);
    CHECK(settings::page_settings(Page::common_tweaks)[2] == Setting::path_search);
    CHECK(settings::page_settings(Page::language)[0] == Setting::language);
    CHECK(settings::page_settings(Page::graphics)[0] == Setting::max_frame_rate);
    CHECK(settings::page_settings(Page::graphics)[1] == Setting::anti_aliasing);
    CHECK(settings::page_settings(Page::graphics).size() == 5);
    CHECK(settings::page_settings(Page::graphics)[2] == Setting::screen_size);
    CHECK(settings::page_settings(Page::graphics)[3] == Setting::hardware_acceleration);
    CHECK(settings::page_settings(Page::graphics)[4] == Setting::vertical_sync);
    CHECK(settings::page_settings(Page::developer).size() == 2);
    CHECK(settings::page_settings(Page::developer)[0] == Setting::developer_mode);
    CHECK(settings::page_settings(Page::developer)[1] == Setting::frame_stats);
    // Graphics' five rows are taller than the view, by 80 rows.
    const auto graphics = geometry::place_rows(Page::graphics, {});
    CHECK(graphics.rows.size() == 5);
    CHECK(geometry::scroll_limit(geometry::content_height(graphics, 0)) == 80);

    auto parts = settings::dialog_layout(opened(Page::controls));
    for (const std::string_view text :
         {"OPEN ANNIHILATION",
          "SETTINGS",
          "v0.2.0",
          "Mods",
          "Controls",
          "Common Tweaks",
          "Language",
          "Graphics",
          "Developer",
          "CONTROLS",
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
    // Each entry is its section's control.
    for (const Page page : kPages) {
        const auto* entry = find_part(parts, {}, settings::page_control(page));
        CHECK(entry != nullptr && entry->rect.y == geometry::list_item(page).y);
    }
    // Common Tweaks shows its heading and its rows' labels in order.
    parts = settings::dialog_layout(opened(Page::common_tweaks));
    CHECK(find_part(parts, "COMMON TWEAKS", settings::no_control) != nullptr);
    const auto tweaks = geometry::place_rows(Page::common_tweaks, {});
    CHECK(tweaks.rows.size() == 3);
    CHECK(tweaks.rows[0].label.y < tweaks.rows[1].label.y);
    CHECK(tweaks.rows[1].label.y < tweaks.rows[2].label.y);
}

void a_click_on_an_entry_shows_its_section() {
    settings::Dialog dialog = opened(Page::common_tweaks);
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
    const auto stats = geometry::place_rows(Page::developer, {}).rows[1].control_area;
    CHECK(click(developer, {stats.x + stats.width - 4, stats.y + 4}) == DialogAction::changed);
    CHECK(developer.chosen.frame_stats && !developer.chosen.developer_mode);
}

void language_and_text_shows_a_language_four_switches_and_a_size() {
    const auto rows = settings::page_settings(Page::language);
    CHECK(rows.size() == 6);
    CHECK(rows[0] == Setting::language);
    CHECK(rows[1] == Setting::modern_fonts);
    CHECK(rows[2] == Setting::text_size);
    CHECK(rows[3] == Setting::text_outline);
    CHECK(rows[4] == Setting::text_shadow);
    CHECK(rows[5] == Setting::text_background);
    for (const Setting setting : rows)
        CHECK(
            setting == Setting::language ? geometry::is_choice(setting)
            : setting == Setting::text_size
                ? geometry::is_slider(setting)
                : geometry::is_switch(setting) && !geometry::is_slider(setting) &&
                      !geometry::is_choice(setting)
        );
    // Its entry comes after Common Tweaks and before Graphics, over the line
    // above Developer.
    const auto entry = geometry::list_item(Page::language);
    CHECK(settings::page_control(Page::language) == 3);
    CHECK(entry.y > geometry::list_item(Page::common_tweaks).y);
    CHECK(entry.y < geometry::list_item(Page::graphics).y);
    CHECK(entry.y + entry.height < geometry::list_divider().y);
    CHECK(geometry::list_divider().y < geometry::list_item(Page::developer).y);

    // The player's own defaults: the system's language, modern fonts,
    // outline and shadow On, the background Off, the text at 80%.
    settings::Inputs own{};
    own.players_own_profile = true;
    const auto defaults = settings::default_settings(own);
    CHECK(defaults.language == "system");
    settings::Dialog dialog;
    settings::open_dialog(dialog, defaults, defaults, {}, "v0.2.0", Page::language);
    const auto parts = settings::dialog_layout(dialog);
    for (const std::string_view text :
         {"Language",
          "LANGUAGE",
          "Language",
          "The game's own text and unit names, where its",
          "data has them in the language.",
          "System default (English)",
          "Use modern fonts for game text",
          "Modern fonts for in-game text,",
          "including internationalization.",
          "Text size",
          "The size of game text in the modern fonts.",
          "Larger sizes are easier to read.",
          "80%"})
        CHECK(find_part(parts, text, settings::no_control) != nullptr);
    // The six are taller than the view by 129 rows: the switches under Text
    // size show as the section scrolls, under its scroll bar.
    CHECK(geometry::open_rows(dialog).limit == 129);
    CHECK(find_part(parts, {}, settings::scroll_bar_control) != nullptr);
    dialog.scroll[static_cast<std::size_t>(Page::language)] = 129;
    for (const std::string_view text :
         {"Font outline",
          "A dark edge round each letter of modern text.",
          "Font shadow",
          "A dark shadow under modern text.",
          "Game text background",
          "A shaded box behind each line of game text."})
        CHECK(find_part(settings::dialog_layout(dialog), text, settings::no_control) != nullptr);
    dialog.scroll[static_cast<std::size_t>(Page::language)] = 0;
    const auto placed = geometry::place_rows(Page::language, {});
    CHECK(placed.rows[0].hint_lines == 2 && placed.rows[1].hint_lines == 2);
    CHECK(placed.rows[2].hint_lines == 2);
    for (std::size_t row = 3; row < placed.rows.size(); ++row)
        CHECK(placed.rows[row].hint_lines == 1);
    // No game locks a Language row; only the dialog's own lock on
    // Text size, while it shows the modern fonts Off, and the command
    // line's on the language.
    for (const auto& locks : lock_states())
        for (const auto& row : geometry::place_rows(Page::language, locks).rows)
            CHECK(row.lock == Lock::none);
    for (const auto& row : geometry::open_rows(dialog).rows.rows)
        CHECK(row.lock == Lock::none);

    // A click on each switch's Off half sets it Off, and its On half On;
    // the text style follows at once.
    const auto off_of = [](const renderer::SourceRect& area) {
        return Point{area.x + 4, area.y + area.height / 2};
    };
    const auto on_of = [](const renderer::SourceRect& area) {
        return Point{area.x + area.width - 4, area.y + area.height / 2};
    };
    CHECK(settings::text_style(dialog.chosen) == oa::present::TextStyle{});
    CHECK(click(dialog, off_of(placed.rows[1].control_area)) == DialogAction::changed);
    CHECK(!dialog.chosen.modern_fonts && !settings::text_style(dialog.chosen).modern_fonts);
    dialog.scroll[static_cast<std::size_t>(Page::language)] = 129;
    const auto scrolled = geometry::open_rows(dialog).rows.rows;
    CHECK(click(dialog, off_of(scrolled[3].control_area)) == DialogAction::changed);
    CHECK(!settings::text_style(dialog.chosen).outline);
    CHECK(click(dialog, off_of(scrolled[4].control_area)) == DialogAction::changed);
    CHECK(!settings::text_style(dialog.chosen).shadow);
    CHECK(click(dialog, on_of(scrolled[5].control_area)) == DialogAction::changed);
    CHECK(settings::text_style(dialog.chosen).background);
    CHECK(click(dialog, on_of(scrolled[5].control_area)) == DialogAction::redraw);
    dialog.scroll[static_cast<std::size_t>(Page::language)] = 0;
    const oa::present::TextStyle plain_text{
        .modern_fonts = false,
        .outline = false,
        .shadow = false,
        .background = true,
        .size = settings::default_text_size,
    };
    CHECK(settings::text_style(dialog.chosen) == plain_text);
    // The rest of the settings stay as they were.
    auto others = dialog.chosen;
    others.modern_fonts = defaults.modern_fonts;
    others.text_outline = defaults.text_outline;
    others.text_shadow = defaults.text_shadow;
    others.text_background = defaults.text_background;
    CHECK(others == defaults);

    // Keys: the focus starts on the language; Space flips the focused
    // switch, Left sets Off and Right On; with the modern fonts Off the
    // focus passes over Text size.
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_row_control);
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_row_control + 1);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_row_control + 3);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::changed);
    CHECK(dialog.chosen.text_outline);
    CHECK(settings::dialog_key(dialog, DialogKey::up) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::changed);
    CHECK(dialog.chosen.modern_fonts);
    // With them On, Down reaches Text size, whose arrows step 10%.
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_row_control + 2);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::changed);
    CHECK(dialog.chosen.text_size == 90 && settings::text_style(dialog.chosen).size == 90);
    CHECK(find_part(settings::dialog_layout(dialog), "90%", settings::no_control) != nullptr);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::changed);
    CHECK(dialog.chosen.text_size == settings::default_text_size);
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::changed);
    CHECK(dialog.chosen.text_shadow);
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::changed);
    CHECK(!dialog.chosen.text_background);
    CHECK(dialog.chosen == defaults);

    // Restore defaults puts them back, and Cancel what the dialog opened with.
    settings::EngineSettings plain = defaults;
    plain.language = "it";
    plain.modern_fonts = false;
    plain.text_shadow = false;
    plain.text_background = true;
    plain.text_size = 150;
    settings::open_dialog(dialog, plain, defaults, {}, "v0.2.0", Page::language);
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.chosen == defaults);
    CHECK(click(dialog, centre(geometry::cancel_button)) == DialogAction::cancelled);
    CHECK(dialog.chosen == plain);
}

/// Opens the dialog on Language with the player's own defaults and
/// German as the operating system's language.
///
/// @return the dialog
settings::Dialog language_dialog() {
    settings::Inputs own{};
    own.players_own_profile = true;
    const auto defaults = settings::default_settings(own);
    settings::Dialog dialog;
    settings::open_dialog(
        dialog,
        defaults,
        defaults,
        {},
        "v0.2.0",
        Page::language,
        {},
        settings::highest_unit_limit,
        {},
        {},
        oa::data::languages::find_by_tag("de")
    );
    return dialog;
}

void language_drop_down_names_each_language_in_itself() {
    // System default first, naming the system's language in itself, then
    // English and the others in the order of their own names.
    auto dialog = language_dialog();
    CHECK(geometry::choice_count(dialog, Setting::language) == 6);
    const std::array<std::string_view, 6> names{
        "System default (Deutsch)",
        "English",
        "Deutsch",
        "Espa\xC3\xB1"
        "ol",
        "Fran\xC3\xA7"
        "ais",
        "Italiano",
    };
    for (std::size_t index = 0; index < names.size(); ++index)
        CHECK(geometry::choice_text(dialog, Setting::language, index) == names[index]);
    settings::Dialog english = dialog;
    english.system_language = nullptr;
    CHECK(geometry::choice_text(english, Setting::language, 0) == "System default (English)");
    CHECK(geometry::choice_text(dialog, Setting::language, 6).empty());
    CHECK(geometry::choice_count(dialog, Setting::modern_fonts) == 0);
    // Each choice keeps its tag, and a tag not offered shows System default.
    settings::Dialog state = dialog;
    const std::array<std::string_view, 6> tags{"system", "en", "de", "es", "fr", "it"};
    for (std::size_t index = 0; index < tags.size(); ++index) {
        geometry::set_choice(state, Setting::language, index);
        CHECK(state.chosen.language == tags[index]);
        CHECK(geometry::choice_index(state, Setting::language) == index);
    }
    geometry::set_choice(state, Setting::language, 99);
    CHECK(state.chosen.language == "it");
    state.chosen.language = "pt";
    CHECK(geometry::choice_index(state, Setting::language) == 0);

    // The row: its label and two hint lines, and the field on a line of its
    // own showing the choice, its list closed.
    const auto open = geometry::open_rows(dialog);
    const auto& row = open.rows.rows[0];
    CHECK(row.setting == Setting::language && row.control == settings::first_row_control);
    CHECK(row.control_area.x == geometry::content_left);
    CHECK(row.control_area.width == geometry::choice_width);
    CHECK(row.control_area.height == geometry::choice_line_height);
    CHECK(row.control_area.y > row.hints[1].y);
    const auto parts = settings::dialog_layout(dialog);
    const auto* field = find_part(parts, "System default (Deutsch)", settings::no_control);
    CHECK(field != nullptr && field->control == row.control);
    CHECK(find_part(parts, "Italiano", settings::no_control) == nullptr);
}

void language_drop_down_opens_marks_and_chooses() {
    auto dialog = language_dialog();
    const auto field = geometry::open_rows(dialog).rows.rows[0].control_area;
    const auto list = geometry::choice_list(field, 6);
    // A list of six under its field, as wide, one item a line.
    CHECK(list.x == field.x && list.y == field.y + field.height && list.width == field.width);
    CHECK(list.height == 6 * geometry::choice_item_height + 2);
    CHECK(list.y + list.height <= geometry::footer_rule_row);
    const auto item = [&](int32_t shown) { return centre(geometry::choice_item(list, shown)); };

    // A click on the field opens the list, marking the choice.
    CHECK(click(dialog, centre(field)) == DialogAction::redraw);
    CHECK(dialog.open_list == settings::first_row_control);
    CHECK(dialog.list_marked == 0 && dialog.list_first == 0);
    // Open, its items are listed and what lies under them is not.
    auto parts = settings::dialog_layout(dialog);
    for (const std::string_view text : {"English", "Deutsch", "Italiano"}) {
        const auto* part = find_part(parts, text, settings::no_control);
        CHECK(part != nullptr && inside(part->rect, list));
    }
    CHECK(find_part(parts, "Use modern fonts for game text", settings::no_control) == nullptr);
    for (std::size_t a = 0; a < parts.size(); ++a)
        for (std::size_t b = a + 1; b < parts.size(); ++b)
            CHECK(!overlap(parts[a].rect, parts[b].rect));
    // The pointer marks the item under it; a press and a release on one
    // choose it, close the list and put the language in effect.
    CHECK(settings::dialog_pointer_move(dialog, item(4).x, item(4).y) == DialogAction::redraw);
    CHECK(dialog.list_marked == 4);
    CHECK(settings::dialog_pointer_move(dialog, item(4).x, item(4).y + 1) == DialogAction::none);
    CHECK(click(dialog, item(4)) == DialogAction::changed);
    CHECK(dialog.chosen.language == "fr" && dialog.open_list == settings::no_control);
    CHECK(
        find_part(
            settings::dialog_layout(dialog),
            "Fran\xC3\xA7"
            "ais",
            settings::no_control
        ) != nullptr
    );
    // A press off the list closes it and does nothing more, even on a
    // button; a release on another item than the one pressed chooses nothing.
    CHECK(click(dialog, centre(field)) == DialogAction::redraw);
    CHECK(dialog.list_marked == 4);
    CHECK(
        settings::dialog_pointer_down(
            dialog, centre(geometry::restore_button).x, centre(geometry::restore_button).y
        ) == DialogAction::redraw
    );
    CHECK(dialog.open_list == settings::no_control && !dialog.restored);
    CHECK(
        settings::dialog_pointer_up(
            dialog, centre(geometry::restore_button).x, centre(geometry::restore_button).y
        ) == DialogAction::none
    );
    CHECK(!dialog.restored && dialog.chosen.language == "fr");
    CHECK(click(dialog, centre(field)) == DialogAction::redraw);
    CHECK(settings::dialog_pointer_down(dialog, item(1).x, item(1).y) == DialogAction::redraw);
    CHECK(settings::dialog_pointer_up(dialog, item(2).x, item(2).y) == DialogAction::redraw);
    CHECK(dialog.chosen.language == "fr" && dialog.open_list == settings::first_row_control);
    // A press on the field itself closes the open list, and its release
    // does nothing.
    CHECK(click(dialog, centre(field)) == DialogAction::none);
    CHECK(dialog.open_list == settings::no_control);
    // The wheel over an open list of six moves neither it nor the section.
    CHECK(click(dialog, centre(field)) == DialogAction::redraw);
    CHECK(settings::dialog_wheel(dialog, item(2).x, item(2).y, -1.0F) == DialogAction::none);
    CHECK(dialog.scroll[static_cast<std::size_t>(Page::language)] == 0);
    CHECK(dialog.list_first == 0);

    // Keys work the open list: Up and Down mark, Home and End the ends;
    // Escape closes it unchanged and leaves the dialog open.
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(dialog.list_marked == 5);
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::none);
    CHECK(settings::dialog_key(dialog, DialogKey::home) == DialogAction::redraw);
    CHECK(dialog.list_marked == 0);
    CHECK(settings::dialog_key(dialog, DialogKey::up) == DialogAction::none);
    CHECK(settings::dialog_key(dialog, DialogKey::end) == DialogAction::redraw);
    CHECK(dialog.list_marked == 5);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::none);
    CHECK(settings::dialog_key(dialog, DialogKey::escape) == DialogAction::redraw);
    CHECK(dialog.open_list == settings::no_control && dialog.chosen.language == "fr");
    // Closed, the focused field takes Space to open, Left and Right to step,
    // and Enter is the dialog's OK.
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_row_control);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::changed);
    CHECK(dialog.chosen.language == "it");
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::changed);
    CHECK(dialog.chosen.language == "fr");
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::redraw);
    CHECK(dialog.open_list == settings::first_row_control && dialog.list_marked == 4);
    CHECK(settings::dialog_key(dialog, DialogKey::up) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::up) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::enter) == DialogAction::changed);
    CHECK(dialog.chosen.language == "de" && dialog.open_list == settings::no_control);
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::redraw);
    CHECK(dialog.chosen.language == "de" && dialog.open_list == settings::no_control);
    // Tab closes an open list and moves the focus on.
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(dialog.open_list == settings::no_control);
    CHECK(dialog.focused == settings::first_row_control + 1);
    // A press on another section's entry only closes an open list; the next
    // press opens the section.
    CHECK(settings::dialog_key(dialog, DialogKey::back_tab) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::redraw);
    CHECK(click(dialog, centre(geometry::list_item(Page::graphics))) == DialogAction::none);
    CHECK(dialog.open_list == settings::no_control && dialog.page == Page::language);
    CHECK(click(dialog, centre(geometry::list_item(Page::graphics))) == DialogAction::redraw);
    CHECK(dialog.page == Page::graphics);
    CHECK(click(dialog, centre(geometry::list_item(Page::language))) == DialogAction::redraw);
    CHECK(dialog.open_list == settings::no_control);
    // Enter with the list closed keeps the choice; Restore defaults puts
    // System default back and Cancel what the dialog opened with.
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.chosen.language == "system");
    CHECK(settings::dialog_key(dialog, DialogKey::enter) == DialogAction::accepted);
    auto cancelled = language_dialog();
    CHECK(click(cancelled, centre(field)) == DialogAction::redraw);
    CHECK(click(cancelled, item(3)) == DialogAction::changed);
    CHECK(cancelled.chosen.language == "es");
    CHECK(click(cancelled, centre(geometry::cancel_button)) == DialogAction::cancelled);
    CHECK(cancelled.chosen.language == "system");
}

void language_drop_down_locks_by_the_command_line() {
    // 3.1c's command line naming a language decides it for the run: the row
    // shows its lock, takes no press and no focus, and keeps its choice.
    settings::GameState state{};
    state.language_from_command_line = true;
    auto dialog = language_dialog();
    dialog.locks = settings::settings_locks(state);
    const auto open = geometry::open_rows(dialog);
    const auto& row = open.rows.rows[0];
    CHECK(row.lock == Lock::command_line);
    CHECK(row.lock_area.width > 0 && row.lock_area.y == row.label.y);
    CHECK(
        find_part(
            settings::dialog_layout(dialog), "Set on the command line", settings::no_control
        ) != nullptr
    );
    CHECK(click(dialog, centre(row.control_area)) == DialogAction::none);
    CHECK(dialog.open_list == settings::no_control);
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_row_control + 1);
    // Restore defaults keeps a locked choice.
    dialog.chosen.language = "es";
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.chosen.language == "es");
}

void drop_down_lists_scroll_and_open_over_their_field() {
    // A list of more items than it shows is as tall as most_shown_choices,
    // and opens over its field when it would reach below the footer's line.
    CHECK(geometry::shown_choices(3) == 3 && geometry::shown_choices(12) == 8);
    const renderer::SourceRect high{geometry::content_left, 80, geometry::choice_width, 16};
    const auto under = geometry::choice_list(high, 12);
    CHECK(under.y == 96 && under.height == 8 * geometry::choice_item_height + 2);
    const renderer::SourceRect low{geometry::content_left, 250, geometry::choice_width, 16};
    const auto over = geometry::choice_list(low, 12);
    CHECK(over.y + over.height == low.y && over.height == under.height);
    CHECK(over.y >= geometry::body_top);
    for (int32_t shown = 0; shown < 8; ++shown) {
        const auto item = geometry::choice_item(over, shown);
        CHECK(inside(item, over));
        CHECK(item.height == geometry::choice_item_height);
    }
}

/// Counts a text's characters, not its bytes, one source pixel each.
int32_t one_a_character(std::string_view text) {
    int32_t characters = 0;
    for (const char byte : text)
        if ((static_cast<unsigned char>(byte) & 0xC0U) != 0x80U)
            ++characters;
    return characters;
}

void path_tails_keep_the_last_components_that_fit() {
    const auto bytes = [](std::string_view text) { return static_cast<int32_t>(text.size()); };
    // A path that fits shows whole.
    CHECK(settings::path_tail("/a/b", 10, bytes) == "/a/b");
    // Else "..." and the most whole components that fit, their separator kept.
    CHECK(settings::path_tail("/Users/player/TA/mods/My Mod", 15, bytes) == ".../mods/My Mod");
    CHECK(settings::path_tail("/Users/player/TA/mods/My Mod", 14, bytes) == ".../My Mod");
    CHECK(settings::path_tail("C:\\Games\\TA\\mods\\Mod", 12, bytes) == "...\\mods\\Mod");
    // A separator at its end is dropped.
    CHECK(settings::path_tail("/a/b/", 10, bytes) == "/a/b");
    // A last component too long alone keeps as much of its end as fits,
    // whole characters only.
    CHECK(settings::path_tail("/x/abcdefghij", 8, bytes) == "...fghij");
    CHECK(
        settings::path_tail("/x/\xC3\xA9\xC3\xA9\xC3\xA9", 5, one_a_character) ==
        "...\xC3\xA9\xC3\xA9"
    );
    // Room for nothing more shows "..." alone.
    CHECK(settings::path_tail("/x/abcdefghij", 2, bytes) == "...");
    CHECK(settings::path_tail({}, 2, bytes).empty());
}

void text_size_runs_from_half_to_three_times_in_tenths() {
    // 50% to 300% of the game fonts' sizes in steps of 10%: 26 stops, the
    // default 80% the fourth.
    CHECK(settings::lowest_text_size == 50 && settings::highest_text_size == 300);
    CHECK(settings::text_size_step == 10 && settings::default_text_size == 80);
    CHECK(geometry::slider_of(Setting::text_size).stops == 26);
    settings::EngineSettings state{};
    CHECK(state.text_size == 80 && geometry::stop_of(state, Setting::text_size) == 3);
    CHECK(geometry::value_text(Setting::text_size, state) == "80%");
    const renderer::SourceRect track{100, 50, 189, geometry::slider_line_height};
    for (int32_t stop = 0; stop < 26; ++stop) {
        geometry::set_stop(state, Setting::text_size, stop);
        CHECK(state.text_size == 50 + 10 * stop);
        CHECK(geometry::stop_of(state, Setting::text_size) == stop);
        CHECK(
            geometry::value_text(Setting::text_size, state) == std::to_string(50 + 10 * stop) + "%"
        );
        CHECK(geometry::stop_at(track, geometry::knob_column(track, stop, 26), 26) == stop);
    }
    geometry::set_stop(state, Setting::text_size, 99);
    CHECK(state.text_size == 300);
    geometry::set_stop(state, Setting::text_size, -4);
    CHECK(state.text_size == 50);
    // A size off the steps, as a file may hold, shows at the nearest stop
    // and keeps its value until moved.
    state.text_size = 84;
    CHECK(geometry::stop_of(state, Setting::text_size) == 3);
    CHECK(geometry::value_text(Setting::text_size, state) == "84%");
    state.text_size = 85;
    CHECK(geometry::stop_of(state, Setting::text_size) == 4);
    // Its row: a slider under a label and two hint lines, which the
    // drag sets as every slider's does.
    settings::Inputs own{};
    own.players_own_profile = true;
    const auto defaults = settings::default_settings(own);
    settings::Dialog dialog;
    settings::open_dialog(dialog, defaults, defaults, {}, "v0.2.0", Page::language);
    const auto row = geometry::open_rows(dialog).rows.rows[2];
    CHECK(row.setting == Setting::text_size && row.control == settings::first_row_control + 2);
    CHECK(
        settings::dialog_pointer_down(
            dialog, row.control_area.x + row.control_area.width - 1, row.control_area.y + 4
        ) == DialogAction::changed
    );
    CHECK(dialog.chosen.text_size == 300);
    CHECK(
        settings::dialog_pointer_move(dialog, row.control_area.x - 30, row.control_area.y) ==
        DialogAction::changed
    );
    CHECK(dialog.chosen.text_size == 50);
    static_cast<void>(settings::dialog_pointer_up(dialog, 0, 0));
    CHECK(find_part(settings::dialog_layout(dialog), "50%", settings::no_control) != nullptr);
}

void text_size_waits_for_the_modern_fonts() {
    // With the modern fonts Off the slider is locked: faded, its padlock
    // saying it needs them, its second hint line that the game's own fonts
    // have fixed sizes, and it takes no press and no focus.
    settings::EngineSettings plain{};
    CHECK(!plain.modern_fonts);
    settings::Dialog dialog;
    settings::open_dialog(dialog, plain, plain, {}, "v0.2.0", Page::language);
    CHECK(geometry::shown_locks(dialog).text_size == Lock::needs_modern_fonts);
    const auto open = geometry::open_rows(dialog);
    const auto& row = open.rows.rows[2];
    CHECK(row.setting == Setting::text_size && row.lock == Lock::needs_modern_fonts);
    CHECK(row.lock_area.width > 0 && row.lock_area.y == row.label.y);
    const auto parts = settings::dialog_layout(dialog);
    for (const std::string_view text :
         {"Text size",
          "Needs modern fonts",
          "The size of game text in the modern fonts.",
          "The game's own fonts have fixed sizes.",
          "80%"})
        CHECK(find_part(parts, text, settings::no_control) != nullptr);
    CHECK(find_part(parts, "Larger sizes are easier to read.", settings::no_control) == nullptr);
    for (const auto& part : parts)
        CHECK(part.control != row.control);
    CHECK(geometry::lock_text(Lock::needs_modern_fonts) == "Needs modern fonts");
    CHECK(
        settings::dialog_pointer_down(
            dialog, row.control_area.x + row.control_area.width - 1, row.control_area.y + 4
        ) == DialogAction::none
    );
    static_cast<void>(settings::dialog_pointer_up(dialog, 0, 0));
    CHECK(dialog.chosen.text_size == settings::default_text_size);
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_row_control + 3);

    // Turning them On lifts the lock at once, and Off puts it back; the
    // size keeps its value through both.
    const auto modern_on = geometry::open_rows(dialog).rows.rows[1].control_area;
    CHECK(
        click(dialog, {modern_on.x + modern_on.width - 4, modern_on.y + 4}) == DialogAction::changed
    );
    CHECK(geometry::shown_locks(dialog).text_size == Lock::none);
    CHECK(geometry::open_rows(dialog).rows.rows[2].lock == Lock::none);
    CHECK(
        find_part(settings::dialog_layout(dialog), "Needs modern fonts", settings::no_control) ==
        nullptr
    );
    CHECK(
        find_part(
            settings::dialog_layout(dialog),
            "Larger sizes are easier to read.",
            settings::no_control
        ) != nullptr
    );
    // A lock the game puts on Text size is kept, whatever the switch.
    dialog.locks.text_size = Lock::in_game;
    CHECK(geometry::shown_locks(dialog).text_size == Lock::in_game);
    dialog.locks.text_size = Lock::none;
    // Restore defaults resets the size even while it is locked by the
    // modern fonts; Cancel brings back what the dialog opened with.
    settings::EngineSettings larger = plain;
    larger.text_size = 200;
    settings::open_dialog(dialog, larger, plain, {}, "v0.2.0", Page::language);
    CHECK(geometry::open_rows(dialog).rows.rows[2].lock == Lock::needs_modern_fonts);
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.chosen.text_size == settings::default_text_size && dialog.chosen == plain);
    CHECK(click(dialog, centre(geometry::cancel_button)) == DialogAction::cancelled);
    CHECK(dialog.chosen.text_size == 200);
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
    // A mod's higher maximum adds stops past 1500, up to the largest a profile may name.
    const uint16_t highest = oa::data::limits::highest_units_per_player;
    CHECK(geometry::slider_of(settings::Setting::unit_limit, highest).stops == 131);
    geometry::set_stop(state, settings::Setting::unit_limit, 130, highest);
    CHECK(state.unit_limit == 6550);
    CHECK(geometry::stop_of(state, settings::Setting::unit_limit, highest) == 130);
    geometry::set_stop(state, settings::Setting::unit_limit, 200, 3000);
    CHECK(state.unit_limit == 3000);
    geometry::set_stop(state, settings::Setting::unit_limit, 4);
    // The frame rate from 30, a frame for each tick, to 120 in steps of 5.
    geometry::set_stop(state, settings::Setting::max_frame_rate, 0);
    CHECK(state.max_frame_rate == 30);
    CHECK(geometry::value_text(settings::Setting::max_frame_rate, state) == "30 fps");
    geometry::set_stop(state, settings::Setting::max_frame_rate, 2);
    CHECK(state.max_frame_rate == 40);
    geometry::set_stop(state, settings::Setting::max_frame_rate, 18);
    CHECK(state.max_frame_rate == 120);
    // A limit off the slider's steps, such as an installation's 20, shows at
    // the nearest stop and keeps its value until moved.
    state.unit_limit = 20;
    CHECK(geometry::stop_of(state, settings::Setting::unit_limit) == 0);
    CHECK(geometry::value_text(settings::Setting::unit_limit, state) == "20 per player");
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
    // Pathfinding cycles, Common Tweaks' third row.
    settings::Dialog dialog = opened(Page::common_tweaks);
    const auto track = geometry::place_rows(Page::common_tweaks, {}).rows[2].control_area;
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
    for (int32_t step = 0; step < 3; ++step)
        CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_row_control + 2);
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

    // The unit limit, under Your files.
    settings::Dialog gameplay = opened(Page::common_tweaks);
    CHECK(settings::dialog_key(gameplay, DialogKey::down) == DialogAction::redraw);
    CHECK(settings::dialog_key(gameplay, DialogKey::down) == DialogAction::redraw);
    CHECK(gameplay.focused == settings::first_row_control + 1);
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
    CHECK(levels[4]->text == "16x");
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
    // The row asks for 16x: four across is fewer than asked, which the
    // second line says.
    CHECK(
        find_part(in_full, "In Full the card draws 4x4 samples a pixel", settings::no_control) !=
        nullptr
    );
    CHECK(
        find_part(in_full, "the most this window allows, scaled down.", settings::no_control) !=
        nullptr
    );
    CHECK(find_part(in_full, "Needs a fast CPU.", settings::no_control) == nullptr);
    full.full_supersample = 16;
    CHECK(settings::set_acceleration_status(dialog, full) == DialogAction::redraw);
    const auto at_sixteen = settings::dialog_layout(dialog);
    CHECK(
        find_part(
            at_sixteen, "In Full the card draws 16x16 samples a pixel", settings::no_control
        ) != nullptr
    );
    CHECK(
        find_part(at_sixteen, "and scales them down for smoother edges.", settings::no_control) !=
        nullptr
    );
    full.full_supersample = 2;
    CHECK(settings::set_acceleration_status(dialog, full) == DialogAction::redraw);
    CHECK(
        find_part(
            settings::dialog_layout(dialog),
            "In Full the card draws 2x2 samples a pixel",
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
    settings::open_dialog(dialog, current, defaults, {}, "v0.2.0", Page::common_tweaks);
    for (int32_t step = 0; step < 4; ++step)
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
        settings::page_control(Page::mods),
        settings::page_control(Page::controls),
        settings::page_control(Page::common_tweaks),
        settings::page_control(Page::language),
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
    settings::Dialog dialog = opened(Page::common_tweaks, in_game);
    auto parts = settings::dialog_layout(dialog);
    CHECK(find_part(parts, "Locked during a game", settings::no_control) != nullptr);
    CHECK(find_part(parts, "Shared game - still running", settings::no_control) == nullptr);
    const auto track = geometry::place_rows(Page::common_tweaks, in_game).rows[2].control_area;
    CHECK(
        settings::dialog_pointer_down(dialog, track.x + track.width - 1, track.y + 4) ==
        DialogAction::none
    );
    CHECK(dialog.chosen.path_search_nodes == settings::base_path_search_nodes);
    // The focus passes over the locked sliders, from Your files to the footer.
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_row_control);
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(dialog.focused == settings::restore_control);
    // A locked slider takes no part a press can find.
    for (const auto& part : parts)
        CHECK(
            part.control != settings::first_row_control + 1 &&
            part.control != settings::first_row_control + 2
        );

    const auto shared = settings::settings_locks(settings::GameState{true, true, false, false});
    parts = settings::dialog_layout(opened(Page::common_tweaks, shared));
    CHECK(find_part(parts, "Set by the host", settings::no_control) != nullptr);
    CHECK(find_part(parts, "Shared game - still running", settings::no_control) != nullptr);
    const auto replay = settings::settings_locks(settings::GameState{true, false, true, false});
    parts = settings::dialog_layout(opened(Page::common_tweaks, replay));
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
    // Controls' three switches are 162 and Common Tweaks' Your files, unit
    // limit and pathfinding sliders 210, and both fit; Graphics' five rows
    // are 316 and Language's drop-down, four switches and slider 365, and
    // both scroll. Mods' list and Developer's list scroll in views of their
    // own (mods_scroll, developer_*).
    const std::array<Page, 4> pages{
        Page::controls, Page::common_tweaks, Page::graphics, Page::language
    };
    const std::array<int32_t, 4> content{162, 210, 316, 365};
    const std::array<int32_t, 4> limits{0, 0, 80, 129};
    for (std::size_t index = 0; index < content.size(); ++index) {
        const Page page = pages[index];
        if (limits[index] != 0) {
            for (const auto& locks : lock_states()) {
                const auto at_top = geometry::place_rows(page, locks);
                CHECK(geometry::content_height(at_top, 0) == content[index]);
                CHECK(geometry::scroll_limit(content[index]) == limits[index]);
            }
            continue;
        }
        for (const auto& locks : lock_states()) {
            settings::Dialog dialog = opened(page, locks);
            const auto at_top = geometry::place_rows(page, locks);
            CHECK(geometry::content_height(at_top, 0) == content[index]);
            CHECK(geometry::scroll_limit(content[index]) == 0);
            // A stored offset is clamped to the limit: the rows stay put.
            dialog.scroll[static_cast<std::size_t>(page)] = 100;
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
    // Each entry is its place in the list with Touch, whether or not the
    // dialog lists Touch: the five sections before Touch are 0 to 4, Touch
    // 5 and Developer 6; Game files, listed only by the main menu's dialog
    // of a game that brings game files in, is 7.
    for (std::size_t index = 0; index < kTouchPages.size(); ++index)
        CHECK(settings::page_control(kTouchPages[index]) == static_cast<int32_t>(index));
    CHECK(settings::page_control(Page::touch) == 5);
    CHECK(settings::page_control(Page::developer) == 6);
    CHECK(settings::page_control(Page::game_files) == 7);
    CHECK(settings::most_listed_pages == 8);
    CHECK(settings::restore_control == 8);
    CHECK(settings::cancel_control == 9);
    CHECK(settings::ok_control == 10);
    CHECK(settings::scroll_bar_control == 11);
    CHECK(settings::first_row_control == 12);
    // Developer's two rows come first, Enable Developer Mode and Show
    // performance statistics, then its footer's switch and button, then
    // its list's rows.
    CHECK(settings::developer_mode_control == 12);
    CHECK(settings::active_only_control == 14);
    CHECK(settings::restore_profile_control == 15);
    CHECK(settings::first_hack_list_control == 16);

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
    for (int32_t value = 0; value <= static_cast<int32_t>(Setting::text_background); ++value) {
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
    CHECK(switches == 10);
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
    const std::array<Expected, 18> fixed{{
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
    const std::array<std::pair<AccelerationState, std::string_view>, 6> in_use{{
        {AccelerationState::full_cannot_save, "Basic in use: the game cannot save its files."},
        {AccelerationState::full_too_little_memory,
         "Basic in use: there is too little memory for Full."},
        {AccelerationState::full_lacks_feature,
         "Basic in use: the card lacks a feature Full needs."},
        {AccelerationState::in_use_on_another_driver,
         "Basic in use, on another driver: one failed."},
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
    const std::array<std::pair<AccelerationState, std::string_view>, 1> full_in_use{{
        {AccelerationState::full_in_use, "Full in use: the graphics card draws the view."},
    }};
    const std::array<std::pair<uint8_t, std::string_view>, 7> anti_aliasing{{
        {1, "Smoothed at every zoom."},
        {2, "Smoothed at every zoom; 2x2 samples a pixel."},
        {3, "Smoothed at every zoom; 2x2 samples a pixel."},
        {4, "Smoothed at every zoom; 4x4 samples a pixel."},
        {8, "Smoothed at every zoom; 8x8 samples a pixel."},
        {16, "Smoothed at every zoom; 16x16 samples a pixel."},
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
            {AccelerationState::full_lacks_feature,
             AccelerationReach::menus,
             false,
             HardwareAcceleration::full}
        ) == DialogAction::redraw
    );
    const auto full_parts = settings::dialog_layout(dialog);
    CHECK(
        find_part(
            full_parts, "Basic in use: the card lacks a feature Full needs.", settings::no_control
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

/// A 40 by 40 icon of one opaque colour, but for its clear top left 2 by 2
/// pixels.
struct IconPicture {
    std::vector<uint8_t> pixels;

    renderer::RgbaPicture picture() const { return {kIconSide, kIconSide, pixels}; }

    static constexpr uint32_t kIconSide = 40;
};

/// The test icon's colour.
constexpr renderer::Rgb kIconColor{0xd4, 0xa0, 0x30};

IconPicture solid_icon() {
    IconPicture icon;
    for (uint32_t y = 0; y < IconPicture::kIconSide; ++y)
        for (uint32_t x = 0; x < IconPicture::kIconSide; ++x) {
            icon.pixels.insert(icon.pixels.end(), kIconColor.begin(), kIconColor.end());
            icon.pixels.push_back(x < 2 && y < 2 ? 0 : 0xff);
        }
    return icon;
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
    settings::draw_dialog(canvas.surface, {0, 0, 1}, dialog, fonts, kNoIcon);
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
    settings::draw_dialog(larger.surface, {100, 50, 2}, dialog, fonts, kNoIcon);
    CHECK(larger.at(99, 49) == (renderer::Rgb{0, 0, 0}));
    CHECK(larger.at(100 + 2 * 300, 50 + 2 * 3) == kBand);
    CHECK(
        larger.at(100 + 2 * (geometry::ok_button.x + 2), 50 + 2 * (geometry::ok_button.y + 2)) ==
        kAccent
    );

    // A slider's filled track runs to its knob.
    Canvas graphics = blank(settings::dialog_width, settings::dialog_height);
    settings::Dialog shown = opened(Page::graphics);
    settings::draw_dialog(graphics.surface, {0, 0, 1}, shown, fonts, kNoIcon);
    const auto track = geometry::place_rows(Page::graphics, {}).rows[0].control_area;
    CHECK(graphics.at(track.x + 2, track.y + geometry::track_offset + 1) == kAccent);

    // Without the icon, the header shows the OA mark's green outlined
    // square, 13 pixels a side in the middle of the icon's place.
    const auto mark = geometry::header_mark;
    const int32_t square_left = mark.x + (mark.width - geometry::header_mark_square) / 2;
    const int32_t square_top = mark.y + (mark.height - geometry::header_mark_square) / 2;
    CHECK(canvas.at(square_left, square_top) == kAccent);
    CHECK(canvas.at(square_left + geometry::header_mark_square - 1, square_top + 6) == kAccent);
    CHECK(canvas.at(square_left - 1, square_top) == kBand);

    // With the icon, the icon fills its 20 by 20 place in the header's
    // middle rows, and the band shows round it.
    const IconPicture icon = solid_icon();
    Canvas iconic = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(iconic.surface, {0, 0, 1}, dialog, fonts, icon.picture());
    CHECK(mark.width == 20 && mark.height == 20);
    CHECK(
        mark.y - geometry::header_top ==
        geometry::header_top + geometry::header_height - (mark.y + mark.height)
    );
    CHECK(iconic.at(mark.x, mark.y + 5) == kIconColor);
    CHECK(iconic.at(mark.x + mark.width - 1, mark.y + mark.height - 1) == kIconColor);
    CHECK(iconic.at(mark.x - 1, mark.y + 5) == kBand);
    CHECK(iconic.at(mark.x + mark.width, mark.y + 5) == kBand);
    CHECK(iconic.at(mark.x + 5, mark.y - 1) == kBand);
    CHECK(iconic.at(mark.x + 5, mark.y + mark.height) == kBand);
    CHECK(iconic.at(mark.x, mark.y) == kBand); // the icon's clear corner
    CHECK(iconic.at(mark.x + 1, mark.y) == kIconColor);
    CHECK(iconic.at(square_left, square_top) != kAccent);
    // Twice as large, the icon covers 40 by 40 surface pixels, one of the
    // icon's pixels each: its clear corner is 2 by 2 surface pixels.
    Canvas twice = blank(2 * settings::dialog_width, 2 * settings::dialog_height);
    settings::draw_dialog(twice.surface, {0, 0, 2}, dialog, fonts, icon.picture());
    CHECK(twice.at(2 * mark.x + 1, 2 * mark.y + 1) == kBand);
    CHECK(twice.at(2 * mark.x + 2, 2 * mark.y) == kIconColor);
    CHECK(
        twice.at(2 * mark.x + 2 * mark.width - 1, 2 * mark.y + 2 * mark.height - 1) == kIconColor
    );
    CHECK(twice.at(2 * mark.x + 2 * mark.width, 2 * mark.y + 5) == kBand);
    CHECK(twice.at(2 * mark.x - 1, 2 * mark.y + 5) == kBand);

    // The OA button's face.
    Canvas button = blank(settings::menu_button_side, settings::menu_button_side);
    settings::draw_oa_button(
        button.surface,
        {0, 0, 1},
        settings::menu_button_side,
        settings::ButtonLook::idle,
        fonts,
        kNoIcon
    );
    CHECK(button.at(3, 3) == kPanel);
    Canvas pressed = blank(settings::ingame_button_side, settings::ingame_button_side);
    settings::draw_oa_button(
        pressed.surface,
        {0, 0, 1},
        settings::ingame_button_side,
        settings::ButtonLook::pressed,
        fonts,
        kNoIcon
    );
    CHECK(pressed.at(2, 2) == kBand);
}

constexpr std::array<Page, 5> kModPages{
    Page::mod_keys,
    Page::mod_patrol,
    Page::mod_guard,
    Page::mod_tools,
    Page::mod_chat,
};

/// A mod's options as a profile offers them: snap radii up to 6 and 4.
settings::EngineSettings mod_settings() {
    settings::EngineSettings state{};
    auto& options = state.mod_options;
    options.snap_override_key = settings::option_keys[0].code;
    options.autoclick_key = settings::option_keys[8].code;
    options.rotate_build_key = settings::option_keys[15].code;
    options.patrol = {0, 1, 2};
    options.guard = {0, 1, 2};
    options.mex_snap_most = 6;
    options.wreck_snap_most = 4;
    options.mex_snap_radius = 3;
    options.wreck_snap_radius = 2;
    return state;
}

/// The mod options dialog opened over mod_settings, with defaults that
/// differ from them.
settings::Dialog opened_mod_options(Page page, const settings::Locks& locks = {}) {
    settings::EngineSettings defaults = mod_settings();
    defaults.mod_options.mex_snap_radius = 6;
    defaults.mod_options.wreck_snap_radius = 4;
    defaults.mod_options.full_rings = true;
    defaults.wheel_zoom = !defaults.wheel_zoom;
    settings::Dialog dialog;
    settings::open_mod_options_dialog(dialog, mod_settings(), defaults, locks, "v0.2.0", page);
    return dialog;
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
    // Sections that fit draw no scroll bar, whatever offset they hold:
    // every one but Graphics, Language, and Mods and Developer, whose
    // lists scroll in views of their own.
    for (const Page page : kPages) {
        if (page == Page::graphics || page == Page::language || page == Page::mods ||
            page == Page::developer)
            continue;
        Canvas canvas = blank(settings::dialog_width, settings::dialog_height);
        settings::Dialog dialog = opened(page);
        settings::draw_dialog(canvas.surface, {0, 0, 1}, dialog, fonts, kNoIcon);
        CHECK(canvas.at(473, 150) == kPanel);
        CHECK(canvas.at(300, geometry::first_row_top) == kRule);
        Canvas held = blank(settings::dialog_width, settings::dialog_height);
        dialog.scroll[static_cast<std::size_t>(page)] = 50;
        settings::draw_dialog(held.surface, {0, 0, 1}, dialog, fonts, kNoIcon);
        CHECK(held.surface.rgb == canvas.surface.rgb);
    }

    // At the top: the well with its border, the thumb in its top 174 rows.
    // On a canvas taller than the dialog, nothing of the rows below the view
    // is drawn under the dialog.
    Scrolling five(five_rows());
    Canvas top = blank(settings::dialog_width, 400);
    settings::draw_dialog(top.surface, {0, 0, 1}, five.dialog, fonts, kNoIcon);
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
    settings::draw_dialog(scrolled.surface, {0, 0, 1}, five.dialog, fonts, kNoIcon);
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
    settings::draw_dialog(canvas.surface, {0, 0, 1}, locked.dialog, fonts, kNoIcon);
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
    settings::draw_dialog(off.surface, {0, 0, 1}, locked.dialog, fonts, kNoIcon);
    CHECK(off.at(418, 245) == faded(kOffSelected)); // its Off, faded
    CHECK(off.at(444, 245) == faded(kWell));

    // With the focus shown on a row, a press held on the bar leaves its
    // outline drawn.
    Scrolling focused(five_rows());
    CHECK(settings::dialog_key(focused.dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(settings::dialog_pointer_down(focused.dialog, 473, 100) == DialogAction::redraw);
    Canvas held = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(held.surface, {0, 0, 1}, focused.dialog, fonts, kNoIcon);
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
    settings::draw_dialog(well.surface, {0, 0, 1}, jumped.dialog, fonts, kNoIcon);
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
    settings::draw_dialog(at_top.surface, {0, 0, 1}, top, fonts, kNoIcon);
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
    settings::draw_dialog(canvas.surface, {0, 0, 1}, dialog, fonts, kNoIcon);
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
    settings::draw_dialog(unlocked.surface, {0, 0, 1}, open, fonts, kNoIcon);
    CHECK(unlocked.at(434, 186) == kAccent);
    CHECK(unlocked.at(400, 186) == kWell);
    CHECK(unlocked.at(366, 186) == kWell);
    CHECK(unlocked.at(444, 245) == kAccent);
}

// ---- Developer, and Developer Mode's list

namespace profiles = oa::data::mod_profile;

/// The section's place among Dialog::scroll.
constexpr std::size_t kDeveloperScroll = static_cast<std::size_t>(Page::developer);

/// Returns a hack's place among the standard hacks.
///
/// @param id the hack's id
/// @return its place
std::size_t hack_index(std::string_view id) {
    const auto index = profiles::standard_hack_index(id);
    CHECK(index.has_value());
    return index.value_or(0);
}

/// Returns an area's place among developer_areas.
///
/// @param name the area
/// @return its place
std::size_t area_index(std::string_view name) {
    const auto areas = settings::developer_areas();
    for (std::size_t index = 0; index < areas.size(); ++index)
        if (areas[index].name == name)
            return index;
    CHECK(false);
    return 0;
}

/// Opens the dialog on Developer, Developer Mode On or Off, over a
/// profile's hacks.
///
/// @param on Developer Mode is on
/// @param overrides the overrides the settings hold
/// @param profile the profile's hacks; empty for 3.1c's
/// @return the dialog
settings::Dialog developer_dialog(
    bool on,
    std::vector<profiles::HackOverride> overrides = {},
    const std::vector<profiles::HackState>& profile = {}
) {
    settings::EngineSettings current{};
    current.developer_mode = on;
    current.hack_overrides = std::move(overrides);
    settings::Dialog dialog;
    settings::open_dialog(
        dialog,
        current,
        settings::EngineSettings{},
        {},
        "v0.2.0",
        Page::developer,
        {},
        settings::highest_unit_limit,
        {},
        profile
    );
    return dialog;
}

/// Opens an area and one of its hacks.
///
/// @param[in,out] dialog the dialog
/// @param hack the hack's id
void open_hack(settings::Dialog& dialog, std::string_view hack) {
    const std::size_t index = hack_index(hack);
    dialog.developer.areas_open[area_index(profiles::standard_hacks()[index]->area)] = 1;
    dialog.developer.hacks_open[index] = 1;
}

/// Returns the list's row that matches, scrolled so that the row's top is
/// the view's first row, as far as the list scrolls.
///
/// @param[in,out] dialog the dialog
/// @param matches tells the row
/// @return the row as placed at the new offset; an empty row when none matches
template <typename Matches>
geometry::ListRow shown_row(settings::Dialog& dialog, Matches matches) {
    const auto open = geometry::open_rows(dialog);
    for (const auto& row : open.list.rows) {
        if (!matches(row))
            continue;
        dialog.scroll[kDeveloperScroll] = row.top + open.scroll - geometry::developer_view.y;
        for (const auto& moved : geometry::open_rows(dialog).list.rows)
            if (matches(moved))
                return moved;
    }
    CHECK(false);
    return {};
}

/// Returns the header of a hack, shown.
///
/// @param[in,out] dialog the dialog
/// @param hack the hack's id
/// @return the row
geometry::ListRow hack_header(settings::Dialog& dialog, std::string_view hack) {
    const std::size_t index = hack_index(hack);
    return shown_row(dialog, [&](const geometry::ListRow& row) {
        return row.kind == geometry::ListRowKind::hack && row.hack == index;
    });
}

/// Returns a parameter's row of a hack, shown.
///
/// @param[in,out] dialog the dialog
/// @param hack the hack's id
/// @param parameter the parameter's index within the hack
/// @param item a list's item or a set's value, list_length, or whole_parameter
/// @return the row
geometry::ListRow parameter_row(
    settings::Dialog& dialog,
    std::string_view hack,
    int32_t parameter,
    int32_t item = geometry::whole_parameter
) {
    const std::size_t index = hack_index(hack);
    return shown_row(dialog, [&](const geometry::ListRow& row) {
        return (row.kind == geometry::ListRowKind::slider ||
                row.kind == geometry::ListRowKind::toggle) &&
               row.hack == index && row.parameter == parameter && row.item == item;
    });
}

/// Returns the point of a switch's half.
///
/// @param area the switch
/// @param on true for its On half
/// @return the point
Point switch_half(const renderer::SourceRect& area, bool on) {
    return {on ? area.x + area.width - 4 : area.x + 4, area.y + area.height / 2};
}

/// Returns a hack's override among the chosen settings'.
///
/// @param dialog the dialog
/// @param hack the hack's id
/// @return the override; null for none
const profiles::HackOverride*
chosen_override(const settings::Dialog& dialog, std::string_view hack) {
    return profiles::find_override(dialog.chosen.hack_overrides, hack);
}

/// Returns the value an override sets for a parameter.
///
/// @param override the override; null for none
/// @param name the parameter's name
/// @return its canonical text; "absent" when it sets none
std::string override_value(const profiles::HackOverride* override, std::string_view name) {
    if (override == nullptr)
        return "absent";
    for (const auto& parameter : override->parameters)
        if (parameter.name == name)
            return profiles::canonical_json(parameter.value);
    return "absent";
}

/// Focuses a row's control and takes a key on it.
///
/// @param[in,out] dialog the dialog
/// @param control the row's control
/// @param key the key
/// @return what it asks of the host
DialogAction key_on(settings::Dialog& dialog, int32_t control, DialogKey key) {
    dialog.focused = control;
    return settings::dialog_key(dialog, key);
}

void developer_mode_lists_every_hack_by_area_closed_at_first() {
    const auto areas = settings::developer_areas();
    const auto hacks = profiles::standard_hacks();
    std::size_t listed = 0;
    for (const auto& area : areas) {
        CHECK(!area.hacks.empty());
        for (const std::size_t hack : area.hacks) {
            CHECK(hacks[hack]->area == area.name);
            ++listed;
        }
    }
    CHECK(listed == hacks.size());
    CHECK(areas.front().name == "ai");
    settings::Dialog dialog = developer_dialog(false);
    CHECK(dialog.developer.profile.size() == hacks.size());
    CHECK(dialog.developer.areas_open.size() == areas.size());
    CHECK(dialog.developer.hacks_open.size() == hacks.size());
    const auto open = geometry::open_rows(dialog);
    CHECK(open.list.rows.size() == areas.size());
    for (std::size_t index = 0; index < open.list.rows.size(); ++index) {
        const auto& row = open.list.rows[index];
        CHECK(row.kind == geometry::ListRowKind::area && !row.open);
        CHECK(row.control == settings::first_hack_list_control + static_cast<int32_t>(index));
        CHECK(
            row.top ==
            geometry::developer_view.y + static_cast<int32_t>(index) * geometry::list_header_height
        );
    }
    CHECK(open.list.rows[0].text == "AI" && open.list.rows[1].text == "Aircraft");
    CHECK(open.list.rows[0].shown == "0 of " + std::to_string(areas[0].hacks.size()) + " on");
    // The closed areas are taller than the list's view: the list scrolls.
    CHECK(
        open.limit == static_cast<int32_t>(areas.size()) * geometry::list_header_height +
                          geometry::end_gap - geometry::developer_view.height
    );
    CHECK(same_rect(open.area.view, geometry::developer_view));
    const auto parts = settings::dialog_layout(dialog);
    const std::string label = geometry::active_only_text(0, hacks.size());
    CHECK(label == "Show Active Only (0/" + std::to_string(hacks.size()) + ")");
    for (const std::string_view text :
         {"Developer",
          "DEVELOPER",
          "Enable Developer Mode",
          "Your changes to the profile's hacks apply while on.",
          "Show performance statistics",
          "Frame and tick times over the battlefield."})
        CHECK(find_part(parts, text, settings::no_control) != nullptr);
    CHECK(find_part(parts, label, settings::no_control) != nullptr);
    const auto* restore = find_part(parts, "RESTORE PROFILE VALUES", settings::no_control);
    CHECK(restore != nullptr && restore->control == settings::no_control);
    int32_t bars = 0;
    for (const auto& part : parts)
        if (part.control == settings::scroll_bar_control) {
            ++bars;
            CHECK(same_rect(part.rect, {470, 133, 7, 114}));
        }
    CHECK(bars == 1);
    // The section's parts keep their places: its two rows at its top, closer
    // than a section's, Enable Developer Mode first; the list between the
    // line under them and its footer's line.
    const auto rows = open.rows.rows;
    CHECK(rows.size() == 2);
    CHECK(rows[0].setting == Setting::developer_mode && rows[1].setting == Setting::frame_stats);
    CHECK(rows[0].control == settings::developer_mode_control);
    CHECK(rows[1].control == settings::first_row_control + 1);
    CHECK(rows[0].top == geometry::first_row_top && rows[1].top == 93);
    CHECK(same_rect(rows[0].control_area, {415, 59, 52, 16}));
    CHECK(same_rect(rows[1].control_area, {415, 98, 52, 16}));
    CHECK(same_rect(rows[1].hints[0], {158, 116, 309, 12}));
    CHECK(open.rows.bottom == geometry::developer_list_rule);
    CHECK(same_rect(geometry::developer_view, {158, 133, 309, 114}));
    CHECK(same_rect(geometry::active_only_switch, {415, 251, 52, 16}));
    CHECK(same_rect(geometry::restore_profile_button, {158, 271, 142, 17}));
    CHECK(geometry::developer_list_rule == 132 && geometry::developer_footer_rule == 247);
    CHECK(
        geometry::restore_profile_button.y + geometry::restore_profile_button.height <
        geometry::footer_rule_row
    );
}

void areas_and_hacks_open_and_close() {
    settings::Dialog dialog = developer_dialog(false);
    auto open = geometry::open_rows(dialog);
    CHECK(click(dialog, centre(open.list.rows[0].label)) == DialogAction::redraw);
    CHECK(dialog.developer.areas_open[0] == 1);
    open = geometry::open_rows(dialog);
    const auto& area = settings::developer_areas()[0];
    CHECK(open.list.rows.size() == settings::developer_areas().size() + area.hacks.size());
    const auto& entry = *profiles::standard_hacks()[area.hacks[0]];
    const auto first = open.list.rows[1];
    CHECK(first.kind == geometry::ListRowKind::hack && first.hack == area.hacks[0]);
    CHECK(first.text == entry.title && first.text == "Attack Wave Size");
    CHECK(!first.on && first.locked && !first.open);
    CHECK(first.control == settings::first_hack_list_control + 1);
    // A press on its header opens it: its id, its summary, its scope and
    // its note.
    CHECK(click(dialog, centre(first.label)) == DialogAction::redraw);
    CHECK(dialog.developer.hacks_open[area.hacks[0]] == 1);
    open = geometry::open_rows(dialog);
    CHECK(open.list.rows[2].kind == geometry::ListRowKind::id);
    CHECK(open.list.rows[2].text == entry.id && open.list.rows[2].control == settings::no_control);
    const auto lines = geometry::summary_lines(entry.summary);
    CHECK(!lines.empty());
    for (std::size_t line = 0; line < lines.size(); ++line) {
        CHECK(open.list.rows[3 + line].kind == geometry::ListRowKind::text);
        CHECK(open.list.rows[3 + line].text == lines[line]);
    }
    CHECK(open.list.rows[3 + lines.size()].kind == geometry::ListRowKind::scope);
    CHECK(open.list.rows[3 + lines.size()].text == "Applies at next match");
    CHECK(open.list.rows[4 + lines.size()].text == "Off: it plays as 3.1c.");
    // A display (view-scope) hack says nothing of the next match.
    open_hack(dialog, "ui.whiteboard");
    open = geometry::open_rows(dialog);
    const std::size_t whiteboard = hack_index("ui.whiteboard");
    for (const auto& row : open.list.rows)
        if (row.hack == whiteboard)
            CHECK(row.kind != geometry::ListRowKind::scope);
    dialog.developer.areas_open[area_index("ui")] = 0;
    // The keys: the focus goes to Enable Developer Mode, Show performance
    // statistics, then the list's headers; Left closes an open area, Right
    // opens it, Space flips it.
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(dialog.focused == settings::developer_mode_control);
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_row_control + 1);
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_hack_list_control);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::none);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::redraw);
    CHECK(dialog.developer.areas_open[0] == 0);
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::redraw);
    CHECK(dialog.developer.areas_open[0] == 1);
    // Down reaches the open hack's header, whose Space closes it.
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_hack_list_control + 1);
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::redraw);
    CHECK(dialog.developer.hacks_open[area.hacks[0]] == 0);
    // Opening and closing changes no setting.
    CHECK(dialog.chosen == dialog.opened);
}

/// Returns a text with A to Z lowered.
///
/// @param text the text
/// @return it lowered
std::string lowered(std::string_view text) {
    std::string low(text);
    for (char& letter : low)
        if (letter >= 'A' && letter <= 'Z')
            letter = static_cast<char>(letter - 'A' + 'a');
    return low;
}

/// Tells whether titles are in alphabetical order, without regard to case.
///
/// @param titles the titles
/// @return true when each comes before the next
bool alphabetical(const std::vector<std::string>& titles) {
    return std::is_sorted(titles.begin(), titles.end(), [](const auto& left, const auto& right) {
        return lowered(left) < lowered(right);
    });
}

/// Returns the titles the list's area headers show, top to bottom, and
/// each area's hack titles, with every area open.
///
/// @param[out] hacks each area's hack titles, in the list's order
/// @return the areas' titles, in the list's order
std::vector<std::string> listed_titles(std::vector<std::vector<std::string>>& hacks) {
    settings::Dialog dialog = developer_dialog(false);
    dialog.developer.areas_open.assign(dialog.developer.areas_open.size(), 1);
    std::vector<std::string> areas;
    hacks.clear();
    for (const auto& row : geometry::open_rows(dialog).list.rows) {
        if (row.kind == geometry::ListRowKind::area) {
            areas.push_back(row.text);
            hacks.emplace_back();
        } else if (row.kind == geometry::ListRowKind::hack) {
            CHECK(!hacks.empty() && row.text == profiles::standard_hacks()[row.hack]->title);
            if (!hacks.empty())
                hacks.back().push_back(row.text);
        }
    }
    return areas;
}

void hacks_show_their_titles_alphabetically() {
    namespace registry = profiles::registry;
    // Every hack has a title of its own, and every area a name.
    std::set<std::string> titles;
    for (const auto* hack : profiles::standard_hacks()) {
        CHECK(!hack->title.empty() && hack->title != hack->id);
        CHECK(titles.insert(lowered(hack->title)).second);
        CHECK(!registry::area_title(hack->area).empty());
        CHECK(registry::area_title(hack->area) != hack->area);
    }
    CHECK(registry::find_entry("economy.deterministic-wind")->title == "Deterministic Wind");
    CHECK(registry::find_entry("ui.megamap")->title == "Megamap");
    CHECK(registry::find_entry("ui.build-tools")->title == "Build Tools");
    CHECK(registry::find_entry("limits.units-per-player")->title.empty());
    CHECK(registry::area_title("ui") == "Interface" && registry::area_title("ai") == "AI");
    CHECK(registry::area_title("nowhere") == "nowhere");
    CHECK(registry::table().area_titles.size() == settings::developer_areas().size());
    // The areas, and the hacks within each, follow their titles
    // alphabetically, in developer_areas and in the list.
    std::vector<std::string> area_titles;
    for (const auto& area : settings::developer_areas()) {
        CHECK(area.title == registry::area_title(area.name));
        area_titles.emplace_back(area.title);
        std::vector<std::string> hack_titles;
        for (const std::size_t hack : area.hacks)
            hack_titles.emplace_back(profiles::standard_hacks()[hack]->title);
        CHECK(alphabetical(hack_titles));
    }
    CHECK(alphabetical(area_titles));
    std::vector<std::vector<std::string>> hacks;
    const auto areas = listed_titles(hacks);
    CHECK(areas == area_titles);
    CHECK(areas.size() >= 3 && areas[0] == "AI" && areas[1] == "Aircraft");
    CHECK(!areas.empty() && areas.back() == "Weapons");
    for (const auto& titles_of_area : hacks)
        CHECK(!titles_of_area.empty() && alphabetical(titles_of_area));
    // In another language the list follows the names as that language
    // shows them, and the areas keep their places among developer_areas.
    oa::data::languages::InterfaceText catalogue;
    CHECK(catalogue.add(
        "[Weapons]\n{\nde=Aaa Waffen;\n}\n[Whiteboard]\n{\nde=Aaa Tafel;\n}\n"
        "[Build Preview]\n{\nde=Zzz Vorschau;\n}\n"
    ));
    oa::data::languages::set_interface_language(
        &catalogue, *oa::data::languages::find_by_tag("de")
    );
    const auto german = listed_titles(hacks);
    oa::data::languages::set_interface_language(nullptr, oa::data::languages::english());
    CHECK(german.size() == areas.size() && german.front() == "Weapons" && german[1] == "AI");
    const auto interface = static_cast<std::size_t>(
        std::find(german.begin(), german.end(), "Interface") - german.begin()
    );
    CHECK(interface < hacks.size());
    if (interface < hacks.size()) {
        CHECK(hacks[interface].front() == "Whiteboard");
        CHECK(hacks[interface].back() == "Build Preview");
    }
    CHECK(settings::developer_areas().back().name == "weapons");
    CHECK(listed_titles(hacks) == area_titles);
}

void developer_mode_off_shows_the_profile_and_takes_no_change() {
    const std::size_t wave = hack_index("ai.attack-wave-size");
    settings::Dialog dialog =
        developer_dialog(false, {profiles::HackOverride{"ai.attack-wave-size", true, {}}});
    // Off, the kept override is not laid over the profile.
    CHECK(!settings::shown_hacks(dialog)[wave].on);
    CHECK(settings::active_hack_count(dialog) == 0);
    open_hack(dialog, "ai.attack-wave-size");
    auto header = hack_header(dialog, "ai.attack-wave-size");
    CHECK(header.locked && !header.on && header.open);
    CHECK(click(dialog, switch_half(header.toggle, true)) == DialogAction::redraw);
    CHECK(key_on(dialog, header.control, DialogKey::right) == DialogAction::redraw);
    CHECK(dialog.chosen == dialog.opened);
    // Restore profile values takes no press and no focus.
    const Point restore = centre(geometry::restore_profile_button);
    CHECK(settings::dialog_pointer_down(dialog, restore.x, restore.y) == DialogAction::none);
    CHECK(settings::dialog_pointer_up(dialog, restore.x, restore.y) == DialogAction::none);
    dialog.focused = settings::active_only_control;
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(dialog.focused == settings::restore_control);
    // Show performance statistics takes a change whatever Developer Mode is.
    const auto developer_rows = geometry::open_rows(dialog).rows.rows;
    const auto& enable = developer_rows[0].control_area;
    const auto& stats = developer_rows[1].control_area;
    CHECK(click(dialog, switch_half(stats, true)) == DialogAction::changed);
    CHECK(dialog.chosen.frame_stats && !dialog.chosen.developer_mode);
    CHECK(click(dialog, switch_half(stats, false)) == DialogAction::changed);
    CHECK(dialog.chosen == dialog.opened);
    // On, the override applies and the count shows it; Off again keeps it.
    CHECK(click(dialog, switch_half(enable, true)) == DialogAction::changed);
    CHECK(dialog.chosen.developer_mode);
    CHECK(settings::shown_hacks(dialog)[wave].on && settings::active_hack_count(dialog) == 1);
    CHECK(
        find_part(
            settings::dialog_layout(dialog),
            geometry::active_only_text(1, profiles::standard_hacks().size()),
            settings::no_control
        ) != nullptr
    );
    const auto profile_parts = settings::dialog_layout(dialog);
    const auto* button = find_part(profile_parts, "RESTORE PROFILE VALUES", 0);
    CHECK(button != nullptr && button->control == settings::restore_profile_control);
    // Show performance statistics, on, stays as it is.
    CHECK(click(dialog, switch_half(stats, true)) == DialogAction::changed);
    CHECK(dialog.chosen.frame_stats && settings::active_hack_count(dialog) == 1);
    CHECK(click(dialog, switch_half(enable, false)) == DialogAction::changed);
    CHECK(dialog.chosen.frame_stats);
    CHECK(!dialog.chosen.developer_mode && dialog.chosen.hack_overrides.size() == 1);
    // The keys set it too.
    CHECK(
        key_on(dialog, settings::developer_mode_control, DialogKey::right) == DialogAction::changed
    );
    CHECK(dialog.chosen.developer_mode);
    CHECK(
        key_on(dialog, settings::developer_mode_control, DialogKey::space) == DialogAction::changed
    );
    CHECK(!dialog.chosen.developer_mode);
}

void hacks_turn_on_and_off_and_their_overrides_follow_the_profile() {
    // A profile with ai.attack-wave-size on at 25.
    auto profile = profiles::base_hack_states();
    const std::size_t wave = hack_index("ai.attack-wave-size");
    profile[wave].on = true;
    profile[wave].values[0] = profiles::make_integer(25);
    settings::Dialog dialog = developer_dialog(true, {}, profile);
    CHECK(settings::active_hack_count(dialog) == 1);
    open_hack(dialog, "ai.attack-wave-size");
    auto header = hack_header(dialog, "ai.attack-wave-size");
    CHECK(header.on && !header.locked);
    // Off: an override that turns it off.
    CHECK(click(dialog, switch_half(header.toggle, false)) == DialogAction::changed);
    const auto* off = chosen_override(dialog, "ai.attack-wave-size");
    CHECK(off != nullptr && !off->on && off->parameters.empty());
    CHECK(!settings::shown_hacks(dialog)[wave].on);
    // On again: the profile's own state, and no override.
    header = hack_header(dialog, "ai.attack-wave-size");
    CHECK(click(dialog, switch_half(header.toggle, true)) == DialogAction::changed);
    CHECK(dialog.chosen.hack_overrides.empty());
    // Its parameter's slider: 1 to 1500 units, at the profile's 25.
    auto units = parameter_row(dialog, "ai.attack-wave-size", 0);
    CHECK(units.kind == geometry::ListRowKind::slider && !units.locked);
    CHECK(units.stops == 1500 && units.stop == 24 && units.shown == "25 units");
    CHECK(key_on(dialog, units.control, DialogKey::right) == DialogAction::changed);
    CHECK(override_value(chosen_override(dialog, "ai.attack-wave-size"), "units") == "26");
    // Back to the profile's value drops the override.
    CHECK(key_on(dialog, units.control, DialogKey::left) == DialogAction::changed);
    CHECK(dialog.chosen.hack_overrides.empty());
    // A press at the track's right end, and a drag back to its left.
    units = parameter_row(dialog, "ai.attack-wave-size", 0);
    const auto& track = units.control_area;
    CHECK(
        settings::dialog_pointer_down(dialog, track.x + track.width - 1, track.y + 5) ==
        DialogAction::changed
    );
    CHECK(override_value(chosen_override(dialog, "ai.attack-wave-size"), "units") == "1500");
    CHECK(
        settings::dialog_pointer_move(dialog, track.x - 20, track.y + 5) == DialogAction::changed
    );
    CHECK(override_value(chosen_override(dialog, "ai.attack-wave-size"), "units") == "1");
    CHECK(settings::dialog_pointer_up(dialog, track.x - 20, track.y + 5) == DialogAction::redraw);

    // A hack the profile has off: turned on, its defaults, and the override
    // holds only what differs from them.
    open_hack(dialog, "ai.patrol-group-size");
    header = hack_header(dialog, "ai.patrol-group-size");
    CHECK(!header.on);
    CHECK(click(dialog, switch_half(header.toggle, true)) == DialogAction::changed);
    const auto* patrol = chosen_override(dialog, "ai.patrol-group-size");
    CHECK(patrol != nullptr && patrol->on && patrol->parameters.empty());
    auto group = parameter_row(dialog, "ai.patrol-group-size", 0);
    CHECK(group.shown == "15 units");
    CHECK(key_on(dialog, group.control, DialogKey::right) == DialogAction::changed);
    CHECK(override_value(chosen_override(dialog, "ai.patrol-group-size"), "units") == "16");
    // Off drops the parameters it set; the override is gone with the profile's state.
    header = hack_header(dialog, "ai.patrol-group-size");
    CHECK(key_on(dialog, header.control, DialogKey::left) == DialogAction::changed);
    CHECK(chosen_override(dialog, "ai.patrol-group-size") == nullptr);
    CHECK(settings::active_hack_count(dialog) == 1);
}

void every_kind_of_parameter_has_its_control() {
    std::vector<profiles::HackOverride> on;
    for (const std::string_view hack :
         {"orders.build-site-kickout",
          "orders.weapons-free-while-busy",
          "veterancy.model",
          "ai.difficulty-names",
          "console.atm-amount",
          "ai.income-multipliers",
          "console.game-speed-range",
          "setup.ai-player-name-format"})
        on.push_back(profiles::HackOverride{std::string{hack}, true, {}});
    settings::Dialog dialog = developer_dialog(true, on);
    for (const auto& override : on)
        open_hack(dialog, override.hack);

    // A boolean: a switch, set by its halves.
    auto kickout = parameter_row(dialog, "orders.build-site-kickout", 1);
    CHECK(kickout.kind == geometry::ListRowKind::toggle && kickout.on && kickout.text == "kickout");
    CHECK(click(dialog, switch_half(kickout.control_area, false)) == DialogAction::changed);
    CHECK(
        override_value(chosen_override(dialog, "orders.build-site-kickout"), "kickout") == "false"
    );
    auto retry = parameter_row(dialog, "orders.build-site-kickout", 2);
    CHECK(retry.stops == 1000 && retry.shown == "20 attempts");

    // A set: a switch for each value, the set kept in the registry's order.
    auto repair = parameter_row(dialog, "orders.weapons-free-while-busy", 0, 4);
    CHECK(repair.kind == geometry::ListRowKind::toggle && repair.text == "repair" && !repair.on);
    CHECK(click(dialog, switch_half(repair.control_area, true)) == DialogAction::changed);
    CHECK(
        override_value(chosen_override(dialog, "orders.weapons-free-while-busy"), "states") ==
        "[\"nanolathe\",\"repair\"]"
    );

    // An ascending list of whole numbers: its length, and each item between
    // its neighbours.
    auto length = parameter_row(dialog, "veterancy.model", 1, geometry::list_length);
    CHECK(length.kind == geometry::ListRowKind::slider && length.text == "Items");
    CHECK(length.stops == 32 && length.stop == 4 && length.shown == "5");
    CHECK(key_on(dialog, length.control, DialogKey::right) == DialogAction::changed);
    CHECK(
        override_value(chosen_override(dialog, "veterancy.model"), "default-thresholds") ==
        "[5,10,15,20,25,26]"
    );
    auto second = parameter_row(dialog, "veterancy.model", 1, 1);
    CHECK(second.text == "Item 2" && second.stops == 9 && second.stop == 4);
    CHECK(second.shown == "10 kills");
    CHECK(key_on(dialog, second.control, DialogKey::left) == DialogAction::changed);
    CHECK(
        override_value(chosen_override(dialog, "veterancy.model"), "default-thresholds") ==
        "[5,9,15,20,25,26]"
    );
    // An int-or-none: none at the first stop.
    auto cap = parameter_row(dialog, "veterancy.model", 6);
    CHECK(cap.stops == 1002 && cap.stop == 0 && cap.shown == "none");
    CHECK(key_on(dialog, cap.control, DialogKey::right) == DialogAction::changed);
    CHECK(override_value(chosen_override(dialog, "veterancy.model"), "damage-dealt-cap") == "0");
    // An enumeration: a slider of its words.
    auto source = parameter_row(dialog, "veterancy.model", 0);
    CHECK(source.stops == 2 && source.stop == 1 && source.shown == "thresholds");

    // A list of distinct words: a word moved in swaps with the item that held it.
    auto easiest = parameter_row(dialog, "ai.difficulty-names", 0, 0);
    CHECK(easiest.shown == "hard" && easiest.stops == 3);
    CHECK(key_on(dialog, easiest.control, DialogKey::left) == DialogAction::changed);
    CHECK(
        override_value(chosen_override(dialog, "ai.difficulty-names"), "names") ==
        "[\"medium\",\"hard\",\"easy\"]"
    );

    // A decimal over a wide range: its stops hold the registry's values.
    auto amount = parameter_row(dialog, "console.atm-amount", 0);
    CHECK(amount.shown == "4294967296000 metal and energy");
    CHECK(amount.stops == 1003 && amount.stop == 6);
    for (int32_t press = 0; press < 5; ++press)
        static_cast<void>(key_on(dialog, amount.control, DialogKey::left));
    CHECK(override_value(chosen_override(dialog, "console.atm-amount"), "amount") == "1000");
    // A list of decimals: each item in tenths.
    auto production = parameter_row(dialog, "ai.income-multipliers", 0, 2);
    CHECK(production.stops == 1001 && production.stop == 40 && production.shown == "4 x income");
    CHECK(key_on(dialog, production.control, DialogKey::right) == DialogAction::changed);
    CHECK(
        override_value(chosen_override(dialog, "ai.income-multipliers"), "production") ==
        "[0.5,1,4.1]"
    );

    // Two parameters a constraint ties: min goes no higher than max.
    auto most = parameter_row(dialog, "console.game-speed-range", 1);
    CHECK(most.stops == 21 && most.stop == 20);
    for (int32_t press = 0; press < 10; ++press)
        static_cast<void>(key_on(dialog, most.control, DialogKey::left));
    auto least = parameter_row(dialog, "console.game-speed-range", 0);
    CHECK(least.stops == 11 && least.stop == 0);
    CHECK(least.shown == "0 speed steps (10 = normal)");
    most = parameter_row(dialog, "console.game-speed-range", 1);
    CHECK(most.stops == 21 && most.stop == 10);

    // A string: a slider of the values the registry gives it.
    auto format = parameter_row(dialog, "setup.ai-player-name-format", 0);
    CHECK(format.stops == 2 && format.stop == 1 && format.shown == "AI:%s %d");
    CHECK(key_on(dialog, format.control, DialogKey::left) == DialogAction::changed);
    CHECK(
        override_value(chosen_override(dialog, "setup.ai-player-name-format"), "format") ==
        "\"AI:%s\""
    );
    // Every override the dialog made is one the resolver lays.
    for (const auto& override : dialog.chosen.hack_overrides) {
        const std::size_t index = hack_index(override.hack);
        const auto state = profiles::overridden_state(
            *profiles::standard_hacks()[index], dialog.developer.profile[index], &override
        );
        const auto parameters =
            oa::data::mod_profile::registry::parameters_of(*profiles::standard_hacks()[index]);
        for (std::size_t at = 0; at < parameters.size(); ++at) {
            profiles::Value normal{};
            CHECK(!oa::data::mod_profile::registry::check_value(
                parameters[at].value, state.values[at], {}, normal
            ));
        }
    }
}

void restore_profile_values_and_show_active_only() {
    settings::Dialog dialog = developer_dialog(
        true,
        {profiles::HackOverride{"ai.attack-wave-size", true, {}},
         profiles::HackOverride{"ui.whiteboard", true, {}}}
    );
    CHECK(settings::active_hack_count(dialog) == 2);
    // Show Active Only lists only the areas and hacks that are on.
    CHECK(click(dialog, switch_half(geometry::active_only_switch, true)) == DialogAction::redraw);
    CHECK(dialog.developer.active_only);
    auto open = geometry::open_rows(dialog);
    CHECK(
        open.list.rows.size() == 2 && open.list.rows[0].text == "AI" &&
        open.list.rows[1].text == "Interface"
    );
    CHECK(
        open.list.rows[0].shown ==
        "1 of " + std::to_string(settings::developer_areas()[0].hacks.size()) + " on"
    );
    CHECK(open.limit == 0);
    dialog.developer.areas_open.assign(dialog.developer.areas_open.size(), 1);
    open = geometry::open_rows(dialog);
    CHECK(open.list.rows.size() == 4);
    CHECK(open.list.rows[1].text == "Attack Wave Size" && open.list.rows[3].text == "Whiteboard");
    // Restore profile values clears every override, at once.
    CHECK(click(dialog, centre(geometry::restore_profile_button)) == DialogAction::changed);
    CHECK(dialog.chosen.hack_overrides.empty() && settings::active_hack_count(dialog) == 0);
    CHECK(geometry::open_rows(dialog).list.rows.empty());
    CHECK(click(dialog, centre(geometry::restore_profile_button)) == DialogAction::redraw);
    // The keys: Space on the filter's switch, and on the button.
    CHECK(key_on(dialog, settings::active_only_control, DialogKey::space) == DialogAction::redraw);
    CHECK(!dialog.developer.active_only);
    CHECK(key_on(dialog, settings::active_only_control, DialogKey::right) == DialogAction::redraw);
    CHECK(dialog.developer.active_only);
    dialog.chosen.hack_overrides.push_back(profiles::HackOverride{"ui.megamap", true, {}});
    CHECK(
        key_on(dialog, settings::restore_profile_control, DialogKey::space) == DialogAction::changed
    );
    CHECK(dialog.chosen.hack_overrides.empty());
    // Cancel puts the overrides the dialog opened with back.
    CHECK(settings::dialog_key(dialog, DialogKey::escape) == DialogAction::cancelled);
    CHECK(dialog.chosen.hack_overrides.size() == 2);
}

void restore_defaults_turns_developer_mode_off_and_keeps_the_overrides() {
    settings::Dialog dialog =
        developer_dialog(true, {profiles::HackOverride{"ai.attack-wave-size", true, {}}});
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(!dialog.chosen.developer_mode);
    CHECK(dialog.chosen.hack_overrides.size() == 1);
}

void the_list_scrolls_and_shows_the_focused_row() {
    settings::Dialog dialog = developer_dialog(true);
    // Page Down and End move the list by its own view.
    const auto first = geometry::open_rows(dialog);
    CHECK(settings::dialog_key(dialog, DialogKey::page_down) == DialogAction::redraw);
    CHECK(
        dialog.scroll[kDeveloperScroll] ==
        std::min(first.limit, geometry::developer_view.height - 36)
    );
    CHECK(settings::dialog_key(dialog, DialogKey::end) == DialogAction::redraw);
    CHECK(geometry::open_rows(dialog).scroll == first.limit);
    CHECK(settings::dialog_key(dialog, DialogKey::home) == DialogAction::redraw);
    // The wheel over the dialog too.
    CHECK(wheel(dialog, -1.0F) == DialogAction::redraw);
    CHECK(dialog.scroll[kDeveloperScroll] == geometry::wheel_step);
    CHECK(settings::dialog_key(dialog, DialogKey::home) == DialogAction::redraw);
    // The focus brings the last area into view, and back up to the first.
    const int32_t last = settings::first_hack_list_control +
                         static_cast<int32_t>(settings::developer_areas().size()) - 1;
    dialog.focused = settings::developer_mode_control;
    while (dialog.focused != last)
        CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(geometry::open_rows(dialog).scroll == first.limit);
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(dialog.focused == settings::active_only_control);
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(dialog.focused == settings::restore_profile_control);
    CHECK(settings::dialog_key(dialog, DialogKey::down) == DialogAction::redraw);
    CHECK(dialog.focused == settings::restore_control);
    dialog.focused = settings::first_hack_list_control;
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::redraw);
    CHECK(geometry::open_rows(dialog).scroll == 0);
    // The scroll bar takes a drag in the list's own margin.
    CHECK(
        settings::dialog_pointer_down(dialog, 473, geometry::developer_view.y + 2) ==
        DialogAction::redraw
    );
    CHECK(
        settings::dialog_pointer_move(dialog, 473, geometry::developer_view.y + 400) ==
        DialogAction::redraw
    );
    CHECK(geometry::open_rows(dialog).scroll == geometry::open_rows(dialog).limit);
    CHECK(
        settings::dialog_pointer_up(dialog, 473, geometry::developer_view.y + 400) ==
        DialogAction::redraw
    );
}

void summaries_break_into_lines_the_fonts_hold() {
    for (const auto* hack : profiles::standard_hacks()) {
        const auto lines = geometry::summary_lines(hack->summary);
        CHECK(!lines.empty());
        std::string joined;
        for (const auto& line : lines) {
            CHECK(!line.empty() && line.size() <= geometry::summary_line_characters);
            for (const char letter : line)
                CHECK(letter >= ' ' && letter < 0x7f);
            joined += line;
        }
        std::string expected = geometry::ascii_text(hack->summary);
        std::erase(expected, ' ');
        std::erase(joined, ' ');
        CHECK(joined == expected);
    }
    CHECK(geometry::ascii_text("above 45\xC2\xB0") == "above 45 degrees");
    CHECK(
        geometry::ascii_text(
            "4\xC3\x97 work, a\xC2\xB7"
            "b, \xC2\xB1"
            "100"
        ) == "4x work, a*b, +/-100"
    );
    CHECK(geometry::ascii_text("caf\xC3\xA9!") == "caf?!");
    // A word longer than a line breaks after its last slash that fits.
    const auto broken = geometry::summary_lines(
        "Commands +sharemetal/+shareenergy/+setshare*/+shootall/+noshake work."
    );
    CHECK(broken.size() == 2);
    CHECK(broken[0] == "Commands +sharemetal/+shareenergy/+setshare*/");
    CHECK(broken[1] == "+shootall/+noshake work.");
    CHECK(geometry::list_value_text(profiles::make_string(""), {}) == "\"\"");
}

/// Opens Developer Mode On with every hack on and every area and hack open.
///
/// @return the dialog
settings::Dialog everything_open() {
    std::vector<profiles::HackOverride> on;
    for (const auto* hack : profiles::standard_hacks())
        if (hack->implemented)
            on.push_back(profiles::HackOverride{std::string{hack->id}, true, {}});
    settings::Dialog dialog = developer_dialog(true, on);
    dialog.developer.areas_open.assign(dialog.developer.areas_open.size(), 1);
    dialog.developer.hacks_open.assign(dialog.developer.hacks_open.size(), 1);
    return dialog;
}

void the_open_list_keeps_its_parts_apart() {
    settings::Dialog dialog = everything_open();
    const auto open = geometry::open_rows(dialog);
    const renderer::SourceRect face{
        geometry::edge,
        geometry::edge,
        settings::dialog_width - 2 * geometry::edge,
        settings::dialog_height - 2 * geometry::edge,
    };
    // Every row in the section's columns; a control's focus outline clear of the scroll bar.
    for (const auto& row : open.list.rows) {
        CHECK(row.label.x >= geometry::content_left);
        CHECK(row.label.x + row.label.width <= geometry::content_right);
        if (row.control_area.width > 0)
            CHECK(
                row.control_area.x + row.control_area.width + geometry::focus_inset <
                geometry::developer_scroll.well.x
            );
        if (row.kind == geometry::ListRowKind::slider)
            CHECK(row.stops >= 1 && row.stop >= 0 && row.stop < row.stops);
    }
    // At offsets through the whole list: every part inside the dialog and
    // apart, every part over the list's view wholly in it, and each listed
    // control pressed where it is drawn.
    for (int32_t scroll = 0; scroll <= open.limit + 300; scroll += 300) {
        dialog.scroll[kDeveloperScroll] = std::min(scroll, open.limit);
        const auto parts = settings::dialog_layout(dialog);
        for (std::size_t a = 0; a < parts.size(); ++a) {
            CHECK(parts[a].rect.width > 0 && parts[a].rect.height > 0);
            CHECK(inside(parts[a].rect, face));
            if (overlap(parts[a].rect, geometry::developer_view))
                CHECK(inside(parts[a].rect, geometry::developer_view));
            for (std::size_t b = a + 1; b < parts.size(); ++b)
                if (overlap(parts[a].rect, parts[b].rect)) {
                    std::cerr << "overlap: '" << parts[a].text << "' and '" << parts[b].text
                              << "'\n";
                    CHECK(!overlap(parts[a].rect, parts[b].rect));
                }
        }
        for (const auto& part : parts) {
            if (part.control == settings::no_control)
                continue;
            const Point point = centre(part.rect);
            static_cast<void>(settings::dialog_pointer_move(dialog, point.x, point.y));
            CHECK(dialog.hovered == part.control);
        }
    }
}

void the_developer_section_draws_its_parts() {
    const auto fonts = block_fonts();
    // Closed, an area's arrow points right.
    settings::Dialog closed_list = developer_dialog(true);
    Canvas closed_canvas = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(closed_canvas.surface, {0, 0, 1}, closed_list, fonts, kNoIcon);
    const auto closed_arrow = geometry::open_rows(closed_list).list.rows[0].arrow;
    CHECK(closed_canvas.at(closed_arrow.x + 1, closed_arrow.y) == kHint);
    CHECK(closed_canvas.at(closed_arrow.x, closed_arrow.y + 1) == kPanel);
    settings::Dialog dialog = developer_dialog(true);
    dialog.developer.areas_open[0] = 1;
    Canvas canvas = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(canvas.surface, {0, 0, 1}, dialog, fonts, kNoIcon);
    // Its four lines, Enable Developer Mode's On with the accent, Show
    // performance statistics' and Show Active Only's Off, and the list's
    // scroll bar.
    const auto rows = geometry::open_rows(dialog).rows.rows;
    CHECK(canvas.at(300, geometry::first_row_top) == kRule);
    CHECK(canvas.at(300, rows[1].top) == kRule);
    CHECK(canvas.at(300, geometry::developer_list_rule) == kRule);
    CHECK(canvas.at(300, geometry::developer_footer_rule) == kRule);
    const auto& enable = rows[0].control_area;
    CHECK(canvas.at(enable.x + enable.width - 3, enable.y + 2) == kAccent);
    const auto& stats = rows[1].control_area;
    CHECK(canvas.at(stats.x + 2, stats.y + 2) == kOffSelected);
    const auto& active = geometry::active_only_switch;
    CHECK(canvas.at(active.x + 2, active.y + 2) == kOffSelected);
    CHECK(canvas.at(473, geometry::developer_view.y + 2) == kControlHover);
    CHECK(canvas.at(470, geometry::developer_view.y + 20) == kControlBorder);
    // Open, it points down.
    const auto open = geometry::open_rows(dialog);
    const auto& first = open.list.rows[0];
    CHECK(canvas.at(first.arrow.x, first.arrow.y + 1) == kHint);
    // Nothing of the list is drawn below its view.
    for (const auto& row : open.list.rows)
        if (row.top >= geometry::developer_footer_rule)
            CHECK(canvas.at(row.label.x + 1, geometry::developer_footer_rule + 2) == kPanel);
    // Off, a hack's switch fades, its On without the accent.
    settings::Dialog off = developer_dialog(false, {}, [] {
        auto profile = profiles::base_hack_states();
        profile[settings::developer_areas()[0].hacks[0]].on = true;
        return profile;
    }());
    off.developer.areas_open[0] = 1;
    Canvas still = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(still.surface, {0, 0, 1}, off, fonts, kNoIcon);
    const auto hack = geometry::open_rows(off).list.rows[1];
    CHECK(hack.kind == geometry::ListRowKind::hack && hack.on && hack.locked);
    CHECK(
        still.at(hack.toggle.x + hack.toggle.width - 3, hack.toggle.y + 2) == faded(kControlHover)
    );
}

/// Checks that every row of Developer Mode's list, with everything open and
/// every hack on, fits its places in the game's fonts.
///
/// @param fonts the dialog's fonts
void developer_texts_fit(const settings::DialogFonts& fonts) {
    settings::Dialog dialog = everything_open();
    const auto width = [](const renderer::TextFont& font, std::string_view text) {
        return static_cast<int32_t>(oa::formats::fnt::measure_text(font.font, text));
    };
    const auto fits = [&](const renderer::TextFont& font, std::string_view text, int32_t room) {
        if (width(font, text) <= room)
            return;
        std::cerr << "'" << text << "' is " << width(font, text) << " wide in " << room << '\n';
        CHECK(width(font, text) <= room);
    };
    for (const auto& row : geometry::open_rows(dialog).list.rows) {
        const bool area = row.kind == geometry::ListRowKind::area;
        fits(area ? fonts.regular : fonts.small, row.text, row.label.width);
        if (row.kind == geometry::ListRowKind::slider || area)
            fits(fonts.small, row.shown, row.value.width);
    }
    // Each slider of a hack at each end of its scale.
    for (const auto& row : geometry::open_rows(dialog).list.rows) {
        if (row.kind != geometry::ListRowKind::slider)
            continue;
        const auto same = [&row](const geometry::ListRow& candidate) {
            return candidate.control == row.control;
        };
        for (const bool high : {false, true}) {
            settings::Dialog trial = dialog;
            const auto shown = shown_row(trial, same);
            const auto track = shown.control_area;
            const int32_t x = high ? track.x + track.width - 1 : track.x;
            static_cast<void>(settings::dialog_pointer_down(trial, x, track.y + 5));
            static_cast<void>(settings::dialog_pointer_up(trial, x, track.y + 5));
            const auto placed = shown_row(trial, same);
            fits(fonts.small, placed.shown, placed.value.width);
        }
    }
    const auto hacks = profiles::standard_hacks().size();
    fits(
        fonts.regular, geometry::active_only_text(hacks, hacks), geometry::active_only_label.width
    );
    fits(fonts.small, geometry::restore_profile_text, geometry::restore_profile_button.width - 8);
    for (const auto& area : settings::developer_areas())
        fits(
            fonts.small,
            std::to_string(area.hacks.size()) + " of " + std::to_string(area.hacks.size()) + " on",
            geometry::area_count_width
        );
}

/// Checks that MANAGE\u2026 draws as the other buttons do, in the game's small
/// font with its ellipsis as three full stops, and centred inside its box with
/// clear columns each side at every scale.
///
/// @param fonts the dialog's fonts
void manage_draws_in_the_game_font_inside_its_button(const settings::DialogFonts& fonts) {
    const int32_t width =
        settings::dialog_text_width(fonts, settings::DialogFont::small, "MANAGE…");
    const auto stops =
        static_cast<int32_t>(oa::formats::fnt::measure_text(fonts.small.font, "MANAGE..."));
    CHECK(width == stops);
    CHECK(width + 2 * 3 <= geometry::manage_button_width);
    // The mark between a location's folders, as a greater-than sign.
    CHECK(
        settings::dialog_text_width(
            fonts, settings::DialogFont::small, "On My iPhone › Open Annihilation"
        ) ==
        static_cast<int32_t>(
            oa::formats::fnt::measure_text(fonts.small.font, "On My iPhone > Open Annihilation")
        )
    );
    std::cout << "'MANAGE…' is " << width << " columns in " << geometry::manage_button_width
              << '\n';

    constexpr renderer::Rgb kAccentLight{0xb6, 0xe0, 0x5a};
    constexpr renderer::Rgb kOnAccent{0x10, 0x12, 0x0d};
    for (const bool touch : {false, true}) {
        const settings::Dialog dialog = opened_with_game_files(Page::game_files, touch);
        const auto rows = geometry::open_rows(dialog).rows;
        CHECK(!rows.rows.empty());
        if (rows.rows.empty())
            continue;
        const renderer::SourceRect button = rows.rows.front().control_area;
        CHECK(button.width == geometry::manage_button_width);
        for (const int32_t scale : {1, 2, 3}) {
            Canvas canvas = blank(
                static_cast<uint32_t>(settings::dialog_width * scale),
                static_cast<uint32_t>(settings::dialog_height * scale)
            );
            settings::draw_dialog(canvas.surface, {0, 0, scale}, dialog, fonts, kNoIcon);
            // Inside the button only its face, its edge and its caption, in the
            // text's colour on the face, show.
            int32_t left = button.width * scale;
            int32_t right = -1;
            int32_t top = button.height * scale;
            int32_t bottom = -1;
            bool clean = true;
            for (int32_t y = 0; y < button.height * scale; ++y)
                for (int32_t x = 0; x < button.width * scale; ++x) {
                    const renderer::Rgb seen =
                        canvas.at(button.x * scale + x, button.y * scale + y);
                    if (seen == kAccent || seen == kAccentLight)
                        continue;
                    // The caption's pixels, its letters' edges mixed into the face.
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                    for (std::size_t channel = 0; channel < seen.size(); ++channel)
                        if (seen[channel] < kOnAccent[channel] ||
                            seen[channel] > kAccent[channel]) {
                            if (clean)
                                std::cerr << "MANAGE… at scale " << scale << ": colour "
                                          << int{seen[0]} << ',' << int{seen[1]} << ','
                                          << int{seen[2]} << " at " << x << ',' << y << '\n';
                            clean = false;
                        }
                }
            CHECK(clean);
            CHECK(right >= left);
            // Clear columns each side, the caption centred as every caption is,
            // by its letters' advances: within the font's two columns of bearing.
            const int32_t before = left;
            const int32_t after = button.width * scale - 1 - right;
            CHECK(before >= 3 * scale && after >= 3 * scale);
            CHECK(std::abs(before - after) <= 2 * scale);
            CHECK(top >= scale && button.height * scale - 1 - bottom >= scale);
            // Nothing of the caption spills past the button's right edge.
            for (int32_t y = 0; y < button.height * scale; ++y)
                for (int32_t x = 0; x < 4 * scale; ++x) {
                    const int32_t column = (button.x + button.width) * scale + x;
                    if (column < static_cast<int32_t>(canvas.surface.width))
                        CHECK(canvas.at(column, button.y * scale + y) != kOnAccent);
                }
            if (!clean || before < 3 * scale || after < 3 * scale ||
                std::abs(before - after) > 2 * scale)
                std::cerr << "MANAGE… at scale " << scale << ": " << before << " and " << after
                          << " clear columns\n";
        }
    }
}

/// The player's own folder as a host gives it to the dialog.
constexpr std::string_view kUserFolder = "/home/player/Documents/Open Annihilation";

/// Returns Common Tweaks with the player's own folder, whose first row is Your files.
settings::Dialog your_files_dialog() {
    settings::Dialog dialog = opened(Page::common_tweaks);
    dialog.user_folder = std::string(kUserFolder);
    return dialog;
}

void your_files_shows_the_folder_and_opens_its_folders() {
    settings::Dialog dialog = your_files_dialog();
    const auto open = geometry::open_rows(dialog);
    CHECK(open.rows.rows.size() == 3);
    const geometry::Row row = open.rows.rows[0];
    CHECK(row.setting == Setting::user_folder);
    CHECK(row.lock == Lock::none);
    CHECK(geometry::is_buttons(Setting::user_folder) && !geometry::is_switch(Setting::user_folder));
    // Its three buttons stand on its label line at the right, apart.
    CHECK(row.control_area.width == geometry::folder_buttons_width);
    CHECK(row.control_area.x + row.control_area.width == geometry::content_right);
    CHECK(row.control_area.y == row.label.y);
    CHECK(row.label.x + row.label.width < row.control_area.x);
    const auto parts = settings::dialog_layout(dialog);
    const std::array<std::string_view, 3> captions{"SAVES", "SCREENSHOTS", "MODS"};
    for (std::size_t index = 0; index < captions.size(); ++index) {
        const auto* part = find_part(parts, captions[index], settings::no_control);
        CHECK(part != nullptr && part->control == row.control);
        CHECK(
            part != nullptr &&
            same_rect(part->rect, geometry::folder_button(row.control_area, index))
        );
    }
    CHECK(find_part(parts, "Your files", settings::no_control) != nullptr);
    CHECK(find_part(parts, kUserFolder, settings::no_control) != nullptr);
    CHECK(find_part(parts, geometry::user_folder_hint_text, settings::no_control) != nullptr);
    // A click on each button asks for its folder and marks it.
    for (std::size_t index = 0; index < settings::folder_button_count; ++index) {
        const auto button = geometry::folder_button(row.control_area, index);
        CHECK(click(dialog, centre(button)) == DialogAction::open_folder);
        CHECK(dialog.folder_to_open == static_cast<settings::FolderButton>(index));
        CHECK(dialog.folder_marked == static_cast<settings::FolderButton>(index));
    }
    // A press on one button released on another opens nothing, nor a click
    // between two buttons.
    const auto saves = centre(geometry::folder_button(row.control_area, 0));
    const auto mods = centre(geometry::folder_button(row.control_area, 2));
    static_cast<void>(settings::dialog_pointer_down(dialog, saves.x, saves.y));
    CHECK(settings::dialog_pointer_up(dialog, mods.x, mods.y) == DialogAction::redraw);
    const auto first = geometry::folder_button(row.control_area, 0);
    CHECK(
        click(dialog, {first.x + first.width + geometry::folder_button_gap / 2, first.y + 2}) ==
        DialogAction::redraw
    );
    // The pointer lights the button under it.
    CHECK(settings::dialog_pointer_move(dialog, mods.x, mods.y) != DialogAction::open_folder);
    CHECK(dialog.hovered == row.control && dialog.folder_hovered == 2);
    CHECK(settings::dialog_pointer_move(dialog, saves.x, saves.y) == DialogAction::redraw);
    CHECK(dialog.folder_hovered == 0);
    // Keys: Space presses the marked button; Left and Right move the mark
    // and stop at the ends.
    dialog.folder_marked = settings::FolderButton::saves;
    dialog.focused = row.control;
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::open_folder);
    CHECK(dialog.folder_to_open == settings::FolderButton::saves);
    CHECK(settings::dialog_key(dialog, DialogKey::left) != DialogAction::open_folder);
    CHECK(dialog.folder_marked == settings::FolderButton::saves);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::redraw);
    CHECK(dialog.folder_marked == settings::FolderButton::mods);
    CHECK(settings::dialog_key(dialog, DialogKey::right) != DialogAction::redraw);
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::open_folder);
    CHECK(dialog.folder_to_open == settings::FolderButton::mods);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::redraw);
    CHECK(dialog.folder_marked == settings::FolderButton::screenshots);
    // Enter still keeps the dialog's settings; the row changes none.
    CHECK(dialog.chosen == dialog.opened);
    // A folder that cannot be opened is said on the second hint line, in
    // amber, until a folder opens.
    CHECK(
        settings::set_folder_notice(dialog, "The file manager could not open it.") ==
        DialogAction::redraw
    );
    auto hint = geometry::row_hint(dialog, Setting::user_folder, 1);
    CHECK(hint.notice && hint.text == "The file manager could not open it.");
    CHECK(
        settings::set_folder_notice(dialog, "The file manager could not open it.") ==
        DialogAction::none
    );
    CHECK(settings::set_folder_notice(dialog, {}) == DialogAction::redraw);
    hint = geometry::row_hint(dialog, Setting::user_folder, 1);
    CHECK(!hint.notice && hint.text == geometry::user_folder_hint_text);
    // A long folder shows its tail that fits the hint line.
    dialog.user_folder = "/home/player/" + std::string(30, 'd') + "/" + std::string(30, 'e') + "/" +
                         std::string(kUserFolder.substr(1));
    const auto long_parts = settings::dialog_layout(dialog);
    const settings::LayoutPart* tail = nullptr;
    for (const auto& part : long_parts)
        if (part.text.starts_with(geometry::path_ellipsis) &&
            part.text.ends_with("Open Annihilation"))
            tail = &part;
    CHECK(tail != nullptr);
    CHECK(
        tail != nullptr &&
        one_a_character(tail->text) * settings::estimated_character_width <= tail->rect.width
    );
    // Restore defaults leaves the folder and the mark alone.
    dialog.focused = settings::no_control;
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.folder_marked == settings::FolderButton::screenshots);
}

/// The offered mod folders' titles and paths of Mods' tests: Zeta, alpha,
/// and Beta, whose folder holds no oamod.yaml.
const std::vector<std::string> kModNames{"Zeta", "alpha", "Beta"};
const std::vector<std::string> kModFolders{
    "/games/ta/mods/zeta",
    "/games/ta/mods/alpha",
    "/games/ta/mods/Beta",
};

/// The colour of Zeta's badge.
constexpr renderer::Rgb kBadgeColor{0xc0, 0x30, 0x30};

/// Returns what Mods shows of kModFolders: Zeta's version, description and
/// a 2 by 2 badge of kBadgeColor; alpha's version and description and no
/// badge; Beta's folder, without an oamod.yaml.
std::vector<settings::ModDetails> offered_details() {
    std::vector<settings::ModDetails> details(kModFolders.size());
    details[0].version = "1.2";
    details[0].description = "Faster tanks and longer ranges.";
    details[0].badge_width = 2;
    details[0].badge_height = 2;
    for (int32_t pixel = 0; pixel < 4; ++pixel) {
        details[0].badge_pixels.insert(
            details[0].badge_pixels.end(), kBadgeColor.begin(), kBadgeColor.end()
        );
        details[0].badge_pixels.push_back(0xff);
    }
    details[1].version = "0.9";
    details[1].description = "New maps.";
    details[2].has_profile = false;
    return details;
}

/// Opens the dialog on Mods with kModFolders offered.
///
/// @param playing the mod folder the game plays now; empty for none
/// @param locks what cannot be changed now
/// @return the dialog
settings::Dialog mods_dialog(std::string_view playing = {}, const settings::Locks& locks = {}) {
    const auto details = offered_details();
    settings::Dialog dialog;
    settings::open_dialog(
        dialog,
        settings::EngineSettings{},
        settings::EngineSettings{},
        locks,
        "v0.2.0",
        Page::mods,
        {},
        settings::highest_unit_limit,
        settings::ModOffer{kModNames, kModFolders, details, playing}
    );
    return dialog;
}

/// Returns the place among Mods' rows of the row that lists an offered mod
/// folder.
///
/// @param dialog the dialog
/// @param offered the folder's place among Dialog::mod_folders; no_mod_row for No Mod
/// @return the row's place; the rows' count when none lists it
std::size_t listed_at(const settings::Dialog& dialog, int32_t offered) {
    const auto rows = settings::mod_rows(dialog);
    for (std::size_t index = 0; index < rows.size(); ++index)
        if (rows[index].offered == offered)
            return index;
    return rows.size();
}

/// Returns the control of the row of Mods that lists an offered mod folder.
int32_t mod_control(const settings::Dialog& dialog, int32_t offered) {
    return settings::first_row_control + static_cast<int32_t>(listed_at(dialog, offered));
}

/// Returns the middle of the row of Mods that lists an offered mod folder.
Point mod_point(const settings::Dialog& dialog, int32_t offered) {
    return centre(geometry::open_rows(dialog).rows.rows[listed_at(dialog, offered)].control_area);
}

void mods_lists_the_mod_played_first_then_no_mod_then_the_others_by_title() {
    // While no mod plays, No Mod comes first, as the one played; the others
    // follow by title, whatever the case of its letters.
    settings::Dialog dialog = mods_dialog();
    auto rows = settings::mod_rows(dialog);
    CHECK(rows.size() == 4);
    CHECK(rows[0].offered == settings::no_mod_row && rows[0].playing);
    CHECK(rows[1].offered == 1 && rows[2].offered == 2 && rows[3].offered == 0);
    CHECK(!rows[1].playing && !rows[2].playing && !rows[3].playing);
    // The mod played comes first, then No Mod, then the others by title.
    dialog = mods_dialog(kModFolders[0]);
    rows = settings::mod_rows(dialog);
    CHECK(rows.size() == 4);
    CHECK(rows[0].offered == 0 && rows[0].playing);
    CHECK(rows[1].offered == settings::no_mod_row && !rows[1].playing);
    CHECK(rows[2].offered == 1 && rows[3].offered == 2);
    CHECK(!rows[2].playing && !rows[3].playing);
    // A control a row, each pressed where it lies, then OPEN MODS FOLDER
    // under the list, and the note under the button.
    const auto open = geometry::open_rows(dialog);
    CHECK(open.rows.rows.size() == rows.size());
    const auto parts = settings::dialog_layout(dialog);
    for (std::size_t index = 0; index < open.rows.rows.size(); ++index) {
        const auto& row = open.rows.rows[index];
        CHECK(row.setting == Setting::mod && row.lock == Lock::none);
        CHECK(row.control == settings::first_row_control + static_cast<int32_t>(index));
        CHECK(inside(row.control_area, open.area.view));
        const auto* part = find_part(parts, {}, row.control);
        CHECK(part != nullptr && same_rect(part->rect, row.control_area));
    }
    const int32_t folder = geometry::mods_folder_control(open.rows);
    CHECK(folder == settings::first_row_control + 4);
    const auto* button = find_part(parts, geometry::open_mods_folder_text, settings::no_control);
    CHECK(button != nullptr && button->control == folder);
    CHECK(button != nullptr && same_rect(button->rect, geometry::mods_folder_button));
    CHECK(open.area.view.y + open.area.view.height <= geometry::mods_folder_button.y);
    CHECK(
        geometry::mods_folder_button.y + geometry::mods_folder_button.height <=
        geometry::mods_note_first.y
    );
    for (const std::string_view text :
         {std::string_view{"MODS"}, geometry::mods_folders_text[0], geometry::mods_folders_text[1]})
        CHECK(find_part(parts, text, settings::no_control) != nullptr);
    CHECK(find_part(parts, geometry::mod_in_game_text, settings::no_control) == nullptr);
    CHECK(find_part(parts, geometry::mod_from_command_line_text, settings::no_control) == nullptr);
}

void each_mod_row_shows_its_badge_title_version_and_description() {
    settings::Dialog dialog = mods_dialog(kModFolders[0]);
    const auto rows = settings::mod_rows(dialog);
    // The mod played: its title, version, description and badge.
    auto shown = geometry::mod_row_text(dialog, rows[0]);
    CHECK(shown.title == "Zeta" && shown.version == "1.2");
    CHECK(shown.description == "Faster tanks and longer ranges.");
    CHECK(shown.has_profile && shown.details == &dialog.mod_details[0]);
    // No Mod: the game's own rules, as 3.1c plays them.
    shown = geometry::mod_row_text(dialog, rows[1]);
    CHECK(shown.title == "No Mod" && shown.version == "3.1c");
    CHECK(shown.description == geometry::no_mod_description_text);
    CHECK(shown.has_profile && shown.details == nullptr);
    // A folder without an oamod.yaml: its folder's name, N/A, and that it
    // has none.
    shown = geometry::mod_row_text(dialog, rows[3]);
    CHECK(shown.title == "Beta" && shown.version == "N/A");
    CHECK(shown.description == "No oamod.yaml present" && !shown.has_profile);
    // A folder the host read nothing of shows its title alone.
    dialog.mod_details.resize(1);
    shown = geometry::mod_row_text(dialog, rows[2]);
    CHECK(shown.title == "alpha" && shown.version.empty() && shown.description.empty());
    CHECK(shown.details == nullptr);

    // Drawn: Zeta's badge, No Mod's in the green outline of the OA mark,
    // and a dashed square for alpha, which has no badge.
    const auto fonts = block_fonts();
    Canvas canvas = blank(settings::dialog_width, settings::dialog_height);
    dialog = mods_dialog(kModFolders[0]);
    settings::draw_dialog(canvas.surface, {0, 0, 1}, dialog, fonts, kNoIcon);
    const auto open = geometry::open_rows(dialog);
    const auto badge_of = [&open](std::size_t index) {
        const auto& box = open.rows.rows[index].control_area;
        return renderer::SourceRect{
            box.x + geometry::mod_row_inset,
            box.y + (box.height - geometry::mod_badge_side) / 2,
            geometry::mod_badge_side,
            geometry::mod_badge_side,
        };
    };
    const auto zeta = badge_of(listed_at(dialog, 0));
    CHECK(canvas.at(zeta.x + zeta.width / 2, zeta.y + zeta.height / 2) == kBadgeColor);
    const auto no_mod = badge_of(listed_at(dialog, settings::no_mod_row));
    CHECK(canvas.at(no_mod.x, no_mod.y + no_mod.height / 2) == kAccent);
    const auto alpha = badge_of(listed_at(dialog, 1));
    CHECK(canvas.at(alpha.x, alpha.y) == kControlBorder);
    CHECK(canvas.at(alpha.x + 2, alpha.y) == kList);
    CHECK(canvas.at(alpha.x + alpha.width / 2, alpha.y + alpha.height / 2) == kList);
}

void long_mod_texts_are_cut_with_an_ellipsis() {
    const auto bytes = [](std::string_view text) { return static_cast<int32_t>(text.size()); };
    // A text that fits shows whole; else as much of its start as fits
    // before "...", in whole characters; else "..." alone.
    CHECK(geometry::cut_text("Zeta", 4, bytes) == "Zeta");
    CHECK(geometry::cut_text("abcdefghij", 7, bytes) == "abcd...");
    CHECK(
        geometry::cut_text(
            "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 5, one_a_character
        ) == "\xC3\xA9\xC3\xA9..."
    );
    CHECK(geometry::cut_text("abcdefghij", 2, bytes) == "...");
    const auto estimate = [](std::string_view text) {
        return one_a_character(text) * settings::estimated_character_width;
    };
    // The question's title is cut to its line, and its text to its lines.
    settings::Dialog dialog = mods_dialog();
    dialog.mod_names[1].clear();
    for (int32_t word = 0; word < 40; ++word)
        dialog.mod_names[1] += "wide ";
    CHECK(click(dialog, mod_point(dialog, 1)) == DialogAction::redraw);
    CHECK(dialog.switch_question == 1);
    bool cut = false;
    for (const auto& part : settings::dialog_layout(dialog)) {
        if (!same_rect(part.rect, geometry::question_title))
            continue;
        cut = part.text.starts_with("wide wide") && part.text.ends_with("...");
        CHECK(estimate(part.text) <= part.rect.width);
    }
    CHECK(cut);
    auto lines = geometry::question_text_lines(dialog, estimate);
    CHECK(lines.size() == geometry::question_lines);
    CHECK(!lines.empty() && lines.back().ends_with("..."));
    for (const auto& line : lines)
        CHECK(estimate(line) <= geometry::question_first_line.width);
    // A short title's question shows whole.
    dialog.switch_question = 0;
    lines = geometry::question_text_lines(dialog, estimate);
    CHECK(lines.size() <= geometry::question_lines);
    std::string joined;
    for (const auto& line : lines)
        joined += (joined.empty() ? "" : " ") + line;
    CHECK(
        joined == std::string(geometry::switch_ask_before_text) + "Zeta" +
                      std::string(geometry::switch_ask_after_text)
    );
}

/// Opens Mods with more mod folders than its view holds rows for.
///
/// @param count the mod folders offered
/// @return the dialog
settings::Dialog many_mods_dialog(std::size_t count) {
    std::vector<std::string> names;
    std::vector<std::string> folders;
    for (std::size_t index = 0; index < count; ++index) {
        const std::string number = (index < 10 ? "0" : "") + std::to_string(index);
        names.push_back("Mod " + number);
        folders.push_back("/games/ta/mods/" + number);
    }
    settings::Dialog dialog;
    settings::open_dialog(
        dialog,
        settings::EngineSettings{},
        settings::EngineSettings{},
        {},
        "v0.2.0",
        Page::mods,
        {},
        settings::highest_unit_limit,
        settings::ModOffer{names, folders, {}, {}}
    );
    return dialog;
}

void the_mods_list_scrolls_while_its_button_and_note_stay() {
    settings::Dialog dialog = many_mods_dialog(20);
    const auto first = geometry::open_rows(dialog);
    const int32_t stride = geometry::mod_row_height + geometry::mod_row_gap;
    CHECK(first.rows.rows.size() == 21);
    CHECK(first.content_height == 21 * stride - geometry::mod_row_gap);
    CHECK(first.limit > 0 && first.limit == first.content_height - first.area.view.height);
    const auto offset = [&dialog] { return dialog.scroll[static_cast<std::size_t>(Page::mods)]; };
    // The wheel scrolls the list 24 rows a notch.
    const Point in_list = centre(first.area.view);
    CHECK(settings::dialog_wheel(dialog, in_list.x, in_list.y, -1.0F) == DialogAction::redraw);
    CHECK(offset() == 24);
    CHECK(settings::dialog_wheel(dialog, in_list.x, in_list.y, 1.0F) == DialogAction::redraw);
    CHECK(offset() == 0);
    CHECK(settings::dialog_wheel(dialog, in_list.x, in_list.y, 1.0F) == DialogAction::none);
    // Page Down and Page Up scroll by the view less a row; End and Home to
    // the list's ends.
    CHECK(first.area.page_step == first.area.view.height - stride);
    CHECK(settings::dialog_key(dialog, DialogKey::page_down) == DialogAction::redraw);
    CHECK(offset() == first.area.page_step);
    CHECK(settings::dialog_key(dialog, DialogKey::end) == DialogAction::redraw);
    CHECK(offset() == first.limit);
    CHECK(settings::dialog_key(dialog, DialogKey::end) == DialogAction::none);
    CHECK(settings::dialog_key(dialog, DialogKey::page_up) == DialogAction::redraw);
    CHECK(offset() == first.limit - first.area.page_step);
    CHECK(settings::dialog_key(dialog, DialogKey::home) == DialogAction::redraw);
    CHECK(offset() == 0);
    // A press on the scroll bar's well at its foot scrolls towards the end,
    // and a drag past it shows the end.
    const auto& hit = first.area.hit;
    const int32_t bar = hit.x + hit.width / 2;
    static_cast<void>(settings::dialog_pointer_move(dialog, bar, hit.y + hit.height - 1));
    static_cast<void>(settings::dialog_pointer_down(dialog, bar, hit.y + hit.height - 1));
    CHECK(offset() > 0);
    static_cast<void>(settings::dialog_pointer_move(dialog, bar, hit.y + hit.height + 40));
    CHECK(offset() == first.limit);
    static_cast<void>(settings::dialog_pointer_up(dialog, bar, hit.y + hit.height + 40));
    // Space on the last row brings it into view before it asks.
    dialog.scroll[static_cast<std::size_t>(Page::mods)] = 0;
    CHECK(
        key_on(dialog, settings::first_row_control + 20, DialogKey::space) == DialogAction::redraw
    );
    CHECK(offset() == first.limit && dialog.switch_question == 19);
    CHECK(settings::dialog_key(dialog, DialogKey::no) == DialogAction::redraw);
    // At every offset the button, the note and the view stay put, and only
    // the rows wholly in the view are listed, clear of the button.
    const int32_t folder = geometry::mods_folder_control(first.rows);
    for (int32_t scroll = 0; scroll <= first.limit; scroll += 5) {
        dialog.scroll[static_cast<std::size_t>(Page::mods)] = scroll;
        const auto parts = settings::dialog_layout(dialog);
        const auto* button =
            find_part(parts, geometry::open_mods_folder_text, settings::no_control);
        CHECK(button != nullptr && same_rect(button->rect, geometry::mods_folder_button));
        CHECK(button != nullptr && button->control == folder);
        const auto* note = find_part(parts, geometry::mods_folders_text[1], settings::no_control);
        CHECK(note != nullptr && same_rect(note->rect, geometry::mods_note_second));
        const auto open = geometry::open_rows(dialog);
        CHECK(open.scroll == scroll && same_rect(open.area.view, first.area.view));
        std::size_t listed = 0;
        for (const auto& row : open.rows.rows) {
            const auto* part = find_part(parts, {}, row.control);
            const bool whole = inside(row.control_area, open.area.view);
            CHECK((part != nullptr) == whole);
            if (part != nullptr)
                CHECK(!overlap(part->rect, geometry::mods_folder_button));
            listed += whole ? 1 : 0;
        }
        CHECK(listed >= 4);
    }
}

void choosing_another_mod_asks_before_switching() {
    settings::Dialog dialog = mods_dialog(kModFolders[0]);
    const settings::EngineSettings before = dialog.chosen;
    // The row of the mod played asks nothing.
    static_cast<void>(click(dialog, mod_point(dialog, 0)));
    CHECK(dialog.switch_question == settings::no_question);
    // Another row asks the Switch Mod question, SWITCH marked, over the
    // dialog.
    const Point alpha = mod_point(dialog, 1);
    CHECK(click(dialog, alpha) == DialogAction::redraw);
    CHECK(dialog.switch_question == 1 && !dialog.question_marks_no);
    auto parts = settings::dialog_layout(dialog);
    for (const std::string_view text :
         {geometry::switch_heading_text, std::string_view{"alpha"}, std::string_view{"0.9"}})
        CHECK(find_part(parts, text, settings::no_control) != nullptr);
    const auto* yes = find_part(parts, {}, settings::question_yes_control);
    CHECK(yes != nullptr && yes->text == "SWITCH");
    CHECK(yes != nullptr && same_rect(yes->rect, geometry::question_yes_button));
    const auto* no = find_part(parts, {}, settings::question_no_control);
    CHECK(no != nullptr && no->text == "CANCEL");
    CHECK(no != nullptr && same_rect(no->rect, geometry::question_no_button));
    for (const auto& part : parts)
        CHECK(
            !overlap(part.rect, geometry::question_box) || inside(part.rect, geometry::question_box)
        );
    // A press elsewhere and the wheel do nothing while it shows.
    CHECK(click(dialog, centre(geometry::ok_button)) == DialogAction::none);
    CHECK(settings::dialog_wheel(dialog, alpha.x, alpha.y, -1.0F) == DialogAction::none);
    CHECK(dialog.switch_question == 1);
    // CANCEL leaves the settings chosen as they were.
    CHECK(click(dialog, centre(geometry::question_no_button)) == DialogAction::redraw);
    CHECK(dialog.switch_question == settings::no_question && dialog.chosen == before);
    // SWITCH chooses the mod and asks the host to switch to it.
    CHECK(click(dialog, alpha) == DialogAction::redraw);
    CHECK(click(dialog, centre(geometry::question_yes_button)) == DialogAction::switch_mod);
    CHECK(dialog.switch_question == settings::no_question);
    CHECK(dialog.chosen.mod_folder == kModFolders[1]);
    // No Mod clears the mod chosen.
    dialog = mods_dialog(kModFolders[0]);
    dialog.chosen.mod_folder = kModFolders[0];
    CHECK(click(dialog, mod_point(dialog, settings::no_mod_row)) == DialogAction::redraw);
    CHECK(dialog.switch_question == settings::no_mod_row);
    parts = settings::dialog_layout(dialog);
    CHECK(find_part(parts, "No Mod", settings::no_control) != nullptr);
    CHECK(find_part(parts, "3.1c", settings::no_control) != nullptr);
    CHECK(click(dialog, centre(geometry::question_yes_button)) == DialogAction::switch_mod);
    CHECK(dialog.chosen.mod_folder.empty());
    // The question for a folder without an oamod.yaml adds a line saying
    // the game's own rules apply.
    dialog = mods_dialog(kModFolders[0]);
    dialog.switch_question = 1;
    auto lines = geometry::question_text_lines(dialog, one_a_character);
    CHECK(
        lines.size() == 1 && lines[0] == std::string(geometry::switch_ask_before_text) + "alpha" +
                                             std::string(geometry::switch_ask_after_text)
    );
    dialog.switch_question = settings::no_question;
    CHECK(click(dialog, mod_point(dialog, 2)) == DialogAction::redraw);
    CHECK(dialog.switch_question == 2);
    lines = geometry::question_text_lines(dialog, one_a_character);
    CHECK(lines.size() == 2 && lines[0].starts_with("Switch to Beta now?"));
    CHECK(lines.size() == 2 && lines[1] == geometry::switch_no_profile_text);
    parts = settings::dialog_layout(dialog);
    CHECK(find_part(parts, "N/A", settings::no_control) != nullptr);
    bool noted = false;
    for (const auto& part : parts)
        noted = noted || (part.text.starts_with("This folder has no oamod.yaml") &&
                          inside(part.rect, geometry::question_box));
    CHECK(noted);
    CHECK(click(dialog, centre(geometry::question_yes_button)) == DialogAction::switch_mod);
    CHECK(dialog.chosen.mod_folder == kModFolders[2]);
}

void the_keys_answer_the_switch_mod_question() {
    settings::Dialog dialog = mods_dialog(kModFolders[0]);
    const int32_t alpha = mod_control(dialog, 1);
    // Y and N do nothing while no question shows, and Space on the row of
    // the mod played asks nothing.
    CHECK(settings::dialog_key(dialog, DialogKey::yes) == DialogAction::none);
    CHECK(settings::dialog_key(dialog, DialogKey::no) == DialogAction::none);
    CHECK(key_on(dialog, mod_control(dialog, 0), DialogKey::space) == DialogAction::none);
    CHECK(dialog.switch_question == settings::no_question);
    // Space on another row asks, SWITCH marked; Left marks CANCEL, Right
    // SWITCH, and Tab and Shift+Tab the other button.
    CHECK(key_on(dialog, alpha, DialogKey::space) == DialogAction::redraw);
    CHECK(dialog.switch_question == 1 && !dialog.question_marks_no);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::none);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::redraw);
    CHECK(dialog.question_marks_no);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::none);
    CHECK(settings::dialog_key(dialog, DialogKey::tab) == DialogAction::redraw);
    CHECK(!dialog.question_marks_no);
    CHECK(settings::dialog_key(dialog, DialogKey::back_tab) == DialogAction::redraw);
    CHECK(dialog.question_marks_no);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::redraw);
    CHECK(!dialog.question_marks_no);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::redraw);
    // Enter answers the button marked: CANCEL.
    CHECK(settings::dialog_key(dialog, DialogKey::enter) == DialogAction::redraw);
    CHECK(dialog.switch_question == settings::no_question && dialog.chosen.mod_folder.empty());
    // N and Escape answer CANCEL; Escape leaves the dialog open.
    for (const DialogKey answer : {DialogKey::no, DialogKey::escape}) {
        CHECK(key_on(dialog, alpha, DialogKey::space) == DialogAction::redraw);
        CHECK(settings::dialog_key(dialog, answer) == DialogAction::redraw);
        CHECK(dialog.switch_question == settings::no_question);
        CHECK(dialog.chosen.mod_folder.empty());
    }
    // Y, and Enter or Space while SWITCH is marked, answer SWITCH.
    for (const DialogKey answer : {DialogKey::yes, DialogKey::enter, DialogKey::space}) {
        dialog = mods_dialog(kModFolders[0]);
        CHECK(key_on(dialog, alpha, DialogKey::space) == DialogAction::redraw);
        CHECK(settings::dialog_key(dialog, answer) == DialogAction::switch_mod);
        CHECK(dialog.switch_question == settings::no_question);
        CHECK(dialog.chosen.mod_folder == kModFolders[1]);
    }
}

void mods_locks_during_a_game_and_by_the_command_line() {
    const auto fonts = block_fonts();
    const settings::Dialog unlocked = mods_dialog(kModFolders[0]);
    Canvas unlocked_canvas = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(unlocked_canvas.surface, {0, 0, 1}, unlocked, fonts, kNoIcon);
    const auto unlocked_rows = geometry::open_rows(unlocked);
    for (const Lock lock : {Lock::in_game, Lock::command_line}) {
        settings::Locks locks{};
        locks.mod = lock;
        settings::Dialog dialog = mods_dialog(kModFolders[0], locks);
        const auto open = geometry::open_rows(dialog);
        // The list lies under the line that says why it is locked.
        CHECK(open.area.view.y >= geometry::mods_lock_line.y + geometry::mods_lock_line.height);
        const auto parts = settings::dialog_layout(dialog);
        const std::string_view why = lock == Lock::in_game ? geometry::mod_in_game_text
                                                           : geometry::mod_from_command_line_text;
        // The reason fills as many of its two lines as it needs.
        std::string said;
        for (const auto& part : parts)
            if (part.control == settings::no_control && inside(part.rect, geometry::mods_lock_line))
                said += (said.empty() ? "" : " ") + part.text;
        CHECK(said == why);
        if (lock == Lock::in_game)
            CHECK(why == "Locked during a game. Choose the mod from the main menu.");
        // Every row is inert: none is a control, and neither a press nor
        // Space on one asks.
        for (const auto& row : open.rows.rows) {
            CHECK(row.lock == lock);
            CHECK(find_part(parts, {}, row.control) == nullptr);
            CHECK(click(dialog, centre(row.control_area)) == DialogAction::none);
            CHECK(key_on(dialog, row.control, DialogKey::space) == DialogAction::none);
            CHECK(dialog.switch_question == settings::no_question);
        }
        // OPEN MODS FOLDER shows, but opens nothing.
        const auto* button =
            find_part(parts, geometry::open_mods_folder_text, settings::no_control);
        CHECK(button != nullptr && button->control == settings::no_control);
        CHECK(click(dialog, centre(geometry::mods_folder_button)) == DialogAction::none);
        // The focus passes over the rows and the button to the footer.
        dialog.focused = settings::no_control;
        static_cast<void>(settings::dialog_key(dialog, DialogKey::tab));
        CHECK(dialog.focused == settings::restore_control);
        CHECK(dialog.chosen == dialog.opened);
        // The rows other than the mod played are drawn dimmed.
        Canvas canvas = blank(settings::dialog_width, settings::dialog_height);
        settings::draw_dialog(canvas.surface, {0, 0, 1}, dialog, fonts, kNoIcon);
        for (std::size_t index = 0; index < open.rows.rows.size(); ++index) {
            const auto& box = open.rows.rows[index].control_area;
            const auto& unlocked_box = unlocked_rows.rows.rows[index].control_area;
            const auto at = canvas.at(box.x + box.width - 3, box.y + box.height - 3);
            const auto unlocked_at = unlocked_canvas.at(
                unlocked_box.x + unlocked_box.width - 3, unlocked_box.y + unlocked_box.height - 3
            );
            CHECK((at == unlocked_at) == (index == 0));
        }
    }
}

void open_mods_folder_asks_for_the_mods_folder() {
    settings::Dialog dialog = mods_dialog();
    CHECK(dialog.folder_to_open == settings::FolderButton::saves);
    CHECK(click(dialog, centre(geometry::mods_folder_button)) == DialogAction::open_folder);
    CHECK(dialog.folder_to_open == settings::FolderButton::mods);
    // The focus reaches it after the last row, and Space presses it.
    dialog.folder_to_open = settings::FolderButton::saves;
    const int32_t folder = geometry::mods_folder_control(geometry::open_rows(dialog).rows);
    dialog.focused = folder - 1;
    static_cast<void>(settings::dialog_key(dialog, DialogKey::tab));
    CHECK(dialog.focused == folder);
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::open_folder);
    CHECK(dialog.folder_to_open == settings::FolderButton::mods);
    // A folder that cannot be opened is said in place of the note's second
    // line.
    CHECK(
        settings::set_folder_notice(dialog, "The folder cannot be made.") == DialogAction::redraw
    );
    const auto parts = settings::dialog_layout(dialog);
    const auto* notice = find_part(parts, "The folder cannot be made.", settings::no_control);
    CHECK(notice != nullptr && same_rect(notice->rect, geometry::mods_note_second));
    CHECK(find_part(parts, geometry::mods_folders_text[1], settings::no_control) == nullptr);
}

/// A notice as the main menu shows it after the saved games moved.
settings::Notice moved_notice() {
    settings::Notice notice;
    notice.title = "SAVED GAMES MOVED";
    notice.paragraphs = {
        {"3 saved games moved to:", false},
        {"/home/player/Documents/Open Annihilation/Saves", true},
        {"Screenshots, films and mods now go in the same Open Annihilation folder.", false},
    };
    notice.open_caption = "OPEN FOLDER";
    return notice;
}

void the_notice_wraps_its_text_and_places_its_buttons() {
    settings::Notice notice = moved_notice();
    const int32_t height = settings::notice_height(notice);
    CHECK(height >= settings::least_notice_height && height <= settings::greatest_notice_height);
    const auto parts = settings::notice_layout(notice);
    const renderer::SourceRect whole{0, 0, settings::notice_width, height};
    for (std::size_t a = 0; a < parts.size(); ++a) {
        CHECK(inside(parts[a].rect, whole));
        for (std::size_t b = a + 1; b < parts.size(); ++b)
            CHECK(!overlap(parts[a].rect, parts[b].rect));
    }
    CHECK(find_part(parts, "SAVED GAMES MOVED", settings::no_control) != nullptr);
    CHECK(find_part(parts, "3 saved games moved to:", settings::no_control) != nullptr);
    CHECK(find_part(parts, {}, settings::notice_ok_control) != nullptr);
    CHECK(find_part(parts, {}, settings::notice_open_control) != nullptr);
    // The path's lines, put together, give the path whole, each in the
    // regular font.
    std::string path;
    for (const auto& part : parts)
        if (part.font == settings::DialogFont::regular && part.control == settings::no_control &&
            !part.text.empty() && part.text != "SAVED GAMES MOVED")
            path += part.text;
    CHECK(path == "/home/player/Documents/Open Annihilation/Saves");
    // A failure adds its amber line and the notice grows.
    notice.failure = "The file manager could not be opened.";
    CHECK(settings::notice_height(notice) > height);
    CHECK(
        find_part(settings::notice_layout(notice), notice.failure, settings::no_control) != nullptr
    );

    // A long path breaks after its separators, a long component within it,
    // and a long text between its words.
    const auto width = [](std::string_view text) { return one_a_character(text); };
    const std::string deep = "/home/player/" + std::string(25, 'x') + "/Open Annihilation/Saves";
    const auto lines = settings::wrap_path(deep, 20, width);
    std::string joined;
    for (const auto& line : lines) {
        CHECK(one_a_character(line) <= 20);
        joined += line;
    }
    CHECK(joined == deep);
    CHECK(lines.size() >= 4 && lines[0] == "/home/player/");
    const auto words = settings::wrap_text("Screenshots, films and mods now go here.", 16, width);
    CHECK(words.size() == 3);
    CHECK(words[0] == "Screenshots," && words[1] == "films and mods" && words[2] == "now go here.");
    CHECK(settings::wrap_text("", 16, width).empty());
    CHECK(settings::wrap_text(std::string(40, 'w'), 16, width).size() == 3);
}

void the_notice_answers_its_buttons_and_keys() {
    settings::Notice notice = moved_notice();
    const int32_t height = settings::notice_height(notice);
    const auto parts = settings::notice_layout(notice);
    const auto ok = centre(find_part(parts, {}, settings::notice_ok_control)->rect);
    const auto opener = centre(find_part(parts, {}, settings::notice_open_control)->rect);
    const auto press = [&](Point at) {
        static_cast<void>(settings::notice_pointer_move(notice, at.x, at.y, height));
        static_cast<void>(settings::notice_pointer_down(notice, at.x, at.y, height));
        return settings::notice_pointer_up(notice, at.x, at.y, height);
    };
    CHECK(press(opener) == settings::NoticeAction::open_folder);
    CHECK(press(ok) == settings::NoticeAction::closed);
    // A press on one button released on the other does nothing; a press
    // off the buttons holds nothing.
    static_cast<void>(settings::notice_pointer_down(notice, opener.x, opener.y, height));
    CHECK(
        settings::notice_pointer_up(notice, ok.x, ok.y, height) == settings::NoticeAction::redraw
    );
    CHECK(settings::notice_pointer_down(notice, 5, 5, height) == settings::NoticeAction::none);
    CHECK(settings::notice_pointer_up(notice, 5, 5, height) == settings::NoticeAction::none);
    // A finger just under OK takes it, and its release where it landed
    // closes the notice; a finger far from both buttons holds nothing.
    const auto ok_rect = find_part(parts, {}, settings::notice_ok_control)->rect;
    const Point under_ok{ok.x, ok_rect.y + ok_rect.height - 1 + 5};
    static_cast<void>(settings::notice_finger_down(notice, under_ok.x, under_ok.y, height, 22));
    CHECK(notice.pressed == settings::notice_ok_control);
    CHECK(
        settings::notice_pointer_up(notice, under_ok.x, under_ok.y, height) ==
        settings::NoticeAction::closed
    );
    CHECK(settings::notice_finger_down(notice, 5, 5, height, 22) == settings::NoticeAction::none);
    CHECK(settings::notice_pointer_up(notice, 5, 5, height) == settings::NoticeAction::none);
    // The pointer lights the button under it.
    CHECK(
        settings::notice_pointer_move(notice, ok.x, ok.y, height) == settings::NoticeAction::redraw
    );
    CHECK(notice.hovered == settings::notice_ok_control);
    CHECK(
        settings::notice_pointer_move(notice, ok.x, ok.y, height) == settings::NoticeAction::none
    );
    // Enter and Escape close it; Space presses the marked button, OK at
    // first; the arrows and Tab move the mark.
    CHECK(settings::notice_key(notice, DialogKey::enter) == settings::NoticeAction::closed);
    CHECK(settings::notice_key(notice, DialogKey::escape) == settings::NoticeAction::closed);
    CHECK(notice.marked == settings::notice_ok_control);
    CHECK(settings::notice_key(notice, DialogKey::space) == settings::NoticeAction::closed);
    CHECK(settings::notice_key(notice, DialogKey::tab) == settings::NoticeAction::redraw);
    CHECK(notice.marked == settings::notice_open_control);
    CHECK(settings::notice_key(notice, DialogKey::space) == settings::NoticeAction::open_folder);
    CHECK(settings::notice_key(notice, DialogKey::left) == settings::NoticeAction::redraw);
    CHECK(notice.marked == settings::notice_ok_control);
    CHECK(settings::notice_key(notice, DialogKey::yes) == settings::NoticeAction::none);
    CHECK(settings::notice_key(notice, DialogKey::page_down) == settings::NoticeAction::none);
}

void the_notice_draws_in_the_dialogs_colours(const settings::DialogFonts& fonts) {
    const settings::Notice notice = moved_notice();
    const int32_t height = settings::notice_height(notice, &fonts);
    Canvas canvas = blank(settings::notice_width, static_cast<uint32_t>(height));
    settings::draw_notice(canvas.surface, {0, 0, 1}, notice, fonts, kNoIcon);
    CHECK(canvas.at(300, 3) == kBand);          // the header
    CHECK(canvas.at(300, height - 4) == kBand); // the footer
    CHECK(canvas.at(3, 30 + 1) == kPanel);      // the text's panel
    const auto parts = settings::notice_layout(notice, &fonts);
    const auto ok = find_part(parts, {}, settings::notice_ok_control);
    CHECK(canvas.at(ok->rect.x + 2, ok->rect.y + 2) == kAccent);
    // OK is marked: a green ring round it.
    CHECK(canvas.at(ok->rect.x - 2, ok->rect.y + 5) == kAccent);
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
    for (const Page page : kModPages) {
        settings::Locks locks{};
        locks.mex_snap = Lock::set_by_mod;
        locks.wreck_snap = Lock::set_by_mod;
        for (const auto& lock_state : {settings::Locks{}, locks}) {
            settings::Dialog dialog = opened_mod_options(page, lock_state);
            for (const auto& part : settings::dialog_layout(dialog)) {
                if (part.text.empty())
                    continue;
                const auto& font = part.font == settings::DialogFont::regular ? fonts.regular.font
                                                                              : fonts.small.font;
                const auto width =
                    static_cast<int32_t>(oa::formats::fnt::measure_text(font, part.text)) +
                    part.tracking * static_cast<int32_t>(part.text.size() - 1);
                if (width > part.rect.width) {
                    std::cerr << "'" << part.text << "' is " << width << " wide in a box "
                              << part.rect.width << " wide\n";
                    CHECK(width <= part.rect.width);
                }
            }
        }
    }
    the_dialog_draws_its_faces_and_accents(fonts);

    // The Touch section, with each way of its strips and at every offset,
    // and the list with Touch in it: every text fits its place.
    for (const auto drag : settings::touch_drag_choices)
        for (const auto latches : settings::touch_latches_choices) {
            settings::Dialog dialog = opened_with_touch(Page::touch);
            dialog.chosen.touch_drag = drag;
            dialog.chosen.touch_latches = latches;
            dialog.chosen.touch_hold_ms = settings::highest_touch_hold_ms;
            const int32_t limit = geometry::open_rows(dialog).limit;
            for (int32_t scroll = 0; scroll <= limit; ++scroll) {
                dialog.scroll[static_cast<std::size_t>(Page::touch)] = scroll;
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
    for (const std::string_view text : {"Automatic", "Scroll", "Stay on", "One action"})
        std::cout << "'" << text << "' is "
                  << oa::formats::fnt::measure_text(fonts.small.font, text) << " columns\n";

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
    // Mods: the lock lines, the notes under the list and a notice an open
    // that failed gives fit their places, and so does the Switch Mod
    // question for each mod: a long title is cut to its line, and the note
    // for a folder without an oamod.yaml shows whole.
    {
        const auto fits_drawn = [&](const settings::Dialog& shown) {
            for (const auto& part : settings::dialog_layout(shown, &fonts)) {
                if (part.text.empty())
                    continue;
                const auto width = settings::dialog_text_width(fonts, part.font, part.text) +
                                   part.tracking * static_cast<int32_t>(part.text.size() - 1);
                if (width > part.rect.width) {
                    std::cerr << "'" << part.text << "' is " << width << " wide in a box "
                              << part.rect.width << " wide\n";
                    CHECK(width <= part.rect.width);
                }
            }
        };
        for (const Lock lock : {Lock::none, Lock::command_line, Lock::in_game}) {
            settings::Locks locks{};
            locks.mod = lock;
            settings::Dialog dialog = mods_dialog(kModFolders[0], locks);
            fits_drawn(dialog);
            dialog.folder_notice = "The file manager could not open it.";
            fits_drawn(dialog);
        }
        settings::Dialog dialog = mods_dialog();
        dialog.mod_names[0] = "A Rather Long Mod Title That Runs Past Its Line 3.1";
        for (const auto& row : settings::mod_rows(dialog)) {
            dialog.switch_question = row.offered;
            fits_drawn(dialog);
        }
        dialog.switch_question = 2;
        const auto lines = geometry::question_text_lines(dialog, [&](std::string_view text) {
            return settings::dialog_text_width(fonts, settings::DialogFont::small, text);
        });
        std::string joined;
        for (const auto& line : lines)
            joined += (joined.empty() ? "" : " ") + line;
        CHECK(joined.ends_with(geometry::switch_no_profile_text));
        for (const std::string_view text :
             {geometry::mod_in_game_text,
              geometry::mod_from_command_line_text,
              geometry::switch_no_profile_text,
              geometry::mods_folders_text[0],
              geometry::mods_folders_text[1]})
            std::cout << "'" << text << "' is " << small_width(text) << " columns\n";
    }
    // Your files: a long folder's tail and the notice an open that failed
    // gives fit their hint lines; the buttons' captions fit the buttons.
    {
        settings::Dialog dialog = your_files_dialog();
        dialog.user_folder = "C:\\Documents and Settings\\A Rather Long User Name\\"
                             "My Documents\\Open Annihilation";
        const auto fits_drawn = [&](const settings::Dialog& shown) {
            for (const auto& part : settings::dialog_layout(shown, &fonts)) {
                if (part.text.empty())
                    continue;
                const auto width = settings::dialog_text_width(fonts, part.font, part.text) +
                                   part.tracking * static_cast<int32_t>(part.text.size() - 1);
                if (width > part.rect.width) {
                    std::cerr << "'" << part.text << "' is " << width << " wide in a box "
                              << part.rect.width << " wide\n";
                    CHECK(width <= part.rect.width);
                }
            }
        };
        fits_drawn(dialog);
        for (const std::string_view notice :
             {"The file manager could not open it.",
              "No file manager is there to open it.",
              "The folder cannot be made."}) {
            dialog.folder_notice = std::string(notice);
            fits_drawn(dialog);
            std::cout << "'" << notice << "' is " << small_width(notice) << " columns\n";
        }
    }
    // The notice of saved games that moved: every line it lists fits the
    // notice in its fonts, and a long path wraps into it whole.
    {
        settings::Notice notice = moved_notice();
        notice.paragraphs[1].text = "C:\\Documents and Settings\\A Rather Long User Name\\"
                                    "My Documents\\Open Annihilation\\Saves";
        notice.failure = "No file manager is there to open it.";
        const int32_t height = settings::notice_height(notice, &fonts);
        CHECK(height < settings::greatest_notice_height);
        std::string path;
        for (const auto& part : settings::notice_layout(notice, &fonts)) {
            if (part.text.empty())
                continue;
            const auto width = settings::dialog_text_width(fonts, part.font, part.text) +
                               part.tracking * static_cast<int32_t>(part.text.size() - 1);
            CHECK(width <= part.rect.width);
            if (part.font == settings::DialogFont::regular &&
                part.control == settings::no_control && part.text != notice.title)
                path += part.text;
        }
        CHECK(path == notice.paragraphs[1].text);
        Canvas canvas = blank(settings::notice_width, static_cast<uint32_t>(height));
        settings::draw_notice(canvas.surface, {0, 0, 1}, notice, fonts, kNoIcon);
        CHECK(canvas.at(300, 3) == kBand);
    }

    // Enhanced anti-aliasing's hint in Full, at each factor and each level.
    for (const uint8_t supersample : {uint8_t{1}, uint8_t{2}, uint8_t{4}, uint8_t{8}, uint8_t{16}})
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
    developer_texts_fit(fonts);

    // The Game files section with a host's usual texts, and the Language &
    // Text dialog alone: every text fits its place, as the dialog measures
    // it (the characters the game fonts lack in the modern fonts).
    const auto dialog_fits = [&](const settings::Dialog& dialog) {
        for (const auto& part : settings::dialog_layout(dialog)) {
            if (part.text.empty())
                continue;
            const int32_t width = settings::dialog_text_width(fonts, part.font, part.text) +
                                  part.tracking * static_cast<int32_t>(part.text.size() - 1);
            if (width > part.rect.width) {
                std::cerr << "'" << part.text << "' is " << width << " wide in a box "
                          << part.rect.width << " wide\n";
                CHECK(width <= part.rect.width);
            }
        }
    };
    for (const bool touch : {false, true}) {
        settings::Dialog files = opened_with_game_files(Page::game_files, touch);
        files.game_files_summary = "3.1c · Core Contingency · Battle Tactics · music · 1 mod";
        files.game_files_sizes = "1.1 GB · 11 GB free on this tablet";
        files.game_files_location = "In the file manager: On My tablet › Open Annihilation › Total "
                                    "Annihilation";
        dialog_fits(files);
        files.game_files_device.clear();
        dialog_fits(files);
    }
    settings::Dialog language;
    settings::open_language_text_dialog(language, {}, {}, {}, "v0.6.0");
    dialog_fits(language);
    manage_draws_in_the_game_font_inside_its_button(fonts);
}

void the_mod_options_dialog_lists_its_own_sections() {
    const auto pages = settings::dialog_pages(settings::DialogKind::mod_options);
    CHECK(std::equal(pages.begin(), pages.end(), kModPages.begin(), kModPages.end()));
    const auto engine = settings::dialog_pages(settings::DialogKind::engine);
    CHECK(std::equal(engine.begin(), engine.end(), kPages.begin(), kPages.end()));
    // An engine section asked of it opens its first section instead.
    const settings::Dialog fallback = opened_mod_options(Page::graphics);
    CHECK(fallback.kind == settings::DialogKind::mod_options);
    CHECK(fallback.page == Page::mod_keys);
    settings::Dialog dialog = opened_mod_options(Page::mod_keys);
    const auto parts = settings::dialog_layout(dialog);
    CHECK(find_part(parts, "Keys", settings::page_control(Page::mod_keys)) != nullptr);
    CHECK(find_part(parts, "Snap & chat", settings::no_control) != nullptr);
    CHECK(find_part(parts, "Graphics", settings::no_control) == nullptr);
    CHECK(find_part(parts, "Snap override key", settings::no_control) != nullptr);
    CHECK(find_part(parts, "Alt", settings::no_control) != nullptr);
    for (const Page page : kModPages) {
        CHECK(click(dialog, centre(geometry::list_item(page))) == DialogAction::redraw);
        CHECK(dialog.page == page);
        const auto rows = geometry::place_rows(page, {});
        CHECK(rows.rows.size() == settings::page_settings(page).size());
        CHECK(rows.bottom < geometry::footer_rule_row);
        const auto shown = settings::dialog_layout(dialog);
        for (std::size_t a = 0; a < shown.size(); ++a) {
            for (std::size_t b = a + 1; b < shown.size(); ++b)
                CHECK(!overlap(shown[a].rect, shown[b].rect));
        }
    }
}

void the_mod_options_change_only_the_mod_options() {
    settings::Dialog dialog = opened_mod_options(Page::mod_keys);
    // The key sliders step through option_keys.
    dialog.focused = settings::first_row_control;
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::changed);
    CHECK(dialog.chosen.mod_options.snap_override_key == settings::option_keys[1].code);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::changed);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::redraw);
    CHECK(dialog.chosen.mod_options.snap_override_key == settings::option_keys[0].code);
    CHECK(geometry::value_text(settings::Setting::rotate_build_key, dialog.chosen) == "\\");
    // A three-way choice names its choices.
    settings::EngineSettings state = mod_settings();
    CHECK(geometry::value_text(settings::Setting::patrol_hold, state) == "Reclaim only");
    CHECK(geometry::value_text(settings::Setting::patrol_roam, state) == "Assist only");
    CHECK(geometry::value_text(settings::Setting::guard_maneuver, state) == "Normal");
    CHECK(geometry::value_text(settings::Setting::panel_background, state) == "None");
    // A snap radius runs to the mod's most and no further.
    CHECK(geometry::stops_of(state, settings::Setting::mex_snap_radius) == 7);
    CHECK(geometry::stops_of(state, settings::Setting::wreck_snap_radius) == 5);
    geometry::set_stop(state, settings::Setting::mex_snap_radius, 9);
    CHECK(state.mod_options.mex_snap_radius == 6);
    CHECK(geometry::value_text(settings::Setting::mex_snap_radius, state) == "6 cells");
    state.mod_options.wreck_snap_most = 0;
    CHECK(geometry::stops_of(state, settings::Setting::wreck_snap_radius) == 2);
    geometry::set_stop(state, settings::Setting::wreck_snap_radius, 1);
    CHECK(state.mod_options.wreck_snap_radius == 0);
    // A switch flips with a click.
    CHECK(click(dialog, centre(geometry::list_item(Page::mod_tools))) == DialogAction::redraw);
    const auto rows = geometry::place_rows(Page::mod_tools, {});
    CHECK(click(dialog, centre(rows.rows[1].control_area)) == DialogAction::changed);
    CHECK(dialog.chosen.mod_options.full_rings != dialog.opened.mod_options.full_rings);
    CHECK(geometry::switch_on(dialog.chosen, settings::Setting::full_rings));
    // Restore defaults restores the mod options and keeps the engine's settings.
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.chosen.mod_options.mex_snap_radius == 6);
    CHECK(dialog.chosen.mod_options.wreck_snap_radius == 4);
    CHECK(dialog.chosen.wheel_zoom == dialog.opened.wheel_zoom);
    // Cancel puts back what it opened with.
    CHECK(settings::dialog_key(dialog, DialogKey::escape) == DialogAction::cancelled);
    CHECK(dialog.chosen == dialog.opened);
}

void a_mod_set_snap_radius_is_locked() {
    settings::Locks locks{};
    locks.mex_snap = Lock::set_by_mod;
    settings::Dialog dialog = opened_mod_options(Page::mod_tools, locks);
    const auto parts = settings::dialog_layout(dialog);
    CHECK(find_part(parts, "Set by the mod", settings::no_control) != nullptr);
    const auto rows = geometry::place_rows(Page::mod_tools, locks);
    CHECK(rows.rows[2].lock == Lock::set_by_mod);
    static_cast<void>(click(dialog, centre(rows.rows[2].control_area)));
    CHECK(dialog.chosen.mod_options.mex_snap_radius == 3);
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.chosen.mod_options.mex_snap_radius == 3);
    CHECK(dialog.chosen.mod_options.wreck_snap_radius == 4);
}

/// Walks the focus with Tab once round every control of a dialog that takes
/// it, from the first Tab's.
///
/// @param[in,out] dialog the dialog
/// @return the sections' entries the focus met, in order
std::vector<int32_t> entries_walked(settings::Dialog& dialog) {
    std::vector<int32_t> walked;
    static_cast<void>(settings::dialog_key(dialog, DialogKey::tab));
    const int32_t first = dialog.focused;
    for (int32_t press = 0; press < 64; ++press) {
        if (dialog.focused >= settings::first_page_control &&
            dialog.focused < settings::restore_control)
            walked.push_back(dialog.focused);
        static_cast<void>(settings::dialog_key(dialog, DialogKey::tab));
        if (dialog.focused == first)
            break;
    }
    return walked;
}

void touch_is_listed_only_with_touch_controls() {
    // Without touch controls the engine's settings list their six sections,
    // with Touch seven; a mod's options list their five either way.
    const auto engine = settings::dialog_pages(settings::DialogKind::engine);
    CHECK(std::equal(engine.begin(), engine.end(), kPages.begin(), kPages.end()));
    const auto touch = settings::dialog_pages(settings::DialogKind::engine, true);
    CHECK(std::equal(touch.begin(), touch.end(), kTouchPages.begin(), kTouchPages.end()));
    const auto mods = settings::dialog_pages(settings::DialogKind::mod_options, true);
    CHECK(std::equal(mods.begin(), mods.end(), kModPages.begin(), kModPages.end()));

    // The list without Touch lies where it always has: 21 rows an entry from
    // row 36, the line at 256 and Developer under it at the list's foot, at
    // 262.
    constexpr std::array<int32_t, 5> kTops{36, 57, 78, 99, 120};
    for (std::size_t index = 0; index < kTops.size(); ++index)
        CHECK(geometry::list_item(kPages[index]).y == kTops[index]);
    CHECK(geometry::list_divider().y == 256);
    CHECK(geometry::list_item(Page::developer).y == 262);
    CHECK(geometry::list_item(Page::mod_chat).y == 120);
    // With Touch, Touch takes the sixth place; the line and Developer stay at
    // the list's foot.
    for (std::size_t index = 0; index < kTops.size(); ++index)
        CHECK(geometry::list_item(kTouchPages[index], true).y == kTops[index]);
    CHECK(geometry::list_item(Page::touch, true).y == 141);
    CHECK(geometry::list_item(Page::developer, true).y == 262);
    CHECK(geometry::list_item(Page::mod_chat, true).y == 120);

    // Each section's entry keeps its number with or without Touch listed;
    // a dialog without Touch has no entry numbered as Touch.
    for (const Page page : kPages) {
        const auto plain = list_entries(opened_with_touch(page, false));
        const auto with_touch = list_entries(opened_with_touch(page));
        CHECK(plain.size() == kPages.size());
        CHECK(with_touch.size() == kTouchPages.size());
        for (std::size_t index = 0; index < plain.size(); ++index) {
            CHECK(plain[index].first == settings::page_control(kPages[index]));
            CHECK(plain[index].first != settings::page_control(Page::touch));
        }
        for (std::size_t index = 0; index < with_touch.size(); ++index)
            CHECK(with_touch[index].first == settings::page_control(kTouchPages[index]));
    }
    const auto parts = settings::dialog_layout(opened_with_touch(Page::common_tweaks, false));
    CHECK(find_part(parts, "Touch", settings::no_control) == nullptr);
    const auto touch_parts = settings::dialog_layout(opened_with_touch(Page::common_tweaks));
    const auto* entry = find_part(touch_parts, "Touch", settings::no_control);
    CHECK(entry != nullptr && entry->control == settings::page_control(Page::touch));

    // Touch opens only where it is listed.
    CHECK(opened_with_touch(Page::touch, false).page == Page::mods);
    CHECK(opened_with_touch(Page::touch).page == Page::touch);
    CHECK(opened(Page::developer).page == Page::developer && !opened(Page::developer).touch);

    // A click on Touch's entry shows it; Developer's entry still shows
    // Developer, where it stands with Touch listed.
    settings::Dialog dialog = opened_with_touch(Page::common_tweaks);
    CHECK(click(dialog, centre(geometry::list_item(Page::touch, true))) == DialogAction::redraw);
    CHECK(dialog.page == Page::touch);
    CHECK(
        click(dialog, centre(geometry::list_item(Page::developer, true))) == DialogAction::redraw
    );
    CHECK(dialog.page == Page::developer);

    // The keyboard focus walks the entries in the list's order, Touch among
    // them.
    settings::Dialog keys = opened_with_touch(Page::common_tweaks);
    const std::vector<int32_t> walked = entries_walked(keys);
    CHECK(walked.size() == kTouchPages.size());
    for (std::size_t index = 0; index < walked.size() && index < kTouchPages.size(); ++index)
        CHECK(walked[index] == settings::page_control(kTouchPages[index]));

    // The touch controls coming on list Touch at once; going off, a dialog
    // showing Touch shows its first section.
    settings::Dialog later = opened_with_touch(Page::graphics, false);
    CHECK(settings::set_touch_controls(later, false) == DialogAction::none);
    CHECK(settings::set_touch_controls(later, true) == DialogAction::redraw);
    CHECK(later.touch && list_entries(later).size() == kTouchPages.size());
    CHECK(click(later, centre(geometry::list_item(Page::touch, true))) == DialogAction::redraw);
    later.focused = settings::first_row_control;
    CHECK(settings::set_touch_controls(later, false) == DialogAction::redraw);
    CHECK(later.page == Page::mods && later.focused == settings::no_control);
    CHECK(list_entries(later).size() == kPages.size());
}

void game_files_is_listed_only_where_the_host_says() {
    // With the flag the engine's settings list Game files between Graphics,
    // or Touch, and Developer; a mod's options and Language alone never do.
    const auto plain = settings::dialog_pages(settings::DialogKind::engine, false, false);
    CHECK(std::equal(plain.begin(), plain.end(), kPages.begin(), kPages.end()));
    const auto files = settings::dialog_pages(settings::DialogKind::engine, false, true);
    CHECK(std::equal(files.begin(), files.end(), kGameFilesPages.begin(), kGameFilesPages.end()));
    const auto both = settings::dialog_pages(settings::DialogKind::engine, true, true);
    CHECK(
        std::equal(
            both.begin(), both.end(), kTouchGameFilesPages.begin(), kTouchGameFilesPages.end()
        )
    );
    const auto mods = settings::dialog_pages(settings::DialogKind::mod_options, true, true);
    CHECK(std::equal(mods.begin(), mods.end(), kModPages.begin(), kModPages.end()));
    CHECK(settings::dialog_pages(settings::DialogKind::language_text, true, true).size() == 1);

    // Its entry takes the place after Graphics, or after Touch; the entries
    // above stay, and the line and Developer stay at the list's foot.
    constexpr std::array<int32_t, 5> kTops{36, 57, 78, 99, 120};
    for (std::size_t index = 0; index < kTops.size(); ++index) {
        CHECK(geometry::list_item(kGameFilesPages[index], false, true).y == kTops[index]);
        CHECK(geometry::list_item(kTouchGameFilesPages[index], true, true).y == kTops[index]);
    }
    CHECK(geometry::list_item(Page::game_files, false, true).y == 141);
    CHECK(geometry::list_item(Page::developer, false, true).y == 262);
    CHECK(geometry::list_item(Page::touch, true, true).y == 141);
    CHECK(geometry::list_item(Page::game_files, true, true).y == 162);
    CHECK(
        geometry::list_item(Page::game_files, true, true).y + geometry::list_item_height <
        geometry::list_divider().y
    );
    CHECK(geometry::list_item(Page::developer, true, true).y == 262);
    CHECK(
        geometry::list_item(Page::developer, true, true).y + geometry::list_item_height <
        geometry::footer_rule_row
    );

    // Touch is 5, Developer 6 and Game files 7 in every dialog: a dialog
    // without Touch has no control 5, one without Game files no control 7.
    CHECK(settings::page_control(Page::touch) == 5);
    CHECK(settings::page_control(Page::developer) == 6);
    CHECK(settings::page_control(Page::game_files) == 7);
    for (const bool touch : {false, true}) {
        const auto entries = list_entries(opened_with_game_files(Page::common_tweaks, touch));
        const auto& order = touch ? std::span<const Page>(kTouchGameFilesPages)
                                  : std::span<const Page>(kGameFilesPages);
        CHECK(entries.size() == order.size());
        for (std::size_t index = 0; index < entries.size() && index < order.size(); ++index) {
            CHECK(entries[index].first == settings::page_control(order[index]));
            CHECK(entries[index].second.y == geometry::list_item(order[index], touch, true).y);
        }
    }
    for (const auto& [control, rect] : list_entries(opened_with_touch(Page::common_tweaks)))
        CHECK(control != settings::page_control(Page::game_files));
    const auto parts = settings::dialog_layout(opened_with_game_files(Page::common_tweaks));
    const auto* entry = find_part(parts, "Game files", settings::no_control);
    CHECK(entry != nullptr && entry->control == settings::page_control(Page::game_files));
    CHECK(
        find_part(settings::dialog_layout(opened(Page::common_tweaks)), "Game files", 0) == nullptr
    );

    // Game files opens only where it is listed.
    CHECK(opened(Page::game_files).page == Page::mods);
    CHECK(opened_with_game_files(Page::game_files).page == Page::game_files);
    CHECK(opened_with_game_files(Page::game_files).game_files);

    // A click on its entry shows it; the focus walks the entries in the
    // list's order, Game files among them.
    settings::Dialog dialog = opened_with_game_files(Page::common_tweaks, true);
    CHECK(
        click(dialog, centre(geometry::list_item(Page::game_files, true, true))) ==
        DialogAction::redraw
    );
    CHECK(dialog.page == Page::game_files);
    CHECK(
        click(dialog, centre(geometry::list_item(Page::developer, true, true))) ==
        DialogAction::redraw
    );
    CHECK(dialog.page == Page::developer);
    settings::Dialog keys = opened_with_game_files(Page::common_tweaks, true);
    const std::vector<int32_t> walked = entries_walked(keys);
    CHECK(walked.size() == kTouchGameFilesPages.size());
    for (std::size_t index = 0; index < walked.size() && index < kTouchGameFilesPages.size();
         ++index)
        CHECK(walked[index] == settings::page_control(kTouchGameFilesPages[index]));
    // Touch going off keeps Game files listed.
    CHECK(settings::set_touch_controls(keys, false) == DialogAction::redraw);
    CHECK(list_entries(keys).size() == kGameFilesPages.size());
}

void game_files_shows_what_is_installed_the_backups_and_the_folder() {
    const auto rows = settings::page_settings(Page::game_files);
    CHECK(rows.size() == 3);
    CHECK(rows.size() == 3 && rows[0] == Setting::game_files_summary);
    CHECK(rows.size() == 3 && rows[1] == Setting::game_files_backed_up);
    CHECK(rows.size() == 3 && rows[2] == Setting::game_files_location);
    CHECK(geometry::is_button(Setting::game_files_summary));
    CHECK(geometry::is_text(Setting::game_files_location));
    CHECK(geometry::is_switch(Setting::game_files_backed_up));
    CHECK(!geometry::is_switch(Setting::game_files_summary));
    CHECK(!geometry::is_switch(Setting::game_files_location));

    // MANAGE… stands at the label line's right; the switch where every
    // switch stands; where the files are has no control. Every row fits the
    // view.
    const auto placed = geometry::place_rows(Page::game_files, {});
    CHECK(placed.rows.size() == 3);
    const auto& summary = placed.rows[0];
    CHECK(summary.control == settings::first_row_control);
    CHECK(summary.control_area.width == geometry::manage_button_width);
    CHECK(summary.control_area.x + summary.control_area.width == geometry::content_right);
    CHECK(summary.control_area.y == summary.label.y);
    CHECK(summary.label.x + summary.label.width + geometry::label_gap == summary.control_area.x);
    CHECK(summary.hint_lines == 2);
    const auto& backups = placed.rows[1];
    CHECK(backups.control_area.width == geometry::switch_width);
    CHECK(backups.hint_lines == 2);
    const auto& location = placed.rows[2];
    CHECK(location.control_area.width == 0 && location.hint_lines == 2);
    CHECK(geometry::scroll_limit(geometry::content_height(placed, 0)) == 0);

    // The layout: the host's texts, MANAGE… as the first row's control, the
    // switch's halves as the second's, nothing pressable for the third.
    settings::Dialog dialog = opened_with_game_files(Page::game_files);
    const auto parts = settings::dialog_layout(dialog);
    for (const std::string_view text :
         {"GAME FILES",
          "Installed",
          "3.1c · Core Contingency · Battle Tactics · music",
          "1.1 GB · 37 GB free on this tablet",
          "Include in device backups",
          "After restoring this tablet from a backup,",
          "add the game files again.",
          "Where the files are",
          "In the file manager: Open Annihilation ›",
          "Total Annihilation"})
        CHECK(find_part(parts, text, settings::no_control) != nullptr);
    const auto* manage = find_part(parts, "MANAGE…", settings::no_control);
    CHECK(manage != nullptr && manage->control == settings::first_row_control);
    CHECK(manage != nullptr && same_entry(manage->rect, summary.control_area));
    const auto* annihilation = find_part(parts, "Total Annihilation", settings::no_control);
    CHECK(annihilation != nullptr && annihilation->control == settings::no_control);
    const auto* off = find_part(parts, "OFF", settings::no_control);
    CHECK(off != nullptr && off->control == settings::first_row_control + 1);
    for (const auto& part : parts)
        CHECK(part.control != settings::first_row_control + 2);
    // No two parts overlap, and each lies inside the dialog.
    const renderer::SourceRect face{
        geometry::edge,
        geometry::edge,
        settings::dialog_width - 2 * geometry::edge,
        settings::dialog_height - 2 * geometry::edge,
    };
    for (std::size_t a = 0; a < parts.size(); ++a) {
        CHECK(inside(parts[a].rect, face));
        for (std::size_t b = a + 1; b < parts.size(); ++b)
            CHECK(!overlap(parts[a].rect, parts[b].rect));
    }
    // Every control is pressed where it is drawn.
    for (const auto& part : parts) {
        if (part.control == settings::no_control)
            continue;
        const Point point = centre(part.rect);
        static_cast<void>(settings::dialog_pointer_move(dialog, point.x, point.y));
        CHECK(dialog.hovered == part.control);
    }
    // A focus outline round MANAGE… keeps clear of the label and its lines.
    const renderer::SourceRect focus{
        summary.control_area.x - geometry::focus_inset,
        summary.control_area.y - geometry::focus_inset,
        summary.control_area.width + 2 * geometry::focus_inset,
        summary.control_area.height + 2 * geometry::focus_inset,
    };
    CHECK(!overlap(focus, summary.label));
    for (std::size_t line = 0; line < summary.hint_lines; ++line)
        CHECK(!overlap(focus, summary.hints[line]));

    // Without the device's name the hint says "device"; the host's empty
    // texts list nothing.
    settings::Dialog neutral = opened_with_game_files(Page::game_files);
    neutral.game_files_device.clear();
    neutral.game_files_sizes.clear();
    const auto neutral_parts = settings::dialog_layout(neutral);
    CHECK(find_part(neutral_parts, "After restoring this device from a backup,", 0) != nullptr);
    for (const auto& part : neutral_parts)
        CHECK(!same_entry(part.rect, summary.hints[1]));
    CHECK(geometry::row_hint(neutral, Setting::game_files_summary, 1).text.empty());

    // A long location breaks between words into two lines at most.
    const auto lines = geometry::break_lines(
        "Shown as: On My tablet › Open Annihilation › Total Annihilation", 50, 2
    );
    CHECK(lines.size() == 2);
    CHECK(lines.size() == 2 && lines[0] == "Shown as: On My tablet › Open Annihilation ›");
    CHECK(lines.size() == 2 && lines[1] == "Total Annihilation");
    // The mark in the line's second half is taken over a later space.
    const auto early = geometry::break_lines(
        "In the file manager: On My tablet › Open Annihilation › Total Annihilation", 50, 2
    );
    CHECK(early.size() == 2 && early[0] == "In the file manager: On My tablet ›");
    CHECK(early.size() == 2 && early[1] == "Open Annihilation › Total Annihilation");
    // Without a mark in the line's second half it breaks at its last space.
    const auto plain_words = geometry::break_lines(
        "In the file manager: Open Annihilation's own folder for games", 50, 2
    );
    CHECK(plain_words.size() == 2);
    CHECK(plain_words.size() == 2 && plain_words[1] == "folder for games");
    CHECK(geometry::break_lines("", 50, 2).empty());
    CHECK(geometry::break_lines("short", 50, 2) == std::vector<std::string>{"short"});
    const auto cut = geometry::break_lines(std::string(120, 'x'), 50, 2);
    CHECK(cut.size() == 2 && cut[0].size() == 50 && cut[1].size() == 50);
    const auto words = geometry::break_lines("one two three four", 9, 5);
    CHECK(words == (std::vector<std::string>{"one two", "three", "four"}));
}

void manage_asks_the_host_and_the_backups_switch_changes_at_once() {
    const auto placed = geometry::place_rows(Page::game_files, {});
    const auto manage = placed.rows[0].control_area;
    const auto backups = placed.rows[1].control_area;
    // A click on MANAGE… asks the host to open the Game files screen and
    // changes no setting; a press released elsewhere asks nothing.
    settings::Dialog dialog = opened_with_game_files(Page::game_files);
    const auto before = dialog.chosen;
    CHECK(click(dialog, centre(manage)) == DialogAction::manage_game_files);
    CHECK(dialog.chosen == before && dialog.pressed == settings::no_control);
    static_cast<void>(settings::dialog_pointer_down(dialog, manage.x + 2, manage.y + 2));
    CHECK(dialog.pressed == settings::first_row_control);
    CHECK(settings::dialog_pointer_up(dialog, 300, 290) == DialogAction::redraw);
    // A press on where the files are takes nothing.
    const auto location = placed.rows[2].hints[0];
    CHECK(
        settings::dialog_pointer_down(dialog, location.x + 4, location.y + 4) == DialogAction::none
    );
    CHECK(dialog.pressed == settings::no_control);
    static_cast<void>(settings::dialog_pointer_up(dialog, location.x + 4, location.y + 4));

    // The switch, Off at first, turns on by either half and by the keys, and
    // each change asks the host to put it in effect at once.
    CHECK(!dialog.chosen.game_files_backed_up);
    CHECK(click(dialog, {backups.x + backups.width - 4, backups.y + 4}) == DialogAction::changed);
    CHECK(dialog.chosen.game_files_backed_up);
    CHECK(click(dialog, {backups.x + 4, backups.y + 4}) == DialogAction::changed);
    CHECK(!dialog.chosen.game_files_backed_up);

    // The keys: the focus takes MANAGE…, then the switch, then the footer,
    // never where the files are. Space on MANAGE… asks the host; Left and
    // Right do nothing to it.
    settings::Dialog keys = opened_with_game_files(Page::game_files);
    CHECK(settings::dialog_key(keys, DialogKey::tab) == DialogAction::redraw);
    CHECK(keys.focused == settings::first_row_control);
    CHECK(settings::dialog_key(keys, DialogKey::space) == DialogAction::manage_game_files);
    CHECK(settings::dialog_key(keys, DialogKey::right) == DialogAction::none);
    CHECK(settings::dialog_key(keys, DialogKey::left) == DialogAction::none);
    CHECK(keys.chosen == before);
    CHECK(settings::dialog_key(keys, DialogKey::tab) == DialogAction::redraw);
    CHECK(keys.focused == settings::first_row_control + 1);
    CHECK(settings::dialog_key(keys, DialogKey::right) == DialogAction::changed);
    CHECK(keys.chosen.game_files_backed_up);
    CHECK(settings::dialog_key(keys, DialogKey::space) == DialogAction::changed);
    CHECK(!keys.chosen.game_files_backed_up);
    CHECK(settings::dialog_key(keys, DialogKey::tab) == DialogAction::redraw);
    CHECK(keys.focused == settings::restore_control);

    // Restore defaults turns the switch Off; Cancel puts back what it
    // opened with.
    settings::Dialog restore = opened_with_game_files(Page::game_files);
    restore.chosen.game_files_backed_up = true;
    CHECK(click(restore, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(!restore.chosen.game_files_backed_up);
    settings::Dialog opened_on;
    settings::EngineSettings on{};
    on.game_files_backed_up = true;
    settings::open_dialog(
        opened_on,
        on,
        {},
        {},
        "v0.2.0",
        Page::game_files,
        {},
        settings::highest_unit_limit,
        {},
        {},
        nullptr,
        false,
        true
    );
    CHECK(click(opened_on, {backups.x + 4, backups.y + 4}) == DialogAction::changed);
    CHECK(click(opened_on, centre(geometry::cancel_button)) == DialogAction::cancelled);
    CHECK(opened_on.chosen.game_files_backed_up);
    // A dialog that does not list Game files keeps the switch through
    // Restore defaults.
    settings::Dialog in_game;
    settings::open_dialog(in_game, on, {}, {}, "v0.2.0", Page::controls);
    CHECK(click(in_game, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(in_game.chosen.game_files_backed_up);

    // A finger beside MANAGE… takes it within reach, and its release where
    // it landed asks the host.
    settings::Dialog finger = opened_with_game_files(Page::game_files);
    const Point below{manage.x + manage.width / 2, manage.y + manage.height + 6};
    static_cast<void>(settings::dialog_finger_down(finger, below.x, below.y, 22));
    CHECK(finger.pressed == settings::first_row_control);
    CHECK(settings::dialog_pointer_up(finger, below.x, below.y) == DialogAction::manage_game_files);
}

/// What the scripted modern fonts drew: every line they were asked for.
std::vector<std::string> modern_lines;

/// Draws a line in the scripted modern fonts: each character a block 5
/// columns wide and 9 rows high over a baseline 9 rows down, the pen
/// moving 6 columns a character.
std::shared_ptr<const oa::present::TextMask>
scripted_modern_draw(void*, std::string_view text, oa::present::TextFace, int32_t scale, int32_t) {
    modern_lines.emplace_back(text);
    auto mask = std::make_shared<oa::present::TextMask>();
    std::size_t characters = 0;
    for (std::size_t at = 0; at < text.size(); ++at)
        if ((static_cast<unsigned char>(text[at]) & 0xc0) != 0x80)
            ++characters;
    const int32_t advance = 6 * scale;
    mask->width = std::max<int32_t>(1, static_cast<int32_t>(characters) * advance);
    mask->height = 12 * scale;
    mask->baseline = 9 * scale;
    mask->advance = static_cast<int32_t>(characters) * advance;
    mask->alpha.assign(static_cast<std::size_t>(mask->width * mask->height), 0);
    for (std::size_t character = 0; character < characters; ++character) {
        for (int32_t row = 0; row < 9 * scale; ++row)
            for (int32_t column = 0; column < 5 * scale; ++column)
                mask->alpha[static_cast<std::size_t>(
                    row * mask->width + static_cast<int32_t>(character) * advance + column
                )] = 255;
        mask->character_ends.push_back(static_cast<int32_t>(character + 1) * advance);
    }
    return mask;
}

/// The settings the scripted modern fonts draw with: no outline, shadow or
/// background, so that only the letters' colour is laid.
oa::present::TextSettings scripted_text_settings(void*) {
    oa::present::TextSettings settings;
    settings.style.outline = false;
    settings.style.shadow = false;
    settings.style.background = false;
    return settings;
}

void language_text_lists_one_section_and_draws_without_the_game_fonts() {
    settings::Dialog dialog;
    settings::EngineSettings current{};
    current.unit_limit = settings::highest_unit_limit;
    current.text_size = settings::highest_text_size;
    settings::EngineSettings defaults{};
    settings::open_language_text_dialog(dialog, current, defaults, {}, "v0.6");
    CHECK(dialog.kind == settings::DialogKind::language_text);
    CHECK(dialog.page == Page::language);
    CHECK(!dialog.touch && !dialog.game_files);
    // One entry, at the top of the list, keeping its number.
    const auto entries = list_entries(dialog);
    CHECK(entries.size() == 1);
    CHECK(entries.size() == 1 && entries[0].first == settings::page_control(Page::language));
    CHECK(entries.size() == 1 && entries[0].second.y == 36);
    const auto parts = settings::dialog_layout(dialog);
    for (const std::string_view absent : {"Graphics", "Developer", "Game files", "Touch"})
        CHECK(find_part(parts, absent, settings::no_control) == nullptr);
    for (const std::string_view present :
         {"Language", "LANGUAGE", "RESTORE DEFAULTS", "CANCEL", "OK"})
        CHECK(find_part(parts, present, settings::no_control) != nullptr);
    // Where Graphics' entry would be, nothing is pressed.
    const auto graphics = centre(geometry::list_item(Page::graphics));
    CHECK(click(dialog, graphics) == DialogAction::none);
    CHECK(dialog.page == Page::language);
    // The focus walks its rows, the footer and its one entry.
    settings::Dialog keys = dialog;
    std::vector<int32_t> walked;
    for (int32_t press = 0; press < 20; ++press) {
        static_cast<void>(settings::dialog_key(keys, DialogKey::tab));
        if (keys.focused < settings::restore_control)
            walked.push_back(keys.focused);
    }
    CHECK(!walked.empty());
    for (const int32_t control : walked)
        CHECK(control == settings::page_control(Page::language));
    // Restore defaults restores Language's settings alone.
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.chosen.text_size == defaults.text_size);
    CHECK(dialog.chosen.unit_limit == settings::highest_unit_limit);
    CHECK(dialog.restored);
    // Restore defaults keeps a locked language.
    settings::Dialog locked;
    settings::EngineSettings french{};
    french.language = "fr";
    settings::Locks locks{};
    locks.language = Lock::command_line;
    settings::open_language_text_dialog(locked, french, defaults, locks, "v0.6");
    CHECK(click(locked, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(locked.chosen.language == "fr");

    // With fonts that hold no glyphs, every text is drawn in the modern
    // fonts and measured by them.
    const settings::DialogFonts empty{};
    oa::present::GameTextHooks hooks{};
    hooks.draw = scripted_modern_draw;
    hooks.settings = scripted_text_settings;
    oa::present::set_game_text_hooks(hooks);
    CHECK(settings::dialog_text_width(empty, settings::DialogFont::regular, "Language") == 48);
    CHECK(settings::dialog_text_width(empty, settings::DialogFont::small, "OK") == 12);
    modern_lines.clear();
    Canvas canvas = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(canvas.surface, {0, 0, 1}, dialog, empty, kNoIcon);
    const auto drew = [](std::string_view text) {
        return std::find(modern_lines.begin(), modern_lines.end(), text) != modern_lines.end();
    };
    CHECK(drew("OPEN ANNIHILATION") || drew("O"));
    CHECK(drew("Language"));
    CHECK(drew("LANGUAGE"));
    CHECK(drew("OK"));
    CHECK(drew("v0.6"));
    // The label's letters are drawn in the text colour inside its box, the
    // capitals centred as a game font's are.
    const auto label = geometry::place_rows(Page::language, {}).rows[0].label;
    bool letters = false;
    for (int32_t y = label.y; y < label.y + label.height; ++y)
        for (int32_t x = label.x; x < label.x + 48; ++x)
            letters = letters || canvas.at(x, y) == kText;
    CHECK(letters);
    CHECK(canvas.at(label.x + 2, label.y - 1) != kText);
    oa::present::set_game_text_hooks({});
    // Without the modern fonts nothing draws the texts, and nothing fails.
    Canvas bare = blank(settings::dialog_width, settings::dialog_height);
    settings::draw_dialog(bare.surface, {0, 0, 1}, dialog, empty, kNoIcon);
    CHECK(settings::dialog_text_width(empty, settings::DialogFont::regular, "Language") == 0);
}

void touch_shows_its_rows_and_their_values() {
    const auto rows = settings::page_settings(Page::touch);
    CHECK(rows.size() == 5);
    constexpr std::array<Setting, 5> kTouchRows{
        Setting::touch_drag,
        Setting::touch_hold_delay,
        Setting::touch_latches,
        Setting::touch_haptics,
        Setting::touch_left_handed,
    };
    CHECK(std::equal(rows.begin(), rows.end(), kTouchRows.begin(), kTouchRows.end()));
    CHECK(geometry::is_strip(Setting::touch_drag) && geometry::is_strip(Setting::touch_latches));
    CHECK(geometry::is_slider(Setting::touch_hold_delay));
    CHECK(geometry::is_switch(Setting::touch_haptics));
    CHECK(geometry::is_switch(Setting::touch_left_handed));

    settings::Dialog dialog = opened_with_touch(Page::touch);
    // Every part inside the dialog and apart, at every offset; no game lock
    // reaches a Touch row.
    // Its five rows are taller than the view, by 62 rows.
    const auto open = geometry::open_rows(dialog);
    CHECK(open.limit == 62);
    const renderer::SourceRect face{
        geometry::edge,
        geometry::edge,
        settings::dialog_width - 2 * geometry::edge,
        settings::dialog_height - 2 * geometry::edge,
    };
    for (const auto& locks : lock_states()) {
        const auto placed = geometry::place_rows(Page::touch, locks);
        for (const auto& row : placed.rows)
            CHECK(row.lock == Lock::none);
    }
    for (int32_t scroll = 0; scroll <= open.limit; ++scroll) {
        dialog.scroll[static_cast<std::size_t>(Page::touch)] = scroll;
        const auto parts = settings::dialog_layout(dialog);
        for (std::size_t a = 0; a < parts.size(); ++a) {
            CHECK(inside(parts[a].rect, face));
            for (std::size_t b = a + 1; b < parts.size(); ++b)
                CHECK(!overlap(parts[a].rect, parts[b].rect));
        }
    }
    dialog.scroll = {};

    // At its top: the heading, the rows' labels, hints, captions and values.
    auto parts = settings::dialog_layout(dialog);
    for (const std::string_view text :
         {"TOUCH",
          "One-finger drag",
          "Automatic: a selection box on a tablet,",
          "scrolling on a phone.",
          "Automatic",
          "Box",
          "Scroll",
          "Hold delay",
          "How long a finger stays down for a hold.",
          "350 ms",
          "QUEUE and ADD",
          "A tapped QUEUE, ADD or x5 stays on",
          "until it is tapped again.",
          "Stay on",
          "One action",
          "Haptics",
          "A short vibration as a touch control acts."})
        CHECK(find_part(parts, text, settings::no_control) != nullptr);
    // At its end: Left-handed layout.
    CHECK(settings::dialog_key(dialog, DialogKey::end) == DialogAction::redraw);
    parts = settings::dialog_layout(dialog);
    for (const std::string_view text :
         {"Left-handed layout",
          "The minimap and the thumb controls on the",
          "right, the orders on the left."})
        CHECK(find_part(parts, text, settings::no_control) != nullptr);
    CHECK(settings::dialog_key(dialog, DialogKey::home) == DialogAction::redraw);

    // The strips: a click on a caption picks it, and the hint follows.
    const auto click_caption = [&](int32_t control, std::string_view caption) {
        const auto shown = settings::dialog_layout(dialog);
        const auto* part = find_part(shown, caption, settings::no_control);
        CHECK(part != nullptr && part->control == control);
        return part != nullptr ? click(dialog, centre(part->rect)) : DialogAction::none;
    };
    CHECK(click_caption(settings::first_row_control, "Box") == DialogAction::changed);
    CHECK(dialog.chosen.touch_drag == settings::TouchDrag::box);
    CHECK(
        find_part(settings::dialog_layout(dialog), "A drag draws a selection box;", -1) != nullptr
    );
    CHECK(click_caption(settings::first_row_control, "Scroll") == DialogAction::changed);
    CHECK(dialog.chosen.touch_drag == settings::TouchDrag::scroll);
    CHECK(
        find_part(settings::dialog_layout(dialog), "hold, then drag, for a selection box.", -1) !=
        nullptr
    );
    CHECK(click_caption(settings::first_row_control + 2, "One action") == DialogAction::changed);
    CHECK(dialog.chosen.touch_latches == settings::TouchLatches::one_action);
    CHECK(
        find_part(settings::dialog_layout(dialog), "after the next order or selection.", -1) !=
        nullptr
    );

    // The hold delay: ten stops of 50 ms from 250 to 700, each its own value.
    CHECK(geometry::slider_of(Setting::touch_hold_delay).stops == 10);
    settings::EngineSettings state{};
    CHECK(geometry::stop_of(state, Setting::touch_hold_delay) == 2);
    for (int32_t stop = 0; stop < 10; ++stop) {
        geometry::set_stop(state, Setting::touch_hold_delay, stop);
        CHECK(
            state.touch_hold_ms ==
            settings::lowest_touch_hold_ms + 50U * static_cast<uint32_t>(stop)
        );
        CHECK(geometry::stop_of(state, Setting::touch_hold_delay) == stop);
        CHECK(
            geometry::value_text(Setting::touch_hold_delay, state) ==
            std::to_string(state.touch_hold_ms) + " ms"
        );
    }
    geometry::set_stop(state, Setting::touch_hold_delay, 40);
    CHECK(state.touch_hold_ms == settings::highest_touch_hold_ms);
    // The arrows step it by a stop; a press at its track's ends sets the ends.
    dialog.focused = settings::first_row_control + 1;
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::changed);
    CHECK(dialog.chosen.touch_hold_ms == 400);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::changed);
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::changed);
    CHECK(dialog.chosen.touch_hold_ms == 300);
    const auto track = geometry::open_rows(dialog).rows.rows[1].control_area;
    static_cast<void>(click(dialog, {track.x, track.y + track.height / 2}));
    CHECK(dialog.chosen.touch_hold_ms == settings::lowest_touch_hold_ms);
    static_cast<void>(click(dialog, {track.x + track.width - 1, track.y + track.height / 2}));
    CHECK(dialog.chosen.touch_hold_ms == settings::highest_touch_hold_ms);
    CHECK(find_part(settings::dialog_layout(dialog), "700 ms", settings::no_control) != nullptr);

    // The switches.
    const auto haptics = geometry::open_rows(dialog).rows.rows[3].control_area;
    CHECK(click(dialog, {haptics.x + 2, haptics.y + 4}) == DialogAction::changed);
    CHECK(!dialog.chosen.touch_haptics);
    dialog.focused = settings::first_row_control + 4;
    CHECK(settings::dialog_key(dialog, DialogKey::space) == DialogAction::changed);
    CHECK(dialog.chosen.touch_left_handed);

    // Restore defaults puts every Touch row back; Cancel what it opened with.
    CHECK(click(dialog, centre(geometry::restore_button)) == DialogAction::changed);
    CHECK(dialog.chosen.touch_drag == settings::TouchDrag::automatic);
    CHECK(dialog.chosen.touch_hold_ms == settings::default_touch_hold_ms);
    CHECK(dialog.chosen.touch_latches == settings::TouchLatches::stay_on);
    CHECK(dialog.chosen.touch_haptics && !dialog.chosen.touch_left_handed);
    CHECK(settings::dialog_key(dialog, DialogKey::escape) == DialogAction::cancelled);
    CHECK(dialog.chosen == dialog.opened);
}

void touch_changes_only_its_entry_and_the_sections_under_it() {
    // A section drawn with Touch listed differs from it drawn without only
    // in the list from Touch's entry down: the open section, the header,
    // the footer and the entries above Touch's are drawn alike.
    const auto fonts = block_fonts();
    const int32_t touch_top = geometry::list_item(Page::touch, true).y;
    for (const Page page : kPages) {
        Canvas plain = blank(settings::dialog_width, settings::dialog_height);
        Canvas touch = blank(settings::dialog_width, settings::dialog_height);
        settings::draw_dialog(
            plain.surface, {0, 0, 1}, opened_with_touch(page, false), fonts, kNoIcon
        );
        settings::draw_dialog(touch.surface, {0, 0, 1}, opened_with_touch(page), fonts, kNoIcon);
        std::size_t differing_outside = 0;
        std::size_t differing_below = 0;
        for (int32_t y = 0; y < settings::dialog_height; ++y)
            for (int32_t x = 0; x < settings::dialog_width; ++x) {
                if (plain.at(x, y) == touch.at(x, y))
                    continue;
                const bool in_list = x > geometry::edge && x < geometry::list_rule_column &&
                                     y >= touch_top && y < geometry::footer_rule_row;
                ++(in_list ? differing_below : differing_outside);
            }
        CHECK(differing_outside == 0);
        CHECK(differing_below > 0);
    }
}

void a_finger_takes_the_nearest_control() {
    // The touch controls' reach in the dialog's pixels: 22 points, as many
    // canvas pixels a point as the screen has, over the dialog's scale.
    const auto reach_at = [](double pixels_per_point, double dialog_scale) {
        return static_cast<int32_t>(std::lround(22.0 * pixels_per_point / dialog_scale));
    };
    CHECK(reach_at(1.0, 1.0) == 22);
    CHECK(reach_at(3.0, 2.0) == 33);
    for (const auto& [pixels_per_point, dialog_scale] :
         {std::pair{1.0, 1.0}, std::pair{3.0, 2.0}, std::pair{2.0, 1.75}}) {
        const int32_t reach = reach_at(pixels_per_point, dialog_scale);
        // 15 and 30 points in the dialog's pixels.
        const auto points = [&](double count) {
            return static_cast<int32_t>(std::lround(count * pixels_per_point / dialog_scale));
        };
        settings::Dialog dialog = opened_with_touch(Page::touch);
        const auto haptics = geometry::open_rows(dialog).rows.rows[3].control_area;
        const int32_t middle = haptics.y + haptics.height / 2;
        CHECK(dialog.chosen.touch_haptics);
        // A mouse press 15 points left of the switch takes nothing.
        const Point near_left{haptics.x - points(15), middle};
        CHECK(
            settings::dialog_pointer_down(dialog, near_left.x, near_left.y) == DialogAction::none
        );
        CHECK(dialog.pressed == settings::no_control);
        CHECK(settings::dialog_pointer_up(dialog, near_left.x, near_left.y) == DialogAction::none);
        CHECK(dialog.chosen.touch_haptics);
        // A finger 30 points off takes nothing either.
        const Point far_left{haptics.x - points(30), middle};
        CHECK(
            settings::dialog_finger_down(dialog, far_left.x, far_left.y, reach) ==
            DialogAction::none
        );
        CHECK(dialog.pressed == settings::no_control);
        CHECK(dialog.finger_shift_x == 0 && dialog.finger_shift_y == 0);
        CHECK(settings::dialog_pointer_up(dialog, far_left.x, far_left.y) == DialogAction::none);
        CHECK(dialog.chosen.touch_haptics);
        // A finger 15 points off takes the switch at its nearest pixel, its
        // Off half, and the release where it landed turns it Off.
        CHECK(
            settings::dialog_finger_down(dialog, near_left.x, near_left.y, reach) ==
            DialogAction::redraw
        );
        CHECK(dialog.pressed == settings::first_row_control + 3);
        CHECK(dialog.finger_shift_x == points(15) && dialog.finger_shift_y == 0);
        CHECK(
            settings::dialog_pointer_move(dialog, near_left.x, near_left.y) == DialogAction::none
        );
        CHECK(
            settings::dialog_pointer_up(dialog, near_left.x, near_left.y) == DialogAction::changed
        );
        CHECK(!dialog.chosen.touch_haptics);
        CHECK(dialog.finger_shift_x == 0 && dialog.finger_shift_y == 0);
    }
    {
        // Under the switch's On half, 15 points down at a pixel a point, it
        // turns it On again: the scroll bar right of the switch and OK under
        // it lie farther.
        settings::Dialog dialog = opened_with_touch(Page::touch);
        dialog.chosen.touch_haptics = false;
        const auto haptics = geometry::open_rows(dialog).rows.rows[3].control_area;
        const Point under{haptics.x + 28, haptics.y + haptics.height - 1 + 15};
        static_cast<void>(settings::dialog_finger_down(dialog, under.x, under.y, 22));
        CHECK(dialog.pressed == settings::first_row_control + 3);
        CHECK(dialog.finger_shift_x == 0 && dialog.finger_shift_y == -15);
        CHECK(settings::dialog_pointer_up(dialog, under.x, under.y) == DialogAction::changed);
        CHECK(dialog.chosen.touch_haptics);
        // Farther down, OK is nearer than the switch, and a finger there
        // takes OK.
        const Point lower{under.x, under.y + 8};
        static_cast<void>(settings::dialog_finger_down(dialog, lower.x, lower.y, 22));
        CHECK(dialog.pressed == settings::ok_control);
        CHECK(settings::dialog_pointer_up(dialog, lower.x, lower.y) == DialogAction::accepted);
    }

    // A finger on a control presses it where it lands.
    settings::Dialog dialog = opened_with_touch(Page::touch);
    const auto ok = centre(geometry::ok_button);
    static_cast<void>(settings::dialog_finger_down(dialog, ok.x, ok.y, 22));
    CHECK(dialog.pressed == settings::ok_control);
    CHECK(dialog.finger_shift_x == 0 && dialog.finger_shift_y == 0);
    CHECK(settings::dialog_pointer_up(dialog, ok.x, ok.y) == DialogAction::accepted);

    // A finger under a slider's track drags its knob along the track.
    dialog = opened_with_touch(Page::touch);
    const auto track = geometry::open_rows(dialog).rows.rows[1].control_area;
    const int32_t below = track.y + track.height - 1 + 8;
    CHECK(
        settings::dialog_finger_down(dialog, track.x, below, 22) == DialogAction::changed &&
        dialog.chosen.touch_hold_ms == settings::lowest_touch_hold_ms && dialog.dragging
    );
    CHECK(
        settings::dialog_pointer_move(dialog, track.x + track.width - 1, below) ==
        DialogAction::changed
    );
    CHECK(dialog.chosen.touch_hold_ms == settings::highest_touch_hold_ms);
    CHECK(
        settings::dialog_pointer_up(dialog, track.x + track.width - 1, below) ==
        DialogAction::redraw
    );
    CHECK(!dialog.dragging && dialog.pressed == settings::no_control);

    // A finger beside the footer's buttons takes the nearest: under OK.
    dialog = opened_with_touch(Page::touch);
    const Point under_ok{ok.x, geometry::ok_button.y + geometry::ok_button.height - 1 + 5};
    static_cast<void>(settings::dialog_finger_down(dialog, under_ok.x, under_ok.y, 22));
    CHECK(dialog.pressed == settings::ok_control);
    CHECK(settings::dialog_pointer_up(dialog, under_ok.x, under_ok.y) == DialogAction::accepted);

    // While a drop-down list is open, a finger beside it takes its nearest
    // item, and the release where it landed chooses that item.
    settings::Dialog languages = language_dialog();
    const auto field = geometry::open_rows(languages).rows.rows[0].control_area;
    static_cast<void>(click(languages, centre(field)));
    CHECK(languages.open_list == settings::first_row_control);
    const auto list =
        geometry::choice_list(field, geometry::choice_count(languages, Setting::language));
    const auto second = geometry::choice_item(list, 1);
    const Point beside_list{list.x + list.width + 6, second.y + second.height / 2};
    static_cast<void>(settings::dialog_finger_down(languages, beside_list.x, beside_list.y, 22));
    CHECK(languages.list_pressed == 1);
    CHECK(
        settings::dialog_pointer_up(languages, beside_list.x, beside_list.y) ==
        DialogAction::changed
    );
    CHECK(languages.open_list == settings::no_control);
    CHECK(geometry::choice_index(languages, Setting::language) == 1);

    // While the Switch Mod question shows, a finger beside one of its
    // buttons takes that button, and one on a button takes it whatever
    // lies near it under the question.
    settings::Dialog asking = mods_dialog(kModFolders[0]);
    asking.touch = true;
    static_cast<void>(click(asking, mod_point(asking, 1)));
    CHECK(asking.switch_question == 1);
    const auto& yes = geometry::question_yes_button;
    const Point under_yes{yes.x + yes.width / 2, yes.y + yes.height - 1 + 6};
    static_cast<void>(settings::dialog_finger_down(asking, under_yes.x, under_yes.y, 22));
    CHECK(asking.pressed == settings::question_yes_control);
    CHECK(
        settings::dialog_pointer_up(asking, under_yes.x, under_yes.y) == DialogAction::switch_mod
    );
    asking = mods_dialog(kModFolders[0]);
    static_cast<void>(click(asking, mod_point(asking, 1)));
    const auto& no = geometry::question_no_button;
    const Point on_no{no.x + 1, no.y + no.height / 2};
    static_cast<void>(settings::dialog_finger_down(asking, on_no.x, on_no.y, 22));
    CHECK(asking.pressed == settings::question_no_control);
    CHECK(settings::dialog_pointer_up(asking, on_no.x, on_no.y) == DialogAction::redraw);
    CHECK(asking.switch_question == settings::no_question);

    // On Mods a finger beside OPEN MODS FOLDER takes it.
    settings::Dialog mods = mods_dialog();
    const auto& folder = geometry::mods_folder_button;
    const Point under_folder{folder.x + folder.width / 2, folder.y + folder.height - 1 + 6};
    static_cast<void>(settings::dialog_finger_down(mods, under_folder.x, under_folder.y, 22));
    CHECK(mods.pressed == geometry::mods_folder_control(geometry::open_rows(mods).rows));
    CHECK(
        settings::dialog_pointer_up(mods, under_folder.x, under_folder.y) ==
        DialogAction::open_folder
    );
}

} // namespace

/// Returns the columns of a row of a canvas that show a colour.
std::vector<int32_t> columns_of(const Canvas& canvas, int32_t y, renderer::Rgb color) {
    std::vector<int32_t> columns;
    for (int32_t x = 0; x < static_cast<int32_t>(canvas.surface.width); ++x)
        if (canvas.at(x, y) == color)
            columns.push_back(x);
    return columns;
}

void the_oa_button_shows_the_icon_or_the_mark() {
    const settings::DialogFonts fonts{};
    const IconPicture icon = solid_icon();
    const auto button = [&](int32_t side,
                            settings::ButtonLook look,
                            const renderer::RgbaPicture& picture,
                            int32_t scale = 1) {
        Canvas canvas =
            blank(static_cast<uint32_t>(side * scale), static_cast<uint32_t>(side * scale));
        settings::draw_oa_button(canvas.surface, {0, 0, scale}, side, look, fonts, picture);
        return canvas;
    };
    constexpr renderer::Rgb kHover{0x20, 0x24, 0x1c};

    // At rest the icon fills the button but for 3 pixels all round: 26 of
    // the main menu's 32, 18 of the in-game column's 24.
    for (const int32_t side : {settings::menu_button_side, settings::ingame_button_side}) {
        const Canvas idle = button(side, settings::ButtonLook::idle, icon.picture());
        const int32_t middle = side / 2;
        const auto shown = columns_of(idle, middle, kIconColor);
        CHECK(shown.size() == static_cast<std::size_t>(side - 6));
        CHECK(!shown.empty() && shown.front() == 3 && shown.back() == side - 4);
        CHECK(idle.at(2, middle) == kPanel);
        CHECK(idle.at(side - 3, middle) == kPanel);
        CHECK(idle.at(3, 3) == kPanel); // the icon's clear corner
        CHECK(idle.at(8, 3) == kIconColor);
        CHECK(columns_of(idle, middle, kAccent).empty());

        // Under the pointer the face lights and a green ring lies inside
        // the bevel; the icon stays.
        const Canvas hovered = button(side, settings::ButtonLook::hovered, icon.picture());
        CHECK(hovered.at(1, middle) == kAccent && hovered.at(side - 2, middle) == kAccent);
        CHECK(hovered.at(2, middle) == kHover);
        CHECK(columns_of(hovered, middle, kIconColor) == shown);

        // Held, the bevel sinks and the icon moves a pixel right and down.
        const Canvas held = button(side, settings::ButtonLook::pressed, icon.picture());
        const auto moved = columns_of(held, middle, kIconColor);
        CHECK(moved.size() == shown.size() && !moved.empty() && moved.front() == 4);
        CHECK(held.at(3, middle) == kBand);
        CHECK(held.at(4, 4) == kBand && held.at(9, 4) == kIconColor);
    }

    // Twice as large, the icon is drawn at the surface's own resolution.
    const Canvas large =
        button(settings::menu_button_side, settings::ButtonLook::idle, icon.picture(), 2);
    const auto large_row = columns_of(large, 32, kIconColor);
    CHECK(large_row.size() == 52 && large_row.front() == 6 && large_row.back() == 57);

    // Without the icon, a green OA mark in a green outlined square, 20 of
    // the main menu button's 32 pixels a side.
    const Canvas marked = button(settings::menu_button_side, settings::ButtonLook::idle, kNoIcon);
    CHECK(marked.at(6, 6) == kAccent && marked.at(25, 25) == kAccent);
    CHECK(marked.at(5, 6) == kPanel);
    CHECK(columns_of(marked, 16, kIconColor).empty());
    const auto short_pixels = std::vector<uint8_t>(icon.pixels.begin(), icon.pixels.end() - 1);
    const Canvas broken = button(
        settings::menu_button_side,
        settings::ButtonLook::idle,
        renderer::RgbaPicture{IconPicture::kIconSide, IconPicture::kIconSide, short_pixels}
    );
    CHECK(broken.surface.rgb == marked.surface.rgb);
}

void the_oa_mark_alone_shows_the_icon_or_the_button_mark() {
    const IconPicture icon = solid_icon();
    const auto mark = [](int32_t side, const renderer::RgbaPicture& picture, int32_t scale = 1) {
        Canvas canvas =
            blank(static_cast<uint32_t>(side * scale), static_cast<uint32_t>(side * scale));
        settings::draw_oa_mark(canvas.surface, {0, 0, scale}, side, picture);
        return canvas;
    };
    // The icon fills the square, and nothing else is drawn: no face, no bevel.
    const Canvas iconic = mark(26, icon.picture());
    CHECK(columns_of(iconic, 13, kIconColor).size() == 26);
    CHECK(iconic.at(0, 0) == (renderer::Rgb{0, 0, 0})); // the icon's clear corner
    CHECK(iconic.at(25, 25) == kIconColor);
    // Three times as large, at the surface's own resolution.
    const Canvas sharp = mark(26, icon.picture(), 3);
    CHECK(columns_of(sharp, 40, kIconColor).size() == 78);
    // Without the icon, the OA button's mark at rest: the green outlined
    // square 20/32 of the side, the rest left as it was.
    const Canvas marked = mark(settings::menu_button_side, kNoIcon);
    CHECK(marked.at(6, 6) == kAccent && marked.at(25, 25) == kAccent);
    CHECK(marked.at(5, 6) == (renderer::Rgb{0, 0, 0}));
    CHECK(marked.at(0, 0) == (renderer::Rgb{0, 0, 0}));
    Canvas button = blank(settings::menu_button_side, settings::menu_button_side);
    settings::draw_oa_button(
        button.surface,
        {0, 0, 1},
        settings::menu_button_side,
        settings::ButtonLook::idle,
        settings::DialogFonts{},
        kNoIcon
    );
    for (int32_t y = 0; y < settings::menu_button_side; ++y)
        for (int32_t x = 0; x < settings::menu_button_side; ++x)
            if (marked.at(x, y) == kAccent)
                CHECK(button.at(x, y) == kAccent);
    // Nothing for no side.
    Canvas none = blank(4, 4);
    settings::draw_oa_mark(none.surface, {0, 0, 1}, 0, kNoIcon);
    CHECK(none.at(1, 1) == (renderer::Rgb{0, 0, 0}));
}

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv))
        fonts_load_and_every_text_fits_its_place();
    else {
        opening_shows_the_settings_in_effect();
        the_oa_button_shows_the_icon_or_the_mark();
        the_oa_mark_alone_shows_the_icon_or_the_button_mark();
        every_part_lies_inside_the_dialog_and_apart();
        each_section_shows_its_rows();
        a_click_on_an_entry_shows_its_section();
        every_control_is_pressed_where_it_is_drawn();
        switches_take_a_click_on_either_half_and_keys();
        language_and_text_shows_a_language_four_switches_and_a_size();
        language_drop_down_names_each_language_in_itself();
        language_drop_down_opens_marks_and_chooses();
        language_drop_down_locks_by_the_command_line();
        drop_down_lists_scroll_and_open_over_their_field();
        path_tails_keep_the_last_components_that_fit();
        text_size_runs_from_half_to_three_times_in_tenths();
        text_size_waits_for_the_modern_fonts();
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
        the_mod_options_dialog_lists_its_own_sections();
        the_mod_options_change_only_the_mod_options();
        a_mod_set_snap_radius_is_locked();
        the_dialog_draws_the_scroll_bar_and_clips_the_rows();
        the_graphics_page_draws_its_locked_rows();
        developer_mode_lists_every_hack_by_area_closed_at_first();
        areas_and_hacks_open_and_close();
        hacks_show_their_titles_alphabetically();
        developer_mode_off_shows_the_profile_and_takes_no_change();
        hacks_turn_on_and_off_and_their_overrides_follow_the_profile();
        every_kind_of_parameter_has_its_control();
        restore_profile_values_and_show_active_only();
        restore_defaults_turns_developer_mode_off_and_keeps_the_overrides();
        the_list_scrolls_and_shows_the_focused_row();
        summaries_break_into_lines_the_fonts_hold();
        the_open_list_keeps_its_parts_apart();
        the_developer_section_draws_its_parts();
        touch_is_listed_only_with_touch_controls();
        touch_shows_its_rows_and_their_values();
        touch_changes_only_its_entry_and_the_sections_under_it();
        a_finger_takes_the_nearest_control();
        game_files_is_listed_only_where_the_host_says();
        game_files_shows_what_is_installed_the_backups_and_the_folder();
        manage_asks_the_host_and_the_backups_switch_changes_at_once();
        language_text_lists_one_section_and_draws_without_the_game_fonts();
        your_files_shows_the_folder_and_opens_its_folders();
        mods_lists_the_mod_played_first_then_no_mod_then_the_others_by_title();
        each_mod_row_shows_its_badge_title_version_and_description();
        long_mod_texts_are_cut_with_an_ellipsis();
        the_mods_list_scrolls_while_its_button_and_note_stay();
        choosing_another_mod_asks_before_switching();
        the_keys_answer_the_switch_mod_question();
        mods_locks_during_a_game_and_by_the_command_line();
        open_mods_folder_asks_for_the_mods_folder();
        the_notice_wraps_its_text_and_places_its_buttons();
        the_notice_answers_its_buttons_and_keys();
        the_notice_draws_in_the_dialogs_colours(settings::DialogFonts{});
    }
    if (failures != 0)
        return 1;
    std::cout << "settings dialog: ok\n";
    return 0;
}
