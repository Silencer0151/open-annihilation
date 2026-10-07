// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The top and bottom bars on windows of several shapes, for each of two
// sides: each bar runs from the side column's edge to the window's right
// edge with art in every column, and holds the side's panel art where the
// game places it on a screen that wide. The chrome keeps the interface's
// scale, or under ui.resource-panel the scale that gives its clock line
// room in the top bar, with and without the game time.
#include "oa/app/runtime.hpp"
#include "oa/formats/gaf.hpp"
#include "oa/formats/tdf.hpp"
#include "oa/ui/console/game_fields.hpp"
#include "oa/ui/display_layout.hpp"
#include "oa/ui/hud/resource_panel.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace oa::app {
namespace {

// Window sizes: 4:3 at the interface's own size and between it and its
// largest, either side of 1024 pixels wide, and 5:4 at its largest; then
// wider than 4:3 below the largest on either side of 1024 pixels wide, at
// it, and past it on a window twice as wide as tall.
constexpr std::array<std::pair<int, int>, 9> kWindowSizes{{
    {640, 480},
    {800, 600},
    {1024, 768},
    {1152, 864},
    {1280, 1024},
    {1024, 600},
    {1280, 720},
    {1920, 1080},
    {2560, 1080},
}};

// The sides checked, each with panel art of its own.
constexpr std::size_t kSidesChecked = 2;

// Rows each bar shows of its art.
constexpr int kBarRows = 32;

// Failures reported in full; the rest are counted.
constexpr std::size_t kReportedFailures = 40;

// Returns the panel art SIDEDATA.TDF names for each side (its intgaf).
std::vector<std::string> side_panel_art(const std::vector<uint8_t>& sidedata) {
    namespace tdf = oa::formats::tdf;

    struct Parsed {
        tdf::Document document;

        Parsed() { tdf::document_init(&document); }

        ~Parsed() { tdf::document_free(&document); }

        Parsed(const Parsed&) = delete;
        Parsed& operator=(const Parsed&) = delete;
        Parsed(Parsed&&) = delete;
        Parsed& operator=(Parsed&&) = delete;
    } parsed;

    if (sidedata.empty() || sidedata.size() > tdf::max_input_bytes ||
        !tdf::parse_text(
            &parsed.document,
            reinterpret_cast<const char*>(sidedata.data()),
            static_cast<uint32_t>(sidedata.size()),
            false,
            nullptr
        ))
        throw std::runtime_error("match bars check: gamedata/SIDEDATA.TDF does not parse");
    std::vector<std::string> names;
    for (std::size_t side = 0; side < kSidesChecked; ++side) {
        const auto section = "SIDE" + std::to_string(side);
        const auto* block = tdf::find_child(parsed.document.root, section.c_str());
        const char* art = block != nullptr ? tdf::find_value(block, "intgaf") : nullptr;
        if (art == nullptr || *art == '\0')
            throw std::runtime_error("match bars check: [" + section + "] names no intgaf");
        names.emplace_back(art);
    }
    return names;
}

} // namespace

