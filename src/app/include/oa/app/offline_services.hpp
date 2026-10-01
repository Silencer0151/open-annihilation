// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Native boundaries the offline match runtime calls back into (audio, effects, footprints).
#pragma once

#include "oa/audio/unit_announcements.hpp"
#include "oa/core/game_state.h"
#include "oa/sim/unit_effects/effects_offline.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace oa::app {

/// A random stream of the presentation's own, for choices that change only
/// what is heard and shown, such as which variant of a unit announcement
/// plays or which taunt a message line takes: the linear congruential
/// sequence x = x * 214013 + 2531011, drawing bits 16..30. It never draws
/// from the match's streams, so that how often frames present these choices,
/// and what the camera shows, never changes the game.
struct PresentationRandom {
    static constexpr uint32_t multiplier = 214013;
    static constexpr uint32_t increment = 2531011;
    static constexpr uint32_t shift = 16;
    static constexpr uint32_t mask = 0x7fff;
    static constexpr uint32_t first_state = 1;

    uint32_t state{first_state}; ///< the generator's state

    /// Steps the generator.
    ///
    /// @return 0 to 32767
    [[nodiscard]] uint16_t next() noexcept {
        state = state * multiplier + increment;
        return static_cast<uint16_t>((state >> shift) & mask);
    }
};

class NativeOfflineServices final : public oa::sim::match_runtime::OfflineServices {
  public:

    /// Binds the unit effect runtime the script effect calls go to.
    ///
    /// @param effects effect runtime; must outlive the binding
    void bind_effects(oa::sim::match_runtime::Effects& effects) noexcept { effects_ = &effects; }

    /// Forgets the bound match; announcements need bind_announcements() again.
    void clear_match() noexcept { match_ = nullptr; }

    /// Sets the player whose units' announcements count as the viewer's own.
    ///
    /// @param viewpoint player index compared with the speaking slot's player byte
    void set_viewpoint(uint8_t viewpoint) noexcept { viewpoint_ = viewpoint; }

    /// Tests whether a unit is on screen: listed by the last frame drawn.
    using OnScreenTest = bool (*)(const void* context, uint16_t unit);

    /// Sets the test that keeps the under-attack notice of a unit on screen unsaid.
    ///
    /// Without a test every notice is queued.
    ///
    /// @param test on-screen test; null queues every notice
    /// @param context passed back to `test`
    void set_on_screen_test(OnScreenTest test, const void* context) noexcept {
        on_screen_test_ = test;
        on_screen_context_ = context;
    }

    /// Hears the unit announcements every player's units ask for, whoever
    /// owns them: director mode's listener (director_presentation.hpp).
    struct AnnouncementHooks {
        void* context{};
        /// Takes an announcement a unit asks for, before the match's own
        /// queue sees it; null hears none.
        ///
        /// @param context AnnouncementHooks::context
        /// @param owner the speaking unit's player
        /// @param request the request as the match's queue gets it; its
        ///        views are valid for the call only
        void (*heard)(
            void* context, uint8_t owner, const oa::audio::game_audio::AnnouncementRequest& request
        ){};
    };

    /// Sets the listener that hears every unit's announcement requests.
    ///
    /// The match's own queue still gets every request afterwards, so what it
    /// queues does not change.
    ///
    /// @param hooks the listener; a null `heard` hears none
    void set_announcement_hooks(AnnouncementHooks hooks) noexcept { announcement_hooks_ = hooks; }

