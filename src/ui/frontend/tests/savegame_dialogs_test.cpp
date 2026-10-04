// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/persist/hapibank.hpp"
#include "oa/ui/frontend/savegame_dialogs.hpp"
#include "test_support.hpp"
#include "oa/test/scratch_directory.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace oa::ui::frontend::test {
namespace {

// A fixture save directory: file name -> summary fields.
struct Summary {
    std::map<std::string, int32_t> ints;
    std::map<std::string, std::string> strings;
    int32_t radar_width = 0; // no radar image while 0
    int32_t radar_height = 0;
    std::vector<uint8_t> radar; // rows of radar_width bytes
};

// A radar image of `width` by `height` whose pixels count up from `first`.
Summary with_radar(Summary summary, int32_t width, int32_t height, uint8_t first) {
    summary.radar_width = width;
    summary.radar_height = height;
    summary.radar.resize(static_cast<std::size_t>(width * height));
    for (std::size_t index = 0; index < summary.radar.size(); ++index)
        summary.radar[index] = static_cast<uint8_t>(first + index);
    return summary;
}

struct Fixture {
    std::vector<std::string> files; // listing order
    std::map<std::string, Summary> saves;
    std::vector<std::string> removed;
    std::vector<std::string> sounds;
    std::vector<std::string> messages;
    std::map<std::string, std::vector<uint8_t>> written;
};

std::string base_name(const char* path) {
    const std::string text(path);
    const auto slash = text.rfind('\\');
    return slash == std::string::npos ? text : text.substr(slash + 1);
}

SaveDialogContext make_context(Fixture& fixture) {
    SaveDialogContext context;
    context.files.context = &fixture;
    context.files.find = [](void* c,
                            const char* pattern,
                            void (*visit)(void*, const data::campaign::FindRecord&),
                            void* user) {
        const std::string text(pattern);
        const auto extension = text.substr(text.rfind('.') + 1);
        for (const auto& name : static_cast<Fixture*>(c)->files) {
            const auto dot = name.rfind('.');
            if (dot != std::string::npos && name.substr(dot + 1) == extension)
                visit(user, {0, 0, 0, name.c_str()});
        }
    };
    context.files.remove = [](void* c, const char* path) {
        auto& fixture = *static_cast<Fixture*>(c);
        const auto name = base_name(path);
        fixture.removed.push_back(path);
        std::erase(fixture.files, name);
        return true;
    };
    context.reader.context = &fixture;
    context.reader.open = [](void* c, const char* path) -> void* {
        auto& saves = static_cast<Fixture*>(c)->saves;
        const auto found = saves.find(base_name(path));
        return found == saves.end() ? nullptr : &found->second;
    };
    context.reader.get_int = [](void*, void* bank, const char* field, int32_t fallback) {
        const auto& ints = static_cast<Summary*>(bank)->ints;
        const auto found = ints.find(field);
        return found == ints.end() ? fallback : found->second;
    };
    context.reader.get_string =
        [](void*, void* bank, const char* field, char* out, std::size_t capacity) {
            const auto& strings = static_cast<Summary*>(bank)->strings;
            const auto found = strings.find(field);
            if (found == strings.end())
                return false;
            std::snprintf(out, capacity, "%s", found->second.c_str());
            return true;
        };
    context.reader.has_field = [](void*, void* bank, const char* field) {
        return static_cast<Summary*>(bank)->ints.count(field) != 0;
    };
    context.reader.load_radar = [](void*, void* bank, present::SurfaceBuffer& picture) {
        const auto& summary = *static_cast<Summary*>(bank);
        if (summary.radar_width == 0)
            return false;
        picture = present::create_surface(summary.radar_width, summary.radar_height);
        picture.pixels = summary.radar;
        picture.surface.pixels = picture.pixels.data();
        return true;
    };
    context.reader.close = [](void*, void*) {};
    context.host.context = &fixture;
    context.host.play_sound = [](void* c, const char* name) {
        static_cast<Fixture*>(c)->sounds.emplace_back(name);
    };
    context.host.show_message = [](void* c, const char* text, int32_t) {
        static_cast<Fixture*>(c)->messages.emplace_back(text);
    };
    return context;
}

Fixture saves_fixture() {
    Fixture fixture;
    fixture.files = {"first.SAV", "broken.SAV", "second.SAV", "notes.TXT"};
    Summary campaign;
    campaign.strings = {
        {"Description", "Core mission 3"}, {"Campaign", "Core"}, {"Mission", "Core3"}
    };
    campaign.ints = {
        {"Players", 2}, {"Gametype", 1}, {"Game Time", 30 * 3725}, {"Side", 1}, {"Difficulty", 2}
    };
    Summary skirmish;
    skirmish.strings = {
        {"Description", "Skirmish on Coast"},
        {"Map", "Coast To Coast"},
        {"Mission", "Coast To Coast"}
    };
    skirmish.ints = {
        {"Players", 4},
        {"Gametype", 2},
        {"Game Time", 30 * 61},
        {"Side", 0},
        {"Difficulty", 0},
        {"CommanderDeath", 0}
    };
    Summary broken; // no Description: dropped from the list
    broken.ints = {{"Gametype", 2}};
    fixture.saves = {
        {"first.SAV", with_radar(campaign, 5, 4, 0x20)},
        {"second.SAV", skirmish},
        {"broken.SAV", broken}
    };
    return fixture;
}

bool loadgame_panel(Panel& panel) {
    const auto layout = load_gui("loadgame.gui");
    if (!layout)
        return false;
    panel_load_layout(panel, *layout);
    return true;
}

OA_TEST(save_list_keeps_described_files) {
    auto fixture = saves_fixture();
    auto context = make_context(fixture);
    OA_CHECK(savegame_build_list(context) == 2);
    OA_CHECK(std::string(context.list.entries[0].file.data()) == "first.SAV");
    OA_CHECK(std::string(context.list.entries[1].file.data()) == "second.SAV");
    OA_CHECK(std::string(context.list.entries[1].description.data()) == "Skirmish on Coast");
    char path[64];
    savegame_entry_path(context, 1, path, sizeof path);
    OA_CHECK(std::string(path) == "SAVEGAME\\second.SAV");
}

OA_TEST(time_format_uses_thirty_ticks) {
    char text[16];
    savegame_format_time(30 * 3725, text, sizeof text);
    OA_CHECK(std::string(text) == "01:02:05");
    savegame_format_time(0, text, sizeof text);
    OA_CHECK(std::string(text) == "00:00:00");
}

OA_GAME_DATA_TEST(load_dialog_preview_and_load) {
    Panel panel;
    if (!loadgame_panel(panel))
        return;
    auto fixture = saves_fixture();
    auto context = make_context(fixture);
    const std::string_view sides[] = {"ARM", "CORE"};
    context.side_names = sides;
    OA_CHECK(savegame_enter_load(panel, context));
    OA_CHECK(context.hold_game);
    OA_CHECK(panel_control(panel, "DELETE")->active == 0);
    OA_CHECK(panel_control(panel, "GAMENAME")->active == 0);
    OA_CHECK(text_of(panel, "GAMENAME") == "Core mission 3");
    OA_CHECK(text_of(panel, "GAMETYPE") == "Single");
    OA_CHECK(text_of(panel, "MISSION") == "Core3");
    OA_CHECK(text_of(panel, "TIME") == "01:02:05");
    OA_CHECK(text_of(panel, "SIDE") == "CORE");
    OA_CHECK(text_of(panel, "DIFF") == "Hard");
    {
        // ai.difficulty-names: the saved difficulty shows the name it carries.
        using Names = data::match_rules::AiDifficultyNamesNames;
        Panel named_panel;
        if (loadgame_panel(named_panel)) {
            auto named = make_context(fixture);
            named.side_names = sides;
            named.difficulty_names.names = {Names::hard, Names::medium, Names::easy};
            OA_CHECK(savegame_enter_load(named_panel, named));
            OA_CHECK(text_of(named_panel, "DIFF") == "Easy");
        }
    }

    panel_control(panel, "GAMES")->list_selection = 1;
    savegame_on_games_selected(panel, context);
    OA_CHECK(text_of(panel, "GAMETYPE") == "Skirmish (4 players)");
    OA_CHECK(text_of(panel, "MISSION") == "Coast To Coast");
    OA_CHECK(text_of(panel, "TIME") == "00:01:01");

    select(panel, "LOAD");
    const auto result = savegame_on_load_click(panel, context);
    OA_CHECK(result.action == SaveDialogAction::load);
    OA_CHECK(result.game_type == 2);
    OA_CHECK(std::string(result.path.data()) == "SAVEGAME\\second.SAV");
    OA_CHECK(fixture.sounds.back() == "SMLBUTTON");

    // A game type other than campaign or skirmish is rejected.
    fixture.saves["second.SAV"].ints["Gametype"] = 3;
    select(panel, "LOAD");
    OA_CHECK(savegame_on_load_click(panel, context).action == SaveDialogAction::invalid);
    OA_CHECK(fixture.messages.back() == "Invalid savegame file");
    select(panel, "CANCEL");
    OA_CHECK(savegame_on_load_click(panel, context).action == SaveDialogAction::cancelled);
}

// The preview shows the radar image saved with the selected game and the
// name of the side it was saved on, as the load and save dialogs both do.
OA_GAME_DATA_TEST(preview_shows_saved_radar_and_side) {
    const std::string_view sides[] = {"Arm", "Core"};
    for (const bool save_role : {false, true}) {
        Panel panel;
        if (!loadgame_panel(panel))
            return;
        auto fixture = saves_fixture();
        auto context = make_context(fixture);
        context.side_names = sides;
        if (save_role)
            savegame_enter_save(panel, context);
        else
            OA_CHECK(savegame_enter_load(panel, context));
        OA_CHECK(text_of(panel, "SIDE") == "Core");
        OA_CHECK(panel_control(panel, "RADAR")->active == 1);
        OA_CHECK(context.radar_picture.surface.width == 5);
        OA_CHECK(context.radar_picture.surface.height == 4);
        OA_CHECK(context.radar_picture.pixels == fixture.saves["first.SAV"].radar);

        // A save without a radar image hides RADAR and drops the picture.
        panel_control(panel, "GAMES")->list_selection = 1;
        savegame_on_games_selected(panel, context);
        OA_CHECK(text_of(panel, "SIDE") == "Arm");
        OA_CHECK(panel_control(panel, "RADAR")->active == 0);
        OA_CHECK(context.radar_picture.pixels.empty());

        panel_control(panel, "GAMES")->list_selection = 0;
        savegame_on_games_selected(panel, context);
        OA_CHECK(panel_control(panel, "RADAR")->active == 1);
        savegame_release_lists(context);
        OA_CHECK(context.radar_picture.pixels.empty() && context.list.entries.empty());
    }
}

OA_GAME_DATA_TEST(load_dialog_without_saves_reports) {
    Panel panel;
    if (!loadgame_panel(panel))
        return;
    Fixture fixture;
    auto context = make_context(fixture);
    OA_CHECK(!savegame_enter_load(panel, context));
    OA_CHECK(fixture.messages.back() == "There are no saved games to choose from");
}

OA_GAME_DATA_TEST(save_dialog_delete_and_write) {
    Panel panel;
    if (!loadgame_panel(panel))
        return;
    auto fixture = saves_fixture();
    auto context = make_context(fixture);
    savegame_enter_save(panel, context);
    OA_CHECK(panel_find(panel, "TITLE") == -1); // LOADGAME.GUI has no TITLE label
    OA_CHECK(panel_control(panel, "DELETE")->active == 1);
    OA_CHECK((panel_control(panel, "GAMENAME")->attributes & 2U) != 0);

    select(panel, "DELETE");
    OA_CHECK(savegame_on_save_click(panel, context).action == SaveDialogAction::refreshed);
    OA_CHECK(fixture.removed.size() == 1 && fixture.removed[0] == "SAVEGAME\\first.SAV");
    OA_CHECK(context.list.entries.size() == 1);
    OA_CHECK(text_of(panel, "GAMENAME") == "Skirmish on Coast");

    panel_set_text(panel, "GAMENAME", "My Battle");
    select(panel, "LOAD");
    const auto result = savegame_on_save_click(panel, context);
    OA_CHECK(result.action == SaveDialogAction::save);
    OA_CHECK(std::string(result.path.data()) == "SAVEGAME\\My Battle.SAV");
}

// The save dialog saves only through OK and Return. It opens with the first
// listed save's name; a press on the name field only gives it the keys, and
// a press on the list, a label or the radar picture does nothing. OK saves
// under the name, CANCEL leaves without a save, and Return at the end of the
// name saves as OK does.
OA_GAME_DATA_TEST(save_dialog_saves_only_through_its_buttons) {
    Panel panel;
    if (!loadgame_panel(panel))
        return;
    auto fixture = saves_fixture();
    auto context = make_context(fixture);
    savegame_enter_save(panel, context);
    OA_CHECK(text_of(panel, "GAMENAME") == context.list.entries[0].description.data());
    const auto press = [&](const char* name) {
        return savegame_on_save_press(panel, context, panel_find(panel, name)).action;
    };
    for (const auto* name : {"GAMENAME", "GAMES", "GAMETYPE", "RADAR"})
        OA_CHECK(press(name) == SaveDialogAction::none);
    OA_CHECK(savegame_on_save_press(panel, context, 0).action == SaveDialogAction::none);
    OA_CHECK(
        savegame_on_save_press(panel, context, panel.count + 1).action == SaveDialogAction::none
    );
    OA_CHECK(fixture.sounds.empty() && fixture.removed.empty());
    OA_CHECK(context.list.entries.size() == 2);

    panel_set_text(panel, "GAMENAME", "Pressed Save");
    const auto saved = savegame_on_save_press(panel, context, panel_find(panel, "LOAD"));
    OA_CHECK(saved.action == SaveDialogAction::save);
    OA_CHECK(std::string(saved.path.data()) == "SAVEGAME\\Pressed Save.SAV");
    OA_CHECK(press("CANCEL") == SaveDialogAction::cancelled);
    OA_CHECK(fixture.sounds.back() == "Previous");
    select(panel, "GAMENAME");
    OA_CHECK(savegame_on_save_click(panel, context).action == SaveDialogAction::save);
    // An empty name saves nothing, whichever way.
    panel_set_text(panel, "GAMENAME", "");
    OA_CHECK(press("LOAD") == SaveDialogAction::none);
}

OA_TEST(load_summary_fields) {
    auto fixture = saves_fixture();
    auto context = make_context(fixture);
    LoadSummary summary;
    auto* bank = context.reader.open(context.reader.context, "SAVEGAME\\second.SAV");
    OA_CHECK(savegame_read_load_summary(context.reader, bank, summary));
    OA_CHECK(summary.game_type == 2 && summary.players == 4);
    OA_CHECK(summary.commander_death == 0 && summary.location == 1);
    OA_CHECK(std::string(summary.mission.data()) == "Coast To Coast");
    bank = context.reader.open(context.reader.context, "SAVEGAME\\broken.SAV");
    OA_CHECK(!savegame_read_load_summary(context.reader, bank, summary));
}

OA_TEST(restrict_list_round_trip) {
    const int32_t ids[] = {0, 101, 202, 303, 404};
    RestrictRow rows[] = {{1, 5}, {2, -1}, {4, 100}, {0, 7}, {3, 0}};
    const auto bytes = restrict_list_encode(ids, rows);
    OA_CHECK(bytes.size() == 4 + 4 * 8);
    OA_CHECK(bytes[0] == 4 && bytes[1] == 0);
    RestrictRow loaded[] = {{1, 0}, {2, 0}, {4, 0}, {0, 0}, {3, 9}};
    restrict_list_apply(bytes, ids, loaded);
    OA_CHECK(loaded[0].limit == 5 && loaded[1].limit == -1 && loaded[2].limit == 100);
    OA_CHECK(loaded[3].limit == 0); // unit 0 is never stored
    OA_CHECK(loaded[4].limit == 0);
    // Truncated input stops at the last complete pair.
    RestrictRow partial[] = {{1, 0}, {2, 0}};
    restrict_list_apply(std::span(bytes).first(4 + 8 + 4), ids, partial);
    OA_CHECK(partial[0].limit == 5 && partial[1].limit == 0);
}

OA_TEST(restrict_limit_labels) {
    char text[16];
    OA_CHECK(restrict_limit_label(100, text, sizeof text) == 100);
    OA_CHECK(std::string(text) == "100");
    OA_CHECK(restrict_limit_label(101, text, sizeof text) == kRestrictNoLimit);
    OA_CHECK(std::string(text) == "No Limit");
}

OA_GAME_DATA_TEST(restrict_list_dialogs) {
    Fixture fixture;
    fixture.files = {"tanks.LST", "air.LST", "first.SAV"};
    auto context = make_context(fixture);
    Panel save;
    const auto layout = load_gui("savelist.gui");
    if (!layout)
        return;
    panel_load_layout(save, *layout);
    restrict_enter_save(save, context);
    OA_CHECK(context.list.entries.size() == 2);
    OA_CHECK(text_of(save, "GAMENAME") == "tanks");
    panel_set_text(save, "GAMENAME", "navy");
    select(save, "GAMENAME");
    const auto result = restrict_on_save_click(save, context);
    OA_CHECK(result.action == SaveDialogAction::save);
    OA_CHECK(std::string(result.path.data()) == "SAVEGAME\\navy.LST");

    Panel load;
    const auto list = load_gui("loadlist.gui");
    if (!list)
        return;
    panel_load_layout(load, *list);
    OA_CHECK(restrict_enter_load(load, context));
    panel_control(load, "GAMES")->list_selection = 1;
    restrict_on_games_selected(load, context);
    OA_CHECK(text_of(load, "GAMENAME") == "air");
    select(load, "LOAD");
    const auto loaded = restrict_on_load_click(load, context);
    OA_CHECK(loaded.action == SaveDialogAction::load);
    OA_CHECK(std::string(loaded.path.data()) == "SAVEGAME\\air.LST");
}

} // namespace
} // namespace oa::ui::frontend::test