void Runtime::check_clock_line_room(
    const std::string& label, const std::function<void(std::string)>& failed
) {
    namespace display = oa::ui::display_layout;
    namespace hud = oa::ui::hud;
    const int width = match_layout_.width;
    const auto own = display::fit_side_column(
        display::make_match_layout(width, match_layout_.height), side_column_page_rows()
    );
    const auto scale_text = [](double scale) { return std::to_string(scale); };
    if (!ui_rules().resource_panel.enabled) {
        // Without the hack the chrome keeps the interface's scale.
        if (match_layout_.scale != own.scale)
            failed(
                label + ": the chrome is drawn at scale " + scale_text(match_layout_.scale) +
                ", not the interface's " + scale_text(own.scale)
            );
        return;
    }
    // With ui.resource-panel the clock line goes in the top bar on a window
    // wider than 1024 pixels: in two sections where the interface's bar
    // already reaches far enough, at 1920x1080 and wider; beside the wind's
    // and the tidal strength's figures on the 4:3 and 5:4 windows, with
    // the chrome drawn at the largest scale whose bar reaches 16 columns
    // past the line; and on the battlefield at 1024 pixels wide or
    // narrower, the chrome at the interface's scale.
    const auto* font = match_label_font();
    if (font == nullptr) {
        failed(label + ": no font for the clock line");
        return;
    }
    const auto widths = clock_line_widths(*font, false);
    const auto reaching = [&](hud::ClockLineSpot spot) {
        return static_cast<double>(width) /
               (hud::clock_line_bar_end(top_bar_pieces_, widths, spot) + hud::kClockLineInset);
    };
    const auto expect = [&](hud::ClockLineSpot spot, double scale, const std::string& clock) {
        const auto place = clock_line_place();
        if (place.spot != spot || std::abs(match_layout_.scale - scale) > 1e-9)
            failed(
                label + clock + ": the clock line goes to place " +
                std::to_string(static_cast<int>(place.spot)) + " at scale " +
                scale_text(match_layout_.scale) + ", not place " +
                std::to_string(static_cast<int>(spot)) + " at " + scale_text(scale)
            );
    };
    const bool wide = width > hud::kClockLineBattlefieldMaxWidth;
    // Whether the interface's bar ends short of the line at a place.
    const auto short_of = [&](hud::ClockLineSpot spot) {
        return display::kSourceLeft + own.bar_columns() <
               hud::clock_line_bar_end(top_bar_pieces_, widths, spot);
    };
    if (!wide)
        expect(hud::ClockLineSpot::battlefield, own.scale, "");
    else if (width >= 1920)
        expect(hud::ClockLineSpot::sections, own.scale, "");
    else if (short_of(hud::ClockLineSpot::beside))
        expect(hud::ClockLineSpot::beside, reaching(hud::ClockLineSpot::beside), "");
    else
        expect(hud::ClockLineSpot::beside, own.scale, "");
    // While the console's Clock shows the game time, the line leaves it
    // out: the top bar's first section, the chrome on a window wider than
    // 1024 pixels drawn as large as it can be with the bar reaching 16
    // columns past the figures; on a narrower one, the chrome at the
    // interface's scale, where its bar reaches past the figures, at 800x600
    // and 1024x768 with HUD scaling Off and at 1024x600, and elsewhere on
    // the battlefield, with no line for the time. Turned off again, the
    // line and the chrome are back as they were.
    const auto shown = clock_line_place().spot;
    const double shown_scale = match_layout_.scale;
    auto& game = match_->state().game;
    const auto flags = oa::ui::console::console_flags(game);
    oa::ui::console::set_console_flags(
        game, static_cast<uint16_t>(flags | oa::ui::console::console_flag::clock)
    );
    render_match_surface();
    if (clock_line_shows_time() || hud::clock_line_battlefield_rows(clock_line_shows_time()).time)
        failed(label + " with +clock: the clock line keeps the game time");
    if (!wide)
        expect(
            short_of(hud::ClockLineSpot::first_section) ? hud::ClockLineSpot::battlefield
                                                        : hud::ClockLineSpot::first_section,
            own.scale,
            " with +clock"
        );
    else
        expect(
            hud::ClockLineSpot::first_section,
            short_of(hud::ClockLineSpot::first_section)
                ? reaching(hud::ClockLineSpot::first_section)
                : own.scale,
            " with +clock"
        );
    oa::ui::console::set_console_flags(game, flags);
    render_match_surface();
    if (!clock_line_shows_time() || clock_line_place().spot != shown ||
        match_layout_.scale != shown_scale)
        failed(label + ": the game time did not come back with +clock off");
}

