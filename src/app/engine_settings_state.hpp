// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Open Annihilation settings in effect and their open dialog
// (Runtime::EngineSettingsState). runtime_engine_settings.cpp reads, applies
// and saves them; the main menu's and the match's hosts open the dialog and
// hand its events back through Runtime::take_engine_settings_action. The
// places a setting takes effect call the static members below.
#pragma once

#include "oa/app/runtime.hpp"
#include "oa/ui/engine_settings.hpp"
#include "oa/ui/engine_settings/dialog.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace oa::app {

struct Runtime::EngineSettingsState {
    oa::ui::engine_settings::EngineSettings current{}; ///< in effect
    /// The installation's totala.ini, as read; empty when it has none or
    /// --preferences-file names the preferences file.
    std::string installation_ini;
    std::optional<oa::ui::engine_settings::Dialog> dialog; ///< the open dialog
    /// The section the dialog showed when it last closed; it opens there.
    oa::ui::engine_settings::Page last_page{oa::ui::engine_settings::Page::path_search};
    std::optional<oa::ui::engine_settings::DialogFonts> fonts; ///< loaded on first use
    bool fonts_missing{}; ///< loading the fonts failed; they are not tried again

    /// The battlefield's zoom when the dialog opened, which Cancel eases back to.
    float opened_zoom_target{};
    /// The unit limit of the next game when the dialog opened, which Cancel
    /// puts back; a loaded game may have set it apart from the setting.
    uint16_t opened_run_unit_limit{};
    /// The running match plays at the base path credit: it is shared, a
    /// replay, or was either since it started.
    bool path_credit_held{};

    /// Returns what the defaults depend on in this run.
    ///
    /// @param runtime the runtime
    /// @return the platform, whether the preferences file is the player's
    ///     own, and the installation's totala.ini
    [[nodiscard]] static oa::ui::engine_settings::Inputs inputs(const Runtime& runtime);

    /// Reads the installation's totala.ini, its name matched without regard
    /// to case, at most installation_ini_limit bytes of it.
    ///
    /// @param game_directory the installation's folder
    /// @return the file's text; empty when there is none or it cannot be read
    [[nodiscard]] static std::string read_installation_ini(const fs::path& game_directory);

    /// Tells whether 3.1c's SwitchAlt is on where it is in effect: the
    /// running match's options, else the frontend's preferences.
    ///
    /// @param runtime the runtime
    /// @return true while a number key alone selects its group
    [[nodiscard]] static bool live_switch_alt(const Runtime& runtime);

    /// Takes into the settings in effect what the console changes without
    /// the dialog: SwitchAlt and the frame statistics.
    ///
    /// @param runtime the runtime
    static void take_live_settings(Runtime& runtime);

    /// Writes the preferences file now.
    ///
    /// @param runtime the runtime
    /// @return why the file was not written; nothing when it was
    [[nodiscard]] static std::optional<std::string> flush(Runtime& runtime);

    /// Shows or hides the frame statistics and saves the choice, as +stats
    /// without an argument does.
    ///
    /// @param runtime the runtime
    /// @param shown the statistics show
    static void save_frame_stats(Runtime& runtime, bool shown);

    /// Tells the player a choice was not saved: a message box on the main
    /// menu, a line in the message log in a match.
    ///
    /// @param runtime the runtime
    /// @param reason why the preferences file was not written
    static void report_failed_save(Runtime& runtime, const std::string& reason);

    /// Returns the unit limit the next new game plays at: the run's
    /// (Game.max_units_setting of the frontend), which the setting sets and
    /// a loaded game may change, else the setting.
    ///
    /// @param runtime the runtime
    /// @return units per player
    [[nodiscard]] static uint16_t run_unit_limit(Runtime& runtime);

    /// Sets the run's unit limit to the setting when a frontend clear has
    /// emptied it; the main menu calls it each time it is entered.
    ///
    /// @param runtime the runtime
    static void keep_run_unit_limit(Runtime& runtime);

    /// Builds a skirmish's world from the skirmish roster at a unit limit.
    ///
    /// @param runtime the runtime
    /// @param units_per_player the unit limit
    static void start_skirmish(Runtime& runtime, uint16_t units_per_player);

    /// Returns the unit limit a restart of the running match plays at: the
    /// limit it was started with.
    ///
    /// @param runtime the runtime
    /// @return units per player; the run's limit without a match
    [[nodiscard]] static uint16_t restart_unit_limit(Runtime& runtime);

    /// Gives a match that has just been built its path credit: the setting's
    /// in a game played alone, the base credit in a shared game or a replay.
    ///
    /// @param runtime the runtime; its match is built
    /// @param shared_or_replay the match is known at its start to be shared
    ///     or a replay
    static void start_path_credit(Runtime& runtime, bool shared_or_replay);

    /// Puts the base path credit back once the running match turns out to be
    /// shared or a replay, which an extension may say only after the match
    /// is built. The match keeps it from then on.
    ///
    /// @param runtime the runtime
    /// @param extension_bits the extension's state bits (extension_state)
    static void hold_path_credit(Runtime& runtime, uint32_t extension_bits);

    /// Eases the battlefield's zoom to a scale about the battlefield's centre
    /// (runtime_camera.cpp).
    ///
    /// @param runtime the runtime
    /// @param target the scale to ease to
    static void ease_zoom_about_centre(Runtime& runtime, float target);

    /// Tells whether Escape, with nothing left for it to close, cancel or
    /// clear, opens the in-game menu.
    ///
    /// @param runtime the runtime
    /// @return the Escape opens the game menu setting
    [[nodiscard]] static bool escape_opens_menu(Runtime& runtime);
};

} // namespace oa::app
