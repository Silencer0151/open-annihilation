// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/ui/frontend_state/game_entry.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace oa::ui::frontend_state::map_selection {
using MenuHandle = game_entry::MenuHandle;
inline constexpr uint32_t modal_flags = 0x880, menu_input_flags = 0x40, active_cursor_index = 19;
inline constexpr std::size_t sort_entry_limit = 3000, sort_byte_limit = 96000;
inline constexpr std::string_view no_maps_message = "There are no skirmish maps to choose from";
enum class Button { map_names, load, previous_menu };

/// Returns the gadget name of a SELMAP.GUI button.
///
/// @param button Button.
/// @return Its record name.
constexpr std::string_view resource_name(Button button) noexcept {
    switch (button) {
    case Button::map_names:
        return "MAPNAMES";
    case Button::load:
        return "LOAD";
    case Button::previous_menu:
        return "PREVMENU";
    }
    return {};
}

struct PictureHandle {
    uintptr_t value{};
};

struct LoadedPicture {
    PictureHandle picture;
    int32_t terrain_width{},
        terrain_height{}; // TNT header width/height as the picture loader reports them
};

struct PictureSize {
    int16_t width{}, height{};
};

struct ModalState {
    MenuHandle menu;
    std::vector<std::string> names; // the eligible map names, owned here
    bool active{};
};

// The routines the map-selection modal calls.
class Host {
  public:

    virtual ~Host() = default;

    /// Counts the maps eligible for skirmish.
    ///
    /// @return The count; negative is invalid.
    virtual int32_t map_count() = 0;

    /// Copies the names of the eligible maps.
    ///
    /// @return At least map_count() names.
    virtual std::vector<std::string> copy_map_names() = 0;

    /// Translates interface text.
    ///
    /// @param text Text to translate.
    /// @return The translation, or the text itself.
    virtual std::string translate_ui(std::string_view text) = 0;

    /// Shows a frontend message box.
    ///
    /// @param text Translated message.
    /// @param width Message box width in pixels.
    /// @param show_ok Nonzero shows the OK button.
    /// @param fit_width Nonzero fits the box to its longest line.
    virtual void show_frontend_message(
        std::string_view text, int32_t width, int32_t show_ok, int32_t fit_width
    ) = 0;

    /// Loads a GUI as a modal panel.
    ///
    /// @param resource GUI file name.
    /// @param flags Panel load flags.
    /// @return The panel.
    virtual MenuHandle load_modal(std::string_view resource, uint32_t flags) = 0;

    /// Installs the map modal's event callback; the modal data is owned by ModalState.
    ///
    /// @param menu The modal panel.
    virtual void install_map_event_callback(MenuHandle menu) = 0;

    /// Loads the modal's background bitmap.
    ///
    /// @param name Bitmap name.
    virtual void load_background(std::string_view name) = 0;

    /// Binds the map names to the MAPNAMES list.
    ///
    /// @param names Sorted map names.
    virtual void bind_map_names(std::span<const std::string> names) = 0;

    /// Installs the MAPNAMES selection-change callback.
    virtual void install_map_selection_callback() = 0;

    /// Selects a row of the MAPNAMES list.
    ///
    /// @param index Row, from 0.
    virtual void set_selected_map_index(int16_t index) = 0;

    /// Returns the selected row of the MAPNAMES list.
    ///
    /// @return The row, from 0.
    virtual int16_t selected_map_index() = 0;

    /// Enables or disables panel input.
    ///
    /// @param enabled Nonzero enables.
    virtual void set_input_enabled(int32_t enabled) = 0;

    /// Adds flags to the menu and draws it with them.
    ///
    /// @param flags Panel flags.
    virtual void add_menu_flags(uint32_t flags) = 0;

    /// Selects a cursor animation.
    ///
    /// @param index Cursor animation table index.
    virtual void select_cursor_animation(uint32_t index) = 0;

    /// Reports whether a button was activated by the event.
    ///
    /// @param menu Menu the event came from.
    /// @param button Button to test.
    /// @return Nonzero when it was; the whole word is tested.
    virtual uint32_t button_result(MenuHandle menu, Button button) = 0;

    /// Plays an interface sound.
    ///
    /// @param name ALLSOUND name.
    /// @param argument Second argument, always 0.
    virtual void play_ui_sound(std::string_view name, uint32_t argument) = 0;

    /// Clears the selected record of the menu an event came from.
    ///
    /// @param menu Menu of the event.
    virtual void clear_event_selection(MenuHandle menu) = 0;

    /// Selects a map by name.
    ///
    /// @param name Map name.
    /// @return Nonzero when the map loads.
    virtual int32_t select_map(std::string_view name) = 0;

    /// Writes the chosen map name to the MapName gadget of the panel below.
    ///
    /// @param menu Menu of the event.
    /// @param name Map name.
    virtual void set_parent_map_name(MenuHandle menu, std::string_view name) = 0;