    /// Binds the unit sound catalog and the match for unit announcements.
    ///
    /// Maps each type's simulation record to its definition's sound category,
    /// and starts the announcements' random stream again.
    /// Throws std::runtime_error unless there is exactly one more type than
    /// definitions (type 0 has none).
    ///
    /// @param catalog unit sound catalog; must outlive the binding
    /// @param match running match; must outlive the binding
    /// @param types spawn types, index 0 unused
    /// @param definitions unit definitions of types 1 and up
    /// @param gates presentation gates the queue starts with
    void bind_announcements(
        const oa::audio::game_audio::UnitSoundCatalog& catalog,
        oa::sim::match_runtime::Match& match,
        std::span<oa::sim::unit_spawn::Type> types,
        std::span<const oa::data::unit_definitions::UnitDefinition> definitions,
        oa::audio::game_audio::AnnouncementPresentationGates gates
    ) {
        if (types.size() != definitions.size() + 1U)
            throw std::runtime_error("unit announcement type catalog is inconsistent");
        announcement_catalog_ = &catalog;
        match_ = &match;
        announcement_gates_ = gates;
        announcement_random_ = {};
        sound_categories_.clear();
        for (std::size_t index = 1; index < types.size(); ++index)
            sound_categories_.emplace(
                &types[index].simulation, definitions[index - 1U].sound_category
            );
    }

    /// Returns the presentation gates the queue reads as each record is presented.
    ///
    /// @return the gates, writable
    oa::audio::game_audio::AnnouncementPresentationGates& announcement_gates() noexcept {
        return announcement_gates_;
    }

    /// Presents at most one queued announcement and hands over those presented since the last call.
    ///
    /// A record is pumped only while the catalog and match are bound and the
    /// queue is not empty; its random draw comes from the announcements' own
    /// stream (PresentationRandom), never from the match's, so that how often
    /// this is called never changes the game.
    ///
    /// @return the presented announcements, oldest first; the list starts empty again
    [[nodiscard]] std::vector<oa::audio::game_audio::UnitAnnouncement> pump_announcements() {
        if (announcement_catalog_ != nullptr && match_ != nullptr &&
            announcement_queue_.size() != 0) {
            const auto random = announcement_random_.next();
            if (auto event = announcement_queue_.pump(
                    *announcement_catalog_, announcement_gates_, random, match_->simulation().tick
                ))
                presented_announcements_.push_back(std::move(*event));
        }
        return std::exchange(presented_announcements_, {});
    }

    /// Queues a unit's activation or deactivation sound as an announcement.
    ///
    /// @param slot unit whose state changed
    /// @param sound sound the change calls for, taken as its sound category
    void activation_sound(
        oa::sim::unit_spawn::Slot& slot, oa::sim::unit_activation::Sound sound
    ) override {
        enqueue_announcement(slot, static_cast<uint32_t>(sound));
    }

    /// Appends an attachment notification to attachment_notifications.
    ///
    /// @param slot unit whose state changed
    /// @param value event bits of the change
    void attachment_notification(oa::sim::unit_spawn::Slot& slot, uint32_t value) override {
        attachment_notifications.emplace_back(slot.unit_index, value);
    }

    /// Appends a unit whose order panel needs a redraw to refreshed_units.
    ///
    /// @param slot unit the panel shows
    void refresh_selected_unit(oa::sim::unit_spawn::Slot& slot) override {
        refreshed_units.push_back(slot.unit_index);
    }

    /// Queues a unit's command speech as an announcement.
    ///
    /// The under-attack notice (category 2) of a unit the on-screen test finds
    /// on screen is dropped: only units out of view are announced.
    ///
    /// @param slot speaking unit
    /// @param category speech category (5 order, 7 failed, 8 complete, ...)
    void command_sound(oa::sim::unit_spawn::Slot& slot, uint32_t category) override {
        if (category == static_cast<uint32_t>(
                            oa::audio::game_audio::UnitAnnouncementCategory::under_attack
                        ) &&
            on_screen_test_ != nullptr && on_screen_test_(on_screen_context_, slot.unit_index))
            return;
        enqueue_announcement(slot, category);
    }

    /// Queues a unit's command speech captioned with its order's own text.
    ///
    /// The owner reads the caption in place of the category's own; the
    /// category still picks the sound.
    ///
    /// @param slot speaking unit
    /// @param category speech category (5 order, 7 failed, 8 complete, ...)
    /// @param caption the caption, as the application shows it
    void
    command_speech(oa::sim::unit_spawn::Slot& slot, uint32_t category, std::string_view caption) {
        enqueue_announcement(slot, category, caption);
    }

