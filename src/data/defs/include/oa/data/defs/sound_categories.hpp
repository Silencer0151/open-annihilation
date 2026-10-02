// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit sound categories from GAMEDATA\SOUND.TDF.
#pragma once

#include "oa/data/defs/files.hpp"
#include "oa/formats/tdf.hpp"

#include <cstdint>

namespace oa::data::defs {

inline constexpr uint32_t sound_event_count = 23;
inline constexpr uint32_t sound_event_slots = sound_event_count + 1; // slot 0 is never loaded
inline constexpr uint32_t sound_choice_capacity = 64;
inline constexpr uint32_t sound_category_limit = 4096;

// Event keys in the game's table order (select, underattack, ... canceldestruct);
// key i fills SoundCategory.events[i + 1].
extern const char* const sound_event_keys[sound_event_count];

// Alternatives for one event: KEY, KEY1, KEY2, ... and their KEYtext captions.
struct SoundChoices {
    int32_t count{};
    char (*sounds)[sound_choice_capacity];
    char (*texts)[sound_choice_capacity];
};

// One sound category: its name and each event's sound choices.
struct SoundCategory {
    char name[64]{};
    SoundChoices events[sound_event_slots];
};

struct SoundCategoryTable {
    SoundCategory* categories{};
    uint32_t count{};
};

/// Appends the sound a key names, and its "<key>text" caption, to an event's alternatives.
///
/// @param section SOUND.TDF category section
/// @param key event key, e.g. "select" or "select2"
/// @param[in,out] choices alternatives to extend; sound and caption are cut at 63 characters
/// @return false when the key is missing, the event already has 1024
///     alternatives or an allocation fails; a missing caption is stored as ""
bool sound_choices_add(
    const formats::tdf::Block* section, const char* key, SoundChoices* choices
) noexcept;

/// Builds one sound category per top-level section of a parsed SOUND.TDF.
///
/// Each category takes the section name (up to 63 characters) and reads every
/// event as KEY, then KEY1, KEY2, ... until one is missing.
///
/// @param[in,out] sound parsed SOUND.TDF; its cursor is moved
/// @param[out] table table to fill; its previous contents are not freed
/// @return false when the file has more than sound_category_limit sections or
///     the category array cannot be allocated
bool sound_category_table_load(formats::tdf::Document* sound, SoundCategoryTable* table) noexcept;

/// Loads GAMEDATA/SOUND.TDF, preferring the variant directory.
///
/// @param files file boundary
/// @param[out] table table to fill; emptied first, not freed
/// @param variant game-data variant suffix; null or empty for none
/// @return false when the file is missing, fails to parse or cannot be stored
bool load_sound_categories(
    const Files* files, SoundCategoryTable* table, const char* variant
) noexcept;

/// Frees every alternative array and the category array.
///
/// @param[in,out] table table to release; left empty
void sound_category_table_free(SoundCategoryTable* table) noexcept;

/// Finds a sound category by name.
///
/// @param table loaded table
/// @param name category name, matched case-insensitively
/// @return index of the first match, or -1
[[nodiscard]] int sound_category_find(const SoundCategoryTable* table, const char* name) noexcept;

// GAMEDATA\ALLSOUND.TDF: the section name is copied into 0x20 bytes, the
// sound= value into 0x100.
inline constexpr std::size_t allsound_name_capacity = 0x20;
inline constexpr std::size_t allsound_sound_capacity = 0x100;

// The named-sound cache (counted by Game.sound_count) the allsound entries go into.
struct SoundCache {
    void* context{};
    void (*clear)(void* context) = nullptr;
    void (*add)(void* context, const char* name, const char* sound) = nullptr;
};

/// Refills the named-sound cache from GAMEDATA/ALLSOUND.TDF, then loads the sound categories.
///
/// The cache is cleared first; every section with a sound= key is added under
/// its section name. A missing ALLSOUND.TDF leaves the cache empty.
///
/// @param files file boundary
/// @param variant game-data variant suffix; null or empty for none
/// @param cache named-sound cache to refill
/// @param[out] categories sound category table, loaded as load_sound_categories does
void load_all_sounds(
    const Files* files, const char* variant, const SoundCache& cache, SoundCategoryTable* categories
) noexcept;

} // namespace oa::data::defs
