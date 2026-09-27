// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/formats/hpi.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::audio::game_audio {

// Indices of the fixed announcement descriptor table. These are also the
// category values the game's announcement requests carry.
enum class UnitAnnouncementCategory : uint8_t {
    select = 1,
    under_attack = 2,
    activate = 3,
    deactivate = 4,
    acknowledge = 5,
    arrived = 6,
    cannot_comply = 7,
    unit_complete = 8,
    build = 9,
    repair = 10,
    working = 11,
    load = 12,
    unload = 13,
    cloak = 14,
    uncloak = 15,
    capture = 16,
    count_5 = 17,
    count_4 = 18,
    count_3 = 19,
    count_2 = 20,
    count_1 = 21,
    count_0 = 22,
    cancel_destruct = 23,
};

struct UnitAnnouncementDescriptor {
    uint8_t priority{};
    uint8_t cooldown_seconds{};
    std::string_view sound_key;
    std::string_view default_text;
};

/// Returns the fixed descriptor of an announcement category.
///
/// @param category Announcement category.
/// @return Priority, cooldown, SOUND.TDF key and default caption; null for 0 or an unknown value.
[[nodiscard]] const UnitAnnouncementDescriptor*
descriptor(UnitAnnouncementCategory category) noexcept;

/// Converts a numeric category value as the game stores it.
///
/// @param category_number Category number, 1..23 when valid.
/// @return The category, or nothing for 0 or values past the table.
[[nodiscard]] std::optional<UnitAnnouncementCategory>
unit_announcement_category(uint32_t category_number) noexcept;

// Parsed form of gamedata/SOUND.TDF. Values are configured WAV names; the
// presenter resolves them directly below the sounds directory.
class UnitSoundCatalog {
  public:

    struct Choice {
        std::string sound;
        std::string text;
    };

    /// Loads and parses gamedata/sound.tdf.
    ///
    /// @param assets Asset store searched in the game's archive order.
    /// @return The catalog.
    /// @throws std::runtime_error when the file is not valid TDF.
    [[nodiscard]] static UnitSoundCatalog load(const oa::AssetStore& assets);

    /// Parses SOUND.TDF text into per-section sound lists.
    ///
    /// Each category key contributes its plain value and its numbered
    /// variants (key1, key2, ...) with their "<key>text" captions; empty
    /// values are skipped.
    ///
    /// @param source File contents.
    /// @return The catalog, keyed by lower-cased section name.
    /// @throws std::runtime_error when the text is not valid TDF.
    [[nodiscard]] static UnitSoundCatalog parse_sound_tdf(std::string_view source);

    /// Returns the sounds a unit sound category offers for an announcement.
    ///
    /// @param sound_category Unit's SOUND.TDF section name, compared case-insensitively.
    /// @param category Announcement category.
    /// @return The choices, or null when the section or category is unknown.
    [[nodiscard]] const std::vector<Choice>*
    choices(std::string_view sound_category, UnitAnnouncementCategory category) const noexcept;

    /// Returns the number of sound sections.
    [[nodiscard]] std::size_t size() const noexcept { return categories_.size(); }

  private:

    using SoundsByCategory = std::array<std::vector<Choice>, 24>;
    std::map<std::string, SoundsByCategory, std::less<>> categories_;
};

struct AnnouncementRequest {
    uint16_t unit_index{};
    std::string_view unit_sound_category;
    UnitAnnouncementCategory category{};
    uint32_t tick{};
    bool local_owner{};
    bool owner_can_announce{};
    bool unit_text_enabled{};
    std::optional<std::string_view> text_override;
};

enum class AnnouncementEnqueueStatus : uint8_t {
    queued,
    invalid_category,
    not_local_owner,
    owner_suppressed,
    cooling_down,
    duplicate_category,
};

struct AnnouncementPresentationGates {
    uint8_t unit_sound_volume{}; // Game.unit_sound_volume, range 0..10
    uint8_t unit_text_volume{};  // Game.unit_text_volume, range 0..10
    bool play_audio{};           // the presenter's play-audio argument
    bool unit_speech_mode{};     // the speechfx bit (sound_flag::speech) of Game.sound_flags
    bool show_text{};            // the presenter's show-text argument
    // Novelty voice toggled by sound_toggle_novelty_voice(): every announcement plays "honk" on
    // one 30-tick window in eight and "sing" otherwise.
    bool novelty_voice{};
};