    /// Appends a plot height range refresh to masked_registrations.
    ///
    /// @param origin first cell of the rectangle, one cell before the footprint
    ///     origin on each axis
    /// @param footprint rectangle size in cells, the footprint plus two on each axis
    void refresh_plot_height_range(
        std::array<int16_t, 2> origin, std::array<int16_t, 2> footprint
    ) override {
        masked_registrations.emplace_back(origin, footprint);
    }

    /// Appends a changed footprint to footprint_changes.
    ///
    /// @param cell footprint origin cell
    /// @param footprint footprint size in cells
    void notify_footprint_changed(
        std::array<int16_t, 2> cell, std::array<int16_t, 2> footprint
    ) override {
        footprint_changes.emplace_back(cell, footprint);
    }

    /// Appends an object-backed unit that left its footprint to removed_footprints.
    ///
    /// @param unit the removed unit; its object tick has already advanced
    /// @param old_tick the unit's object tick before removal
    void notify_object_footprint_removed(
        oa::sim::spatial_state::Unit& unit, uint32_t old_tick
    ) override {
        removed_footprints.emplace_back(unit.id, old_tick);
    }

    /// Passes a COB EMIT-SFX to the bound effect runtime.
    ///
    /// Throws std::runtime_error when no effect runtime is bound.
    ///
    /// @param slot unit running the script
    /// @param piece COB piece index
    /// @param effect SFX type the script passes
    void emit_sfx(oa::sim::unit_spawn::Slot& slot, uint32_t piece, int32_t effect) override {
        if (!effects_)
            throw std::runtime_error("unit effect runtime is not bound");
        effects_->emit_sfx(slot, piece, effect);
    }

    /// Passes a COB EXPLODE to the bound effect runtime.
    ///
    /// Throws std::runtime_error when no effect runtime is bound.
    ///
    /// @param slot unit running the script
    /// @param piece COB piece index
    /// @param flags explode type flags the script passes
    void explode_piece(oa::sim::unit_spawn::Slot& slot, uint32_t piece, int32_t flags) override {
        if (!effects_)
            throw std::runtime_error("unit effect runtime is not bound");
        effects_->explode_piece(slot, piece, flags);
    }

    /// Passes a COB ATTACH-UNIT to the bound effect runtime.
    ///
    /// Throws std::runtime_error when no effect runtime is bound.
    ///
    /// @param slot carrier running the script
    /// @param first unit id to carry
    /// @param second COB piece index to carry it on
    /// @param third attachment mode
    void attach_unit(
        oa::sim::unit_spawn::Slot& slot, int32_t first, int32_t second, int32_t third
    ) override {
        if (!effects_)
            throw std::runtime_error("unit effect runtime is not bound");
        effects_->attach_unit(slot, first, second, third);
    }

    /// Passes a COB DROP-UNIT to the bound effect runtime.
    ///
    /// Throws std::runtime_error when no effect runtime is bound.
    ///
    /// @param slot carrier running the script
    /// @param target unit id to set down
    void drop_unit(oa::sim::unit_spawn::Slot& slot, int32_t target) override {
        if (!effects_)
            throw std::runtime_error("unit effect runtime is not bound");
        effects_->drop_unit(slot, target);
    }

    std::vector<std::pair<std::array<int16_t, 2>, std::array<int16_t, 2>>> masked_registrations,
        footprint_changes;
    std::vector<std::pair<uint16_t, uint32_t>> removed_footprints;
    std::vector<std::pair<uint16_t, uint32_t>> attachment_notifications;
    std::vector<uint16_t> refreshed_units;

  private:

