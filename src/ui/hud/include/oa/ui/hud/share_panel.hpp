// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// In-game resource share panel (SHARE.GUI).
#pragma once

#include "oa/ui/hud/boundary.hpp"

#include "oa/core/player_setup.h"
#include "oa/core/world.h"
#include "oa/core/player.h"
#include "oa/core/unit.h"

#include <cstddef>
#include <cstdint>

namespace oa::ui::hud {

inline constexpr uint8_t kNoPlayer = 10;
/// Low byte of PlayerSetupInfo.options: a watcher neither gives nor receives
/// shared resources.
inline constexpr uint8_t kSetupWatcher = OA_SETUP_OPTION_WATCHER;

/// Players plus the per-player state the share panel needs.
struct ShareWorld {
    Player* players;               // OA_PLAYER_COUNT records
    UnitEconomy* const* economies; // Player.economy staging blocks, by player index
    const uint8_t* setup_flags;    // low byte of PlayerSetupInfo.options, by player index
    uint8_t local_player;          // Game.local_player_index
    int32_t difficulty;            // Game.difficulty; scales computer income
};

/// How a transfer the local player makes reaches the other players' games.
struct ShareHost {
    void* user;
    void (*send_energy)(void* user, uint8_t from, uint8_t to, float amount);
    void (*send_metal)(void* user, uint8_t from, uint8_t to, float amount);
    /// Hands the giver's transferable units to the recipient.
    void (*give_units)(void* user, uint8_t from, uint8_t to);
    /// Shares the giver's explored map with the recipient.
    void (*share_map)(void* user, uint8_t from, uint8_t to);
};

/// Tells whether a player takes part in the game.
///
/// @param player Player record.
/// @return true for a live record (in use, a local, computer or
///         OA_PLAYER_STATUS_MIRRORED status, a real index) that has units or
///         never built any.
[[nodiscard]] bool player_participating(const Player& player) noexcept;

/// Adds energy income to an economy block, scaled for computer players by difficulty.
///
/// A computer owner gets half the amount on easy and 0.7 of it on medium.
///
/// @param[in,out] economy Economy staging block credited (energy.produced).
/// @param owner Owner of the block; null counts as not a computer.
/// @param amount Energy credited before scaling.
/// @param difficulty Game.difficulty.
void credit_energy(
    UnitEconomy& economy, const Player* owner, float amount, int32_t difficulty
) noexcept;

/// Adds metal income to an economy block, scaled for computer players by difficulty.
///
/// A computer owner gets half the amount on easy and 0.7 of it on medium.
///
/// @param[in,out] economy Economy staging block credited (metal.produced).
/// @param owner Owner of the block; null counts as not a computer.
/// @param amount Metal credited before scaling.
/// @param difficulty Game.difficulty.
void credit_metal(
    UnitEconomy& economy, const Player* owner, float amount, int32_t difficulty
) noexcept;

/// Gives energy from one player to another.
///
/// A local transfer is capped to the giver's store, debits the giver (store
/// and energy.requested of its economy block) and is reported through
/// host.send_energy; a transfer reported by another player's game only
/// credits the recipient. The recipient is credited through credit_energy.
/// A zero amount does nothing.
///
/// @param[in,out] world Players, economy blocks and difficulty.
/// @param from Giving player index; kNoPlayer or above does nothing.
/// @param to Receiving player index; kNoPlayer or above does nothing.
/// @param amount Energy to move.
/// @param local Whether the local player makes the transfer.
/// @param host Reports a local transfer.
void transfer_energy(
    ShareWorld& world, uint8_t from, uint8_t to, float amount, bool local, const ShareHost& host
);
/// Gives metal from one player to another.
///
/// A local transfer is capped to the giver's store, debits the giver (store
/// and metal.requested of its economy block) and is reported through
/// host.send_metal; a transfer reported by another player's game only
/// credits the recipient. The recipient is credited through credit_metal.
/// A zero amount does nothing.
///
/// @param[in,out] world Players, economy blocks and difficulty.
/// @param from Giving player index; kNoPlayer or above does nothing.
/// @param to Receiving player index; kNoPlayer or above does nothing.
/// @param amount Metal to move.
/// @param local Whether the local player makes the transfer.
/// @param host Reports a local transfer.
void transfer_metal(
    ShareWorld& world, uint8_t from, uint8_t to, float amount, bool local, const ShareHost& host
);

/// Recipient list and slider ranges of an opened share panel.
struct SharePanel {
    int32_t recipient_count;
    uint8_t recipients[OA_PLAYER_COUNT];  // player indices, list order
    uint32_t player_ids[OA_PLAYER_COUNT]; // Player.player_id per list entry
    int32_t metal_max;
    int32_t energy_max;
};

/// Fills the recipient list and slider ranges of the share panel for the local player.
///
/// The sliders run up to the local player's whole metal and energy. The
/// recipients are the participating players other than in-use local players
/// and watchers, in player order, each with its Player.player_id.
///
/// @param world Players and watcher flags.
/// @param[out] panel Panel state; reset even when sharing is refused.
/// @param[in,out] frame_flags Game.frame_flags; gains kFrameSharePanelOpen.
/// @return false when the local player may not share (no local player, or a watcher).
bool open_share_panel(const ShareWorld& world, SharePanel& panel, uint16_t& frame_flags) noexcept;

/// Computes the amount a slider selects.
///
/// @param position Slider position in pixels along its track.
/// @param track Track length in pixels; under 2 selects 0.
/// @param maximum Amount at the end of the track.
/// @return maximum * position / (track - 1), truncated.
[[nodiscard]] int32_t slider_amount(int16_t position, int16_t track, int32_t maximum) noexcept;

/// Formats the text of a METAL#/ENERGY# counter.
///
/// @param[out] out Destination buffer.
/// @param size Size of `out` in bytes.
/// @param amount Amount shown, in whole units.
void format_share_amount(char* out, std::size_t size, int32_t amount);

/// What the host does after a share-panel click.
enum class ShareClick : uint8_t {
    none,
    closed,          // panel closed: free the list, panel no longer open
    clear_selection, // clear the clicked control's selection
    toggled,         // MAPINFO/SHARUNIT changed: redraw, then clear the selection
};

/// Handles the share panel's buttons and checkboxes.
///
/// A null name closes the panel (clears kFrameSharePanelOpen). MAPINFO and
/// SHARUNIT play "Options" and report a toggle. OK plays "Options" and, when
/// the chosen recipient is still a live, participating, non-watcher player,
/// gives it the metal and energy (in that order), then the units and the map
/// when those boxes are ticked. CANCEL plays "Previous". Any other control
/// clears its selection.
///
/// @param[in,out] world Players, economy blocks and difficulty.
/// @param panel Recipient list of the open panel.
/// @param name Clicked control name (exact match); null when the panel closes.
/// @param selected PLYRLIST row; out of range gives nothing.
/// @param metal Metal slider amount.
/// @param energy Energy slider amount.
/// @param give_units SHARUNIT checkbox state.
/// @param share_map MAPINFO checkbox state.
/// @param[in,out] frame_flags Game.frame_flags.
/// @param events Receives the button sounds.
/// @param host Reports the transfers and hands over units and map.
/// @return What the host does next.
ShareClick share_panel_click(
    ShareWorld& world,
    const SharePanel& panel,
    const char* name,
    int32_t selected,
    int32_t metal,
    int32_t energy,
    bool give_units,
    bool share_map,
    uint16_t& frame_flags,
    const HudEvents& events,
    const ShareHost& host
);

/// Occupancy (Unit.flags low bits) of an airborne unit.
inline constexpr uint32_t kOccupancyAirborne = 2;

/// Ownership change done by the simulation.
struct UnitTransfer {
    void* user{};
    void (*transfer)(void* user, Unit& unit, Player& recipient){};
};

/// Hands the local player's selected units to another player.
///
/// Commanders, airborne units and units carrying or carried by another are
/// kept. The selection is collected before any transfer, since a transfer
/// may move units.
///
/// @param world World holding the local player and its units.
/// @param recipient Player index receiving the units.
/// @param commander_types Bit mask of commander types, bit (type & 31) of
///                        word (type >> 5); null keeps no type back.
/// @param transfer Performs each ownership change.
void give_selected_units(
    World& world, uint8_t recipient, const uint32_t* commander_types, const UnitTransfer& transfer
);

} // namespace oa::ui::hud
