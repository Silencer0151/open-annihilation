// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/match_runtime.hpp"
#include "oa/sim/unit_effects/effects.hpp"
#include <memory>
#include <vector>

namespace oa::sim::unit_effects {
// Required boundaries whose state is not owned by Match yet. Methods
// must implement the named gate or lifecycle step; throwing is preferable to
// an assumed success.
class OfflineLifecycle {
  public:

    virtual ~OfflineLifecycle() = default;
};

// Two-phase adapter: OfflineServices can forward Effects calls immediately;
// bind() installs actual match-owned state before start_player/Create. Script
// effect events spawn into the match's effect world before reaching the sink.
class OfflineEffects final : public sim::match_runtime::Effects, private Host, private Sink {
  public:

    /// Creates an unbound adapter.
    ///
    /// @param lifecycle boundaries not owned by the match yet; must outlive the adapter
    /// @param sink receives every event after it reaches the effect world; must outlive
    ///        the adapter
    OfflineEffects(OfflineLifecycle& lifecycle, Sink& sink);
    /// Binds the adapter to a match, replacing any earlier binding.
    ///
    /// @param match match whose models, random stream and effect world are used; must
    ///        stay alive until unbind
    void bind(sim::match_runtime::Match& match);
    /// Drops the binding; later calls throw until bind is called again.
    void unbind() noexcept;

    /// Returns whether a match is bound.
    [[nodiscard]] bool bound() const noexcept { return runtime_ != nullptr; }

    /// Runs a COB emit-sfx through Runtime::emit_sfx.
    ///
    /// Throws std::logic_error before bind.
    ///
    /// @param slot emitting unit
    /// @param piece script piece index
    /// @param kind emit-sfx kind
    void emit_sfx(sim::unit_spawn::Slot& slot, uint32_t piece, int32_t kind) override;
    /// Runs a COB explode through Runtime::explode_piece.
    ///
    /// Throws std::logic_error before bind.
    ///
    /// @param slot exploding unit
    /// @param piece script piece index
    /// @param flags COB explode flags
    void explode_piece(sim::unit_spawn::Slot& slot, uint32_t piece, int32_t flags) override;
    /// Forwards a COB attach-unit to Match::script_attach_unit.
    ///
    /// Throws std::logic_error before bind.
    ///
    /// @param slot carrying unit
    /// @param target unit to attach
    /// @param piece script piece to attach it to
    /// @param mode COB attach mode
    void
    attach_unit(sim::unit_spawn::Slot& slot, int32_t target, int32_t piece, int32_t mode) override;
    /// Forwards a COB drop-unit to Match::script_drop_unit.
    ///
    /// Throws std::logic_error before bind.
    ///
    /// @param slot carrying unit
    /// @param target unit to drop
    void drop_unit(sim::unit_spawn::Slot& slot, int32_t target) override;

  private:

    sim::match_runtime::Match* match_{};
    [[maybe_unused]] OfflineLifecycle& lifecycle_;
    Sink& sink_;
    std::unique_ptr<Runtime> runtime_;
    std::vector<sim::effect_particles::PiecePrimitive> primitives_; // the shattering piece's object
    std::vector<FixedVec3> points_;                                 // and its transformed vertices
    // The model instance refresh_transform last rebuilt, the tick it did and
    // the root rotation it used. Each piece a dying unit's script blows off
    // asks for the unit's transforms again; an instance none of whose pieces
    // has moved or turned since already holds them.
    const sim::model_runtime::Instance* refreshed_instance_{};
    uint32_t refreshed_tick_{};
    sim::model_runtime::RotationWords refreshed_rotation_{};
    /// Tests the viewpoint player's view of a unit through Match::unit_visible.
    ///
    /// @param slot unit to test
    /// @return true when visible: owner, cloak and the four-corner sight test
    bool visible(const sim::unit_spawn::Slot& slot) override;
    /// Rebuilds a unit's piece transforms from its bank, heading and pitch.
    ///
    /// Throws std::logic_error for a unit without a model instance.
    ///
    /// @param slot unit whose model is refreshed
    /// @quirk Every transform is rebuilt; 3.1c keeps a piece's transform while its angles
    ///        change by less than a small threshold, which is not modelled. A second
    ///        refresh in the same tick of a model no piece of which has moved or turned,
    ///        at the same bank, heading and pitch, keeps the transforms the first built,
    ///        which a rebuild would build again.
    void refresh_transform(sim::unit_spawn::Slot& slot) override;
    /// Returns a piece's first transformed vertex.
    ///
    /// Throws std::logic_error without a model instance and std::out_of_range for a
    /// piece with fewer than two vertices.
    ///
    /// @param slot unit owning the piece
    /// @param piece script piece index
    /// @return model-space position, 16.16
    Position piece_start(const sim::unit_spawn::Slot& slot, uint32_t piece) override;
    /// Returns a piece's second transformed vertex.
    ///
    /// Throws std::logic_error without a model instance and std::out_of_range for a
    /// piece with fewer than two vertices.
    ///
    /// @param slot unit owning the piece
    /// @param piece script piece index
    /// @return model-space position, 16.16
    Position piece_end(const sim::unit_spawn::Slot& slot, uint32_t piece) override;
    /// Returns a piece's transformed origin.
    ///
    /// Throws std::logic_error for a unit without a model instance.
    ///
    /// @param slot unit owning the piece
    /// @param piece script piece index
    /// @return model-space position, 16.16
    Position piece_origin(const sim::unit_spawn::Slot& slot, uint32_t piece) override;
    /// Returns a piece's world position from the model instance.
    ///
    /// Throws std::logic_error for a unit without a model instance.
    ///
    /// @param slot unit owning the piece
    /// @param piece script piece index
    /// @return world position, 16.16
    Position piece_world(const sim::unit_spawn::Slot& slot, uint32_t piece) override;
    /// Returns the match's sea level as a 16.16 height.
    ///
    /// @return sea level, 16.16
    int32_t sea_level_fixed() override;
    /// Draws from the match's synced random stream.
    ///
    /// @param exclusive_limit upper bound of the draw
    /// @return a value in [0, exclusive_limit)
    uint32_t random_bounded(uint32_t exclusive_limit) override;
    /// Sets or clears a piece's visible flag.
    ///
    /// Throws std::logic_error for a unit without a model instance.
    ///
    /// @param slot unit owning the piece
    /// @param piece script piece index
    /// @param visible new visibility
    void set_piece_visible(sim::unit_spawn::Slot& slot, uint32_t piece, bool visible) override;
    /// Spawns an event into the match's effect world, then passes it to the sink.
    ///
    /// Flames go to the thrust layer, wakes to the wake layer, bubbles as unlit wakes
    /// to the thrust layer, puffs to the smoke layer; explosion sprites become records
    /// with the large flash and debris events whole-piece debris or fragments.
    ///
    /// @param event script effect event
    void effect(const Event& event) override;
    /// Turns a debris event into a debris piece at the piece origin plus the unit position.
    ///
    /// A shattering piece goes to shatter_piece instead.
    ///
    /// @param event debris_piece event
    void add_piece_debris(const Event& event);
    /// Breaks a piece into fragments from its transformed vertices and loaded primitives.
    ///
    /// Does nothing when the match has no primitive loader.
    ///
    /// @param slot unit owning the piece
    /// @param request the piece's debris request
    void
    shatter_piece(sim::unit_spawn::Slot& slot, const sim::effect_particles::DebrisPiece& request);
    /// Returns the bound runtime.
    ///
    /// Throws std::logic_error before bind.
    ///
    /// @return the runtime
    Runtime& required();
    /// Returns the bound match.
    ///
    /// Throws std::logic_error before bind.
    ///
    /// @return the match
    sim::match_runtime::Match& bound_match();
};
} // namespace oa::sim::unit_effects
