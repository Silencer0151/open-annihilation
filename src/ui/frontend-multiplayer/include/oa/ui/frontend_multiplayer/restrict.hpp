// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit build restrictions (RESTRICT2.GUI) opened from the battleroom.
#pragma once

#include "oa/ui/frontend_multiplayer/lobby.hpp"

#include <cstdint>

namespace oa::ui::frontend_multiplayer {

inline constexpr int32_t kRestrictSliders = 12;   // SLIDER0..SLIDER11
inline constexpr int32_t kRestrictNoLimit = 0x65; // slider value shown as "No Limit"
inline constexpr int32_t kRestrictResetLimit = 100;
inline constexpr std::size_t kRestrictTextBytes = 0x52;

// Row flags drawn by DESCLIST/PICLIST.
inline constexpr uint8_t kRestrictRowUnavailable = 0x01;
inline constexpr uint8_t kRestrictRowDisabled = 0x02;

// One restriction row.
struct RestrictEntry {
    char text[kRestrictTextBytes]{}; // "name\rside nM  nE"
    int32_t unit{};                  // unit type index
    int32_t synced_limit{};
    int32_t limit{};     // 0..100, or 0x65 / -1 for no limit
    int32_t available{}; // the host has the unit too
};

// One PICLIST image record: the unit picture a row shows.
struct RestrictPicture {
    uint16_t width{};
    uint16_t height{};
    uint8_t load_state{}; // 9 once a picture loaded
    oa_ref32 image;       // host picture, 0 for none
};

inline constexpr uint16_t kRestrictMissingPictureHeight = 0x20;
inline constexpr uint8_t kRestrictPictureLoaded = 9;

struct RestrictPanel {
    RestrictEntry entries[kMaxSyncUnits];
    int32_t count{};
    int32_t saved[kMaxSyncUnits]{}; // limits when the panel opened
    uint8_t flags[kMaxSyncUnits]{};
    bool host{};
    uint32_t anim_tick{};    // the tick the next row's picture loads after
    int32_t first_visible{}; // DESCLIST's first visible row
    // PICLIST rows visited and the pictures loaded for them.
    int32_t picture_rows{};
    int32_t picture_count{};
    RestrictPicture pictures[kMaxSyncUnits];
};

enum class RestrictAction : uint8_t { none, close, load_list, save_list };

/// Builds the sorted restriction rows and binds the twelve count sliders and SCROLLSLIDER.
///
/// Every restrictable unit gets a row "name\rside nM  nE" with its current
/// limit and availability; Load, Save and Reset are grayed for a client.
/// SCROLLSLIDER shows when the units do not all fit in DESCLIST's 32-pixel
/// rows; its knob is PICLIST's whole rows * its height / units long, in
/// whole pixels, and it has its height less the knob in steps.
///
/// @param[in,out] lobby Lobby state holding the unit table and sync records.
/// @param[in,out] restrict Restriction panel state.
/// @param[in,out] panel RESTRICT2.GUI's panel.
void restrict_open(Lobby& lobby, RestrictPanel& restrict, Panel& panel) noexcept;

/// Stores the limit of the row under a count slider and shows it in COUNTn.
///
/// The top slider value stands for "No Limit" (stored as -1).
///
/// @param[in,out] lobby Lobby state holding the unit table and sync records.
/// @param[in,out] restrict Restriction panel state.
/// @param[in,out] panel RESTRICT2.GUI's panel.
/// @param slider Slider index, 0..11.
void restrict_on_count_slider(
    Lobby& lobby, RestrictPanel& restrict, Panel& panel, int32_t slider
) noexcept;

/// Re-binds SLIDER0..11 to the rows now visible in DESCLIST; sliders are grayed unless hosting and available.
///
/// @param[in,out] lobby Lobby state holding the unit table and sync records.
/// @param[in,out] restrict Restriction panel state.
/// @param[in,out] panel RESTRICT2.GUI's panel.
void restrict_update_sliders(Lobby& lobby, RestrictPanel& restrict, Panel& panel) noexcept;

/// Loads the next row's unit picture (unitpics\<unit>.PCX) into the PICLIST records, one row a call.
///
/// Rows are visited while the row index stays below the unit table's
/// length. A missing picture records the list's width and 32 rows with no
/// image.
///
/// @param[in,out] lobby Lobby state holding the unit table and sync records.
/// @param[in,out] restrict Restriction panel state.
/// @param[in,out] panel RESTRICT2.GUI's panel.
void restrict_load_next_picture(Lobby& lobby, RestrictPanel& restrict, Panel& panel) noexcept;

/// Frees the loaded PICLIST pictures and restarts the picture walk.
///
/// @param lobby Lobby state whose services free the pictures.
/// @param[in,out] restrict Restriction panel state.
void restrict_free_pictures(Lobby& lobby, RestrictPanel& restrict) noexcept;

/// Runs the panel timer: loads a picture every two ticks and applies changed sync records to the rows.
///
/// @param[in,out] lobby Lobby state holding the unit table and sync records.
/// @param[in,out] restrict Restriction panel state.
/// @param[in,out] panel RESTRICT2.GUI's panel.
void restrict_tick(Lobby& lobby, RestrictPanel& restrict, Panel& panel) noexcept;

/// Shows the METALTEXT/ENERGYTEXT costs of the selected row.
///
/// @param lobby Lobby state holding the unit table.
/// @param restrict Restriction panel state.
/// @param[in,out] panel RESTRICT2.GUI's panel.
void restrict_update_totals(Lobby& lobby, RestrictPanel& restrict, Panel& panel) noexcept;

/// Handles a click on RESTRICT2.GUI: OK, Cancel, Reset, Load, Save, the sliders and DESCLIST.
///
/// Reset sets every row to 100, or 0 for units disabled by default; Cancel
/// replays the limits saved at opening in unit-type order. A move of
/// SCROLLSLIDER's knob shows the units from (units - 12) * knob / (range - 1),
/// truncated, and binds the count sliders to them.
///
/// @param[in,out] lobby Lobby state holding the unit table and sync records.
/// @param[in,out] restrict Restriction panel state.
/// @param[in,out] panel RESTRICT2.GUI's panel.
/// @return close for OK and Cancel, load_list or save_list, or none.
RestrictAction restrict_handle_event(Lobby& lobby, RestrictPanel& restrict, Panel& panel) noexcept;

/// Closes the panel: frees the pictures and, when hosting, enables every row's unit unless its limit is 0.
///
/// Rows hold only restrictable units; each change relays its record.
///
/// @param[in,out] lobby Lobby state holding the unit table and sync records.
/// @param[in,out] restrict Restriction panel state.
void restrict_close(Lobby& lobby, RestrictPanel& restrict) noexcept;

/// Compares two rows by their text, used to sort the list.
///
/// @param a First row.
/// @param b Second row.
/// @return strncmp of the row texts.
[[nodiscard]] int restrict_compare(const RestrictEntry& a, const RestrictEntry& b) noexcept;

} // namespace oa::ui::frontend_multiplayer
