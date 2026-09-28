// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend/end_mission.hpp"
#include "test_support.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace oa::ui::frontend::test {
namespace {

namespace fs_state = oa::ui::frontend_state;

// Dispatcher host that records the modes, cursors and steps it is given.
class RecordingHost final : public fs_state::Host {
  public:

    std::vector<int32_t> modes;
    std::vector<int32_t> cursors;
    std::vector<int32_t> end_game_steps;
    std::vector<fs_state::Step> steps;
    int32_t cursor_visible = -1; // the last set_cursor_visible value; -1 before any

    void step(fs_state::Step step, fs_state::State&) override { steps.push_back(step); }

    uint32_t query(fs_state::Query, fs_state::State&) override { return 0; }

    void play_movie(fs_state::State&, std::string_view) override {}

    void set_cursor_visible(fs_state::State&, int32_t value) override { cursor_visible = value; }

    void select_map_list(fs_state::State&, int32_t) override {}

    void open_new_game_panel(fs_state::State&, int32_t) override {}

    void set_app_mode(fs_state::State&, int32_t mode) override { modes.push_back(mode); }

    void set_cursor(fs_state::State&, int32_t index) override { cursors.push_back(index); }

    void set_endgame_state(fs_state::State&, int32_t step) override {
        end_game_steps.push_back(step);
    }

    void shut_down(fs_state::State&) override {}
};

struct PanelCalls {
    std::vector<std::string> sounds;
    std::vector<std::string> messages;
    std::vector<int32_t> music_kinds;
    bool disc = true;
    int refreshed = 0;
    int released = 0;
    int left = 0;
};

// The ENDMSN.GUI records every case reads or writes.
constexpr const char* kPanelControls[] = {
    "Start", "LoadGame", "SaveGame", "KNOB", "Missions", "Difficulty", "AdjustDiff", "MainMenu"
};
// Rows end_mission_marks_each_result walks: three marked results and the row after them.
constexpr int32_t kMarkedRows = 4;

struct Fixture {
    oa::data::campaign::CampaignEnv env{game_campaign_files(), nullptr, 0, 0};
    std::unique_ptr<oa::data::campaign::CampaignFile> campaign =
        std::make_unique<oa::data::campaign::CampaignFile>();
    std::unique_ptr<World> world = std::make_unique<World>();
    fs_state::State state{};
    RecordingHost frontend;
    PanelCalls calls;
    Panel panel;
    EndMissionContext context;

    bool load() {
        const auto layout = load_gui("endmsn.gui");
        if (!layout)
            return false;
        panel_load_layout(panel, *layout);
        for (const char* name : kPanelControls) {
            const bool present = panel_control(panel, name) != nullptr;
            OA_CHECK(present);
            if (!present)
                return false;
        }
        oa::data::campaign::campaign_file_init(campaign.get());
        campaign->kind = oa::data::campaign::SessionKind::campaign;
        const bool loaded =
            oa::data::campaign::campaign_load_file(campaign.get(), &env, "Arm Campaign");
        OA_CHECK(loaded);
        if (!loaded)
            return false;
        context.host.context = &calls;
        context.host.play_sound = [](void* c, const char* name) {
            static_cast<PanelCalls*>(c)->sounds.emplace_back(name);
        };
        context.host.disc_present = [](void* c) { return static_cast<PanelCalls*>(c)->disc; };
        context.host.refresh_archives = [](void* c) { ++static_cast<PanelCalls*>(c)->refreshed; };
        context.host.show_message = [](void* c, const char* text, int32_t) {
            static_cast<PanelCalls*>(c)->messages.emplace_back(text);
        };
        context.host.leave_game = [](void* c) { ++static_cast<PanelCalls*>(c)->left; };
        context.host.set_music_kind = [](void* c, int32_t kind) {
            static_cast<PanelCalls*>(c)->music_kinds.push_back(kind);
        };
        context.host.release_outcome_frames = [](void* c) {
            ++static_cast<PanelCalls*>(c)->released;
        };
        context.state = &state;
        context.frontend = &frontend;
        context.campaign = campaign.get();
        context.env = &env;
        context.world = world.get();
        return true;
    }

