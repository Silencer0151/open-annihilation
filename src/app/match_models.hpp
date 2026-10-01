// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The match renderer's own state (Runtime::match_models): prepared models
// and textures, every unit slot's draw state, the 8-bit bridge onto the RGB
// frame, and what the presentation keeps to draw frames between ticks.
// runtime_match_render.cpp draws from it; the director's frame loop and the
// debug grid reach it too.
#pragma once

#include "oa/app/match_model_draws.hpp"
#include "oa/app/runtime.hpp"
#include "oa/core/world.h"
#include "oa/formats/objects3d.hpp"
#include "oa/present/model/model_draw.hpp"
#include "oa/present/model/model_library.hpp"
#include "oa/present/model/rgb_bridge.hpp"
#include "oa/sim/match_runtime.hpp"
#include "presentation_interpolation.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace oa::app {

/// What the presentation keeps to draw a frame between two ticks: every unit
/// slot's, the projectile pool's and the debris table's state at the last two
/// ticks it saw, and where the frame being drawn lies between them.
struct MatchPresentation {
    /// The part of a tick the frame being drawn lies past the tick before;
    /// whole_tick draws the current tick as it is.
    uint32_t fraction{whole_tick};
    uint32_t tick{}; ///< the tick the presentation saw last
    bool seen{};     ///< `tick` holds a tick seen
    /// The ticks from the tick before to `tick` (batch_ticks): more than one
    /// when a frame's clock step ran a batch of them; 1 when there is no tick
    /// before. Particles that step each tick show that many steps' way.
    uint32_t batch{1};
    /// Counts the draws between ticks; a unit's copies note the draw they
    /// were placed for.
    uint64_t draw{};
    /// Set while a unit draws from its copies: a carried unit then draws from
    /// its own copies too (the renderer's model_of).
    bool blending{};
    std::vector<UnitMotion> units; ///< by unit slot
    ShotFlight shots{};
    /// Each live projectile's pose for the frame being drawn, by pool index.
    std::vector<ShotPose> presented_shots;
    DebrisFall debris{};
};

/// 3DO renderer state of one match: prepared models and textures, the draw
/// state of every unit slot, and the 8-bit bridge onto the RGB frame. Each 3D
/// feature keeps its own draw state in its MatchFeatureDraw.
struct MatchModels {
    const void* match{};
    oa::sim::match_runtime::Match* offline{};
    oa::present::model::ModelLibrary library;
    oa::present::model::ModelDisplay display;
    oa::present::model::ModelRenderer renderer;
    oa::present::model::RgbBridge bridge;
    std::vector<UnitDrawState> units; // by unit slot
    // 3D features draw through one zeroed unit record marked as a z-buffered
    // building owned by the viewpoint player, kept here in place of its part
    // of Game.search_context_block.
    oa::Unit feature_unit{};
    uint32_t animation_tick{};
    bool animation_started{};
    uint32_t debris_tick{}; // the tick whose first draw started the debris particles
    bool debris_drawn{};
    bool weapon_names_loaded{};
    std::unordered_map<uint8_t, std::string> weapon_model_names; // by registry index
    std::unordered_map<uint8_t, std::shared_ptr<const oa::formats::objects3d::Model>> weapon_models;
    std::vector<uint8_t> shadow_pixels;
    oa::Sprite projectile_shadow{}; // FX.GAF "shadow" frame 0
    MatchPresentation presentation{};
    DebugGridRandom debug_random{};
};

/// Returns where a unit shows in a frame drawn part of the way through the current tick.
///
/// @param models the match's renderer state, its tick noted
/// @param world the match's World
/// @param slot unit slot
/// @param fraction the part of the way, 0 to whole_tick
/// @return its position that part of the way from the tick before when the
///     fraction is below whole_tick and the unit moved, else Unit.position;
///     a slot outside the pool gives the origin
[[nodiscard]] FixedVec3 shown_unit_position(
    const MatchModels& models, const oa::World& world, uint16_t slot, uint32_t fraction
);

/// Returns where a unit shows in the frame being drawn: shown_unit_position
/// at the presentation's fraction, which is whole_tick outside a draw.
///
/// @param models the match's renderer state, its tick noted
/// @param world the match's World
/// @param slot unit slot
/// @return its position as the frame shows it
[[nodiscard]] FixedVec3
shown_unit_position(const MatchModels& models, const oa::World& world, uint16_t slot);

/// Notes the tick the match is on in the presentation: every unit's pose,
/// the projectile pool and the debris table, as observe_unit, observe_shots
/// and observe_debris note them. Reads the match and writes nothing of it.
///
/// @param[in,out] models the match's renderer state
/// @param match the match
void observe_match_tick(MatchModels& models, oa::sim::match_runtime::Match& match);

} // namespace oa::app
