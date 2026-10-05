// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The console's cheat gate against the running app: the flag each mission
// start sets, the chat line's command mask, where a cheat's echo goes, the
// Game Settings sheet's Cheat Codes row, and what each cheat does to the
// running match in a campaign and a skirmish.
#include "oa/app/runtime.hpp"
#include "oa/app/game_directory.hpp"
#include "match_fault.hpp"

#include "oa/audio/unit_announcements.hpp"
#include "oa/data/campaign/campaign_file.hpp"
#include "oa/sim/scenario/commander_rules.hpp"
#include "oa/sim/match_runtime.hpp"
#include "oa/sim/simulation_state.hpp"
#include "oa/sim/weapon_execution/weapon_launch.hpp"
#include "oa/ui/console/console.hpp"
#include "oa/ui/console/game_fields.hpp"
#include "oa/ui/frontend/ingame_menu.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

namespace oa::app {

namespace console = oa::ui::console;

namespace {

void require(bool ok, const std::string& what) {
    if (!ok)
        throw std::runtime_error("console cheat check: " + what);
}

// "<name> " as the chat formatter starts a line the local player sends.
std::string speaker_prefix(const oa::World& world) {
    const auto& name = world.game.players[world.game.local_player_index].second_name;
    return "<" + std::string(name, strnlen(name, sizeof name)) + "> ";
}

bool last_line_is(const std::vector<std::string>& lines, std::string_view text) {
    return !lines.empty() && lines.back() == text;
}

bool logged(const std::vector<std::string>& lines, std::string_view text) {
    return std::find(lines.begin(), lines.end(), text) != lines.end();
}

bool developer(const oa::Game& game) {
    return (console::console_flags(game) & console::console_flag::developer) != 0;
}

/// The sight rules the LOS cheats switch: mapping, line of sight and its type.
constexpr uint8_t kSightRules = console::visibility_flag::mapping |
                                console::visibility_flag::line_of_sight |
                                console::visibility_flag::los_type;

/// The damage a test shot deals before the damage cheats change it.
constexpr int32_t kTestShotDamage = 1000;

/// The folder below the save root that "+MakePoster" writes to in the check.
constexpr const char* kPosterFolder = "cheat-posters";

} // namespace

void Runtime::check_console_skirmish_cheats(const std::function<void(const char*)>& enter_line) {
    require(
        match_session_kind() == oa::data::campaign::SessionKind::skirmish &&
            session_cheats_allowed_,
        "the skirmish start did not allow cheats"
    );
    oa::World& world = match_->state();
    oa::Game& game = world.game;
    require(!developer(game), "the developer passphrase is already set");
    const console::Console* con = match_console();
    require(
        con != nullptr && console::console_chat_mask(con) ==
                              (console::command_class::option | console::command_class::cheat |
                               console::command_class::private_echo),
        "the skirmish chat line does not run options and cheats"
    );
    const auto prefix = speaker_prefix(world);
    const auto viewer = game.viewpoint_player;
    const float metal = game.players[viewer].metal;
    const auto mode = game.chat_mode;
    enter_line("+atm");
    // The console adds its ATM amount: 1000, or a mod profile's.
    const float atm = con->atm_amount;
    require(game.players[viewer].metal == metal + atm, "+atm did not add the ATM amount of metal");
    require(last_line_is(match_message_lines(), prefix + "+atm"), "+atm was not echoed");
    require(game.chat_mode == mode, "the cheat's echo left its chat mode set");
    enter_line("+xyzzy");
    require(
        last_line_is(match_message_lines(), prefix + "+xyzzy"),
        "an unknown +word was not sent as chat"
    );
    const auto effects = check_console_cheat_effects(enter_line, false);

    show_match_pause_menu();
    activate_pause_gadget("MISSION");
    require(match_hud_.has_value(), "MISSION left no panel");
    std::vector<std::string> rows;
    for (const auto& gadget : match_hud_->layout.gadgets)
        if (const auto* label = std::get_if<oa::ui::gui_layout::LabelFields>(&gadget.fields))
            rows.push_back(label->text);
    require(
        logged(rows, "Difficulty:") && logged(rows, "Max Units:") && !logged(rows, "Cheat Codes:"),
        "MISSION did not open the skirmish's Game Settings sheet"
    );
    renderer::Surface sheet;
    compose_match_frame(sheet);
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    write_ppm(report_directory / "native-game-settings.ppm", sheet);
    activate_pause_gadget("OK");
    require(
        match_paused_ && match_hud_.has_value() &&
            std::any_of(
                match_hud_->layout.gadgets.begin(),
                match_hud_->layout.gadgets.end(),
                [](const auto& gadget) { return gadget.common.name == "MISSION"; }
            ),
        "the sheet's OK did not return to the options panel"
    );
    resume_match_pause();
    std::cout << "console cheat check: a skirmish runs +atm and echoes it to everyone, sends an "
                 "unknown +word as chat and shows its Game Settings without Cheat Codes; "
              << effects << '\n';
}

void Runtime::check_console_campaign_cheats() {
    require(
        match_session_kind() == oa::data::campaign::SessionKind::campaign &&
            session_cheats_allowed_,
        "the campaign start did not allow cheats"
    );
    oa::World& world = match_->state();
    oa::Game& game = world.game;
    require(!developer(game), "the developer passphrase is already set");
    const console::Console* con = match_console();
    require(
        con != nullptr && console::console_chat_mask(con) ==
                              (console::command_class::option | console::command_class::cheat |
                               console::command_class::private_echo),
        "the campaign chat line does not run options and cheats"
    );
    const auto prefix = speaker_prefix(world);
    const auto viewer = game.viewpoint_player;
    const float metal = game.players[viewer].metal;
    const auto mode = game.chat_mode;
    const auto enter_line = [this](const char* line) { enter_console_check_line(line); };
    enter_line("+atm");
    require(
        game.players[viewer].metal == metal + con->atm_amount,
        "+atm did not add the ATM amount of metal in a campaign"
    );
    require(last_line_is(match_message_lines(), prefix + "+atm"), "+atm was not echoed");
    require(game.chat_mode == mode, "the cheat's echo left its chat mode set");
    const auto clock = console::console_flags(game) & console::console_flag::clock;
    enter_line("+clock");
    require(
        (console::console_flags(game) & console::console_flag::clock) != clock,
        "+clock did not run in a campaign"
    );
    enter_line("+clock");
    const auto effects = check_console_cheat_effects(enter_line, true);
    const auto sung = check_console_sing_command(enter_line);
    require(!developer(game), "a cheat set the developer passphrase");
    std::cout << "console cheat check: a campaign runs +atm and echoes it, runs +clock; " << effects
              << "; +sing " << sung << '\n';
}

std::string Runtime::check_console_cheat_effects(
    const std::function<void(const char*)>& enter_line, bool start_strike
) {
    namespace visibility_flag = console::visibility_flag;
    namespace console_flag = console::console_flag;
    namespace game_audio = oa::audio::game_audio;
    oa::World& world = match_->state();
    oa::Game& game = world.game;
    const uint8_t local = game.local_player_index;
    const console::Console* con = match_console();
    require(
        con != nullptr && (console::console_chat_mask(con) & console::command_class::cheat) != 0,
        "the chat line does not run cheats"
    );
    require(
        game.viewpoint_player == local && match_view_player() == local,
        "the match does not view the local player"
    );
    const auto line = [&](const std::string& text) { enter_line(text.c_str()); };
    const auto flag_on = [&](uint16_t bit) { return (console::console_flags(game) & bit) != 0; };
    const auto sight = [&] { return static_cast<uint8_t>(game.visibility_flags & kSightRules); };
    const auto sight_at_start = sight();
    // The live units of other players than the viewed one that the view
    // does not show.
    const auto hidden_units = [&] {
        std::size_t count = 0;
        const auto viewer = match_view_player();
        for (const auto& slot : match_->world().slots)
            if (slot.unit != nullptr && slot.record.type_index != 0 &&
                slot.record.owner_index != viewer && (slot.record.flags & OA_UNIT_FLAG_LIVE) != 0 &&
                !match_->unit_visible(viewer, slot.unit_index))
                ++count;
        return count;
    };
    // Whether a unit of `owner` answers when selected: only the viewed
    // player's units speak.
    const auto speaks = [&](uint8_t owner) {
        oa::sim::unit_spawn::Slot* speaker = nullptr;
        for (auto& slot : match_->world().slots)
            if (speaker == nullptr && slot.unit != nullptr && slot.record.type_index != 0 &&
                slot.record.owner_index == owner && (slot.record.flags & OA_UNIT_FLAG_LIVE) != 0)
                speaker = &slot;
        require(speaker != nullptr, "player " + std::to_string(owner) + " has no live unit");
        // The queue is emptied first, so that only this request is presented.
        for (std::size_t pump = 0; pump < game_audio::AnnouncementQueue::capacity; ++pump)
            std::ignore = offline_services_.pump_announcements();
        offline_services_.command_sound(
            *speaker, static_cast<uint32_t>(game_audio::UnitAnnouncementCategory::select)
        );
        const auto presented = offline_services_.pump_announcements();
        return std::any_of(presented.begin(), presented.end(), [&](const auto& event) {
            return event.unit_index == speaker->unit_index;
        });
    };
    std::vector<std::string> done;

    // +ATM: the viewed player's metal and energy grow by the console's amount.
    {
        const auto& player = game.players[game.viewpoint_player];
        const float metal = player.metal;
        const float energy = player.energy;
        enter_line("+atm");
        require(
            player.metal == metal + con->atm_amount && player.energy == energy + con->atm_amount,
            "+atm did not add its amount of metal and energy to the viewed player"
        );
        done.push_back("+atm added its amount");
    }

    // +Radar: the minimap shows every unit, in radar contact or not.
    {
        ensure_radar_surfaces();
        compose_radar_final();
        const auto contacts = radar_state_.hot_unit_count;
        std::size_t units = 0;
        for (uint32_t slot = 0; slot < world.unit_slot_count; ++slot)
            units += world.units[slot].type_index != 0 ? 1U : 0U;
        enter_line("+radar");
        require(flag_on(console_flag::full_radar), "+radar did not turn full radar on");
        compose_radar_final();
        const auto everyone = radar_state_.hot_unit_count;
        require(
            everyone == std::min(units, radar_state_.hot_units.size()) && everyone > contacts,
            "+radar did not put every unit on the minimap"
        );
        enter_line("+radar");
        compose_radar_final();
        require(
            !flag_on(console_flag::full_radar) && radar_state_.hot_unit_count == contacts,
            "+radar did not take the units out of contact off the minimap again"
        );
        done.push_back(
            "+radar showed " + std::to_string(everyone) + " units for " + std::to_string(contacts)
        );
    }

    // +View: the match views another player, whose stores +ATM fills and
    // whose units speak; +View back returns to the local player.
    {
        uint8_t other = OA_PLAYER_COUNT;
        for (uint8_t index = 0; index < OA_PLAYER_COUNT && other == OA_PLAYER_COUNT; ++index)
            if (index != local &&
                sim::simulation_state::player_slot_active(index, game.players[index]))
                other = index;
        require(other != OA_PLAYER_COUNT, "no other player is in the game");
        require(speaks(local) && !speaks(other), "the local view does not hear its own units only");
        line("+view " + std::to_string(other));
        require(
            game.viewpoint_player == other && match_view_player() == other,
            "+view did not move the view to the other player"
        );
        // The view now hides what the viewed player cannot see.
        const auto hidden_from_other = hidden_units();
        require(hidden_from_other > 0, "+view did not hide what the viewed player cannot see");
        const float metal = game.players[other].metal;
        enter_line("+atm");
        require(
            game.players[other].metal == metal + con->atm_amount,
            "+atm did not fill the viewed player's stores"
        );
        require(
            speaks(other) && !speaks(local), "+view did not let the viewed player's units speak"
        );
        line("+view " + std::to_string(local));
        require(
            game.viewpoint_player == local && match_view_player() == local && speaks(local),
            "+view did not move the view back"
        );
        done.push_back(
            "+view moved the view to player " + std::to_string(other) + ", which hid " +
            std::to_string(hidden_from_other) + " units, and back"
        );
    }

    // +LOS and +Mapping switch their rule; the view draws by them.
    {
        enter_line("+los");
        const bool los = (sight() & visibility_flag::line_of_sight) != 0;
        require(
            sight() == (sight_at_start ^ visibility_flag::line_of_sight) &&
                match_line_of_sight_on() == los,
            "+los did not switch line of sight"
        );
        if (!los) {
            refresh_radar_mapped();
            require(
                std::all_of(
                    radar_state_.coverage.begin(),
                    radar_state_.coverage.end(),
                    [](uint8_t seen) { return seen != 0; }
                ),
                "with line of sight off the minimap still greys ground out of sight"
            );
        }
        enter_line("+los");
        require(sight() == sight_at_start, "+los did not switch line of sight back");
        enter_line("+mapping");
        const bool mapping = (sight() & visibility_flag::mapping) != 0;
        require(
            sight() == (sight_at_start ^ visibility_flag::mapping) && match_mapping_on() == mapping,
            "+mapping did not switch mapping"
        );
        if (!mapping) {
            const auto bit = static_cast<uint16_t>(1U << local);
            const auto& bits = match_->sight().player_bits;
            require(
                std::all_of(
                    bits.begin(), bits.end(), [bit](uint16_t mapped) { return (mapped & bit) != 0; }
                ),
                "with mapping off the view still has unmapped ground"
            );
        }
        enter_line("+mapping");
        require(sight() == sight_at_start, "+mapping did not switch mapping back");
        done.push_back("+los and +mapping switched their rules");
    }

    // +NowISee: mapping and line of sight off, so every unit shows.
    {
        const auto hidden = hidden_units();
        require(hidden > 0, "every unit of the other players is in sight already");
        enter_line("+nowisee");
        require(
            sight() == (sight_at_start &
                        ~(visibility_flag::mapping | visibility_flag::line_of_sight)) &&
                hidden_units() == 0,
            "+nowisee did not show every unit"
        );
        if ((sight_at_start & visibility_flag::mapping) != 0)
            enter_line("+mapping");
        if ((sight_at_start & visibility_flag::line_of_sight) != 0)
            enter_line("+los");
        require(
            sight() == sight_at_start && hidden_units() == hidden,
            "the sight rules did not come back after +nowisee"
        );
        done.push_back("+nowisee showed " + std::to_string(hidden) + " hidden units");
    }

    // +DoubleShot and +HalfShot: the damage every shot deals.
    {
        const auto rules = match_->rules_view();
        const auto damage = [&] {
            return oa::sim::weapon_execution::projectile_damage(
                kTestShotDamage, 1.0F, nullptr, game, rules
            );
        };
        require(damage() == kTestShotDamage, "a shot's damage is already changed");
        enter_line("+doubleshot");
        require(
            flag_on(console_flag::double_shot) && damage() == 2 * kTestShotDamage,
            "+doubleshot did not double a shot's damage"
        );
        enter_line("+halfshot");
        require(damage() == kTestShotDamage, "+halfshot did not halve the doubled damage");
        enter_line("+doubleshot");
        require(
            !flag_on(console_flag::double_shot) && damage() == kTestShotDamage / 2,
            "+halfshot did not halve a shot's damage"
        );
        enter_line("+halfshot");
        require(
            !flag_on(console_flag::half_shot) && damage() == kTestShotDamage,
            "+halfshot did not turn off"
        );
        done.push_back("+doubleshot and +halfshot doubled and halved a shot");
    }

    // +Meteor 1 and 0 turn storms on and off; +Meteor alone starts a strike,
    // whose first meteor falls with the next tick.
    {
        const bool storms = meteor_enabled();
        enter_line("+meteor 1");
        require(meteor_enabled(), "+meteor 1 did not turn storms on");
        enter_line("+meteor 0");
        require(!meteor_enabled(), "+meteor 0 did not turn storms off");
        if (storms)
            enter_line("+meteor 1");
        std::string strike;
        if (start_strike) {
            const auto shots = match_->projectiles().size();
            enter_line("+meteor");
            ++match_timing_.tick;
            match_->simulation().tick = match_timing_.tick;
            tick_or_raise(*match_);
            require(match_->projectiles().size() > shots, "+meteor did not drop a meteor");
            strike = ", +meteor dropped a meteor";
        }
        done.push_back("+meteor turned storms on and off" + strike);
    }

    // +MakePoster: a picture of the map around the view in the output
    // directory's screenshots folder, here one below the save root.
    {
        const std::string kept_output(
            game.output_directory, strnlen(game.output_directory, sizeof game.output_directory)
        );
        const auto folder = save_game_root() / kPosterFolder;
        std::error_code error;
        fs::remove_all(folder, error);
        std::snprintf(game.output_directory, sizeof game.output_directory, "%s", kPosterFolder);
        enter_line("+makeposter 640 480");
        const auto poster = folder / "screenshots" / "BIGSHOT0001.bmp";
        const bool written = fs::is_regular_file(poster, error) && fs::file_size(poster, error) > 0;
        fs::remove_all(folder, error);
        std::snprintf(
            game.output_directory, sizeof game.output_directory, "%s", kept_output.c_str()
        );
        require(written, "+makeposter wrote no picture");
        done.push_back("+makeposter wrote " + path_to_utf8(poster.filename()));
    }

    require(
        game.viewpoint_player == local && sight() == sight_at_start &&
            !flag_on(console_flag::full_radar) && !flag_on(console_flag::double_shot) &&
            !flag_on(console_flag::half_shot),
        "the cheats did not leave the match as they found it"
    );
    std::string report;
    for (const auto& part : done)
        report += (report.empty() ? "" : ", ") + part;
    return report;
}

} // namespace oa::app