    ~Fixture() { oa::data::campaign::campaign_file_free(campaign.get()); }
};

std::string first_mission(const EndMissionContext& context) {
    return std::string(context.missions.data());
}

OA_GAME_DATA_TEST(end_mission_after_a_victory) {
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->load())
        return;
    auto& context = fixture->context;
    fixture->world->game.victory = 1;
    fixture->world->game.mission_index = 0;
    fixture->world->game.mission_results[0] = 'W';
    context.difficulty = 2;
    end_mission_enter(fixture->panel, context);
    OA_CHECK(context.continuing);
    OA_CHECK(std::strcmp(context.palette, "outcome1") == 0);
    OA_CHECK(std::string(context.enter_control.data()) == "Start");
    OA_CHECK(context.victory_title);
    OA_CHECK(context.mission_count > 1);
    if (context.mission_count <= 1)
        return;
    const auto first = first_mission(context);
    OA_CHECK(first.size() > 2 && static_cast<uint8_t>(first[0]) == 0xfe && first[1] == ' ');
    // Second row: no result recorded yet, so no marker.
    const char* second = context.missions.data() + first.size() + 1;
    OA_CHECK(static_cast<uint8_t>(second[0]) < 0xfd);
    // KNOB height 76, knob size 10: range 76 - 10 - 3.
    OA_CHECK(panel_control(fixture->panel, "KNOB")->slider.range == 63);
    OA_CHECK(panel_control(fixture->panel, "Missions")->list_selection == 1);
    OA_CHECK(panel_stage(fixture->panel, "Difficulty") == 2);
    OA_CHECK(fixture->campaign->mission_index == 0);
    OA_CHECK(fixture->frontend.cursors.size() == 1 && fixture->frontend.cursors[0] == 0x13);
    OA_CHECK(panel_control(fixture->panel, "LoadGame")->grayed == 0);
    OA_CHECK(panel_control(fixture->panel, "SaveGame")->grayed == 0);

    // Game data without the save and load dialog grays both buttons.
    context.saved_games_offered = false;
    end_mission_enter(fixture->panel, context);
    OA_CHECK(panel_control(fixture->panel, "LoadGame")->grayed == 1);
    OA_CHECK(panel_control(fixture->panel, "SaveGame")->grayed == 1);
    context.saved_games_offered = true;

    // A watcher sees the defeat title.
    auto& game = fixture->world->game;
    game.players[0].in_use = 1;
    game.players[0].info = oa_ref_from_index(0);
    fixture->world->player_info[0].options = OA_SETUP_OPTION_WATCHER;
    end_mission_enter(fixture->panel, context);
    OA_CHECK(context.continuing && !context.victory_title);

    // Winning the last mission ends the campaign.
    game.mission_index = context.mission_count - 1;
    end_mission_enter(fixture->panel, context);
    OA_CHECK(!context.continuing);
    OA_CHECK(std::strcmp(context.palette, "outcome0") == 0);
    OA_CHECK(std::string(context.focus.data()) == "MainMenu" && context.enter_control[0] == '\0');
}

OA_GAME_DATA_TEST(end_mission_marks_each_result) {
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->load())
        return;
    auto& context = fixture->context;
    std::memcpy(fixture->world->game.mission_results, "WLUX", 4);
    end_mission_enter(fixture->panel, context);
    OA_CHECK(context.mission_count >= kMarkedRows);
    if (context.mission_count < kMarkedRows)
        return;
    const char* row = context.missions.data();
    const uint8_t expected[] = {0xfe, 0xff, 0xfd};
    for (const uint8_t marker : expected) {
        OA_CHECK(static_cast<uint8_t>(row[0]) == marker && row[1] == ' ');
        row += std::strlen(row) + 1;
    }
    OA_CHECK(static_cast<uint8_t>(row[0]) < 0xfd);
}

