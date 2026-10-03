// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Units' names and descriptions in a language, checked through SDL input on
// a skirmish: each build button of the commander's first page, hovered,
// shows its unit's name and costs at NAME and its description at
// DESCRIPTION in the bottom bar, and the commander under the pointer shows
// its name at UNITNAME, each as the unit's file gives it in the language,
// read here from the file itself. Put back to English, the same places
// show the files' own Name and Description, and the pixels change where the
// two differ.

#include "oa/app/runtime.hpp"
#include "oa/data/defs/layout.hpp"
#include "oa/data/languages.hpp"
#include "oa/data/languages/unit_texts.hpp"
#include "oa/formats/tdf.hpp"
#include "oa/sim/unit_spawn/spawn.hpp"
#include "oa/ui/hud/unit_info.hpp"
#include "oa/ui/hud/unit_panel.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {

namespace {

namespace languages = oa::data::languages;

/// Pixels of a text that must change for a text to count as drawn anew.
constexpr std::size_t kChangedTextPixels = 8;
/// Columns right of NAME and DESCRIPTION the bottom bar's lines may reach.
constexpr int kLineReach = 200;

[[noreturn]] void fail(std::string_view what, std::string_view how = {}) {
    throw std::runtime_error(
        "unit language check: " + std::string(what) + (how.empty() ? "" : " ") + std::string(how)
    );
}

/// A unit's name and description in a language, as its file gives them.
struct FileTexts {
    std::string name;
    std::string description;
};

/// Reads a value of a unit file's UNITINFO section as 3.1c reads a text in
/// a language: "<word><key>", which wins when present, then the key, cut to
/// the field that keeps it.
///
/// @param block the section
/// @param word the language's word; empty reads the key alone
/// @param key the key
/// @param field the bytes of the field, its end included
/// @return the value; empty when neither key is there
std::string language_value(
    const oa::formats::tdf::Block* block, std::string_view word, const char* key, std::size_t field
) {
    const char* value = nullptr;
    if (!word.empty())
        value = oa::formats::tdf::find_value(block, (std::string(word) + key).c_str());
    if (value == nullptr)
        value = oa::formats::tdf::find_value(block, key);
    std::string text = value != nullptr ? value : "";
    if (text.size() > field - 1)
        text.resize(field - 1);
    return text;
}

} // namespace

