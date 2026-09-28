// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_effects/effects_offline.hpp"
#include <bit>
#include <stdexcept>

namespace oa::sim::unit_effects {
namespace {
constexpr int32_t emit_flame_period = 1;  // emit-sfx flames
constexpr int32_t emit_bubble_period = 8; // emit-sfx bubbles

Position position(const formats::objects3d::FixedVector3& p) {
    return {p.x, p.y, p.z};
}

FixedVec3 fixed(const Position& p) noexcept {
    return {p[0], p[1], p[2]};
}
} // namespace

OfflineEffects::OfflineEffects(OfflineLifecycle& l, Sink& s) : lifecycle_(l), sink_(s) {
}

void OfflineEffects::bind(sim::match_runtime::Match& match) {
    unbind();
    match_ = &match;
    runtime_ = std::make_unique<Runtime>(static_cast<Host&>(*this), static_cast<Sink&>(*this));
}

void OfflineEffects::unbind() noexcept {
    runtime_.reset();
    match_ = nullptr;
}

void OfflineEffects::effect(const Event& e) {
    auto& world = match_->effects();
    const auto& game = match_->state().game;
    const auto host = match_->effect_host();
    switch (e.kind) {
    case EventKind::thrust_flame:
        sim::effect_particles::spawn_flame(
            world,
            game,
            host,
            fixed(e.first),
            fixed(e.second),
            emit_flame_period,
            static_cast<int32_t>(e.flags),
            sim::effect_particles::layer_thrust
        );
        break;
    case EventKind::wake:
        sim::effect_particles::spawn_wake_lit(
            world,
            game,
            host,
            fixed(e.first),
            fixed(e.second),
            static_cast<int32_t>(e.flags),
            sim::effect_particles::layer_wake
        );
        break;
    case EventKind::sub_bubbles:
        sim::effect_particles::spawn_wake_unlit(
            world,
            game,
            host,
            fixed(e.first),
            fixed(e.second),
            emit_bubble_period,
            sim::effect_particles::layer_thrust
        );
        break;
    case EventKind::white_smoke:
        sim::effect_particles::spawn_white_smoke(
            world, game, host, fixed(e.first), sim::effect_particles::layer_smoke
        );
        break;
    case EventKind::black_smoke:
        sim::effect_particles::spawn_black_smoke(
            world, game, host, fixed(e.first), sim::effect_particles::layer_smoke
        );
        break;
    case EventKind::explosion_sprite:
        sim::effect_particles::log_cob_explosions(world, game, host, fixed(e.first), e.flags);
        break;
    case EventKind::debris_piece:
        add_piece_debris(e);
        break;
    }
    sink_.effect(e);
}

void OfflineEffects::add_piece_debris(const Event& e) {
    auto& slot = match_->world().slots[e.unit];
    refresh_transform(slot);
    const auto origin = piece_origin(slot, e.piece);
    const auto& piece = match_->instance(e.unit)->model().piece_for_script_index(e.piece);
    sim::effect_particles::DebrisPiece debris{};
    debris.unit = e.unit;
    debris.piece = e.piece;
    debris.spin_rate[0] = e.first[0];
    debris.spin_rate[1] = e.first[1];
    debris.spin_rate[2] = e.first[2];
    debris.velocity = fixed(e.velocity);
    debris.lifetime = e.lifetime;
    debris.flags = e.descriptor_flags;
    debris.shatter = (e.descriptor_flags & sim::effect_particles::debris_shatter) != 0 ? 1 : 0;
    // The spin starts from the piece's yz, xz and xy rotation words.
    debris.spin[0] = piece.rotation.yz;
    debris.spin[1] = piece.rotation.xz;
    debris.spin[2] = piece.rotation.xy;
    debris.model = &match_->instance(e.unit)->model().model();
    debris.object = piece.object_index;
    const auto& unit = *slot.unit;
    debris.position = {
        std::bit_cast<int32_t>(std::bit_cast<uint32_t>(origin[0]) + unit.position[0]),
        std::bit_cast<int32_t>(std::bit_cast<uint32_t>(origin[1]) + unit.position[1]),
        std::bit_cast<int32_t>(std::bit_cast<uint32_t>(origin[2]) + unit.position[2])
    };
    if ((debris.flags & sim::effect_particles::debris_shatter) != 0) {
        shatter_piece(slot, debris);
        return;
    }
    (void)sim::effect_particles::add_debris(match_->effects(), debris);
}

void OfflineEffects::shatter_piece(
    sim::unit_spawn::Slot& slot, const sim::effect_particles::DebrisPiece& request
) {
    const auto& instance = match_->instance(slot.unit_index)->model();
    const auto& piece = instance.piece_for_script_index(request.piece);
    const auto& model = instance.model();
    sim::effect_particles::ShatterPiece shattered{};
    if (!match_->loaded_primitives(
            model, piece.object_index, primitives_, shattered.selection_primitive
        ))
        return;
    points_.clear();
    for (const auto& vertex : piece.transformed_vertices)
        points_.push_back({vertex.x, vertex.y, vertex.z});
    const auto* mover = match_->ground_runtime(slot.unit_index);
    shattered.primitives = primitives_.data();
    shattered.primitive_count = static_cast<uint32_t>(primitives_.size());
    shattered.points = points_.data();
    shattered.point_count = static_cast<uint32_t>(points_.size());
    shattered.origin = {
        piece.transformed_origin.x, piece.transformed_origin.y, piece.transformed_origin.z
    };
    shattered.model = &model;
    shattered.object = piece.object_index;
    sim::effect_particles::shatter_piece(
        match_->effects(),
        match_->state(),
        match_->effect_host(),
        request,
        slot.record,
        mover != nullptr ? &mover->movement : nullptr,
        shattered
    );
}

Runtime& OfflineEffects::required() {
    if (!runtime_)
        throw std::logic_error("offline effects used before bind");
    return *runtime_;
}

sim::match_runtime::Match& OfflineEffects::bound_match() {
    if (!match_)
        throw std::logic_error("offline effects used before bind");
    return *match_;
}

void OfflineEffects::emit_sfx(sim::unit_spawn::Slot& s, uint32_t p, int32_t e) {
    required().emit_sfx(s, p, e);
}

void OfflineEffects::explode_piece(sim::unit_spawn::Slot& s, uint32_t p, int32_t f) {
    required().explode_piece(s, p, f);
}

void OfflineEffects::attach_unit(
    sim::unit_spawn::Slot& s, int32_t target, int32_t piece, int32_t mode
) {
    bound_match().script_attach_unit(s.unit_index, target, piece, mode);
}

void OfflineEffects::drop_unit(sim::unit_spawn::Slot& s, int32_t target) {
    bound_match().script_drop_unit(s.unit_index, target);
}

bool OfflineEffects::visible(const sim::unit_spawn::Slot& slot) {
    // Owner identity, cloak, and the four-corner sight test against the
    // viewpoint player's coverage; the gate lives on Match.
    return match_->unit_visible(match_->world().viewpoint_player, slot.unit_index);
}

void OfflineEffects::refresh_transform(sim::unit_spawn::Slot& slot) {
    auto* i = match_->instance(slot.unit_index);
    if (!i)
        throw std::logic_error("effect unit has no model instance");
    const oa_angle heading = slot.record.heading;
    i->model().rebuild_transforms(
        {slot.record.bank, std::bit_cast<int16_t>(heading), slot.record.pitch}
    );
}

Position OfflineEffects::piece_start(const sim::unit_spawn::Slot& slot, uint32_t p) {
    auto* i = match_->instance(slot.unit_index);
    if (!i)
        throw std::logic_error("effect unit has no model instance");
    const auto& piece = i->model().piece_for_script_index(p);
    if (piece.transformed_vertices.size() < 2)
        throw std::out_of_range("effect piece has fewer than two transformed vertices");
    return position(piece.transformed_vertices[0]);
}

Position OfflineEffects::piece_end(const sim::unit_spawn::Slot& slot, uint32_t p) {
    auto* i = match_->instance(slot.unit_index);
    if (!i)
        throw std::logic_error("effect unit has no model instance");
    const auto& piece = i->model().piece_for_script_index(p);
    if (piece.transformed_vertices.size() < 2)
        throw std::out_of_range("effect piece has fewer than two transformed vertices");
    return position(piece.transformed_vertices[1]);
}

Position OfflineEffects::piece_origin(const sim::unit_spawn::Slot& slot, uint32_t p) {
    auto* i = match_->instance(slot.unit_index);
    if (!i)
        throw std::logic_error("effect unit has no model instance");
    return position(i->model().piece_for_script_index(p).transformed_origin);
}

Position OfflineEffects::piece_world(const sim::unit_spawn::Slot& slot, uint32_t p) {
    auto* i = match_->instance(slot.unit_index);
    if (!i)
        throw std::logic_error("effect unit has no model instance");
    const auto value = i->piece_world(p);
    return {
        std::bit_cast<int32_t>(value[0]),
        std::bit_cast<int32_t>(value[1]),
        std::bit_cast<int32_t>(value[2])
    };
}

int32_t OfflineEffects::sea_level_fixed() {
    return static_cast<int32_t>(match_->simulation().sea_level) << 16;
}

uint32_t OfflineEffects::random_bounded(uint32_t n) {
    return match_->random_bounded(n);
}

void OfflineEffects::set_piece_visible(sim::unit_spawn::Slot& slot, uint32_t p, bool visible) {
    auto* i = match_->instance(slot.unit_index);
    if (!i)
        throw std::logic_error("effect unit has no model instance");
    auto& piece = i->model().piece_for_script_index(p);
    constexpr auto bit = static_cast<uint16_t>(sim::model_runtime::PieceFlag::visible);
    if (visible)
        piece.flags |= bit;
    else
        piece.flags &= static_cast<uint16_t>(~bit);
}

} // namespace oa::sim::unit_effects
