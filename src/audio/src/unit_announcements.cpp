// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/unit_announcements.hpp"
#include "oa/data/defs/layout.hpp"
#include "oa/audio/game_audio.hpp"

#include "oa/formats/tdf.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>

namespace oa::audio::game_audio {
namespace {

/// The novelty voice's windows: the first of its sounds plays in the first
/// window of every novelty_windows, each novelty_window_ticks ticks long.
constexpr uint32_t novelty_window_ticks = 30;
constexpr uint32_t novelty_windows = 8;

constexpr std::array<UnitAnnouncementDescriptor, 24> descriptors{{
    {},
    {10, 0, "select", ""},
    {9, 20, "underattack", "Under Attack"},
    {4, 2, "activate", ""},
    {4, 2, "deactivate", ""},
    {5, 1, "ok", ""},
    {3, 4, "arrived", "Arrived"},
    {8, 1, "cant", "Cannot Comply"},
    {3, 3, "unitcomplete", "Nanolathe Complete"},
    {4, 2, "build", ""},
    {3, 1, "repair", ""},
    {2, 1, "working", ""},
    {7, 1, "load", ""},
    {7, 1, "unload", ""},
    {7, 1, "cloak", "Cloaked"},
    {7, 1, "uncloak", "Visible"},
    {4, 1, "capture", ""},
    {10, 0, "count5", "five"},
    {10, 0, "count4", "four"},
    {10, 0, "count3", "three"},
    {10, 0, "count2", "two"},
    {10, 0, "count1", "one"},
    {10, 0, "count0", "zero"},
    {10, 0, "canceldestruct", "Self destruct terminated"},
}};

std::string lower(std::string_view value) {
    std::string result(value);
    for (char& c : result)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

} // namespace

const UnitAnnouncementDescriptor* descriptor(UnitAnnouncementCategory category) noexcept {
    const auto index = static_cast<std::size_t>(category);
    return index > 0 && index < descriptors.size() ? &descriptors[index] : nullptr;
}

std::optional<UnitAnnouncementCategory>
unit_announcement_category(uint32_t category_number) noexcept {
    if (category_number == 0 || category_number >= descriptors.size())
        return std::nullopt;
    return static_cast<UnitAnnouncementCategory>(category_number);
}

UnitSoundCatalog UnitSoundCatalog::load(const oa::AssetStore& assets) {
    const auto data = assets.read(
        oa::data::defs::data_path(oa::data::defs::DataDirectory::gamedata, "sound.tdf")
    );
    return parse_sound_tdf(
        std::string_view(reinterpret_cast<const char*>(data.bytes.data()), data.bytes.size())
    );
}

UnitSoundCatalog UnitSoundCatalog::parse_sound_tdf(std::string_view source) {
    formats::tdf::OwnedDocument parsed;
    formats::tdf::ParseError error{};
    if (!parsed.parse(source, &error))
        throw std::runtime_error("invalid sound.tdf: " + formats::tdf::describe(error));

    UnitSoundCatalog result;
    for (uint32_t section_index = 0; section_index < formats::tdf::child_count(parsed.root());
         ++section_index) {
        const auto* section = formats::tdf::child_at(parsed.root(), section_index);
        const auto text_of = [section](const std::string& key) {
            const char* text = formats::tdf::find_value(section, (key + "text").c_str());
            return text == nullptr ? std::string{} : std::string(text);
        };
        SoundsByCategory sounds;
        for (std::size_t index = 1; index < descriptors.size(); ++index) {
            const std::string key(descriptors[index].sound_key);
            if (const char* value = formats::tdf::find_value(section, key.c_str());
                value != nullptr && value[0] != '\0')
                sounds[index].push_back({value, text_of(key)});
            for (std::size_t variant = 1;; ++variant) {
                const std::string numbered = key + std::to_string(variant);
                const char* value = formats::tdf::find_value(section, numbered.c_str());
                if (value == nullptr)
                    break;
                if (value[0] != '\0')
                    sounds[index].push_back({value, text_of(numbered)});
            }
        }
        result.categories_.try_emplace(lower(section->name), std::move(sounds));
    }
    return result;
}

const std::vector<UnitSoundCatalog::Choice>* UnitSoundCatalog::choices(
    std::string_view sound_category, UnitAnnouncementCategory category
) const noexcept {
    const auto category_index = static_cast<std::size_t>(category);
    if (category_index == 0 || category_index >= descriptors.size())
        return nullptr;
    const auto found = categories_.find(lower(sound_category));
    if (found == categories_.end())
        return nullptr;
    return &found->second[category_index];
}

AnnouncementEnqueueStatus AnnouncementQueue::enqueue(const AnnouncementRequest& request) {
    const auto* details = descriptor(request.category);
    if (details == nullptr)
        return AnnouncementEnqueueStatus::invalid_category;
    if (!request.local_owner)
        return AnnouncementEnqueueStatus::not_local_owner;
    if (!request.owner_can_announce)
        return AnnouncementEnqueueStatus::owner_suppressed;
    const auto index = static_cast<std::size_t>(request.category);
    if (next_allowed_tick_[index] > request.tick)
        return AnnouncementEnqueueStatus::cooling_down;
    for (std::size_t i = 0; i < size_; ++i)
        if (entries_[i].category == request.category)
            return AnnouncementEnqueueStatus::duplicate_category;

    if (size_ == capacity) {
        // Slot 7 is presented with audio off and text on before removal.
        evicted_ = std::move(entries_[capacity - 1]);
        --size_;
    }
    std::size_t insertion = 0;
    while (insertion < size_) {
        const auto* queued = descriptor(entries_[insertion].category);
        if (queued != nullptr && queued->priority < details->priority)
            break;
        ++insertion;
    }
    for (std::size_t i = size_; i > insertion; --i)
        entries_[i] = std::move(entries_[i - 1]);
    entries_[insertion] = Entry{
        request.unit_index,
        request.category,
        request.unit_text_enabled,
        std::string(request.unit_sound_category),
        request.text_override
            ? std::optional<std::string>(*request.text_override)
            : (details->default_text.empty() ? std::nullopt
                                             : std::optional<std::string>(details->default_text))
    };
    ++size_;
    return AnnouncementEnqueueStatus::queued;
}

std::optional<UnitAnnouncement> AnnouncementQueue::present_front(
    const UnitSoundCatalog& catalog,
    AnnouncementPresentationGates gates,
    uint16_t rng15,
    uint32_t current_tick
) {
    if (size_ == 0)
        return std::nullopt;
    const auto& entry = entries_[0];
    const auto* details = descriptor(entry.category);
    if (details == nullptr)
        return std::nullopt;

    UnitAnnouncement output{entry.unit_index, entry.category, std::nullopt, std::nullopt};
    const auto* sounds = catalog.choices(entry.unit_sound_category, entry.category);
    const UnitSoundCatalog::Choice* choice = nullptr;
    if (sounds != nullptr && !sounds->empty()) {
        const auto bounded_rng = std::min<uint16_t>(rng15, 0x7fffU);
        const auto selected = (static_cast<uint32_t>(bounded_rng) * sounds->size()) / 0x8000U;
        choice = &(*sounds)[selected];
    }
    if (gates.play_audio && gates.unit_speech_mode && choice != nullptr &&
        static_cast<unsigned>(details->priority) >
            10U - std::min<unsigned>(gates.unit_sound_volume, 10U)) {
        if (!gates.novelty_voice)
            output.sound_resource = sound_resource(choice->sound);
        else
            output.sound_resource = sound_resource(
                gates.novelty_sounds
                    [(current_tick / novelty_window_ticks) % novelty_windows == 0 ? 0 : 1]
            );
        next_allowed_tick_[static_cast<std::size_t>(entry.category)] =
            current_tick + static_cast<uint32_t>(details->cooldown_seconds) * 30U;
    }
    const std::string* text =
        entry.text ? &*entry.text
                   : (choice != nullptr && !choice->text.empty() ? &choice->text : nullptr);
    if (gates.show_text && entry.unit_text_enabled && text != nullptr && !text->empty() &&
        static_cast<unsigned>(details->priority) >
            10U - std::min<unsigned>(gates.unit_text_volume, 10U))
        output.text = *text;
    return output;
}

std::optional<UnitAnnouncement> AnnouncementQueue::present_evicted(
    const UnitSoundCatalog& catalog,
    AnnouncementPresentationGates gates,
    uint16_t rng15,
    uint32_t current_tick
) {
    if (!evicted_)
        return std::nullopt;
    const Entry displaced = std::move(*evicted_);
    evicted_.reset();
    Entry saved{};
    const bool had_front = size_ != 0;
    if (had_front)
        saved = std::move(entries_[0]);
    entries_[0] = displaced;
    gates.play_audio = false;
    gates.show_text = true;
    auto result = present_front(catalog, gates, rng15, current_tick);
    if (had_front)
        entries_[0] = std::move(saved);
    return result;
}

std::optional<UnitAnnouncement> AnnouncementQueue::pump(
    const UnitSoundCatalog& catalog,
    AnnouncementPresentationGates gates,
    uint16_t rng15,
    uint32_t current_tick
) {
    if (size_ == 0)
        return std::nullopt;
    if (current_tick < last_audio_tick_ + speech_interval_ticks)
        gates.play_audio = false;
    else
        last_audio_tick_ = current_tick;
    auto result = present_front(catalog, gates, rng15, current_tick);
    pop_front();
    return result;
}

void AnnouncementQueue::pop_front() noexcept {
    if (size_ != 0)
        remove_at(0);
}

void AnnouncementQueue::remove_at(std::size_t index) noexcept {
    if (index >= size_)
        return;
    for (std::size_t i = index + 1; i < size_; ++i)
        entries_[i - 1] = std::move(entries_[i]);
    entries_[size_ - 1] = Entry{};
    --size_;
}

void AnnouncementQueue::clear() noexcept {
    while (size_ > 0)
        remove_at(size_ - 1);
    last_audio_tick_ = 0;
}

void AnnouncementQueue::remove_unit(uint16_t unit_index) noexcept {
    std::size_t index = 0;
    while (index < size_) {
        if (entries_[index].unit_index == unit_index)
            remove_at(index);
        else
            ++index;
    }
}

void AnnouncementQueue::reset_cooldowns() noexcept {
    clear();
    next_allowed_tick_.fill(0);
}

} // namespace oa::audio::game_audio