void Runtime::check_unit_language() {
    namespace hud = oa::ui::hud;
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        fail("needs the SDL renderer");
    const auto* expected = languages::find_by_tag(options_.check_unit_language);
    if (expected == nullptr)
        fail("names no language the game knows:", options_.check_unit_language);
    if (shown_language().tag != expected->tag)
        fail("shows " + std::string(shown_language().tag) + ", not", options_.check_unit_language);
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    start_benchmark_skirmish();
    apply_output_mode();
    auto& slots = match_->world().slots;
    uint16_t commander = 0;
    for (const auto& slot : slots)
        if (slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == match_local_player_) {
            commander = slot.unit_index;
            break;
        }
    if (commander == 0)
        fail("found no local commander");

    // The unit's own file, read apart from the game's tables.
    const auto file_texts = [&](std::string_view unit_name, std::string_view word) {
        const auto path =
            std::string(oa::data::defs::directory_name(oa::data::defs::DataDirectory::units)) +
            "/" + std::string(unit_name) + "." + std::string(oa::data::defs::unit_extension());
        const auto bytes = assets_.read(path).bytes;
        oa::formats::tdf::OwnedDocument document;
        if (!document.parse(
                std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())
            ) ||
            !oa::formats::tdf::select_section(document.get(), "UNITINFO"))
            fail("cannot read the unit file", path);
        const auto* block = oa::formats::tdf::cursor(document.get());
        return FileTexts{
            language_value(block, word, "name", sizeof(oa::UnitDef::name)),
            language_value(block, word, "description", sizeof(oa::UnitDef::description))
        };
    };

    bool running = true;
    const auto send_motion = [&](float x, float y) {
        float window_x = 0.0F;
        float window_y = 0.0F;
        if (!SDL_RenderCoordinatesToWindow(sdl_.renderer, x, y, &window_x, &window_y))
            fail(SDL_GetError());
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = SDL_GetWindowID(sdl_.window);
        event.motion.x = window_x;
        event.motion.y = window_y;
        dispatch_event(event, running);
    };
    const auto pixels = [&](const HudRect& area, int left, int right) {
        render_match_surface();
        std::vector<uint8_t> taken;
        const auto width = static_cast<int>(match_hud_cpu_.width);
        const auto height = static_cast<int>(match_hud_cpu_.height);
        for (int y = area.y; y < area.y + std::max(area.height, 9); ++y)
            for (int x = area.x - left; x < area.x + std::max(area.width, 1) + right; ++x) {
                if (x < 0 || y < 0 || x >= width || y >= height)
                    continue;
                const auto* pixel = match_hud_cpu_.rgb.data() +
                                    (static_cast<std::size_t>(y) * match_hud_cpu_.width +
                                     static_cast<std::size_t>(x)) *
                                        3U;
                taken.insert(taken.end(), pixel, pixel + 3);
            }
        return taken;
    };
    const auto changed = [](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
        std::size_t count = 0;
        for (std::size_t index = 0; index + 2 < a.size() && index + 2 < b.size(); index += 3)
            count +=
                a[index] != b[index] || a[index + 1] != b[index + 1] || a[index + 2] != b[index + 2]
                    ? 1
                    : 0;
        return count;
    };
    const auto snapshot = [&](const std::string& name) {
        render_match_surface();
        renderer::Surface frame;
        compose_match_frame(frame);
        write_ppm(report_directory / name, frame);
    };

    // The commander selected, its first build page open.
    adopt_selection(commander);
    selected_match_unit_ = commander;
    apply_match_hud_for_selection();
    if (!match_hud_ || match_build_page_ != 1)
        fail("did not open the commander's first build page");

    struct Button {
        std::string unit_name;
        std::pair<float, float> centre;
    };

    std::vector<Button> buttons;
    for (const auto& gadget : match_hud_->layout.gadgets) {
        if (gadget.common.width <= 0 || gadget.common.height <= 0 || gadget.common.name.empty() ||
            gadget.common.name == hud::kBuildMenuButton)
            continue;
        if (oa::sim::unit_spawn::find_type_index(spawn_type_names_, gadget.common.name) == 0)
            continue;
        const auto point = oa::ui::display_layout::source_to_canvas(
            match_layout_,
            gadget.common.x + gadget.common.width / 2,
            gadget.common.y + gadget.common.height / 2
        );
        buttons.push_back(
            {gadget.common.name, {static_cast<float>(point.x), static_cast<float>(point.y)}}
        );
    }
    if (buttons.empty())
        fail("found no build button on the commander's first page");

    const std::string word(expected->game_name);
    const auto& world = match_->state();
    std::size_t checked = 0;
    std::optional<Button> differing;
    for (const auto& button : buttons) {
        send_motion(button.centre.first, button.centre.second);
        const char* hovered = hovered_gadget_name();
        if (hovered == nullptr || std::string_view(hovered) != button.unit_name)
            fail("the pointer is not over the build button", button.unit_name);
        char line[hud::kPanelLineBytes];
        std::string_view description;
        if (!hud::build_button_readout(world, hovered, line, sizeof line, &description))
            fail("the bottom bar shows nothing for", button.unit_name);
        const auto in_language = file_texts(button.unit_name, word);
        if (std::string_view(line).rfind(in_language.name + "  M:", 0) != 0)
            fail(
                "NAME shows '" + std::string(line) + "' for " + button.unit_name + ", not",
                "'" + in_language.name + "'"
            );
        if (description != in_language.description)
            fail(
                "DESCRIPTION shows '" + std::string(description) + "' for " + button.unit_name +
                    ", not",
                "'" + in_language.description + "'"
            );
        const auto own = file_texts(button.unit_name, {});
        if (!differing &&
            (own.name != in_language.name || own.description != in_language.description))
            differing = button;
        ++checked;
    }
    snapshot("native-unit-language-" + std::string(expected->tag) + "-build-menu.ppm");

    // The commander under the pointer: its name at UNITNAME.
    const auto commander_name = [&] {
        const auto* def = oa::world_unit_def_of(&world, &slots[commander].record);
        if (def == nullptr)
            fail("the commander has no unit type");
        return std::string(languages::unit_display_name(*def));
    };
    const auto commander_type =
        std::string(spawn_type_names_.at(slots[commander].record.type_index));
    const auto commander_in_language = file_texts(commander_type, word);
    if (commander_name() != commander_in_language.name)
        fail(
            "UNITNAME shows '" + commander_name() + "' for the commander, not",
            "'" + commander_in_language.name + "'"
        );

    // Put back to English, the same places show the files' own texts, and
    // NAME and DESCRIPTION change where the two languages' texts differ.
    if (differing) {
        send_motion(differing->centre.first, differing->centre.second);
        const auto name_shown = pixels(side_hud_.name, 0, kLineReach);
        const auto description_shown = pixels(side_hud_.description, 0, kLineReach);
        set_language_choice(languages::english().tag);
        send_motion(differing->centre.first, differing->centre.second);
        char line[hud::kPanelLineBytes];
        std::string_view description;
        if (!hud::build_button_readout(
                world, differing->unit_name.c_str(), line, sizeof line, &description
            ))
            fail("the bottom bar shows nothing in English for", differing->unit_name);
        const auto own = file_texts(differing->unit_name, languages::english().game_name);
        if (std::string_view(line).rfind(own.name + "  M:", 0) != 0 ||
            description != own.description)
            fail("English does not show the unit file's own texts for", differing->unit_name);
        const auto in_language = file_texts(differing->unit_name, word);
        const auto name_english = pixels(side_hud_.name, 0, kLineReach);
        const auto description_english = pixels(side_hud_.description, 0, kLineReach);
        if (own.name != in_language.name && changed(name_shown, name_english) < kChangedTextPixels)
            fail("NAME did not change between the language and English for", differing->unit_name);
        if (own.description != in_language.description &&
            changed(description_shown, description_english) < kChangedTextPixels)
            fail(
                "DESCRIPTION did not change between the language and English for",
                differing->unit_name
            );
        snapshot("native-unit-language-" + std::string(expected->tag) + "-english.ppm");
        // Back in the language, the same pixels again.
        set_language_choice(expected->tag);
        send_motion(differing->centre.first, differing->centre.second);
        if (changed(name_shown, pixels(side_hud_.name, 0, kLineReach)) != 0)
            fail("NAME did not come back in the language for", differing->unit_name);
    } else if (expected != &languages::english()) {
        fail("found no unit on the commander's first page whose file differs in", expected->tag);
    }
    std::cout << "unit language check: " << expected->english_name << ", " << checked
              << " build buttons showed their unit files' names and descriptions in it"
              << (differing ? ", and " + differing->unit_name +
                                  "'s changed back to English and "
                                  "returned"
                            : std::string{})
              << "; the commander is '" << commander_in_language.name << "'\n";
}

} // namespace oa::app
