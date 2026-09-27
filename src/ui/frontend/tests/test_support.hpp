// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Minimal registration-based test harness for the frontend options package.
// OA_TEST cases are self-contained; OA_GAME_DATA_TEST cases read the installed
// game and run only with --data, over the store main() opens for them.
#pragma once

#include "oa/formats/hpi.hpp"
#include "oa/data/campaign/campaign_assets.hpp"
#include "oa/test/game_assets.hpp"
#include "oa/ui/frontend/options.hpp"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace oa::ui::frontend::test {

using TestFn = void (*)();

struct TestCase {
    const char* name{};
    TestFn fn{};
    // Reads the installed game; runs only with --data.
    bool game_data{};
};

/// Returns the registered cases, in registration order.
///
/// @return the registry every Register adds to
std::vector<TestCase>& registry();
/// Returns the count of failed checks.
///
/// @return a reference to the count OA_CHECK increments
int& failures();
/// Returns the installed game's store the --data run opened.
///
/// @return a reference to the store pointer: null in the self-contained run
oa::AssetStore*& game_assets();

struct Register {
    /// Registers a case at static initialization.
    ///
    /// @param name the case's name
    /// @param fn the case
    /// @param game_data true for a case that reads the installed game
    Register(const char* name, TestFn fn, bool game_data = false) {
        registry().push_back({name, fn, game_data});
    }
};

#define OA_TEST(name)                                                                              \
    static void name();                                                                            \
    static const ::oa::ui::frontend::test::Register name##_registration(#name, name);              \
    static void name()

#define OA_GAME_DATA_TEST(name)                                                                    \
    static void name();                                                                            \
    static const ::oa::ui::frontend::test::Register name##_registration(#name, name, true);        \
    static void name()

#define OA_CHECK(condition)                                                                        \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++::oa::ui::frontend::test::failures();                                                \
        }                                                                                          \
    } while (false)

/// Reads a GUI file of the installed game.
///
/// A missing store (a self-contained case asking for it) or a missing or
/// malformed file counts as a failed check.
///
/// @param name file name under guis/
/// @return the parsed layout, or nullopt after the failure is counted
inline std::optional<ui::gui_layout::Layout> load_gui(const char* name) {
    const oa::AssetStore* assets = game_assets();
    if (assets == nullptr) {
        std::fprintf(
            stderr,
            "guis/%s needs the installed game: register the case with OA_GAME_DATA_TEST\n",
            name
        );
        ++failures();
        return std::nullopt;
    }
    const auto bytes = oa::test::read_game_file(*assets, std::string("guis/") + name);
    auto parsed = ui::gui_layout::parse(bytes);
    if (bytes.empty() || !parsed.ok()) {
        std::fprintf(stderr, "the installed guis/%s is missing or does not parse\n", name);
        ++failures();
        return std::nullopt;
    }
    return std::move(*parsed.layout);
}

/// Returns the installed game's campaign and map files, read through the store the --data run opened.
///
/// Without that store the failure is counted and the services are empty.
///
/// @return services that live for the whole run
inline const data::campaign::CampaignFiles* game_campaign_files() {
    static data::campaign::CampaignFiles files{};
    if (const oa::AssetStore* assets = game_assets())
        files = data::campaign::campaign_asset_files(*assets);
    else
        ++failures();
    return &files;
}

/// Appends the records of `extra` (without its root) to `panel`, the way a
/// sub-panel is merged over the options tab panel.
///
/// @param[in,out] panel panel that gains the records
/// @param extra layout whose records past the root are appended
inline void merge_layout(Panel& panel, const ui::gui_layout::Layout& extra) {
    Panel sub;
    panel_load_layout(sub, extra);
    for (int32_t index = 1; index <= sub.count; ++index) {
        auto* control = panel_append(panel, sub.controls[static_cast<std::size_t>(index)].type, "");
        if (control != nullptr)
            *control = sub.controls[static_cast<std::size_t>(index)];
    }
}

/// Selects the panel record with a name, as a click on it would.
///
/// @param[in,out] panel panel whose selection changes
/// @param name record name; a missing one leaves no selection
inline void select(Panel& panel, std::string_view name) {
    panel.selected = panel_find(panel, name);
}

/// Returns the text of a named panel record.
///
/// @param panel panel to read
/// @param name record name
/// @return the record's text, or "<missing>" when the panel has no such record
inline std::string text_of(const Panel& panel, std::string_view name) {
    const auto* control = panel_control(panel, name);
    return control == nullptr ? std::string("<missing>") : std::string(control_text(*control));
}

} // namespace oa::ui::frontend::test
