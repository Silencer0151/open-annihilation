// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The headless checks of the Pause key, the menus' hold on the match clock
// and the team panels, over the navigation check's skirmish.
#include "oa/app/runtime.hpp"
#include "oa/sim/match_runtime.hpp"
#include "oa/ui/console/game_fields.hpp"
#include "oa/ui/display_layout.hpp"
#include "oa/ui/hud/share_panel.hpp"
#include "oa/ui/hud/team_panels.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace oa::app {

namespace {

// Frames of a checked second of the match clock, and the clock time of each.
constexpr uint32_t kCheckFrameMs = 25;
constexpr uint32_t kCheckFramesPerSecond = 1000 / kCheckFrameMs;
// Map pixels right of the local commander the share check puts its kbot at.
constexpr int32_t kGiftSpread = 48;
// The metal the local player has when the share check opens SHARE.GUI.
constexpr float kGiftMetal = 300.0F;
// Cells of the sight grid the share check marks mapped by the local player alone.
constexpr std::size_t kMappedCells = 256;
// PlayerSetupInfo.state of a human player still in the game.
constexpr uint8_t kHumanPlaying = 1;
// PlayerSetupInfo.role bit of the player hosting the game.
constexpr uint8_t kHostRole = 0x01;

/// Records what the team panels tell the other players' machines.
struct ToldMachines {
    std::vector<std::string> log;
};

/// Returns a player's name.
///
/// @param player player record
/// @return Player.name up to its terminator or its size
std::string player_name(const oa::Player& player) {
    return {player.name, strnlen(player.name, sizeof player.name)};
}

} // namespace

void Runtime::check_pause_key() {
    const auto require = [](bool ok, const std::string& what) {
        if (!ok)
            throw std::runtime_error("pause key check: " + what);
    };
    require(screen_ == Screen::match && match_ && !match_finished_, "needs a running match");
    require(!match_paused_, "a menu is open");
    auto& game = match_->state().game;
    namespace console = oa::ui::console;
    require((game.sim_run_flags & console::kSimRunPaused) == 0, "the skirmish starts paused");
    bool running = true;
    const auto key = [&](SDL_Keycode code, SDL_Scancode scancode) {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = code;
        event.key.scancode = scancode;
        handle_sdl_event(event, running);
    };
    uint32_t clock_ms = 1000;
    // Game ticks a second of 25 ms frames runs, through the frame's gate and
    // the clock as idle_tick has them.
    const auto ticks_in_one_second = [&] {
        match_timing_.previous_clock =
            oa::base::game_loop::scaled_clock(clock_ms, match_clock_scale());
        match_timing_.remainder = 0.0F;
        const auto before = match_timing_.tick;
        for (uint32_t frame = 0; frame < kCheckFramesPerSecond; ++frame) {
            clock_ms += kCheckFrameMs;
            if (match_clock_steps())
                advance_match_clock(clock_ms);
        }
        return match_timing_.tick - before;
    };
    renderer::Surface frame;
    const auto band = match_layout_.top;
    const CanvasRect title{
        (match_layout_.width + match_layout_.left) / 2 - band,
        match_layout_.height / 2 - band / 2,
        2 * band,
        band
    };
    const auto title_pixels = [&] {
        render_match_surface();
        compose_match_frame(frame);
        return copy_rect(frame, title);
    };
    clear_local_selection();
    reset_match_command();
    pending_build_type_ = 0;
    apply_match_hud_for_selection();
    const auto panel = match_hud_panel_;

    // Pause sets the bit, opens nothing and holds the clock; the title shows.
    const auto unpaused = title_pixels();
    key(SDLK_PAUSE, SDL_SCANCODE_PAUSE);
    require((game.sim_run_flags & console::kSimRunPaused) != 0, "Pause did not set the pause bit");
    require(!match_paused_ && match_hud_panel_ == panel, "Pause opened a menu");
    const auto paused_ticks = ticks_in_one_second();
    require(
        paused_ticks == 0, "the paused skirmish ran " + std::to_string(paused_ticks) + " ticks"
    );
    const auto title_changed = changed_pixels(unpaused, title_pixels());
    require(title_changed >= kTextMinPixels, "the paused title did not show");
    key(SDLK_PAUSE, SDL_SCANCODE_PAUSE);
    require((game.sim_run_flags & console::kSimRunPaused) == 0, "Pause again kept the pause bit");
    const auto resumed_ticks = ticks_in_one_second();
    require(resumed_ticks > 0, "Pause again did not resume the skirmish");

    // Escape still opens the options menu, which holds a skirmish.
    key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    require(
        match_paused_ && match_hud_panel_ == "guis/ARMOPT.GUI", "Escape did not open ARMOPT.GUI"
    );
    require(
        !match_clock_steps() && ticks_in_one_second() == 0,
        "the options menu did not hold the skirmish"
    );
    // Pause works while the menu holds the skirmish, and a save made then
    // keeps it, though the held clock has not stepped since.
    const auto saved_paused = [&] {
        return (saved_match_timing().flags & console::kSimRunPaused) != 0;
    };
    key(SDLK_PAUSE, SDL_SCANCODE_PAUSE);
    require(
        match_paused_ && (game.sim_run_flags & console::kSimRunPaused) != 0 && saved_paused(),
        "Pause inside ARMOPT.GUI is not what a save stores"
    );
    key(SDLK_PAUSE, SDL_SCANCODE_PAUSE);
    require(!saved_paused(), "a save after Pause again inside ARMOPT.GUI stores the pause");
    key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    require(!match_paused_, "Escape did not close ARMOPT.GUI");

    // 'h' moves nothing and Tab opens no team menu outside a multiplayer game.
    std::vector<float> stores;
    for (const auto& player : game.players) {
        stores.push_back(player.metal);
        stores.push_back(player.energy);
    }
    key(SDLK_H, SDL_SCANCODE_H);
    std::vector<float> after;
    for (const auto& player : game.players) {
        after.push_back(player.metal);
        after.push_back(player.energy);
    }
    require(stores == after, "'h' moved resources in a skirmish");
    require(
        !match_paused_ && !team_panel_open() && match_hud_panel_ == panel, "'h' opened a panel"
    );
    key(SDLK_TAB, SDL_SCANCODE_TAB);
    require(!match_paused_ && !team_panel_open(), "Tab opened the team menu in a skirmish");
    std::cout << "pause key check: " << resumed_ticks
              << " ticks a second, none while paused, paused title " << title_changed
              << " pixels, ARMOPT holds the skirmish and a save there keeps the Pause key's "
                 "bit, 'h' and Tab open nothing\n";
}