OA_GAME_DATA_TEST(end_mission_after_a_defeat_and_outside_campaigns) {
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->load())
        return;
    auto& context = fixture->context;
    fixture->world->game.victory = 0;
    fixture->world->game.mission_index = 3;
    fixture->world->game.mission_results[3] = 'L';
    end_mission_enter(fixture->panel, context);
    OA_CHECK(context.continuing);
    OA_CHECK(!context.victory_title);
    OA_CHECK(panel_control(fixture->panel, "Missions")->list_selection == 3);
    // A lost mission can be replayed: Start stays enabled.
    auto layout = std::make_unique<campaign::ScoreLayout>();
    end_mission_open(fixture->panel, context, *layout);
    OA_CHECK(panel_control(fixture->panel, "Start")->active == 1);
    OA_CHECK(panel_control(fixture->panel, "Missions")->list_selection == 3);

    fixture->campaign->kind = oa::data::campaign::SessionKind::skirmish;
    context.service_launch = true;
    std::strcpy(context.service_label.data(), "Portal");
    end_mission_enter(fixture->panel, context);
    OA_CHECK(!context.continuing);
    OA_CHECK(std::strcmp(context.palette, "outcome0") == 0);
    OA_CHECK(std::string(context.focus.data()) == "MainMenu");
    OA_CHECK(text_of(fixture->panel, "MainMenu") == "Portal");
    std::strcpy(context.service_label.data(), "PortalGate");
    end_mission_enter(fixture->panel, context);
    OA_CHECK(text_of(fixture->panel, "MainMenu") == "OK");
    // A game with the online flag shows the label as well.
    context.service_launch = false;
    std::strcpy(context.service_label.data(), "BY");
    fixture->world->game.gui_flags = 0x10;
    end_mission_enter(fixture->panel, context);
    OA_CHECK(text_of(fixture->panel, "MainMenu") == "BY");
}

// ENDMSN.GUI places MainMenu at 460,395, 120 by 20: the MainMenu slot of
// Outcome1, the background of a continuing campaign. A game that cannot
// continue moves it down to y 416, into Outcome0's single button housing.
OA_GAME_DATA_TEST(end_mission_places_main_menu_on_its_background) {
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->load())
        return;
    auto& panel = fixture->panel;
    const Control& main_menu = *panel_control(panel, "MainMenu");
    const auto placed_at = [&main_menu](int16_t y) {
        return main_menu.x == 460 && main_menu.y == y && main_menu.width == 120 &&
               main_menu.height == 20;
    };
    OA_CHECK(main_menu.type == ControlType::button);
    OA_CHECK(placed_at(395));

    auto& context = fixture->context;
    auto layout = std::make_unique<campaign::ScoreLayout>();
    fixture->world->game.victory = 0;
    end_mission_open(panel, context, *layout);
    OA_CHECK(context.continuing && std::strcmp(context.palette, "outcome1") == 0);
    OA_CHECK(placed_at(395));

    fixture->campaign->kind = oa::data::campaign::SessionKind::skirmish;
    end_mission_open(panel, context, *layout);
    OA_CHECK(!context.continuing && std::strcmp(context.palette, "outcome0") == 0);
    OA_CHECK(main_menu.active == 1);
    OA_CHECK(placed_at(416));
}

