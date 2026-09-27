// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/model_runtime/instance.hpp"
#include "oa/sim/script_vm.hpp"

#include <cstdint>

namespace oa::sim::model_runtime {

// Implements only the model-state portion of the COB host boundary. Unit and
// world operations intentionally remain abstract for the match runtime.
class ModelHost : public oa::sim::script_vm::Host {
  public:

    /// Binds the host to a model instance.
    ///
    /// @param instance instance whose pieces the script reads and writes; must outlive the host
    explicit ModelHost(Instance& instance) : instance_(instance) {}

    /// Returns the bound model instance.
    [[nodiscard]] Instance& model_instance() noexcept { return instance_; }

    /// Returns the bound model instance.
    [[nodiscard]] const Instance& model_instance() const noexcept { return instance_; }

    // The unit script host's piece slots. No piece render or draw cache is
    // kept, so the setters change only the piece state.

    /// Returns a piece's translation on one axis (PieceState.translation).
    ///
    /// Throws std::out_of_range for a bad piece or axis.
    ///
    /// @param piece COB piece index
    /// @param axis 0 X, 1 Y, 2 Z
    /// @return signed 16.16 translation
    int32_t piece_position(uint32_t piece, uint32_t axis) const override;
    /// Returns a piece's rotation word on one axis (PieceState.rotation).
    ///
    /// Throws std::out_of_range for a bad piece or axis.
    ///
    /// @param piece COB piece index
    /// @param axis 0 X, 1 Y, 2 Z
    /// @return the angle word zero-extended, 65536 per turn
    int32_t piece_angle(uint32_t piece, uint32_t axis) const override;
    /// Returns piece flag bit 0 (visible).
    ///
    /// @param piece COB piece index
    /// @return 1 when set, else 0
    uint32_t piece_visible(uint32_t piece) const override;
    /// Returns piece flag bit 1 (cached).
    ///
    /// @param piece COB piece index
    /// @return 1 when set, else 0
    uint32_t piece_cached(uint32_t piece) const override;
    /// Returns piece flag bit 2 (shaded).
    ///
    /// @param piece COB piece index
    /// @return 1 when set, else 0
    uint32_t piece_shaded(uint32_t piece) const override;
    /// Sets a piece's translation on one axis.
    ///
    /// A changed value resets the piece's transform marker and marks the model's
    /// transforms dirty.
    ///
    /// @param piece COB piece index
    /// @param axis 0 X, 1 Y, 2 Z
    /// @param value signed 16.16 translation
    void set_piece_position(uint32_t piece, uint32_t axis, int32_t value) override;
    /// Sets a piece's rotation word on one axis.
    ///
    /// A changed word resets the piece's transform marker and marks the model's
    /// transforms dirty.
    ///
    /// @param piece COB piece index
    /// @param axis 0 X, 1 Y, 2 Z
    /// @param value angle; only the low 16 bits are kept
    void set_piece_angle(uint32_t piece, uint32_t axis, uint32_t value) override;
    /// Sets piece flag bit 0 (visible) from bit 0 of the input.
    ///
    /// A change resets the piece's transform marker.
    ///
    /// @param piece COB piece index
    /// @param visible script value; only bit 0 is stored
    /// @quirk The change test compares the whole input with the current bit, so an input of 2 on a hidden piece clears the marker without showing it.
    void set_piece_visible(uint32_t piece, uint32_t visible) override;
    /// Sets piece flag bit 1 (cached) from bit 0 of the input.
    ///
    /// @param piece COB piece index
    /// @param cached script value; only bit 0 is stored
    void set_piece_cached(uint32_t piece, uint32_t cached) override;
    /// Sets piece flag bit 2 (shaded) from bit 0 of the input.
    ///
    /// @param piece COB piece index
    /// @param shaded script value; only bit 0 is stored
    void set_piece_shaded(uint32_t piece, uint32_t shaded) override;

  protected:

    Instance& instance_;
};

} // namespace oa::sim::model_runtime
