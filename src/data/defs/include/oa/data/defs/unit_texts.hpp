// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A unit file's name and description in the languages its data gives them
// in: 3.1c's "<Language>Name" and "<Language>Description" keys, which a game
// shown in that language reads in place of Name and Description. The unit
// definition keeps Name and Description (UnitDef.name and description), so
// that the simulation, a saved game and what a shared game sends are the
// same in every language; these texts are for what players see.
#pragma once

#include "oa/core/unit_def.h"
#include "oa/formats/tdf.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::data::defs {

/// The most bytes of a unit's name in another language, its end included:
/// as many as UnitDef.name holds.
inline constexpr std::size_t unit_text_name_bytes = sizeof(UnitDef::name);
/// The most bytes of a unit's description in another language, its end
/// included: as many as UnitDef.description holds.
inline constexpr std::size_t unit_text_description_bytes = sizeof(UnitDef::description);

/// Receives the names and descriptions a unit file gives in other languages.
struct UnitTextSink {
    void* context{}; ///< passed back to text
    /// The words the game data knows its languages by, as "German": each is
    /// read as the start of a Name and a Description key.
    const char* const* languages{};
    uint32_t language_count{}; ///< entries of `languages`
    /// Receives a unit file's name and description in one language, each
    /// cut to its field as 3.1c cuts it and null when the file lacks its
    /// key; called only for a language the file has one of the two in.
    /// Null receives nothing.
    void (*text)(
        void* context,
        const char* unit_name,
        uint32_t language,
        const char* name,
        const char* description
    ){};
};

/// Reads a unit file's names and descriptions in the sink's languages: for
/// each word, the values of "<word>Name" and "<word>Description", the keys
/// matched without regard to case, as 3.1c reads them for the language it
/// shows. A key longer than 255 bytes is not read.
///
/// @param block the file's UNITINFO section
/// @param unit_name the unit's name, as UnitDef.unit_name holds it
/// @param sink where the texts go; null reads nothing
void read_unit_texts(
    const formats::tdf::Block* block, const char* unit_name, const UnitTextSink* sink
) noexcept;

} // namespace oa::data::defs