    /// Queues a unit announcement for a game sound category.
    ///
    /// A category with no announcement is dropped. The announcement listener,
    /// when one is set, hears the request first, whoever owns the unit. When
    /// the queue was full and the record is queued, the record it evicted is
    /// presented at once, with a draw from the announcements' own stream
    /// (PresentationRandom): whether the queue is full depends on how often
    /// frames presented it, so the match's streams are never drawn from. Throws
    /// std::runtime_error when nothing is bound or the unit's type is not in the
    /// catalog.
    ///
    /// @param slot speaking unit
    /// @param category_number the game's sound category, mapped through
    ///     unit_announcement_category()
    /// @param caption text shown in place of the category's own, or nothing
    void enqueue_announcement(
        oa::sim::unit_spawn::Slot& slot,
        uint32_t category_number,
        std::optional<std::string_view> caption = std::nullopt
    ) {
        if (announcement_catalog_ == nullptr || match_ == nullptr || slot.unit == nullptr ||
            slot.unit->type == nullptr)
            throw std::runtime_error("unit announcement runtime is not bound");
        const auto category = oa::audio::game_audio::unit_announcement_category(category_number);
        if (!category)
            return;
        const auto found = sound_categories_.find(slot.unit->type);
        if (found == sound_categories_.end())
            throw std::runtime_error("unit announcement type is not in the loaded catalog");
        const oa::audio::game_audio::AnnouncementRequest request{
            slot.unit_index,
            found->second,
            *category,
            match_->simulation().tick,
            slot.owner_index == viewpoint_,
            (slot.unit->flags & OA_UNIT_FLAG_LIVE) != 0 &&
                (slot.unit->flags & OA_UNIT_FLAG_DEATH_PENDING) == 0,
            // Chatter captions a unit only while it is live (OA_UNIT_FLAG_LIVE).
            (slot.unit->flags & OA_UNIT_FLAG_LIVE) != 0,
            caption
        };
        // Before the queue is touched, so that what it holds is the same with
        // a listener or without.
        if (announcement_hooks_.heard != nullptr)
            announcement_hooks_.heard(announcement_hooks_.context, slot.owner_index, request);
        const bool was_full =
            announcement_queue_.size() == oa::audio::game_audio::AnnouncementQueue::capacity;
        const auto result = announcement_queue_.enqueue(request);
        if (was_full && result == oa::audio::game_audio::AnnouncementEnqueueStatus::queued) {
            const auto random = announcement_random_.next();
            if (auto event = announcement_queue_.present_evicted(
                    *announcement_catalog_, announcement_gates_, random, match_->simulation().tick
                ))
                presented_announcements_.push_back(std::move(*event));
        }
    }

    oa::sim::match_runtime::Effects* effects_{};
    const oa::audio::game_audio::UnitSoundCatalog* announcement_catalog_{};
    oa::sim::match_runtime::Match* match_{};
    oa::audio::game_audio::AnnouncementQueue announcement_queue_;
    oa::audio::game_audio::AnnouncementPresentationGates announcement_gates_{};
    PresentationRandom announcement_random_{};
    std::unordered_map<const oa::sim::simulation_state::UnitType*, std::string> sound_categories_;
    std::vector<oa::audio::game_audio::UnitAnnouncement> presented_announcements_;
    uint8_t viewpoint_{};
    OnScreenTest on_screen_test_{};
    const void* on_screen_context_{};
    AnnouncementHooks announcement_hooks_{};
};

class NativeEffectBoundary final : public oa::sim::unit_effects::OfflineLifecycle,
                                   public oa::sim::unit_effects::Sink {
  public:

    /// Records a unit effect event with the current tick, keeping the latest 64.
    ///
    /// @param event effect event
    void effect(const oa::sim::unit_effects::Event& event) override {
        const uint32_t tick = clock == nullptr ? 0u : clock->tick;
        events.push_back({event, tick});
        if (events.size() > 64)
            events.erase(
                events.begin(), events.begin() + static_cast<std::ptrdiff_t>(events.size() - 64)
            );
    }

    struct LiveEffect {
        oa::sim::unit_effects::Event event;
        uint32_t spawn_tick{};
    };

    const oa::Game* clock = nullptr; ///< game whose tick stamps each event; null stamps 0
    std::vector<LiveEffect> events;
};

} // namespace oa::app
