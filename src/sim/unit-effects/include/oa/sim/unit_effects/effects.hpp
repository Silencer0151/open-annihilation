// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/match_runtime/unit.hpp"
#include <array>
#include <cstdint>

namespace oa::sim::unit_effects {
using Position = std::array<int32_t, 3>;
enum class EventKind : uint8_t {
    thrust_flame,
    wake,
    white_smoke,
    black_smoke,
    sub_bubbles,
    debris_piece,
    explosion_sprite
};

struct Event {
    EventKind kind{};
    uint16_t unit{}, target{};
    uint32_t piece{}, code{}, flags{};
    uint8_t descriptor_flags{};
    Position first{}, second{}, velocity{};
    int32_t lifetime{};
};

class Sink {
  public:

    virtual ~Sink() = default;
    /// Receives one effect event.
    ///
    /// @param event the event
    virtual void effect(const Event& event) = 0;
};

class Host {
  public:

    virtual ~Host() = default;
    /// Tests whether the viewer sees a unit: owner, cloak and sight.
    ///
    /// @param slot unit to test
    /// @return true when visible
    virtual bool visible(const sim::unit_spawn::Slot& slot) = 0;
    /// Rebuilds a unit's piece transforms.
    ///
    /// @param slot unit whose model is refreshed
    virtual void refresh_transform(sim::unit_spawn::Slot& slot) = 0;
    /// Returns a piece's first transformed vertex.
    ///
    /// @param slot unit owning the piece
    /// @param piece script piece index
    /// @return model-space position, 16.16
    virtual Position piece_start(const sim::unit_spawn::Slot& slot, uint32_t piece) = 0;
    /// Returns a piece's second transformed vertex.
    ///
    /// @param slot unit owning the piece
    /// @param piece script piece index
    /// @return model-space position, 16.16
    virtual Position piece_end(const sim::unit_spawn::Slot& slot, uint32_t piece) = 0;
    /// Returns a piece's transformed origin.
    ///
    /// @param slot unit owning the piece
    /// @param piece script piece index
    /// @return model-space position, 16.16
    virtual Position piece_origin(const sim::unit_spawn::Slot& slot, uint32_t piece) = 0;
    /// Returns a piece's position in the world.
    ///
    /// @param slot unit owning the piece
    /// @param piece script piece index
    /// @return world position, 16.16
    virtual Position piece_world(const sim::unit_spawn::Slot& slot, uint32_t piece) = 0;
    /// Returns the sea level (Game.sea_level) as a 16.16 height.
    ///
    /// @return sea level, 16.16
    virtual int32_t sea_level_fixed() = 0;
    /// Draws from the synced random stream.
    ///
    /// @param exclusive_limit upper bound of the draw
    /// @return a value in [0, exclusive_limit)
    virtual uint32_t random_bounded(uint32_t exclusive_limit) = 0;
    /// Shows or hides a piece of a unit's model (its PieceFlag::visible bit).
    ///
    /// @param slot unit owning the piece
    /// @param piece script piece index
    /// @param visible false hides the piece when it breaks off as debris
    virtual void set_piece_visible(sim::unit_spawn::Slot& slot, uint32_t piece, bool visible) = 0;
};

// The COB emit-sfx and explode effects. Attach-unit and drop-unit change the
// simulation's carry links and belong to Match.
class Runtime final {
  public:

    /// Creates the effect runtime over its host and event sink.
    ///
    /// @param host piece geometry, visibility and random stream; must outlive the runtime
    /// @param sink receives the events; must outlive the runtime
    Runtime(Host& host, Sink& sink);
    /// Runs a COB emit-sfx for a piece.
    ///
    /// For a unit the viewer sees, the piece's world position (its origin for 0x100
    /// kinds, else its first two points) becomes a flame (0, 1), wake (2..5), light
    /// or dark puff (0x101, 0x102) or bubble (0x103) event. Other kinds emit nothing.
    ///
    /// @param slot emitting unit
    /// @param piece script piece index
    /// @param kind emit-sfx kind
    /// @quirk The piece's model Z is subtracted from the unit's Z, not added.
    void emit_sfx(sim::unit_spawn::Slot& slot, uint32_t piece, int32_t kind);
    /// Runs a COB explode for a piece.
    ///
    /// Unless bit 0x20 is set, the piece breaks off as debris: hidden, with random
    /// spin rates below 3000, a random fling, a lifetime of 900 ticks and the explode
    /// flags repacked into debris bits. Each of bits 0x100..0x2000 adds an explosion
    /// sprite event at the piece's world position.
    ///
    /// @param slot exploding unit
    /// @param piece script piece index
    /// @param flags COB explode flags
    void explode_piece(sim::unit_spawn::Slot& slot, uint32_t piece, int32_t flags);

  private:

    Host& host_;
    Sink& sink_;
};

} // namespace oa::sim::unit_effects
