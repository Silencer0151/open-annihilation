// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend/main_menu.hpp"
#include "test_support.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace oa::ui::frontend::test {
namespace {

struct MenuCalls {
    std::vector<std::string> music;
    std::vector<int32_t> kinds;
    std::vector<std::string> messages;
    std::vector<int32_t> widths;
    std::string measured;
    int sparks = 0;
    int closed = 0;
    int revisions = 0;
    bool foreign_player = false;
    bool no_driver = false;
};

MainMenuHost make_host(MenuCalls& calls) {
    MainMenuHost host;
    host.context = &calls;
    host.play_music = [](void* c, const char* name) {
        static_cast<MenuCalls*>(c)->music.emplace_back(name);
    };
    host.set_music_kind = [](void* c, int32_t kind) {
        static_cast<MenuCalls*>(c)->kinds.push_back(kind);
    };
    host.measure_text = [](void* c, const char* text) {
        static_cast<MenuCalls*>(c)->measured = text;
        return static_cast<int32_t>(std::string(text).size() * 7 + 1);
    };
    host.reset_sparks = [](void* c) { ++static_cast<MenuCalls*>(c)->sparks; };
    host.foreign_cd_player = [](void* c) { return static_cast<MenuCalls*>(c)->foreign_player; };
    host.close_cd_player = [](void* c) { ++static_cast<MenuCalls*>(c)->closed; };
    host.sound_driver_missing = [](void* c) { return static_cast<MenuCalls*>(c)->no_driver; };
    host.check_revision = [](void* c) { ++static_cast<MenuCalls*>(c)->revisions; };
    host.show_message = [](void* c, const char* text, int32_t width) {
        static_cast<MenuCalls*>(c)->messages.emplace_back(text);
        static_cast<MenuCalls*>(c)->widths.push_back(width);
    };
    return host;
}

OA_GAME_DATA_TEST(main_menu_setup_centres_version) {
    const auto layout = load_gui("mainmenu.gui");
    if (!layout)
        return;
    Panel panel;
    panel_load_layout(panel, *layout);
    const auto* before = panel_control(panel, "DebugString");
    OA_CHECK(before != nullptr);
    if (before == nullptr)
        return;
    const int16_t authored_x = before->x;
    MenuCalls calls;
    MainMenuChecks checks;
    main_menu_setup(panel, checks, make_host(calls));
    const auto* label = panel_control(panel, "DebugString");
    OA_CHECK(label->active == 1);
    OA_CHECK(text_of(panel, "DebugString") == "v3.1v1117a");
    OA_CHECK(calls.measured == "v3.1v1117a");
    // Width 71: the label moves left by 71 / 2 = 35.
    OA_CHECK(label->x == authored_x - 35);
    OA_CHECK(calls.music.size() == 1 && calls.music[0] == "BGM");
    OA_CHECK(calls.kinds.size() == 1 && calls.kinds[0] == 4);
    OA_CHECK(calls.sparks == 1);
    OA_CHECK(panel.dirty);
}

// Game data that names no revision and has no movies, such as the Total
// Annihilation demo (1997): no version label, INTRO grayed and Credits hidden.
OA_GAME_DATA_TEST(main_menu_setup_without_revision_or_movies) {
    const auto layout = load_gui("mainmenu.gui");
    if (!layout)
        return;
    Panel panel;
    panel_load_layout(panel, *layout);
    MenuCalls calls;
    MainMenuChecks checks;
    auto host = make_host(calls);
    host.movies_present = [](void*) { return false; };
    host.revision_named = [](void*) { return false; };
    main_menu_setup(panel, checks, host);
    OA_CHECK(panel_control(panel, "DebugString")->active == 0);
    OA_CHECK(calls.measured.empty());
    OA_CHECK(panel_control(panel, "INTRO")->grayed == 1);
    OA_CHECK(panel_control(panel, "Credits")->active == 0);
    OA_CHECK(panel_control(panel, "SINGLE")->grayed == 0);
}

OA_TEST(main_menu_setup_runs_checks_once) {
    Panel panel;
    MenuCalls calls;
    MainMenuChecks checks;
    const auto host = make_host(calls);
    main_menu_setup(panel, checks, host);
    OA_CHECK(calls.revisions == 1);
    OA_CHECK(checks.revision_checked);
    // No foreign player and a working driver leave both checks armed.
    OA_CHECK(!checks.cd_player_checked);
    OA_CHECK(!checks.sound_driver_checked);
    OA_CHECK(calls.messages.empty());

    calls.foreign_player = true;
    calls.no_driver = true;
    main_menu_setup(panel, checks, host);
    OA_CHECK(calls.closed == 1);
    OA_CHECK(calls.messages.size() == 1 && calls.messages[0] == kNoSoundDriverMessage);
    OA_CHECK(calls.widths.size() == 1 && calls.widths[0] == 500);
    OA_CHECK(calls.revisions == 1);

    main_menu_setup(panel, checks, host);
    OA_CHECK(calls.closed == 1);
    OA_CHECK(calls.messages.size() == 1);
    OA_CHECK(calls.music.size() == 3);
}

} // namespace
} // namespace oa::ui::frontend::test