namespace oa::ui::frontend::test {
namespace {

// The radar image the real save holds: 7 by 3, its bytes counting up from 0x40.
constexpr uint32_t kRealRadarWidth = 7;
constexpr uint32_t kRealRadarHeight = 3;

// `width` by `height` radar pixels counting up from 0x40.
std::vector<uint8_t>
real_radar_pixels(uint32_t width = kRealRadarWidth, uint32_t height = kRealRadarHeight) {
    std::vector<uint8_t> pixels(static_cast<std::size_t>(width) * height);
    for (std::size_t index = 0; index < pixels.size(); ++index)
        pixels[index] = static_cast<uint8_t>(0x40 + index);
    return pixels;
}

// Writes a Summary account through src/data/persist as the game writes one;
// `radar` adds a `width` by `height` radar image blob, `radar_bytes` then cuts
// it short.
void write_real_save(
    const std::filesystem::path& path,
    bool radar,
    std::size_t radar_bytes = SIZE_MAX,
    uint32_t width = kRealRadarWidth,
    uint32_t height = kRealRadarHeight
) {
    data::persist::Bank bank{};
    data::persist::bank_init(&bank);
    data::persist::bank_reset(&bank);
    data::persist::bank_open_account(&bank, "Summary");
    data::persist::bank_set_text(&bank, "Description", "Arm outpost");
    data::persist::bank_set_int(&bank, "Gametype", 2);
    data::persist::bank_set_int(&bank, "Players", 3);
    data::persist::bank_set_int(&bank, "Game Time", 30 * 90);
    data::persist::bank_set_int(&bank, "Side", 1);
    data::persist::bank_set_text(&bank, "Map", "Lava Run");
    if (radar) {
        const auto pixels = real_radar_pixels(width, height);
        std::vector<uint8_t> blob(8);
        blob[0] = static_cast<uint8_t>(width);
        blob[4] = static_cast<uint8_t>(height);
        blob.insert(blob.end(), pixels.begin(), pixels.end());
        blob.resize(std::min(blob.size(), radar_bytes));
        data::persist::bank_open_blob_name(&bank, "Radar Image");
        data::persist::bank_blob_write(&bank, blob.data(), static_cast<uint32_t>(blob.size()));
    }
    const auto sink = data::persist::stdio_file_sink();
    OA_CHECK(
        data::persist::bank_write_file(
            &bank, path.string().c_str(), data::persist::savegame_description, true, false, &sink
        )
    );
    data::persist::bank_destroy(&bank);
}

// Writes a real HAPIBANK summary with a radar image and a file that is not a
// bank under a fresh SAVEGAME folder.
std::filesystem::path write_real_saves(const char* folder) {
    const auto root = oa::test::make_scratch_directory(folder);
    std::filesystem::create_directories(root / "SAVEGAME");
    write_real_save(root / "SAVEGAME" / "outpost.SAV", true);
    std::ofstream(root / "SAVEGAME" / "junk.SAV") << "not a bank";
    return root;
}

// The real summary read back through the host directory listing and the
// persist-backed reader.
OA_TEST(persist_reader_lists_real_saves) {
    const auto root = write_real_saves("oa-ui-frontend-options-saves");
    SaveDialogContext context;
    const SaveRoots roots{root, root / "SAVEGAME", {}};
    context.files = savegame_host_files(&roots);
    context.reader = savegame_persist_reader(&roots);
    OA_CHECK(savegame_build_list(context) == 1);
    if (context.list.entries.size() == 1)
        OA_CHECK(std::string(context.list.entries[0].description.data()) == "Arm outpost");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

// The persist-backed reader reads the "Radar Image" blob of a save's Summary
// as the image's width, height and rows, and nothing from a save without a
// whole image or with one too small to draw from inside its edges.
OA_TEST(persist_reader_reads_radar_image) {
    const auto root = oa::test::make_scratch_directory("oa-ui-frontend-radar-saves");
    std::filesystem::create_directories(root / "SAVEGAME");
    write_real_save(root / "SAVEGAME" / "radar.SAV", true);
    write_real_save(root / "SAVEGAME" / "plain.SAV", false);
    write_real_save(root / "SAVEGAME" / "short.SAV", true, 8 + kRealRadarWidth * 2);
    write_real_save(root / "SAVEGAME" / "smallest.SAV", true, SIZE_MAX, 2, 2);
    write_real_save(root / "SAVEGAME" / "one-row.SAV", true, SIZE_MAX, 20, 1);
    write_real_save(root / "SAVEGAME" / "one-column.SAV", true, SIZE_MAX, 1, 2);
    write_real_save(root / "SAVEGAME" / "one-pixel.SAV", true, SIZE_MAX, 1, 1);
    write_real_save(root / "SAVEGAME" / "empty.SAV", true, SIZE_MAX, 0, 0);
    write_real_save(root / "SAVEGAME" / "no-rows.SAV", true, SIZE_MAX, 5, 0);
    const SaveRoots roots{root, root / "SAVEGAME", {}};
    const auto reader = savegame_persist_reader(&roots);
    OA_CHECK(reader.load_radar != nullptr);
    const auto read = [&](const char* path, present::SurfaceBuffer& picture) {
        void* bank = reader.open(reader.context, path);
        OA_CHECK(bank != nullptr);
        if (bank == nullptr)
            return false;
        const bool loaded = reader.load_radar(reader.context, bank, picture);
        reader.close(reader.context, bank);
        return loaded;
    };
    present::SurfaceBuffer picture;
    OA_CHECK(read("SAVEGAME\\radar.SAV", picture));
    OA_CHECK(picture.surface.width == static_cast<int32_t>(kRealRadarWidth));
    OA_CHECK(picture.surface.height == static_cast<int32_t>(kRealRadarHeight));
    OA_CHECK(picture.pixels == real_radar_pixels());
    present::SurfaceBuffer none;
    OA_CHECK(!read("SAVEGAME\\plain.SAV", none));
    OA_CHECK(none.pixels.empty());
    present::SurfaceBuffer cut;
    OA_CHECK(!read("SAVEGAME\\short.SAV", cut));
    OA_CHECK(cut.pixels.empty());
    present::SurfaceBuffer smallest;
    OA_CHECK(read("SAVEGAME\\smallest.SAV", smallest));
    OA_CHECK(smallest.surface.width == 2 && smallest.surface.height == 2);
    OA_CHECK(smallest.pixels == real_radar_pixels(2, 2));
    for (const char* path :
         {"SAVEGAME\\one-row.SAV",
          "SAVEGAME\\one-column.SAV",
          "SAVEGAME\\one-pixel.SAV",
          "SAVEGAME\\empty.SAV",
          "SAVEGAME\\no-rows.SAV"}) {
        present::SurfaceBuffer small;
        OA_CHECK(!read(path, small));
        OA_CHECK(small.pixels.empty() && small.surface.width == 0);
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

// The saves folder and two earlier ones: the dialogs list all three, the
// file of the folder listed first winning a name two hold, matched without
// case; a save is read from an earlier folder only while the folders before
// it lack its name, and every file is written to the saves folder.
OA_TEST(host_files_list_the_earlier_folders_too) {
    const auto scratch = oa::test::make_scratch_directory("oa-ui-frontend-earlier-saves");
    const auto earlier = scratch / "SAVEGAME";
    const auto loose = scratch / "Loose";
    const SaveRoots roots{scratch / "root", scratch / "Saves", {earlier, loose}};
    std::filesystem::create_directories(roots.saves);
    std::filesystem::create_directories(earlier);
    std::filesystem::create_directories(loose);
    write_real_save(roots.saves / "outpost.SAV", true);
    write_real_save(earlier / "OUTPOST.sav", false);
    write_real_save(earlier / "older.SAV", false);
    write_real_save(loose / "OLDER.sav", false);
    write_real_save(loose / "loose.SAV", false);
    std::ofstream(earlier / "units.LST") << "list";
    // Paths: the save directory without case, others under the root, an
    // absolute path where it is.
    OA_CHECK(
        savegame_host_path(roots, "SAVEGAME\\outpost.SAV", SavePathUse::read) ==
        roots.saves / "outpost.SAV"
    );
    OA_CHECK(
        savegame_host_path(roots, "savegame\\older.SAV", SavePathUse::read) == earlier / "older.SAV"
    );
    OA_CHECK(
        savegame_host_path(roots, "SAVEGAME\\loose.SAV", SavePathUse::read) == loose / "loose.SAV"
    );
    OA_CHECK(
        savegame_host_path(roots, "SaveGame/older.SAV", SavePathUse::write) ==
        roots.saves / "older.SAV"
    );
    OA_CHECK(
        savegame_host_path(roots, "SAVEGAME\\new.SAV", SavePathUse::read) == roots.saves / "new.SAV"
    );
    OA_CHECK(savegame_host_path(roots, "SAVEGAME", SavePathUse::read) == roots.saves / "");
    OA_CHECK(
        savegame_host_path(roots, "posters\\screenshots", SavePathUse::write) ==
        roots.root / "posters" / "screenshots"
    );
    const auto absolute = scratch / "elsewhere" / "shot.pcx";
    OA_CHECK(savegame_host_path(roots, absolute.string(), SavePathUse::write) == absolute);
    // The listing: the saves folder's outpost, the earlier older and the
    // loose one; the earlier OUTPOST.sav and the loose OLDER.sav are hidden
    // by their names.
    SaveDialogContext context;
    context.files = savegame_host_files(&roots);
    context.reader = savegame_persist_reader(&roots);
    std::vector<std::string> found;
    context.files.find(
        context.files.context,
        "SAVEGAME\\*.SAV",
        [](void* user, const data::campaign::FindRecord& record) {
            static_cast<std::vector<std::string>*>(user)->push_back(record.name);
        },
        &found
    );
    std::sort(found.begin(), found.end());
    OA_CHECK((found == std::vector<std::string>{"loose.SAV", "older.SAV", "outpost.SAV"}));
    std::vector<std::string> lists;
    context.files.find(
        context.files.context,
        "SAVEGAME\\*.LST",
        [](void* user, const data::campaign::FindRecord& record) {
            static_cast<std::vector<std::string>*>(user)->push_back(record.name);
        },
        &lists
    );
    OA_CHECK((lists == std::vector<std::string>{"units.LST"}));
    // The saves read, the earlier ones from where they are; a list is read
    // from the earlier folder and written to the saves folder.
    OA_CHECK(savegame_build_list(context) == 3);
    std::vector<uint8_t> bytes;
    OA_CHECK(context.files.read_file(context.files.context, "SAVEGAME\\units.LST", bytes));
    OA_CHECK(std::string(bytes.begin(), bytes.end()) == "list");
    OA_CHECK(context.files.write_file(context.files.context, "SAVEGAME\\units.LST", bytes));
    OA_CHECK(std::filesystem::exists(roots.saves / "units.LST"));
    // A removal takes the file where it is.
    OA_CHECK(context.files.remove(context.files.context, "SAVEGAME\\older.SAV"));
    OA_CHECK(!std::filesystem::exists(earlier / "older.SAV"));
    std::error_code error;
    std::filesystem::remove_all(scratch, error);
}

// The installed LOADGAME.GUI entered over that listing shows the save.
OA_GAME_DATA_TEST(load_panel_shows_real_saves) {
    const auto root = write_real_saves("oa-ui-frontend-options-saves-data");
    SaveDialogContext context;
    const SaveRoots roots{root, root / "SAVEGAME", {}};
    context.files = savegame_host_files(&roots);
    context.reader = savegame_persist_reader(&roots);
    OA_CHECK(savegame_build_list(context) == 1);
    Panel panel;
    if (auto layout = load_gui("loadgame.gui")) {
        panel_load_layout(panel, *layout);
        const std::string_view sides[] = {"Arm", "Core"};
        context.side_names = sides;
        OA_CHECK(savegame_enter_load(panel, context));
        OA_CHECK(text_of(panel, "GAMETYPE") == "Skirmish (3 players)");
        OA_CHECK(text_of(panel, "MISSION") == "Lava Run");
        OA_CHECK(text_of(panel, "TIME") == "00:01:30");
        OA_CHECK(text_of(panel, "SIDE") == "Core");
        OA_CHECK(panel_control(panel, "RADAR")->active == 1);
        OA_CHECK(context.radar_picture.pixels == real_radar_pixels());
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

} // namespace
} // namespace oa::ui::frontend::test