void Runtime::check_team_panels() {
    const auto require = [](bool ok, const std::string& what) {
        if (!ok)
            throw std::runtime_error("team panel check: " + what);
    };
    require(
        screen_ == Screen::match && match_ && !match_finished_ && !match_paused_,
        "needs a running match with no menu open"
    );
    auto& world = match_->state();
    auto& game = world.game;
    namespace console = oa::ui::console;
    const uint8_t local = game.local_player_index;
    require(local < OA_PLAYER_COUNT, "no local player");
    uint8_t computer = OA_PLAYER_COUNT;
    for (uint8_t index = 0; index < OA_PLAYER_COUNT; ++index)
        if (index != local && game.players[index].in_use != 0 &&
            game.players[index].status == OA_PLAYER_STATUS_COMPUTER) {
            computer = index;
            break;
        }
    require(computer != OA_PLAYER_COUNT, "the skirmish has no computer player");
    oa::Player& me = game.players[local];
    oa::Player& other = game.players[computer];
    auto* my_info = oa::world_player_info(&world, &me);
    auto* other_info = oa::world_player_info(&world, &other);
    require(my_info != nullptr && other_info != nullptr, "the players have no setup blocks");

    // What the check changes, put back at its end.
    const Extension saved_extension = extension_;
    const oa::ui::hud::TeamPanelHost saved_host = team_panel_host_;
    const oa::Player saved_me = me;
    const oa::Player saved_other = other;
    const PlayerSetupInfo saved_my_info = *my_info;
    const PlayerSetupInfo saved_other_info = *other_info;
    const auto saved_mapped = match_->sight().player_bits;
    const auto restore = [&] {
        if (match_paused_ && !match_finished_)
            resume_match_pause();
        extension_ = saved_extension;
        team_panel_host_ = saved_host;
        std::memcpy(me.alliance, saved_me.alliance, sizeof me.alliance);
        std::memcpy(me.allied_by, saved_me.allied_by, sizeof me.allied_by);
        std::memcpy(other.alliance, saved_other.alliance, sizeof other.alliance);
        std::memcpy(other.allied_by, saved_other.allied_by, sizeof other.allied_by);
        me.metal = saved_me.metal;
        me.team = saved_me.team;
        other.team = saved_other.team;
        other.status = saved_other.status;
        *my_info = saved_my_info;
        *other_info = saved_other_info;
        match_->follow_player_alliances(local);
        match_->follow_player_alliances(computer);
        match_->sight_mutable().player_bits = saved_mapped;
        game.sim_run_flags = static_cast<uint16_t>(game.sim_run_flags & ~console::kSimRunPaused);
    };

    ToldMachines told;
    team_panel_host_ = {};
    team_panel_host_.context = &told;
    team_panel_host_.alliance_changed = [](
                                            void* context, uint8_t from, uint8_t to, uint8_t allied
                                        ) {
        static_cast<ToldMachines*>(context)->log.push_back(
            "ally " + std::to_string(from) + ">" + std::to_string(to) + "=" + std::to_string(allied)
        );
    };
    team_panel_host_.setup_changed = [](void* context) {
        static_cast<ToldMachines*>(context)->log.emplace_back("setup");
    };
    team_panel_host_.remove_player = [](void* context, uint8_t player, uint8_t reason) {
        static_cast<ToldMachines*>(context)->log.push_back(
            "remove " + std::to_string(player) + " " + std::to_string(reason)
        );
    };
    team_panel_host_.game_changed = [](void* context) {
        static_cast<ToldMachines*>(context)->log.emplace_back("game");
    };
    team_panel_host_.resources_given =
        [](void* context, uint8_t from, uint8_t to, bool metal, float amount) {
            static_cast<ToldMachines*>(context)->log.push_back(
                std::string(metal ? "metal " : "energy ") + std::to_string(from) + ">" +
                std::to_string(to) + " " + std::to_string(static_cast<int>(amount))
            );
        };
    team_panel_host_.sight_shared = [](void* context, uint8_t from, uint8_t to) {
        static_cast<ToldMachines*>(context)->log.push_back(
            "sight " + std::to_string(from) + ">" + std::to_string(to)
        );
    };
    // The skirmish is taken as a multiplayer match shared with other machines.
    extension_.state = [](void*, const Runtime&) -> uint32_t {
        return extension_state::multiplayer | extension_state::shared_match;
    };

    bool running = true;
    const auto key = [&](SDL_Keycode code, SDL_Scancode scancode) {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = code;
        event.key.scancode = scancode;
        handle_sdl_event(event, running);
    };
    // Clicks a control of the panel loaded as the match HUD where the pointer
    // would: `dx` and `dy` source pixels into it, or its centre for -1.
    const auto click = [&](std::string_view name, int dx = -1, int dy = -1) {
        require(match_hud_.has_value(), "no panel is loaded for " + std::string(name));
        const auto& gadgets = match_hud_->layout.gadgets;
        std::size_t index = 0;
        for (std::size_t i = 1; i < gadgets.size(); ++i)
            if (gadgets[i].common.name == name && gadgets[i].common.active != 0) {
                index = i;
                break;
            }
        require(index != 0, std::string(name) + " does not show on " + match_hud_panel_);
        const auto& common = gadgets[index].common;
        const int x = common.x + (dx < 0 ? common.width / 2 : dx);
        const int y = common.y + (dy < 0 ? common.height / 2 : dy);
        const auto at = oa::ui::display_layout::source_to_canvas(match_layout_, x, y);
        update_pointer(static_cast<float>(at.x), static_cast<float>(at.y));
        require(hovered_ == index, "the pointer over " + std::string(name) + " is not over it");
        activate_match_hud(index);
    };
    const auto shows = [&](std::string_view name) {
        if (!match_hud_)
            return false;
        for (const auto& gadget : match_hud_->layout.gadgets)
            if (gadget.common.name == name)
                return gadget.common.active != 0;
        return false;
    };
    const auto text_of = [&](std::string_view name) -> std::string {
        if (!match_hud_)
            return {};
        for (const auto& gadget : match_hud_->layout.gadgets) {
            if (gadget.common.name != name)
                continue;
            if (const auto* label = std::get_if<oa::ui::gui_layout::LabelFields>(&gadget.fields))
                return label->text;
            if (const auto* button = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget.fields))
                return button->text;
        }
        return {};
    };
    uint32_t clock_ms = 1000;
    const auto ticks_in_one_second = [&] {
        match_timing_.previous_clock =
            oa::base::game_loop::scaled_clock(clock_ms, match_clock_scale());
        match_timing_.remainder = 0.0F;
        const auto before = match_timing_.tick;
        for (uint32_t frame = 0; frame < kCheckFramesPerSecond; ++frame) {
            clock_ms += kCheckFrameMs;
            if (match_clock_steps())
                advance_match_clock(clock_ms);
        }
        return match_timing_.tick - before;
    };
    renderer::Surface frame;
    const auto band = match_layout_.top;
    const CanvasRect title{
        (match_layout_.width + match_layout_.left) / 2 - band,
        match_layout_.height / 2 - band / 2,
        2 * band,
        band
    };
    const auto title_pixels = [&] {
        render_match_surface();
        compose_match_frame(frame);
        return copy_rect(frame, title);
    };
    // The panels' frames go to the reports the navigation check writes.
    const auto snapshot = [&](const char* name) {
        render_match_surface();
        compose_match_frame(frame);
        write_ppm(fs::path("local/reports") / name, frame);
    };

    try {
        clear_local_selection();
        reset_match_command();
        pending_build_type_ = 0;
        apply_match_hud_for_selection();

        // The options menu of a shared match holds nothing and shows no
        // paused title.
        auto before = title_pixels();
        key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        require(
            match_paused_ && match_hud_panel_ == "guis/ARMOPT.GUI", "Escape did not open ARMOPT.GUI"
        );
        const auto menu_title = changed_pixels(before, title_pixels());
        require(
            menu_title < kTextMinPixels,
            "the options menu of a shared match showed the paused title"
        );
        const auto menu_ticks = ticks_in_one_second();
        require(match_clock_steps() && menu_ticks > 0, "the options menu held a shared match");
        // The preferences it opens leave the match screen, and the match
        // goes on beneath them. Escape leaves them; the screen resources they
        // loaded are put back for the checks that follow, which return to the
        // skirmish menu without loading it.
        const auto frontend_resources = resources_;
        click("PREFS");
        require(screen_ == Screen::options, "PREFS did not open the preferences");
        const auto preference_ticks = ticks_in_one_second();
        require(
            match_running() && match_clock_steps() && preference_ticks > 0,
            "the preferences held a shared match"
        );
        key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        resources_ = frontend_resources;
        require(
            screen_ == Screen::match && match_paused_ && match_hud_panel_ == "guis/ARMOPT.GUI",
            "leaving the preferences did not return to ARMOPT.GUI"
        );
        key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        require(!match_paused_, "Escape did not close ARMOPT.GUI");

        // A pause another machine sets holds the clock and shows the title.
        before = title_pixels();
        game.sim_run_flags = static_cast<uint16_t>(game.sim_run_flags | console::kSimRunPaused);
        require(
            match_clock_steps() && ticks_in_one_second() == 0,
            "a pause set elsewhere did not hold the clock"
        );
        const auto pause_title = changed_pixels(before, title_pixels());
        require(pause_title >= kTextMinPixels, "a pause set elsewhere showed no paused title");
        game.sim_run_flags = static_cast<uint16_t>(game.sim_run_flags & ~console::kSimRunPaused);
        require(ticks_in_one_second() > 0, "the match did not resume when the pause bit cleared");

        // Tab opens the tab menu over the running match; ALLIES and SHARE
        // show, CONTROL only for the host.
        key(SDLK_TAB, SDL_SCANCODE_TAB);
        require(
            team_panel_open() && match_hud_panel_ == "guis/TABMENU.GUI",
            "Tab did not open TABMENU.GUI"
        );
        require(
            shows("ALLIES") && shows("SHARE") && !shows("CONTROL"),
            "the tab menu's buttons are wrong"
        );
        snapshot("native-team-tabmenu.ppm");
        require(ticks_in_one_second() > 0, "the tab menu held the match");
        key(SDLK_TAB, SDL_SCANCODE_TAB);
        require(!team_panel_open() && !match_paused_, "Tab did not close the tab menu");

        // 'h' opens SHARE.GUI; OK gives the slider's metal and a chosen unit
        // to the computer player and shares the map.
        const auto kbot = oa::sim::unit_spawn::find_type_index(
            spawn_type_names_, match_side_prefix() == "cor" ? "CORAK" : "ARMPW"
        );
        require(kbot != 0, "the side's kbot is missing");
        uint16_t anchor = 0;
        for (const auto& slot : match_->world().slots)
            if (slot.unit_index != 0 && slot.unit != nullptr && slot.owner_index == local) {
                anchor = slot.unit_index;
                break;
            }
        require(anchor != 0, "no local unit");
        const auto& anchor_unit = *match_->world().slots[anchor].unit;
        const auto x = static_cast<int32_t>(anchor_unit.position[0] >> 16) + kGiftSpread;
        const auto z = static_cast<int32_t>(anchor_unit.position[2] >> 16);
        oa::sim::unit_spawn::Request request;
        request.player = local;
        request.type = kbot;
        request.finished = true;
        request.state = kGroundOccupancyState;
        request.position = {
            static_cast<uint32_t>(x) << 16,
            static_cast<uint32_t>(
                match_->map_height(static_cast<uint32_t>(x) << 16, static_cast<uint32_t>(z) << 16)
            ) << 16,
            static_cast<uint32_t>(z) << 16
        };
        auto* gift = match_->create(request);
        require(gift != nullptr && gift->unit != nullptr, "could not add a kbot to give");
        const auto count_kbots = [&](uint8_t owner) {
            int count = 0;
            for (const auto& slot : match_->world().slots)
                if (slot.unit_index != 0 && slot.unit != nullptr && slot.owner_index == owner &&
                    slot.unit->type_index == kbot &&
                    (slot.unit->flags & OA_UNIT_FLAG_DEATH_PENDING) == 0)
                    ++count;
            return count;
        };
        const auto their_kbots = count_kbots(computer);
        clear_local_selection();
        adopt_selection(gift->unit_index);
        me.metal = kGiftMetal;
        key(SDLK_H, SDL_SCANCODE_H);
        require(
            team_panel_open() && match_hud_panel_ == "guis/SHARE.GUI", "'h' did not open SHARE.GUI"
        );
        require(ticks_in_one_second() > 0, "SHARE.GUI held the match");
        click("PLYRLIST", 4, 3);
        const auto* metal_slider = [&]() -> const oa::ui::gui_layout::Gadget* {
            for (const auto& gadget : match_hud_->layout.gadgets)
                if (gadget.common.name == "METAL")
                    return &gadget;
            return nullptr;
        }();
        require(metal_slider != nullptr, "SHARE.GUI has no METAL slider");
        click("METAL", metal_slider->common.width - 1, metal_slider->common.height / 2);
        const auto given = static_cast<int>(kGiftMetal);
        require(text_of("METAL#") == std::to_string(given), "METAL# reads " + text_of("METAL#"));
        click("SHARUNIT");
        click("MAPINFO");
        snapshot("native-team-share.ppm");
        const auto metal_before = me.metal;
        // Cells the local player has mapped that the computer player has not;
        // the first few rows are made so.
        const auto mine = static_cast<uint16_t>(1U << local);
        const auto theirs = static_cast<uint16_t>(1U << computer);
        const auto unshared_cells = [&] {
            uint32_t count = 0;
            for (const auto bits : match_->sight().player_bits)
                count += (bits & mine) != 0 && (bits & theirs) == 0 ? 1U : 0U;
            return count;
        };
        auto& mapped = match_->sight_mutable().player_bits;
        for (std::size_t cell = 0; cell < std::min<std::size_t>(mapped.size(), kMappedCells);
             ++cell)
            mapped[cell] = static_cast<uint16_t>((mapped[cell] | mine) & ~theirs);
        const auto unshared_before = unshared_cells();
        click("OK");
        require(!team_panel_open() && !match_paused_, "OK did not close SHARE.GUI");
        const auto gave = "metal " + std::to_string(local) + ">" + std::to_string(computer) + " " +
                          std::to_string(given);
        const auto shared = "sight " + std::to_string(local) + ">" + std::to_string(computer);
        require(
            told.log == std::vector<std::string>{gave, shared}, "the share was not told as " + gave
        );
        require(
            me.metal == metal_before - kGiftMetal, "the given metal stayed with the local player"
        );
        require(
            count_kbots(computer) == their_kbots + 1,
            "the chosen kbot did not go to the computer player"
        );
        require(
            unshared_before > 0 && unshared_cells() == 0,
            "MAPINFO did not give the computer player the local player's map (" +
                std::to_string(unshared_before) + " cells before, " +
                std::to_string(unshared_cells()) + " after)"
        );

        // The computer player is taken as another machine's human for
        // ALLIES.GUI, where only such players have a toggle, and on no team
        // with the local player, whose team mates' toggles are greyed.
        other.status = OA_PLAYER_STATUS_MIRRORED;
        other_info->state = kHumanPlaying;
        me.team = OA_PLAYER_NO_TEAM;
        other.team = OA_PLAYER_NO_TEAM;
        told.log.clear();
        key(SDLK_TAB, SDL_SCANCODE_TAB);
        click("ALLIES");
        require(
            team_panel_open() && match_hud_panel_ == "guis/ALLIES.GUI",
            "ALLIES did not open ALLIES.GUI"
        );
        const auto toggle = "LIVEALLY" + std::to_string(computer);
        const auto row = "LIVEPLYR" + std::to_string(computer);
        require(
            shows(row) && text_of(row) == player_name(other),
            "ALLIES.GUI has no row for the other player"
        );
        const bool was_allied = me.alliance[computer] != 0;
        click(toggle);
        snapshot("native-team-allies.ppm");
        require(
            (me.alliance[computer] != 0) != was_allied, "the toggle did not change the alliance"
        );
        require(
            match_->allied(local, computer) != was_allied,
            "the simulation's alliance did not follow the toggle"
        );
        const auto allied = "ally " + std::to_string(local) + ">" + std::to_string(computer) + "=" +
                            std::to_string(was_allied ? 0 : 1);
        require(
            told.log == std::vector<std::string>{allied}, "the alliance was not told as " + allied
        );
        const auto lines = match_message_lines();
        const auto said =
            (was_allied ? "broke alliance with " : "allied with ") + player_name(other);
        require(
            !lines.empty() && lines.back().find(said) != std::string::npos,
            "the alliance was not said in chat"
        );
        click("OK");
        require(!team_panel_open(), "OK did not close ALLIES.GUI");

        // CONTROL.GUI shows for the host; WATCHING and a removal tell the others.
        my_info->role = static_cast<uint8_t>(my_info->role | kHostRole);
        told.log.clear();
        key(SDLK_TAB, SDL_SCANCODE_TAB);
        require(shows("CONTROL"), "CONTROL does not show for the host");
        click("CONTROL");
        require(
            team_panel_open() && match_hud_panel_ == "guis/CONTROL.GUI",
            "CONTROL did not open CONTROL.GUI"
        );
        require(!shows("LIVEPLYR" + std::to_string(local)), "CONTROL.GUI shows the local player");
        const auto watching = (my_info->options & OA_SETUP_OPTION_WATCHING_ALLOWED) != 0;
        click("WATCHING");
        snapshot("native-team-control.ppm");
        require(
            ((my_info->options & OA_SETUP_OPTION_WATCHING_ALLOWED) != 0) != watching,
            "WATCHING did not change watching"
        );
        click(row);
        require(match_hud_panel_ == "guis/YESORNO.GUI", "a player's row did not ask to remove it");
        require(
            text_of("TITLE").find(player_name(other)) != std::string::npos,
            "the removal question does not name the player"
        );
        snapshot("native-team-removal.ppm");
        click("CHOICE1");
        require(!team_panel_open(), "Yes did not close the removal question");
        key(SDLK_TAB, SDL_SCANCODE_TAB);
        click("CONTROL");
        click("OK");
        require(!team_panel_open(), "OK did not close CONTROL.GUI");
        const auto removed = "remove " + std::to_string(computer) + " 1";
        require(
            told.log == std::vector<std::string>{"setup", removed, "game"},
            "CONTROL.GUI told the others the wrong things"
        );
    } catch (...) {
        restore();
        throw;
    }
    restore();
    std::cout << "team panel check: a shared match runs under its menus and preferences and "
                 "holds on a pause set elsewhere; TABMENU, SHARE (map shared here), ALLIES "
                 "(followed by the simulation), CONTROL and the removal question tell the other "
                 "machines\n";
}

} // namespace oa::app