void Runtime::check_match_bars() {
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        throw std::runtime_error("match bars check: needs the SDL renderer");
    namespace display = oa::ui::display_layout;
    std::vector<std::string> failures;
    std::size_t failure_count = 0;
    const auto failed = [&](std::string message) {
        if (failures.size() < kReportedFailures)
            failures.push_back(std::move(message));
        ++failure_count;
    };
    const auto art_names = side_panel_art(assets_.read("gamedata/SIDEDATA.TDF").bytes);

    for (std::size_t side = 0; side < kSidesChecked; ++side) {
        const auto& art_name = art_names[side];
        // The side's art as SIDEDATA names it, read apart from the HUD's.
        oa::formats::gaf::Archive archive;
        append_gaf_file(archive, "anims/" + art_name + ".GAF");
        const auto first_frame =
            [&](std::string_view name) -> std::optional<oa::formats::gaf::RenderedFrame> {
            const auto* sequence = gaf_sequence(archive, name);
            if (sequence == nullptr || sequence->frames.empty())
                return std::nullopt;
            auto rendered = oa::formats::gaf::render_normal(sequence->frames.front());
            if (!rendered.ok())
                return std::nullopt;
            return *rendered.frame;
        };
        const auto top_art = first_frame("PANELTOP");
        const auto bottom_art = first_frame("PANELBOT");
        if (!top_art || !bottom_art || top_art->width == 0 || bottom_art->width == 0)
            throw std::runtime_error(
                "match bars check: anims/" + art_name + ".GAF lacks PANELTOP or PANELBOT"
            );

        if (match_)
            leave_match();
        load(Screen::main_menu);
        exercise_click(menu::resource_name(menu::Button::single_player));
        exercise_click(entry::resource_name(entry::Button::skirmish));
        state_.player_count = 2;
        if (skirmish_settings_.slots.size() < 2)
            throw std::runtime_error("match bars check: the skirmish has fewer than two slots");
        skirmish_settings_.slots[0].side = static_cast<int32_t>(side);
        skirmish_settings_.slots[1].side = static_cast<int32_t>(side == 0 ? 1 : 0);
        exercise_click(skirmish::resource_name(skirmish::Button::start));
        if (screen_ != Screen::match || !match_)
            throw std::runtime_error("match bars check: Start did not enter a match");

        for (const auto& window : kWindowSizes) {
            const int width = window.first;
            const int height = window.second;
            const auto label =
                art_name + ' ' + std::to_string(width) + 'x' + std::to_string(height);
            if (!SDL_SetWindowSize(sdl_.window, width, height) || !SDL_SyncWindow(sdl_.window))
                throw std::runtime_error(std::string("SDL_SetWindowSize: ") + SDL_GetError());
            apply_output_mode();
            if (match_layout_.width != width || match_layout_.height != height)
                throw std::runtime_error("match bars check: could not size the match to " + label);
            render_match_surface();
            check_clock_line_room(label, failed);
            const int left = match_layout_.left;
            const int columns = match_layout_.bar_columns();

            // Each bar runs from the column's edge to the window's right
            // edge, its art at the bars' scale, the window's edge cutting its
            // last source column.
            const auto strips = match_hud_strips();
            for (const auto* bar : {&strips[1], &strips[2]}) {
                const auto* name = bar == &strips[1] ? "top" : "bottom";
                const auto drawn = static_cast<double>(bar->source_w) * match_layout_.scale;
                const auto last_column =
                    bar->x + static_cast<double>(bar->source_w - 1) * match_layout_.scale;
                if (bar->x != left || bar->x + bar->w < width || last_column >= width ||
                    bar->source_w != columns || std::abs(drawn - bar->w) > 1.0)
                    failed(
                        label + ": the " + name + " bar covers columns " + std::to_string(bar->x) +
                        " to " + std::to_string(bar->x + bar->w) + " from " +
                        std::to_string(bar->source_w) + " source columns, not " +
                        std::to_string(left) + " to " + std::to_string(width) + " from " +
                        std::to_string(columns)
                    );
            }

            // Every column of each bar shows art, not black.
            renderer::Surface frame;
            compose_match_frame(frame);
            const auto unlit_columns = [&frame, left, width](int top, int rows) {
                int unlit = 0;
                for (int x = left; x < width; ++x) {
                    bool lit = false;
                    for (int y = top; y < top + rows && !lit; ++y) {
                        const auto* pixel =
                            frame.rgb.data() + (static_cast<std::size_t>(y) * frame.width +
                                                static_cast<std::size_t>(x)) *
                                                   3U;
                        lit = pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0;
                    }
                    unlit += lit ? 0 : 1;
                }
                return unlit;
            };
            if (const int unlit = unlit_columns(0, match_layout_.top); unlit != 0)
                failed(label + ": " + std::to_string(unlit) + " columns of the top bar are black");
            if (const int unlit = unlit_columns(match_layout_.bottom_bar_y(), match_layout_.bottom);
                unlit != 0)
                failed(
                    label + ": " + std::to_string(unlit) + " columns of the bottom bar are black"
                );

            // The HUD's bars hold the side's art where the game places it on
            // a screen as wide as the bars' source columns: along the top,
            // PANELTOP from column 129 and PANELBOT at its own width after
            // it; along the bottom, PANELBOT from column 129; each from its
            // first row.
            const auto& background = match_hud_->background;
            const auto& palette =
                background.palette ? *background.palette : match_hud_->gui_palette;
            const int last = kBattlefieldLeft + columns;
            if (static_cast<int>(background.width) < last ||
                static_cast<int>(background.height) < kCanvasHeight) {
                failed(
                    label + ": the HUD is " + std::to_string(background.width) +
                    " columns wide, not the bars' " + std::to_string(last)
                );
                continue;
            }
            std::size_t wrong = 0;
            for (const bool top_bar : {true, false}) {
                for (int x = display::kSourceBarArtLeft; x < last; ++x) {
                    int along = x - display::kSourceBarArtLeft;
                    const auto* art = &*bottom_art;
                    if (top_bar && along < top_art->width)
                        art = &*top_art;
                    else if (top_bar)
                        along -= top_art->width;
                    const int column = along % art->width;
                    const int layer_top = top_bar ? 0 : kCanvasHeight - kBattlefieldBottom;
                    for (int row = 0; row < std::min<int>(kBarRows, art->height); ++row) {
                        const auto offset = static_cast<std::size_t>(row) * art->width +
                                            static_cast<std::size_t>(column);
                        if (art->coverage[offset] == 0)
                            continue;
                        const auto colour = static_cast<std::size_t>(art->pixels[offset]) * 4U;
                        const auto* shown =
                            background.rgb.data() +
                            (static_cast<std::size_t>(layer_top + row) * background.width +
                             static_cast<std::size_t>(x)) *
                                3U;
                        if (shown[0] != palette[colour] || shown[1] != palette[colour + 1] ||
                            shown[2] != palette[colour + 2])
                            ++wrong;
                    }
                }
            }
            if (wrong != 0)
                failed(
                    label + ": " + std::to_string(wrong) + " pixels of the bars differ from " +
                    art_name + "'s PANELTOP and PANELBOT where the game places them"
                );
        }
    }
    if (match_)
        leave_match();
    if (failure_count != 0) {
        std::string message = "match bars check: " + std::to_string(failure_count) + " failures";
        for (const auto& failure : failures)
            message += "\n  " + failure;
        throw std::runtime_error(message);
    }
    std::cout << "match bars check: " << kSidesChecked << " sides on " << kWindowSizes.size()
              << " windows passed\n";
}

} // namespace oa::app
