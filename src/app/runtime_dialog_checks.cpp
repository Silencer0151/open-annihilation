// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Bounded checks of the stacked frontend dialogs: headless over a frontend
// screen, and over a live match through the SDL presenter.
#include "oa/app/runtime.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/ui/gui_input.hpp"
#include "oa/ui/hud/resource_bar.hpp"
#include "oa/ui/frontend/savegame_dialogs.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace oa::app {

namespace {

using oa::ui::display_layout::Rect;

// Distance from the pointer that covers every cursor frame.
constexpr int32_t kCursorReach = 64;
// Readout steps that close any store gap: an eighth of it each step, then
// one unit at a time.
constexpr int kReadoutSettleSteps = 256;

bool inside(const Rect& rect, int32_t x, int32_t y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

uint8_t* pixel(renderer::Surface& frame, int32_t x, int32_t y) {
    return &frame.rgb
                [(static_cast<std::size_t>(y) * frame.width + static_cast<std::size_t>(x)) * 3U];
}

bool same_pixel(renderer::Surface& a, renderer::Surface& b, int32_t x, int32_t y) {
    return std::equal(pixel(a, x, y), pixel(a, x, y) + 3, pixel(b, x, y));
}

// Pixels of `rect` that differ between the frames.
std::size_t differing_pixels(renderer::Surface& a, renderer::Surface& b, const Rect& rect) {
    std::size_t count = 0;
    for (int32_t y = rect.y; y < rect.y + rect.height; ++y)
        for (int32_t x = rect.x; x < rect.x + rect.width; ++x)
            if (!same_pixel(a, b, x, y))
                ++count;
    return count;
}

// Pixels outside every excluded rectangle that differ between the frames.
std::size_t differing_pixels_outside(
    renderer::Surface& a, renderer::Surface& b, std::initializer_list<Rect> excluded
) {
    std::size_t count = 0;
    for (int32_t y = 0; y < static_cast<int32_t>(a.height); ++y)
        for (int32_t x = 0; x < static_cast<int32_t>(a.width); ++x)
            if (std::none_of(
                    excluded.begin(),
                    excluded.end(),
                    [x, y](const Rect& rect) { return inside(rect, x, y); }
                ) &&
                !same_pixel(a, b, x, y))
                ++count;
    return count;
}

uint64_t brightness(renderer::Surface& frame, const Rect& rect) {
    uint64_t sum = 0;
    for (int32_t y = rect.y; y < rect.y + rect.height; ++y)
        for (int32_t x = rect.x; x < rect.x + rect.width; ++x) {
            const auto* rgb = pixel(frame, x, y);
            sum += static_cast<uint64_t>(rgb[0]) + rgb[1] + rgb[2];
        }
    return sum;
}

// Brightness of the pixels of `rect` outside `excluded`.
uint64_t brightness_outside(renderer::Surface& frame, const Rect& rect, const Rect& excluded) {
    uint64_t sum = 0;
    for (int32_t y = rect.y; y < rect.y + rect.height; ++y)
        for (int32_t x = rect.x; x < rect.x + rect.width; ++x)
            if (!inside(excluded, x, y)) {
                const auto* rgb = pixel(frame, x, y);
                sum += static_cast<uint64_t>(rgb[0]) + rgb[1] + rgb[2];
            }
    return sum;
}

Rect cursor_reach(float x, float y) {
    return {
        static_cast<int32_t>(x) - kCursorReach,
        static_cast<int32_t>(y) - kCursorReach,
        2 * kCursorReach,
        2 * kCursorReach
    };
}

constexpr const char* kLoadBitmap = "bitmaps/dloadgame2.pcx";
constexpr const char* kSaveBitmap = "bitmaps/dsavegame2.pcx";
constexpr const char* kCheckSaveName = "LSCHECK1";
constexpr const char* kCheckSecondSaveName = "LSCHECK2";
constexpr const char* kBriefingGui = "guis/briefing.gui";
constexpr const char* kBriefingBitmap = "bitmaps/igmbrief.pcx";
// The ARM campaign's third mission, whose briefing runs to a second page.
constexpr std::size_t kBriefingCheckMission = 2;

/// Checks a panel drawn over `frame` with a bitmap as its backdrop.
///
/// The root is at `expected`; each panel pixel outside the records and the
/// cursor is the bitmap's index in `palette`, and every panel pixel outside
/// the cursor a colour of that palette.
///
/// @param frame frame the panel is drawn on
/// @param layout the panel's records, the root placed on the frame
/// @param bitmap the backdrop, whose top-left corner is the panel's
/// @param palette palette the frame's colours come from
/// @param expected where the root belongs on the frame
/// @param cursor area the software cursor may cover
/// @param what the check and the panel, which start each failure message
/// @return the panel's rectangle on the frame
Rect check_backdrop_panel(
    renderer::Surface& frame,
    const oa::ui::gui_layout::Layout& layout,
    const oa::Image& bitmap,
    const oa::PaletteBytes& palette,
    oa::ui::display_layout::Point expected,
    const Rect& cursor,
    const std::string& what
) {
    const auto& root = layout.gadgets.front().common;
    if (root.x != expected.x || root.y != expected.y)
        throw std::runtime_error(
            what + " panel is at " + std::to_string(root.x) + ',' + std::to_string(root.y) +
            ", not " + std::to_string(expected.x) + ',' + std::to_string(expected.y)
        );
    const Rect panel{root.x, root.y, root.width, root.height};
    if (panel.x < 0 || panel.y < 0 || panel.x + panel.width > static_cast<int32_t>(frame.width) ||
        panel.y + panel.height > static_cast<int32_t>(frame.height) ||
        panel.width > static_cast<int32_t>(bitmap.width) ||
        panel.height > static_cast<int32_t>(bitmap.height) ||
        bitmap.indices.size() != static_cast<std::size_t>(bitmap.width) * bitmap.height)
        throw std::runtime_error(what + " panel does not fit the frame or its bitmap");
    std::vector<Rect> records;
    for (std::size_t index = 1; index < layout.gadgets.size(); ++index)
        if (const auto rect = oa::ui::gui_input::gadget_geometry(layout.gadgets, index))
            records.push_back(
                {panel.x + rect->left,
                 panel.y + rect->top,
                 rect->right - rect->left + 1,
                 rect->bottom - rect->top + 1}
            );
    std::vector<bool> colours(1U << 24);
    for (std::size_t entry = 0; entry < oa::palette_color_count; ++entry) {
        const auto* rgb = &palette[entry * oa::palette_entry_bytes];
        colours
            [static_cast<std::size_t>(rgb[0]) << 16 | static_cast<std::size_t>(rgb[1]) << 8 |
             rgb[2]] = true;
    }
    std::size_t compared = 0;
    std::size_t differing = 0;
    std::size_t foreign = 0;
    for (int32_t y = panel.y; y < panel.y + panel.height; ++y)
        for (int32_t x = panel.x; x < panel.x + panel.width; ++x) {
            if (inside(cursor, x, y))
                continue;
            const auto* shown = pixel(frame, x, y);
            if (!colours
                    [static_cast<std::size_t>(shown[0]) << 16 |
                     static_cast<std::size_t>(shown[1]) << 8 | shown[2]])
                ++foreign;
            if (std::any_of(records.begin(), records.end(), [x, y](const Rect& rect) {
                    return inside(rect, x, y);
                }))
                continue;
            const auto index = bitmap.indices
                                   [static_cast<std::size_t>(y - panel.y) * bitmap.width +
                                    static_cast<std::size_t>(x - panel.x)];
            ++compared;
            if (!std::equal(
                    shown,
                    shown + 3,
                    &palette[static_cast<std::size_t>(index) * oa::palette_entry_bytes]
                ))
                ++differing;
        }
    const auto area =
        static_cast<std::size_t>(panel.width) * static_cast<std::size_t>(panel.height);
    if (compared < area / 2 || differing != 0)
        throw std::runtime_error(
            what + " backdrop differs from its bitmap in the palette below at " +
            std::to_string(differing) + " of " + std::to_string(compared) + " pixels"
        );
    if (foreign != 0)
        throw std::runtime_error(
            what + " panel shows " + std::to_string(foreign) + " pixels outside the palette below"
        );
    return panel;
}

} // namespace

void Runtime::check_dialogs(const fs::path& report_directory) {
    namespace dialogs = oa::ui::frontend_dialogs;
    load(Screen::single_player);
    show_frontend_message(
        entry::message_text(entry::Message::multiplayer_disc),
        entry::disc_message_width,
        entry::message_show_ok,
        entry::message_fit_width
    );
    if (dialogs::dialog_kind() != dialogs::DialogKind::message_box)
        throw std::runtime_error("navigation check did not open MSGBOX.GUI");
    rebuild_surface();
    write_ppm(report_directory / "native-msgbox.ppm", surface_);
    auto context = screen_context();
    if (!dialogs::dialog_click(&context, "OK") ||
        dialogs::dialog_kind() != dialogs::DialogKind::none)
        throw std::runtime_error("MSGBOX.GUI OK did not release the message box");
    show_cd_check();
    if (dialogs::dialog_kind() != dialogs::DialogKind::cd_check)
        throw std::runtime_error("navigation check did not open CDCHECK.GUI");
    rebuild_surface();
    write_ppm(report_directory / "native-cdcheck.ppm", surface_);
    dialogs::close_dialog();
    open_help();
    const auto* help = dialogs::dialog_resources();
    if (dialogs::dialog_kind() != dialogs::DialogKind::help || help == nullptr ||
        help->layout.gadgets.size() != 4 + 17 * 2)
        throw std::runtime_error("navigation check did not fill HELP.GUI page 1");
    rebuild_surface();
    write_ppm(report_directory / "native-help.ppm", surface_);
    if (!dialogs::dialog_click(&context, "Page") || dialogs::help_page() != 1)
        throw std::runtime_error("HELP.GUI Page did not advance to page 2");
    rebuild_surface();
    write_ppm(report_directory / "native-help-page2.ppm", surface_);
    if (!dialogs::dialog_click(&context, "OK") ||
        dialogs::dialog_kind() != dialogs::DialogKind::none)
        throw std::runtime_error("HELP.GUI OK did not release the help panel");
    std::cout << "dialog check: MSGBOX.GUI, CDCHECK.GUI and HELP.GUI pages\n";
}

void Runtime::check_match_dialogs() {
    namespace dialogs = oa::ui::frontend_dialogs;
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    start_benchmark_skirmish();
    show_match_pause_menu();
    // The HUD metrics panel eases the shown stores a step each frame; with them settled
    // the paused and help frames differ only where the dialog draws.
    if (const auto viewer = match_view_player(); viewer < OA_PLAYER_COUNT) {
        auto& game = match_->state().game;
        for (int step = 0; step < kReadoutSettleSteps; ++step)
            oa::ui::hud::update_resource_readout(
                game.resource_readout, game.players[viewer], game.tick
            );
    }
    renderer::Surface paused;
    capture_frame_ = &paused;
    render();
    capture_frame_ = nullptr;
    write_ppm(report_directory / "native-match-paused.ppm", paused);
    activate_pause_gadget("HELP");
    if (dialogs::dialog_kind() != dialogs::DialogKind::help)
        throw std::runtime_error("match dialog check: HELP did not open HELP.GUI");
    renderer::Surface presented;
    capture_frame_ = &presented;
    render();
    capture_frame_ = nullptr;
    write_ppm(report_directory / "native-match-help.ppm", presented);
    const auto canvas_width = static_cast<uint32_t>(match_layout_.width);
    const auto canvas_height = static_cast<uint32_t>(match_layout_.height);
    if (!match_use_layers_ || presented.width != canvas_width ||
        presented.height != canvas_height || paused.width != canvas_width ||
        paused.height != canvas_height)
        throw std::runtime_error("match dialog check: HELP.GUI left the layered match presenter");
    const auto& gadgets = dialogs::dialog_resources()->layout.gadgets;
    const auto& root = gadgets.front().common;
    const Rect panel{root.x, root.y, root.width, root.height};
    const auto strip = match_layout_.left;
    if (panel.x != (match_layout_.width - strip - panel.width) / 2 + strip ||
        panel.y != (match_layout_.height - panel.height) / 2)
        throw std::runtime_error(
            "match dialog check: HELP.GUI is not centred right of the side column"
        );
    const auto area =
        static_cast<std::size_t>(panel.width) * static_cast<std::size_t>(panel.height);
    if (differing_pixels(paused, presented, panel) < area / 2)
        throw std::runtime_error("match dialog check: the presented frame does not show HELP.GUI");
    const auto& options = match_hud_->layout.gadgets.front().common;
    const auto shaded = oa::ui::display_layout::source_rect_to_canvas(
        match_layout_, options.x, options.y, options.width, options.height
    );
    if (brightness(presented, shaded) * 4 > brightness(paused, shaded) * 3)
        throw std::runtime_error("match dialog check: HELP.GUI did not darken the options panel");
    // The software cursor animates between the two frames.
    const auto pointer_x = static_cast<int32_t>(pointer_x_);
    const auto pointer_y = static_cast<int32_t>(pointer_y_);
    const Rect cursor{
        pointer_x - kCursorReach, pointer_y - kCursorReach, 2 * kCursorReach, 2 * kCursorReach
    };
    if (differing_pixels_outside(paused, presented, {panel, shaded, cursor}) != 0)
        throw std::runtime_error(
            "match dialog check: HELP.GUI changed the frame outside its panels"
        );
    const auto ok = std::find_if(gadgets.begin(), gadgets.end(), [](const auto& gadget) {
        return gadget.common.name == "OK";
    });
    if (ok == gadgets.end())
        throw std::runtime_error("match dialog check: HELP.GUI has no OK button");
    float window_x = 0;
    float window_y = 0;
    if (!SDL_RenderCoordinatesToWindow(
            sdl_.renderer,
            static_cast<float>(panel.x + ok->common.x + ok->common.width / 2),
            static_cast<float>(panel.y + ok->common.y + ok->common.height / 2),
            &window_x,
            &window_y
        ))
        throw std::runtime_error(std::string("SDL_RenderCoordinatesToWindow: ") + SDL_GetError());
    for (const auto type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
        SDL_Event click{};
        click.button.type = type;
        click.button.windowID = SDL_GetWindowID(sdl_.window);
        click.button.button = SDL_BUTTON_LEFT;
        click.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        click.button.clicks = 1;
        click.button.x = window_x;
        click.button.y = window_y;
        dispatch_screen_input(click);
    }
    if (dialogs::dialog_count() != 0)
        throw std::runtime_error(
            "match dialog check: OK at its presented position did not close HELP.GUI"
        );
    std::cout << "match dialog check: HELP.GUI at " << panel.x << ',' << panel.y << " on the "
              << match_layout_.width << 'x' << match_layout_.height << " match canvas\n";
    check_in_game_briefing(report_directory);
}

void Runtime::check_in_game_briefing(const fs::path& report_directory) {
    const std::string what = "match briefing check: BRIEFING.GUI";
    const auto bitmap = oa::decode_pcx(assets_.read(kBriefingBitmap).bytes);
    const auto require = [&what](bool ok, const std::string& failure) {
        if (!ok)
            throw std::runtime_error(what + ' ' + failure);
    };
    const auto authored_layout = oa::ui::gui_layout::parse(assets_.read(kBriefingGui).bytes);
    require(authored_layout.ok() && !authored_layout.layout->gadgets.empty(), "does not parse");
    const auto authored = authored_layout.layout->gadgets.front().common;
    // The authored position centres the panel on a 640x480 screen.
    require(
        authored.x == (kCanvasWidth - authored.width) / 2 &&
            authored.y == (kCanvasHeight - authored.height) / 2,
        "is not authored centred on a 640x480 screen"
    );
    const auto click = [&](const oa::ui::gui_layout::CommonFields& root, std::string_view name) {
        const auto* gadget = widget(name);
        require(gadget != nullptr, "has no " + std::string(name));
        float window_x = 0;
        float window_y = 0;
        if (!SDL_RenderCoordinatesToWindow(
                sdl_.renderer,
                static_cast<float>(root.x + gadget->common.x + gadget->common.width / 2),
                static_cast<float>(root.y + gadget->common.y + gadget->common.height / 2),
                &window_x,
                &window_y
            ))
            throw std::runtime_error(
                std::string("SDL_RenderCoordinatesToWindow: ") + SDL_GetError()
            );
        bool running = true;
        for (const auto type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
            SDL_Event event{};
            event.button.type = type;
            event.button.windowID = SDL_GetWindowID(sdl_.window);
            event.button.button = SDL_BUTTON_LEFT;
            event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.clicks = 1;
            event.button.x = window_x;
            event.button.y = window_y;
            dispatch_event(event, running);
        }
    };
    for (const auto [width, height] :
         {std::pair{kCanvasWidth, kCanvasHeight},
          std::pair{kDefaultWindowWidth, kDefaultWindowHeight}}) {
        const auto size = std::to_string(width) + 'x' + std::to_string(height);
        if (match_)
            leave_match();
        if (!SDL_SetWindowSize(sdl_.window, width, height) || !SDL_SyncWindow(sdl_.window))
            throw std::runtime_error(std::string("SDL_SetWindowSize: ") + SDL_GetError());
        preferences_.side = 0;
        discover_campaigns();
        require(
            campaign_mission_files_.size() > kBriefingCheckMission,
            "has no campaign with mission " + std::to_string(kBriefingCheckMission + 1)
        );
        selected_mission_index_ = kBriefingCheckMission;
        show_mission_briefing();
        require(screen_ == Screen::briefing, "check could not open the mission's briefing");
        // The frontend briefing's first line, as the mission's briefing file has it.
        const auto heading = briefing_text_.substr(0, briefing_text_.find_first_of("\r\n"));
        start_campaign_mission();
        require(
            screen_ == Screen::match && match_ && campaign_mission_,
            "check could not start the mission: " + status_
        );
        require(
            match_layout_.width == width && match_layout_.height == height,
            "check could not size the match canvas to " + size
        );
        show_match_pause_menu();
        // The pointer rests in the window's corner, clear of the panel.
        update_pointer(static_cast<float>(width - 1), static_cast<float>(height - 1));
        // Settled readouts leave the paused frame as the briefing finds it.
        if (const auto viewer = match_view_player(); viewer < OA_PLAYER_COUNT) {
            auto& game = match_->state().game;
            for (int step = 0; step < kReadoutSettleSteps; ++step)
                oa::ui::hud::update_resource_readout(
                    game.resource_readout, game.players[viewer], game.tick
                );
        }
        render();
        renderer::Surface paused;
        compose_match_layers(paused);
        const auto palette = match_palette_;
        activate_pause_gadget("MISSION");
        require(screen_ == Screen::briefing, "did not open from the pause menu's MISSION");
        renderer::Surface presented;
        capture_frame_ = &presented;
        render();
        capture_frame_ = nullptr;
        write_ppm(report_directory / ("native-match-briefing-" + size + ".ppm"), presented);
        require(
            presented.width == paused.width && presented.height == paused.height &&
                surface_.width == paused.width && surface_.height == paused.height,
            "is not drawn over the " + size + " match frame"
        );
        const auto root = resources_.layout.gadgets.front().common;
        const auto reach = cursor_reach(pointer_x_, pointer_y_);
        const auto panel = check_backdrop_panel(
            surface_,
            resources_.layout,
            bitmap,
            palette,
            {authored.x, authored.y},
            reach,
            what + " on the " + size + " window"
        );
        require(
            differing_pixels_outside(paused, surface_, {panel, reach}) == 0,
            "changed the paused match outside its panel on the " + size + " window"
        );
        require(
            briefing_row(0) == heading,
            "reads \"" + briefing_row(0) + "\" on its first row, not \"" + heading + '"'
        );
        click(root, "MOREBAR");
        require(
            screen_ == Screen::briefing && briefing_row(0) != heading,
            "MOREBAR did not turn the page"
        );
        click(root, "OK");
        require(
            screen_ == Screen::match && match_paused_ && match_hud_ &&
                std::any_of(
                    match_hud_->layout.gadgets.begin(),
                    match_hud_->layout.gadgets.end(),
                    [](const auto& gadget) { return gadget.common.name == "MISSION"; }
                ),
            "OK did not return to the pause menu"
        );
        std::cout << what << " at " << panel.x << ',' << panel.y << " on the " << size
                  << " window\n";
    }
    leave_match();
    if (!SDL_SetWindowSize(sdl_.window, kDefaultWindowWidth, kDefaultWindowHeight) ||
        !SDL_SyncWindow(sdl_.window))
        throw std::runtime_error(std::string("SDL_SetWindowSize: ") + SDL_GetError());
}

void Runtime::check_load_save() {
    namespace dialogs = oa::ui::frontend_dialogs;
    if (!offers_saved_games()) {
        check_saved_games_unavailable();
        return;
    }
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    const auto load_bitmap = oa::decode_pcx(assets_.read(kLoadBitmap).bytes);
    const auto save_bitmap = oa::decode_pcx(assets_.read(kSaveBitmap).bytes);
    const auto authored_layout = oa::ui::gui_layout::parse(assets_.read("guis/loadgame.gui").bytes);
    if (!authored_layout.ok() || authored_layout.layout->gadgets.empty())
        throw std::runtime_error("load/save check: LOADGAME.GUI does not parse");
    const auto header = authored_layout.layout->gadgets.front().common;
    bool running = true;
    const auto click = [&](int32_t x, int32_t y) {
        float window_x = 0;
        float window_y = 0;
        if (!SDL_RenderCoordinatesToWindow(
                sdl_.renderer, static_cast<float>(x), static_cast<float>(y), &window_x, &window_y
            ))
            throw std::runtime_error(
                std::string("SDL_RenderCoordinatesToWindow: ") + SDL_GetError()
            );
        for (const auto type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
            SDL_Event event{};
            event.button.type = type;
            event.button.windowID = SDL_GetWindowID(sdl_.window);
            event.button.button = SDL_BUTTON_LEFT;
            event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.clicks = 1;
            event.button.x = window_x;
            event.button.y = window_y;
            dispatch_event(event, running);
        }
    };
    const auto record = [&](std::string_view name) {
        const auto* gadget = widget(name);
        if (gadget == nullptr)
            throw std::runtime_error("load/save check: LOADGAME.GUI has no " + std::string(name));
        const auto origin = panel_origin();
        return Rect{
            origin.x + gadget->common.x,
            origin.y + gadget->common.y,
            gadget->common.width,
            gadget->common.height
        };
    };
    const auto click_record = [&](std::string_view name) {
        const auto rect = record(name);
        click(rect.x + rect.width / 2, rect.y + rect.height / 2);
    };
    std::size_t save_files = 0;
    std::error_code listing;
    for (const auto& entry :
         fs::directory_iterator(save_game_root() / oa::ui::frontend::kSaveDirectory, listing))
        if (entry.is_regular_file())
            ++save_files;

    // Single Player's LOAD GAME: centred on the 640x480 frame over SINGLE.GUI.
    exercise_click(menu::resource_name(menu::Button::single_player));
    if (screen_ != Screen::single_player)
        throw std::runtime_error("load/save check did not reach SINGLE.GUI");
    const auto single_palette = screen_palette();
    auto single = frame_without_cursor();
    exercise_click(entry::resource_name(entry::Button::load_game));
    if (screen_ != Screen::load_game || save_dialog_open())
        throw std::runtime_error("LOAD GAME did not open the load dialog");
    tick_screen_packages();
    if (dialogs::dialog_kind() == dialogs::DialogKind::message_box) {
        if (save_files != 0)
            throw std::runtime_error("load/save check: the load dialog found no save to list");
        rebuild_surface();
        write_ppm(report_directory / "native-loadsave-load-empty.ppm", surface_);
        auto context = screen_context();
        if (!dialogs::dialog_click(&context, "OK") || dialogs::dialog_count() != 0)
            throw std::runtime_error("load/save check: OK did not close the no-saves message");
    } else if (save_files == 0) {
        throw std::runtime_error("load/save check: no message says there is no save to load");
    }
    rebuild_surface();
    write_ppm(report_directory / "native-loadsave-load-frontend.ppm", surface_);
    const oa::ui::display_layout::Point centred{
        (kCanvasWidth - header.width) / 2, (kCanvasHeight - header.height) / 2
    };
    const auto panel = check_backdrop_panel(
        surface_,
        resources_.layout,
        load_bitmap,
        single_palette,
        centred,
        cursor_reach(pointer_x_, pointer_y_),
        "load/save check: the Single Player load"
    );
    if (surface_.width != single.width || surface_.height != single.height)
        throw std::runtime_error(
            "load/save check: the load dialog left the Single Player frame size"
        );
    const Rect whole{0, 0, static_cast<int32_t>(single.width), static_cast<int32_t>(single.height)};
    if (brightness_outside(surface_, whole, panel) * 4 >
        brightness_outside(single, whole, panel) * 3)
        throw std::runtime_error("load/save check: the load dialog did not darken SINGLE.GUI");
    click_record("CANCEL");
    if (screen_ != Screen::single_player)
        throw std::runtime_error(
            "load/save check: CANCEL at its drawn position did not leave the load dialog"
        );
    std::cout << "load/save check: load dialog at " << panel.x << ',' << panel.y
              << " over Single Player\n";

    // A paused skirmish: the save dialog at the authored root over the match.
    exercise_click(entry::resource_name(entry::Button::skirmish));
    state_.player_count = 2;
    if (map_player_capacity() < 2)
        throw std::runtime_error("load/save check map lacks two start positions");
    exercise_click(skirmish::resource_name(skirmish::Button::start));
    if (screen_ != Screen::match || !match_)
        throw std::runtime_error("load/save check Start did not enter a match");
    show_match_pause_menu();

    struct Paused {
        renderer::Surface frame;
        Rect options;
        oa::PaletteBytes palette{};
    };

    const auto open_over_match = [&](std::string_view gadget, bool save) {
        // The HUD metrics panel eases the shown stores a step each frame; settled, the
        // frames before and under the dialog differ only where it draws.
        auto& game = match_->state().game;
        if (const auto viewer = match_view_player(); viewer < OA_PLAYER_COUNT)
            for (int step = 0; step < kReadoutSettleSteps; ++step)
                oa::ui::hud::update_resource_readout(
                    game.resource_readout, game.players[viewer], game.tick
                );
        render();
        Paused paused;
        compose_match_layers(paused.frame);
        const auto& options = match_hud_->layout.gadgets.front().common;
        const auto shaded = oa::ui::display_layout::source_rect_to_canvas(
            match_layout_, options.x, options.y, options.width, options.height
        );
        paused.options = {shaded.x, shaded.y, shaded.width, shaded.height};
        paused.palette = match_palette_;
        activate_pause_gadget(gadget);
        if (screen_ != Screen::load_game || save_dialog_open() != save)
            throw std::runtime_error(
                "load/save check: " + std::string(gadget) + " did not open its dialog"
            );
        tick_screen_packages();
        return paused;
    };
    const auto present = [&](const Paused& paused, const char* role) {
        renderer::Surface presented;
        capture_frame_ = &presented;
        render();
        capture_frame_ = nullptr;
        if (presented.width != paused.frame.width || presented.height != paused.frame.height ||
            surface_.width != paused.frame.width || surface_.height != paused.frame.height)
            throw std::runtime_error(
                std::string("load/save check: the ") + role + " dialog left the match frame size"
            );
    };
    const auto check_over_match = [&](Paused& paused,
                                      const oa::Image& bitmap,
                                      oa::ui::display_layout::Point expected,
                                      const char* role) {
        const auto reach = cursor_reach(pointer_x_, pointer_y_);
        const auto shown = check_backdrop_panel(
            surface_,
            resources_.layout,
            bitmap,
            paused.palette,
            expected,
            reach,
            std::string("load/save check: the ") + role
        );
        if (differing_pixels_outside(paused.frame, surface_, {shown, paused.options, reach}) != 0)
            throw std::runtime_error(
                std::string("load/save check: the ") + role +
                " dialog changed the match outside its panels"
            );
        if (brightness_outside(surface_, paused.options, shown) * 4 >
            brightness_outside(paused.frame, paused.options, shown) * 3)
            throw std::runtime_error(
                std::string("load/save check: the ") + role +
                " dialog did not darken the options panel"
            );
        return shown;
    };
    const oa::ui::display_layout::Point authored{header.x, header.y};
    (void)open_over_match("SAVEGAME", true);
    click_record("CANCEL");
    if (screen_ != Screen::match || save_dialog_open())
        throw std::runtime_error(
            "load/save check: CANCEL at its drawn position did not close the save dialog"
        );
    Rect save_panel{};
    for (const auto* name : {kCheckSaveName, kCheckSecondSaveName}) {
        auto paused = open_over_match("SAVEGAME", true);
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.key = SDLK_BACKSPACE;
        for (int erase = 0; erase < 32; ++erase)
            dispatch_event(key, running);
        SDL_Event text{};
        text.type = SDL_EVENT_TEXT_INPUT;
        text.text.text = name;
        dispatch_event(text, running);
        present(paused, "save");
        if (name == kCheckSaveName) {
            write_ppm(report_directory / "native-loadsave-save-match.ppm", surface_);
            save_panel = check_over_match(paused, save_bitmap, authored, "save");
        }
        key.key.key = SDLK_RETURN;
        dispatch_event(key, running);
        if (screen_ != Screen::match || save_dialog_open())
            throw std::runtime_error(
                "load/save check: Return did not save and close the save dialog"
            );
    }

    // The load dialog centred on the match frame; a click on the second
    // GAMES row moves the lit row there.
    auto paused = open_over_match("LOADGAME", false);
    const oa::ui::display_layout::Point match_centred{
        (static_cast<int32_t>(paused.frame.width) - header.width) / 2,
        (static_cast<int32_t>(paused.frame.height) - header.height) / 2
    };
    present(paused, "match load");
    auto listed = surface_;
    const auto games = record("GAMES");
    const auto* fields = std::get_if<oa::ui::gui_layout::ListBoxFields>(&widget("GAMES")->fields);
    const int32_t step = fields != nullptr && fields->item_height != 0
                             ? fields->item_height
                             : oa::formats::fnt::line_height(resources_.font) + 1;
    const auto row = [&](int32_t index) {
        return Rect{games.x + 2, games.y + 2 + index * step, games.width - 3, step};
    };
    click(row(1).x + 8, row(1).y + step / 2);
    present(paused, "match load");
    if (brightness(surface_, row(1)) <= brightness(listed, row(1)) ||
        brightness(surface_, row(0)) >= brightness(listed, row(0)))
        throw std::runtime_error(
            "load/save check: a click on the second GAMES row did not light it"
        );
    write_ppm(report_directory / "native-loadsave-load-match.ppm", surface_);
    const auto load_panel = check_over_match(paused, load_bitmap, match_centred, "match load");
    click_record("CANCEL");
    if (screen_ != Screen::match || !match_paused_)
        throw std::runtime_error("load/save check: CANCEL did not return to the paused match");
    std::cout << "load/save check: save dialog at " << save_panel.x << ',' << save_panel.y
              << ", load dialog at " << load_panel.x << ',' << load_panel.y << " on the "
              << paused.frame.width << 'x' << paused.frame.height << " match frame\n";
}

} // namespace oa::app
