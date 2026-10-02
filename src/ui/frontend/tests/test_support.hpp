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

#include <cctype>
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

/// The installed game's campaign and map files without one mission's OTA and
/// TNT files, as a campaign whose mission files are missing; the mission
/// loader's messages are kept in the order it shows them.
struct MissingMissionFiles {
    const data::campaign::CampaignFiles* installed = game_campaign_files();
    std::string ota; // maps/<stem>.ota, lower case
    std::string tnt; // maps/<stem>.tnt, lower case
    std::vector<std::string> messages;
    data::campaign::CampaignFiles files{};

    /// Hides maps/<stem>.ota and maps/<stem>.tnt.
    ///
    /// @param stem the mission's file name without its extension, in lower case
    explicit MissingMissionFiles(const std::string& stem)
        : ota("maps/" + stem + ".ota"), tnt("maps/" + stem + ".tnt") {
        files = *installed;
        files.context = this;
        files.size = [](void* context, const char* path) {
            const auto* self = static_cast<MissingMissionFiles*>(context);
            return self->hidden(path) ? -1 : self->installed->size(self->installed->context, path);
        };
        files.read = [](void* context, const char* path, char* buffer, uint32_t capacity) {
            const auto* self = static_cast<MissingMissionFiles*>(context);
            return self->hidden(path)
                       ? -1
                       : self->installed->read(self->installed->context, path, buffer, capacity);
        };
        files.list = [](void* context,
                        const char* directory,
                        const char* extension,
                        void (*visit)(void*, const char*),
                        void* visit_context) {
            const auto* self = static_cast<MissingMissionFiles*>(context);
            if (self->installed->list != nullptr)
                self->installed->list(
                    self->installed->context, directory, extension, visit, visit_context
                );
        };
        files.message = [](void* context, const char* text) {
            static_cast<MissingMissionFiles*>(context)->messages.emplace_back(text);
        };
        files.count = [](void* context, const char* pattern) {
            const auto* self = static_cast<MissingMissionFiles*>(context);
            return self->installed->count != nullptr
                       ? self->installed->count(self->installed->context, pattern)
                       : 0;
        };
        files.find = [](void* context,
                        const char* pattern,
                        void (*visit)(void*, const data::campaign::FindRecord&),
                        void* user) {
            const auto* self = static_cast<MissingMissionFiles*>(context);
            if (self->installed->find != nullptr)
                self->installed->find(self->installed->context, pattern, visit, user);
        };
    }

    MissingMissionFiles(const MissingMissionFiles&) = delete;
    MissingMissionFiles& operator=(const MissingMissionFiles&) = delete;

    /// Tests a path against the hidden files, ignoring case and separators.
    ///
    /// @param path path the loader asks for
    /// @return true for the OTA or TNT file this hides
    [[nodiscard]] bool hidden(const char* path) const {
        std::string folded(path);
        for (auto& c : folded)
            c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return folded == ota || folded == tnt;
    }
};

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