    // Preview. Preview pixels and actual resource loading remain platform and
    // format host operations; no synthetic thumbnail is generated here.

    /// Reports whether the modal has a MAPNAME gadget.
    ///
    /// @return True when it does.
    virtual bool has_map_name_widget() = 0;

    /// Returns the selected map's display name.
    ///
    /// @return The name.
    virtual std::string map_display_name() = 0;

    /// Returns the selected map's memory requirement (its OTA GlobalHeader memory).
    ///
    /// @return The requirement text.
    virtual std::string map_memory_requirement_text() = 0;

    /// Returns the selected map's permitted player counts (its OTA numplayers).
    ///
    /// @return The counts text.
    virtual std::string permitted_player_counts_text() = 0;

    /// Returns the selected map's description, in the language shown when
    /// gamedata\translate.tdf has it in lower case.
    ///
    /// @return The description.
    virtual std::string map_description() = 0;

    /// Returns the selected map's terrain resource path.
    ///
    /// @return The TNT path.
    virtual std::string terrain_resource_path() = 0;

    /// Sets the text of a modal gadget.
    ///
    /// @param widget Gadget name.
    /// @param text New text.
    /// @param max_chars Text-box length; 0 keeps the current one.
    virtual void
    set_modal_text(std::string_view widget, std::string_view text, int32_t max_chars) = 0;

    /// Returns the MAPPIC gadget's picture.
    ///
    /// @return The picture, null for none.
    virtual PictureHandle picture() = 0;

    /// Sets the MAPPIC gadget's picture.
    ///
    /// @param picture Picture; null for none.
    virtual void set_picture(PictureHandle picture) = 0;

    /// Frees a picture.
    ///
    /// @param picture Picture to free.
    virtual void release_picture(PictureHandle picture) = 0;

    /// Loads the minimap picture of a terrain file.
    ///
    /// @param terrain_path TNT path.
    /// @return The picture and the TNT header width and height.
    virtual LoadedPicture load_picture(std::string_view terrain_path) = 0;

    /// Returns the MAPPIC gadget's size, its signed 16-bit width and height.
    ///
    /// @return The size in pixels.
    virtual PictureSize picture_size() = 0;

    /// Fits a picture into the MAPPIC gadget.
    ///
    /// @param picture Picture to fit.
    /// @param width Gadget width in pixels.
    /// @param height Gadget height in pixels.
    /// @param world_width Map width in world units (TNT width << 4).
    /// @param world_height Map height in world units (TNT height << 4).
    virtual void fit_picture(
        PictureHandle picture,
        int32_t width,
        int32_t height,
        int32_t world_width,
        int32_t world_height
    ) = 0;

    /// Marks the menu for a redraw.
    virtual void invalidate_menu() = 0;
};

/// Sorts the map name list by case-insensitive name through the paired list sort.
///
/// No second list or keys are sorted with it.
///
/// @param[in,out] names Map names.
/// @throws std::invalid_argument for more than 3000 names, 96000 bytes, or a
///         name that cannot fit the game's 256-byte map field.
void sort_map_names(std::vector<std::string>& names);

/// Opens the SELMAP.GUI map-selection modal.
///
/// Loads the modal and background, binds the sorted map names, selects the
/// saved map and previews it.
///
/// @param[in,out] modal Modal state; becomes active.
/// @param settings Skirmish settings supplying the saved map name.
/// @param[in,out] h Routines the modal calls.
/// @return False only when no eligible map exists (a message is shown).
/// @throws std::invalid_argument for an invalid map count or a list that
///         changes while opening, and std::logic_error when already active.
bool open(ModalState& modal, const game_entry::SkirmishSettings& settings, Host& h);

/// Fills the preview of the selected map: name, size and players, minimap and description.
///
/// @param[in,out] h Routines the modal calls.
/// @throws std::invalid_argument when the SIZE summary is 100 bytes or longer.
void update_preview(Host& h);

/// Previews the highlighted map without changing the saved map name.
///
/// @param modal Modal state holding the names.
/// @param[in,out] h Routines the modal calls.
/// @throws std::out_of_range when the selected row is outside the name list.
void preview_selection(const ModalState& modal, Host& h);

/// Handles a map-selection modal event.
///
/// Destroy releases the preview and names; MAPNAMES or LOAD commits the
/// highlighted map to the saved map name and the MapName gadget below.
///
/// @param[in,out] modal Modal state.
/// @param[in,out] settings Skirmish settings; map_name changes on commit.
/// @param event Modal event.
/// @param[in,out] h Routines the modal calls.
void handle_event(
    ModalState& modal,
    game_entry::SkirmishSettings& settings,
    const game_entry::Event& event,
    Host& h
);
} // namespace oa::ui::frontend_state::map_selection