struct UnitAnnouncement {
    uint16_t unit_index{};
    UnitAnnouncementCategory category{};
    std::optional<std::string> sound_resource;
    std::optional<std::string> text;
};

class AnnouncementQueue {
  public:

    static constexpr std::size_t capacity = 8;

    /// Queues a request after the owner gates.
    ///
    /// Skipped while its category cools down or is already queued; a full
    /// queue first evicts its last record (see present_evicted()); records
    /// stay ordered by descending priority.
    ///
    /// @param request Unit, categories, tick and owner gates of the announcement.
    /// @return queued, or the gate that refused it.
    [[nodiscard]] AnnouncementEnqueueStatus enqueue(const AnnouncementRequest& request);

    /// Picks a random sound of the front record's category and captions it.
    ///
    /// Audio needs play_audio, speech mode, a sound and a priority above
    /// 10 - unit_sound_volume; text needs show_text, the record's text flag
    /// and a priority above 10 - unit_text_volume. The record stays queued.
    ///
    /// @param catalog Unit sound catalog.
    /// @param gates Presentation gates.
    /// @param rng15 rand() result, constrained to 0..32767, choosing among the sounds.
    /// @param current_tick Current game tick; the category's cooldown restarts
    ///        from it only when a sound is selected.
    /// @return The sound and caption to present, or nothing when the queue is empty.
    [[nodiscard]] std::optional<UnitAnnouncement> present_front(
        const UnitSoundCatalog& catalog,
        AnnouncementPresentationGates gates,
        uint16_t rng15,
        uint32_t current_tick
    );

    /// Presents the record evicted by the last enqueue into a full queue.
    ///
    /// Call this once after such an enqueue.
    ///
    /// @param catalog Unit sound catalog.
    /// @param gates Presentation gates; audio is forced off and text on.
    /// @param rng15 rand() result, constrained to 0..32767.
    /// @param current_tick Current game tick.
    /// @return The caption to present, or nothing when no record was evicted.
    /// @quirk A full queue presents slot seven with audio disabled and text
    ///        enabled before removing it; rng15 is still consumed because the
    ///        presenter always draws a random number.
    [[nodiscard]] std::optional<UnitAnnouncement> present_evicted(
        const UnitSoundCatalog& catalog,
        AnnouncementPresentationGates gates,
        uint16_t rng15,
        uint32_t current_tick
    );

    /// Presents and removes the front record; call once per speech tick.
    ///
    /// @param catalog Unit sound catalog.
    /// @param gates Presentation gates.
    /// @param rng15 rand() result, constrained to 0..32767.
    /// @param current_tick Current game tick.
    /// @return The sound and caption to present, or nothing when the queue is empty.
    /// @quirk Audio is attempted no more than once per 30 ticks; text and the
    ///        random draw still occur between plays.
    [[nodiscard]] std::optional<UnitAnnouncement> pump(
        const UnitSoundCatalog& catalog,
        AnnouncementPresentationGates gates,
        uint16_t rng15,
        uint32_t current_tick
    );

    /// Removes the front record, if any.
    void pop_front() noexcept;

    /// Removes one record and closes the gap.
    ///
    /// @param index Queue position; ignored when out of range.
    void remove_at(std::size_t index) noexcept;

    /// Removes every record, newest first, and forgets the last audio tick.
    void clear() noexcept;

    /// Removes every record queued for the unit.
    ///
    /// @param unit_index Unit whose records are dropped.
    void remove_unit(uint16_t unit_index) noexcept;

    /// Clears the queue and every category cooldown.
    void reset_cooldowns() noexcept;

    /// Returns the number of queued records.
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

  private:

    struct Entry {
        uint16_t unit_index{};
        UnitAnnouncementCategory category{};
        bool unit_text_enabled{};
        std::string unit_sound_category;
        std::optional<std::string> text;
    };

    std::array<Entry, capacity> entries_{};
    std::optional<Entry> evicted_;
    std::array<uint32_t, 24> next_allowed_tick_{};
    std::size_t size_{};
    uint32_t last_audio_tick_{};
    static constexpr uint32_t speech_interval_ticks = 30;
};

} // namespace oa::audio::game_audio
