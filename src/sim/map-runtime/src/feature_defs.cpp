// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/map_runtime/feature_defs.hpp"
#include "oa/base/game_math.hpp"

#include "oa/formats/tdf.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace oa::sim::map_runtime {
namespace {

constexpr std::size_t name_capacity = sizeof(FeatureDef::name);
constexpr std::size_t description_capacity = sizeof(FeatureDef::description);
constexpr std::size_t animation_file_capacity = sizeof(FeatureDef::animation_file);
constexpr uint16_t flag_forced_no_draw_under_gray = OA_FEATURE_FLAG_NO_DRAW_UNDER_GRAY;
// Simulation ticks in each second of a TDF sparktime.
constexpr double spark_ticks_per_second = 30.0;

// Names whose features are always drawn over the mapped-but-unseen layer.
constexpr std::string_view forced_gray_names[] = {
    "dragons teeth", "dragons teeth core", "fortification", "fortification core"
};

bool equal_nocase(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

/// Copies a key's value into a bounded buffer.
///
/// The buffer is zero padded and left unterminated when full, as strncpy leaves it.
///
/// @param section TDF section to read
/// @param key key name
/// @param[out] out buffer of `capacity` bytes, zeroed first
/// @param capacity buffer size in bytes
/// @return false when the key is absent
bool tdf_string(
    const formats::tdf::Block* section, const char* key, char* out, std::size_t capacity
) {
    std::memset(out, 0, capacity);
    const char* value = formats::tdf::find_value(section, key);
    if (value == nullptr)
        return false;
    std::memcpy(out, value, std::min(std::strlen(value), capacity));
    return true;
}

std::string_view bounded(const char* text, std::size_t capacity) {
    return {text, ::strnlen(text, capacity)};
}

/// Converts a TDF sparktime in seconds to FeatureDef.spark_time in ticks.
///
/// @param seconds the sparktime value
/// @return the product of `seconds` and spark_ticks_per_second rounded to a
///         53-bit significand, truncated toward zero at 64 bits, with its low
///         16 bits kept; a product outside the signed 64-bit range, or not
///         finite, gives 0
int16_t spark_time_from(double seconds) {
    const double ticks = seconds * spark_ticks_per_second;
    return static_cast<int16_t>(base::game_math::truncate_to_int64(ticks));
}

void store_u32(uint8_t* bytes, uint32_t value) noexcept {
    for (std::size_t i = 0; i < 4; ++i)
        bytes[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xffu);
}

oa_ref32 load_animation(const FeatureDefHost* host, const char* gaf_name) {
    return host != nullptr && host->load_animation != nullptr
               ? host->load_animation(host->context, gaf_name)
               : 0;
}

oa_ref32 load_object(const FeatureDefHost* host, const char* object_name) {
    return host != nullptr && host->load_object != nullptr
               ? host->load_object(host->context, object_name)
               : 0;
}

oa_ref32 find_weapon(const FeatureDefHost* host, const char* weapon_name) {
    return host != nullptr && host->find_weapon != nullptr
               ? host->find_weapon(host->context, weapon_name)
               : 0;
}

// A sequence key's ref within the feature's archive; 0 when the key is absent
// or empty.
oa_ref32 sequence_ref(
    const formats::tdf::Block* section,
    const char* key,
    oa_ref32 animation,
    bool one_shot,
    const FeatureDefHost* host
) {
    char name[0x100];
    if (!tdf_string(section, key, name, sizeof name) || name[0] == '\0')
        return 0;
    if (host == nullptr || host->find_sequence == nullptr)
        return 0;
    name[sizeof name - 1] = '\0';
    return host->find_sequence(host->context, animation, name, one_shot);
}

/// Starts a definition-level cursor on a sequence's first frame.
///
/// Leaves the cursor's reserved bytes as they were.
///
/// @param[out] cursor FeatureDef.animation_cursor or FeatureDef.shadow_cursor
/// @param sequence sequence ref to start
/// @param host resolver giving the first frame's duration and repeat flag; null leaves them zero
void start_cursor(FeatureDefCursor& cursor, oa_ref32 sequence, const FeatureDefHost* host) {
    uint16_t frame_count = 0;
    uint8_t repeat = 0;
    uint16_t duration = 0;
    if (host != nullptr && host->sequence_frame != nullptr)
        host->sequence_frame(host->context, sequence, 0, &frame_count, &repeat, &duration);
    cursor.frame = 0;
    cursor.remaining = duration;
    cursor.repeat = repeat;
    cursor.sequence = sequence;
}

uint16_t flag_bit(int32_t value, unsigned bit) {
    return static_cast<uint16_t>((static_cast<uint32_t>(value) & 1u) << bit);
}

Error missing_feature(std::string_view name) {
    return {
        ErrorCode::missing_feature_definition, "feature definition is missing: " + std::string(name)
    };
}

} // namespace

uint16_t burnt_feature(const FeatureDef& def) noexcept {
    return def.burnt_feature;
}

uint16_t reclamate_feature(const FeatureDef& def) noexcept {
    return def.reclamate_feature;
}

const formats::tdf::Block* find_feature_section(
    std::span<const formats::tdf::OwnedDocument> documents, std::string_view name
) {
    for (const auto& document : documents)
        for (uint32_t index = 0; index < formats::tdf::child_count(document.root()); ++index) {
            const auto* section = formats::tdf::child_at(document.root(), index);
            if (equal_nocase(section->name, name))
                return section;
        }
    return nullptr;
}

uint16_t find_feature_index(std::span<const FeatureDef> defs, std::string_view name) {
    for (std::size_t index = 0; index < defs.size() && index < no_feature_index; ++index)
        if (equal_nocase(bounded(defs[index].name, name_capacity), name))
            return static_cast<uint16_t>(index);
    return no_feature_index;
}

FeatureIndexResult load_feature_def(
    FeatureDefTable& table,
    std::span<const formats::tdf::OwnedDocument> documents,
    std::string_view name,
    const FeatureDefHost* host
) {
    const auto* section = find_feature_section(documents, name);
    if (section == nullptr)
        return {no_feature_index, missing_feature(name)};
    if (table.defs.size() >= no_feature_index)
        return {
            no_feature_index, Error{ErrorCode::feature_table_mismatch, "feature table is full"}
        };
    // Sequence refs of a sprite reusing an earlier GAF resolve through that
    // archive while the record's own animation ref stays 0.
    oa_ref32 sequence_archive = 0;
    FeatureDef def{};
    std::memcpy(def.name, name.data(), std::min(name.size(), name_capacity - 1));
    tdf_string(section, "description", def.description, description_capacity);
    def.footprint_x = static_cast<int16_t>(formats::tdf::get_int(section, "footprintx", 0));
    def.footprint_z = static_cast<int16_t>(formats::tdf::get_int(section, "footprintz", 0));
    def.height = static_cast<int8_t>(formats::tdf::get_int(section, "height", 0));
    char object_name[0x100];
    if (!tdf_string(section, "object", object_name, sizeof object_name) || object_name[0] == '\0') {
        def.flags |= OA_FEATURE_FLAG_SPRITE;
        char gaf_name[0x100];
        tdf_string(section, "filename", gaf_name, sizeof gaf_name);
        bool reused = false;
        for (const auto& earlier : table.defs) {
            if (std::strncmp(earlier.animation_file, gaf_name, animation_file_capacity) != 0)
                continue;
            def.animation = 0;
            sequence_archive = earlier.animation;
            std::memcpy(
                def.animation_file, reused_animation_name.data(), reused_animation_name.size()
            );
            reused = true;
            break;
        }
        if (!reused) {
            gaf_name[sizeof gaf_name - 1] = '\0';
            def.animation = load_animation(host, gaf_name);
            std::memcpy(
                def.animation_file,
                gaf_name,
                std::min(std::strlen(gaf_name), animation_file_capacity)
            );
            sequence_archive = def.animation;
        }
        def.seq_name = sequence_ref(section, "seqname", sequence_archive, false, host);
        def.seq_name_shadow = sequence_ref(section, "seqnameshad", sequence_archive, false, host);
        def.seq_name_burn = sequence_ref(section, "seqnameburn", sequence_archive, true, host);
        def.seq_name_burn_shadow =
            sequence_ref(section, "seqnameburnshad", sequence_archive, true, host);
        def.seq_name_die = sequence_ref(section, "seqnamedie", sequence_archive, true, host);
        def.seq_name_die_shadow =
            sequence_ref(section, "seqnamedieshad", sequence_archive, true, host);
        def.seq_name_reclamate =
            sequence_ref(section, "seqnamereclamate", sequence_archive, true, host);
        def.seq_name_reclamate_shadow =
            sequence_ref(section, "seqnamereclamateshad", sequence_archive, true, host);
    } else {
        def.flags &= static_cast<uint16_t>(~OA_FEATURE_FLAG_SPRITE);
        object_name[sizeof object_name - 1] = '\0';
        store_u32(reinterpret_cast<uint8_t*>(def.animation_file), load_object(host, object_name));
    }
    def.spread_chance = static_cast<int8_t>(formats::tdf::get_int(section, "spreadchance", 0));
    def.reproduce = static_cast<int8_t>(formats::tdf::get_int(section, "reproduce", 0));
    def.reproduce_area = static_cast<int8_t>(formats::tdf::get_int(section, "reproducearea", 0));
    def.metal = static_cast<float>(
        static_cast<uint32_t>(formats::tdf::get_int(section, "metal", 0)) & 0xffffu
    );
    def.energy = static_cast<float>(
        static_cast<uint32_t>(formats::tdf::get_int(section, "energy", 0)) & 0xffffu
    );
    def.damage = static_cast<int16_t>(formats::tdf::get_int(section, "damage", 0));
    def.flags |= flag_bit(formats::tdf::get_int(section, "animating", 0), 1);
    def.flags |= flag_bit(formats::tdf::get_int(section, "animtrans", 0), 2);
    def.flags |= flag_bit(formats::tdf::get_int(section, "shadtrans", 0), 3);
    def.flags |= flag_bit(formats::tdf::get_int(section, "flamable", 0), 4);
    def.flags |= flag_bit(formats::tdf::get_int(section, "geothermal", 0), 5);
    def.flags |= flag_bit(formats::tdf::get_int(section, "blocking", 0), 6);
    def.flags |= flag_bit(formats::tdf::get_int(section, "reclaimable", 0), 7);
    def.flags |= flag_bit(formats::tdf::get_int(section, "autoreclaimable", 1), 8);
    def.flags |= flag_bit(formats::tdf::get_int(section, "indestructible", 0), 9);
    def.flags |= flag_bit(formats::tdf::get_int(section, "nodisplayinfo", 0), 10);
    def.flags |= flag_bit(formats::tdf::get_int(section, "nodrawundergray", 0), 11);
    for (const auto forced : forced_gray_names)
        if (equal_nocase(name, forced))
            def.flags |= flag_forced_no_draw_under_gray;
    def.spark_time = spark_time_from(formats::tdf::get_double(section, "sparktime", 0.0));
    char weapon_name[0x100];
    tdf_string(section, "burnweapon", weapon_name, sizeof weapon_name);
    weapon_name[sizeof weapon_name - 1] = '\0';
    def.burn_weapon = weapon_name[0] != '\0' ? find_weapon(host, weapon_name) : 0;
    if (def.flags & OA_FEATURE_FLAG_ANIMATING) {
        if (def.seq_name != 0)
            start_cursor(def.animation_cursor, def.seq_name, host);
        if (def.seq_name_shadow != 0)
            start_cursor(def.shadow_cursor, def.seq_name_shadow, host);
    }
    // The link pass fills these; until then nothing is linked.
    def.dead_feature = no_feature_index;
    def.burnt_feature = no_feature_index;
    def.reclamate_feature = no_feature_index;
    table.defs.push_back(def);
    return {static_cast<uint16_t>(table.defs.size() - 1), std::nullopt};
}

FeatureIndexResult find_or_load_feature(
    FeatureDefTable& table,
    std::span<const formats::tdf::OwnedDocument> documents,
    std::string_view name,
    const FeatureDefHost* host
) {
    const auto existing = find_feature_index(table, name);
    if (existing != no_feature_index)
        return {existing, std::nullopt};
    return load_feature_def(table, documents, name, host);
}

std::optional<Error> init_feature_table(
    FeatureDefTable& table,
    const formats::tnt::Map& map,
    std::span<const formats::tdf::OwnedDocument> documents,
    const FeatureDefHost* host
) {
    table.defs.clear();
    table.defs.reserve(map.features.size());
    for (const auto& feature : map.features) {
        const auto loaded = load_feature_def(table, documents, feature.name, host);
        if (!loaded.ok())
            return loaded.error;
    }
    return std::nullopt;
}

std::optional<Error> load_feature_links(
    FeatureDefTable& table,
    std::span<const formats::tdf::OwnedDocument> documents,
    const FeatureDefHost* host
) {
    // Definitions appended while linking are linked in turn.
    for (std::size_t index = 0; index < table.defs.size(); ++index) {
        const std::string name(bounded(table.defs[index].name, name_capacity));
        const auto* section = find_feature_section(documents, name);
        if (section == nullptr)
            return missing_feature(name);

        struct Link {
            const char* key{};
            uint16_t FeatureDef::* field{};
        };

        const Link links[] = {
            {"featuredead", &FeatureDef::dead_feature},
            {"featurereclamate", &FeatureDef::reclamate_feature},
            {"featureburnt", &FeatureDef::burnt_feature},
        };
        for (const auto& link : links) {
            char target[0x100];
            uint16_t linked = no_feature_index;
            if (tdf_string(section, link.key, target, sizeof target)) {
                target[sizeof target - 1] = '\0';
                const auto resolved = find_or_load_feature(table, documents, target, host);
                if (!resolved.ok())
                    return resolved.error;
                linked = resolved.index;
            }
            table.defs[index].*link.field = linked;
        }
    }
    return std::nullopt;
}

} // namespace oa::sim::map_runtime
