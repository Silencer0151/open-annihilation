// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Open Annihilation settings in effect and their open dialog
// (Runtime::EngineSettingsState). runtime_engine_settings.cpp reads, applies
// and saves them; the main menu's and the match's hosts open the dialog and
// hand its events back through Runtime::take_engine_settings_action. The
// places a setting takes effect call the static members below.
#pragma once

#include "oa/app/mod_profile_loader.hpp"
#include "oa/app/runtime.hpp"
#include "oa/app/window_icon.hpp"
#include "oa/base/sha256.hpp"
#include "oa/data/mod_profile.hpp"
#include "oa/ui/engine_settings.hpp"
#include "oa/ui/engine_settings/dialog.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace oa::app {

struct Runtime::EngineSettingsState {
    oa::ui::engine_settings::EngineSettings current{}; ///< in effect
    /// The installation's totala.ini, as read; empty when it has none or
    /// --preferences-file names the preferences file.
    std::string installation_ini;
    /// The game runs on a Raspberry Pi, as read once at start.
    bool raspberry_pi{};
    /// The game runs on a light machine (oa::platform::light_machine), as
    /// read once at start.
    bool light_machine{};
    /// The desktop's size, as read once at start; zero by zero when unknown.
    oa::ui::engine_settings::ScreenSize desktop{};
    /// The mod folders Mods lists, as absolute UTF-8 paths, read once at
    /// start: the game folder's mods folder's (list_mod_folders), the
    /// player's own Mods folder's, the folder an earlier Pick Folder...
    /// stored while it is still a folder, and the one played.
    std::vector<std::string> mod_folders;
    /// Their titles, in the same order, as Mods shows them (read_mod_summary).
    std::vector<std::string> mod_names;
    /// Their versions, descriptions and badges, in the same order.
    std::vector<oa::ui::engine_settings::ModDetails> mod_details;
    /// The mod folder the game plays, as the preferences keep one; empty
    /// for none. With --mod-dir or --base-game, the Mod setting as read at
    /// start, which the flags set aside for the run.
    std::string playing_mod_folder;
    /// The Mod setting as stored when the start dropped its folder for
    /// being gone; empty when none was. The dialog shows No Mod in its
    /// place, and the settings' next save stores the choice over it.
    std::string dropped_mod_folder;
    /// The machine's physical memory in bytes, as read once at start; 0 when
    /// the system does not say.
    uint64_t physical_memory{};
    /// The open dialog's requests to try the graphics card afresh
    /// (Dialog::forget_renderer_failures) that the run has acted on.
    uint32_t retries_taken{};
    /// The renderer host whose records the open dialog's retries cleared,
    /// which OK writes and Cancel puts back; it outlives the runtime. Null
    /// when no retry cleared them.
    RendererHost* records_host{};
    std::optional<oa::ui::engine_settings::Dialog> dialog; ///< the open dialog
    /// The section the dialog showed when it last closed; it opens there.
    oa::ui::engine_settings::Page last_page{oa::ui::engine_settings::Page::common_tweaks};
    std::optional<oa::ui::engine_settings::DialogFonts> fonts; ///< loaded on first use
    bool fonts_missing{}; ///< loading the fonts failed; they are not tried again
    /// The icon the header and the OA buttons draw: the window icon's
    /// visible part, decoded on first use.
    std::optional<WindowIcon> icon;
    bool icon_missing{}; ///< decoding the icon failed; it is not tried again
    /// The pointer event being taken comes from a finger: its mouse is
    /// SDL_TOUCH_MOUSEID, as the touch controls' clicks and SDL's own are.
    /// A finger's press takes the nearest control of the dialog within the
    /// touch controls' pick distance.
    bool finger_pointer{};

    /// The battlefield's zoom when the dialog opened, which Cancel eases back to.
    float opened_zoom_target{};
    /// The unit limit of the next game when the dialog opened, which Cancel
    /// puts back; a loaded game may have set it apart from the setting.
    uint16_t opened_run_unit_limit{};
    /// The running match plays at the base path credit: it is shared, a
    /// replay, or was either since it started.
    bool path_credit_held{};