OA_GAME_DATA_TEST(end_mission_clicks) {
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->load())
        return;
    auto& context = fixture->context;
    auto& panel = fixture->panel;
    fixture->world->game.victory = 1;
    end_mission_enter(panel, context);

    context.difficulty = 2;
    select(panel, "Difficulty");
    OA_CHECK(end_mission_on_click(panel, context) == EndMissionAction::none);
    OA_CHECK(context.difficulty == 0 && context.skirmish_difficulty == 0);
    OA_CHECK(fixture->calls.sounds.back() == "SKirmish");
    OA_CHECK(panel.selected == kNoSelection);

    select(panel, "LoadGame");
    OA_CHECK(end_mission_on_click(panel, context) == EndMissionAction::open_load_game);
    OA_CHECK(fixture->calls.sounds.back() == "BigButton");

    fixture->calls.disc = false;
    panel_control(panel, "Missions")->list_selection = 1;
    select(panel, "Start");
    OA_CHECK(end_mission_on_click(panel, context) == EndMissionAction::start_mission);
    OA_CHECK(
        fixture->calls.messages.size() == 1 &&
        fixture->calls.messages[0] == "Please insert the Campaign CD (Disc 2) and try again"
    );
    OA_CHECK(fixture->calls.refreshed == 1);
    OA_CHECK(fixture->campaign->mission_index == 1);
    OA_CHECK(fixture->state.state == fs_state::state_id::briefing_to_end_mission);
    OA_CHECK((fixture->state.session_flags & fs_state::flags::single_player) != 0);
    OA_CHECK((fixture->state.session_flags & fs_state::flags::live_game) == 0);
    OA_CHECK(fixture->frontend.modes.back() == 2);
    OA_CHECK(fixture->frontend.cursor_visible == 1);

    select(panel, "MainMenu");
    OA_CHECK(end_mission_on_click(panel, context) == EndMissionAction::main_menu);
    OA_CHECK(fixture->state.state == fs_state::state_id::main_menu);
    OA_CHECK(fixture->frontend.modes.back() == 1);
    OA_CHECK(fixture->frontend.cursors.back() == 0x14);

    fixture->world->game.gui_flags = 0x10;
    panel.selected = kNoSelection;
    OA_CHECK(end_mission_on_click(panel, context) == EndMissionAction::closed);
    OA_CHECK(fixture->calls.released == 1 && fixture->calls.left == 1);
    OA_CHECK(fixture->calls.music_kinds.size() == 1 && fixture->calls.music_kinds[0] == 4);
    OA_CHECK(context.mission_count == 0);
}

// After a launcher started the game, MAIN MENU leaves for the main menu as
// it always does but leaves the pointer's picture as it is.
OA_GAME_DATA_TEST(end_mission_main_menu_after_a_launch) {
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->load())
        return;
    auto& context = fixture->context;
    auto& panel = fixture->panel;
    fixture->world->game.victory = 1;
    context.service_launch = true;
    std::strcpy(context.service_label.data(), "Launcher");
    end_mission_enter(panel, context);
    const auto cursors = fixture->frontend.cursors.size();
    fixture->frontend.cursor_visible = -1;
    select(panel, "MainMenu");
    OA_CHECK(end_mission_on_click(panel, context) == EndMissionAction::main_menu);
    OA_CHECK(fixture->calls.sounds.back() == "BigButton");
    OA_CHECK(fixture->state.state == fs_state::state_id::main_menu);
    OA_CHECK(!fixture->frontend.modes.empty() && fixture->frontend.modes.back() == 1);
    OA_CHECK(fixture->frontend.cursor_visible == 1);
    OA_CHECK(fixture->frontend.cursors.size() == cursors);

    // Without the launch the same press selects the leaving cursor.
    context.service_launch = false;
    end_mission_enter(panel, context);
    const auto before = fixture->frontend.cursors.size();
    select(panel, "MainMenu");
    OA_CHECK(end_mission_on_click(panel, context) == EndMissionAction::main_menu);
    OA_CHECK(fixture->frontend.cursors.size() == before + 1);
    OA_CHECK(!fixture->frontend.cursors.empty() && fixture->frontend.cursors.back() == 0x14);
}

OA_GAME_DATA_TEST(end_mission_state_opens_the_panel) {
    auto fixture = std::make_unique<Fixture>();
    if (!fixture->load())
        return;
    auto& context = fixture->context;
    fixture->world->game.victory = 1;
    std::strcpy(fixture->world->game.scores[0].name, "Commander");
    fixture->world->game.score_maxima[campaign::score_kills] = 10;
    fixture->world->game.endgame_column = 7;
    auto layout = std::make_unique<campaign::ScoreLayout>();
    end_mission_open(fixture->panel, context, *layout);
    OA_CHECK(layout->row_count == 1 && fixture->world->game.endgame_column == 0);
    OA_CHECK(!layout->rows[0].bars[campaign::score_kills].active);
    for (const char* name : kPanelControls)
        OA_CHECK(panel_control(fixture->panel, name)->active == 1);
    OA_CHECK(fixture->frontend.modes.back() == 7);
    OA_CHECK(
        fixture->frontend.end_game_steps.size() == 1 &&
        fixture->frontend.end_game_steps[0] == OA_ENDGAME_STAT_BARS
    );
}

} // namespace
} // namespace oa::ui::frontend::test