    // Developer Mode: the player's overrides of the standard hacks, laid over
    // the profile the game plays (Runtime::apply_hack_overrides).

    /// The profile's layers are set up (Runtime::load_profile_layers);
    /// before, the profile plays as it ships.
    bool layered{};
    /// What the profile is resolved from again with the overrides: the mod's
    /// profile and the settings it binds, or the plain 3.1c baseline without
    /// a mod. Empty when it cannot be read again, and no override applies.
    std::optional<ProfileSource> profile_source;
    /// The id the overrides are kept under (Inputs::profile_id).
    std::string profile_id;
    /// Every standard hack as the profile resolves it without overrides,
    /// which Developer Mode shows.
    std::vector<oa::data::mod_profile::HackState> profile_hacks;
    /// Developer Mode's open areas and hacks and its filter when the dialog
    /// last closed, which it opens with again.
    std::optional<oa::ui::engine_settings::DeveloperList> last_developer_list;
    /// The profile with the overrides in effect now: the display rules read
    /// it at once. The profile as it ships while none apply; null for a game
    /// without a mod while none apply.
    std::shared_ptr<const oa::data::mod_profile::ModProfile> latest;
    /// The profile the rules are played by (Runtime::mod_profile): a copy
    /// of latest, held from a match's start until the match ends. It is one
    /// object for the whole run, which each change is copied into, so that
    /// what network play and the multiplayer screens bind to it stays valid
    /// and follows it.
    std::shared_ptr<oa::data::mod_profile::ModProfile> played;
    /// The game plays 3.1c's rules and no profile: a game without a mod
    /// whose overrides change none of them.
    bool plays_base_rules{};
    /// The plain 3.1c baseline's sim hash: a game without a mod whose
    /// overrides change only display rules plays by no profile.
    oa::base::sha256::Digest base_sim_hash{};
    /// The overrides the last resolution left out, one line each, as reported.
    std::vector<std::string> refused;

    /// Returns what the defaults depend on in this run.
    ///
    /// @param runtime the runtime
    /// @return the platform, whether the preferences file is the player's
    ///     own, the installation's totala.ini, whether the machine is a
    ///     Raspberry Pi or a light machine, and the desktop's size
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
    /// (runtime_camera.cpp); a camera tracking a unit eases about the unit
    /// and goes on tracking it.
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

    /// Notes whether a pointer event comes from a finger
    /// (finger_pointer); other events leave the note as it is. The
    /// application loop calls it for every event before the screens see it
    /// (Runtime::take_engine_settings_request).
    ///
    /// @param runtime the runtime
    /// @param event the event just received
    static void note_pointer_source(Runtime& runtime, const SDL_Event& event);

    /// Returns how far from a control a finger's press may land and still
    /// take it: the touch controls' pick distance
    /// (oa::ui::touch_hud::gadget_pick_points) on the screen shown, in
    /// pixels of something drawn at a scale.
    ///
    /// @param runtime the runtime
    /// @param canvas_per_pixel canvas pixels a pixel of it is drawn as: 1 for
    ///     the canvas itself, the dialog's scale for its source pixels
    /// @return the distance, in its pixels, rounded; at least 1
    [[nodiscard]] static int32_t finger_reach(const Runtime& runtime, double canvas_per_pixel);

    /// Puts the Include in device backups setting in effect on the game
    /// files at once: the game folder, the import's staging folder and the
    /// demo's unpacked data are kept in the device's backups or out of them
    /// (game_files::apply_backup_setting). Nothing where the platform brings
    /// no game files in (runtime_game_files.cpp).
    ///
    /// @param runtime the runtime
    /// @param backed_up the game files are kept in the device's backups
    static void apply_game_files_backups(Runtime& runtime, bool backed_up);
};

} // namespace oa::app
