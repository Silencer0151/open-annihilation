// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/persist/save_sections.hpp"

#include "bank_util.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace oa::data::persist {
namespace {

using detail::load_le16;
using detail::store_le16;

constexpr const char* features_account = save_key::features;
constexpr const char* type_names_blob = save_key::feature_type_names;
constexpr const char* normal_blob = save_key::normal_features;
constexpr const char* animating_blob = save_key::animating_features;
constexpr const char* object_blob = save_key::object_features;
constexpr const char* normal_count_field = save_key::normal_feature_count;
constexpr const char* object_count_field = save_key::object_feature_count;
constexpr const char* animating_count_field = save_key::animating_feature_count;

constexpr uint32_t type_name_bytes = 0x80;
constexpr uint32_t normal_record_bytes = 8;
constexpr uint32_t animating_record_bytes = 10;
constexpr uint32_t object_record_bytes = 0x1a;

// An animating record's last byte packs which sequence is playing, in its low
// nibble, with the high nibble of the spread countdown.
enum class FeatureSequence : uint8_t { burn = 0, die = 1, reclamate = 2 };
constexpr uint8_t sequence_mask = 0x0f;
constexpr uint8_t countdown_high_mask = 0xf0;

// The table may be reallocated while missing types load; re-read it each use.
const FeatureDef* feature_defs(const SaveContext* save) {
    return save->world->feature_defs;
}

/// Returns a placed-feature record of the context.
///
/// @param save save context holding the records
/// @param index record index, as a plot's feature_record word holds it
/// @return the record, or null when the index is not below the context's
///     feature_record_count
uint8_t* feature_record_at(const SaveContext* save, uint16_t index) {
    if (save->feature_records == nullptr || index >= save->feature_record_count)
        return nullptr;
    return save->feature_records + static_cast<std::size_t>(index) * feature_record_bytes;
}

/// Returns the placed-feature record a restored plot's record index names.
///
/// @param save save context holding the records
/// @param plot the plot an animating or object record was just placed on
/// @return the record, or null when the plot holds no feature of its own
///     (the placement was refused, as over a plot hidden under the map's
///     edges) or its index is not below feature_record_count
uint8_t* restored_record(const SaveContext* save, const uint8_t* plot) {
    if (load_le16(plot + plot::feature) >= plot_first_reserved_feature)
        return nullptr;
    return feature_record_at(save, load_le16(plot + plot::feature_record));
}

// Appends one record to the end of a named blob.
void append_record(Bank* bank, const char* blob, const uint8_t* record, uint32_t bytes) {
    bank_open_blob_name(bank, blob);
    bank_blob_seek(bank, bank_blob_size(bank));
    bank_blob_write(bank, record, bytes);
}

uint16_t remap(const int16_t* types, uint32_t type_count, uint16_t saved) {
    return saved < type_count ? static_cast<uint16_t>(types[saved]) : 0xffff;
}

/// Returns how many of a count's records the open blob holds whole.
///
/// Records past the blob's end, or cut short by it, restore nothing, so the
/// load reads only these: each read then starts inside the blob, and a
/// forged count can neither stall the load nor place a record twice.
///
/// @param bank bank with the record blob open
/// @param count the section's count for the blob; below 0 names no record
/// @param record_bytes size of one record
/// @return the records from the first that the load reads, 0 to `count`
int32_t whole_records(const Bank* bank, int32_t count, uint32_t record_bytes) {
    const int32_t held = std::max(bank_blob_size(bank), 0) / static_cast<int32_t>(record_bytes);
    return std::clamp(count, 0, held);
}

} // namespace

void save_write_features(const SaveContext* save, Bank* bank) {
    const Game* game = (&save->world->game);
    bank_open_account(bank, features_account);
    const int32_t def_count = game->feature_def_count;
    const FeatureDef* defs = feature_defs(save);
    const std::size_t names_bytes =
        static_cast<std::size_t>(def_count > 0 ? def_count : 0) * type_name_bytes;
    auto* names = static_cast<char*>(std::calloc(names_bytes != 0 ? names_bytes : 1, 1));
    if (names == nullptr)
        return;
    for (int32_t i = 0; i < def_count; ++i)
        std::strncpy(
            names + static_cast<std::size_t>(i) * type_name_bytes, defs[i].name, type_name_bytes
        );
    bank_open_blob_name(bank, type_names_blob);
    bank_blob_write(bank, names, static_cast<uint32_t>(def_count) << 7);
    std::free(names);

    int32_t normal = 0, objects = 0, animating = 0;
    int32_t x = 0, z = 0;
    const int32_t cells = game->map_height * game->map_width;
    for (int32_t cell = 0; cell < cells; ++cell) {
        const uint8_t* p = save->plots + static_cast<std::size_t>(cell) * plot_bytes;
        const uint16_t type = load_le16(p + plot::feature);
        // A word past the FeatureDef table names no type, so writes no record.
        if (type < plot_first_reserved_feature && static_cast<int32_t>(type) < def_count) {
            const FeatureDef& def = defs[type];
            const uint16_t record_index = load_le16(p + plot::feature_record);
            // An object or animating feature is written from its record.
            const uint8_t* f = feature_record_at(save, record_index);
            if ((def.flags & feature_def_flag_sprite) == 0) {
                if (f != nullptr) {
                    uint8_t record[object_record_bytes];
                    store_le16(record + 0, static_cast<uint16_t>(x));
                    store_le16(record + 2, static_cast<uint16_t>(z));
                    store_le16(record + 4, type);
                    std::memcpy(record + 6, f + feature_record::damage, 2);
                    std::memcpy(
                        record + 8, f + feature_record::position, feature_record::position_bytes
                    );
                    std::memcpy(
                        record + 20,
                        f + feature_record::orientation,
                        feature_record::orientation_bytes
                    );
                    append_record(bank, object_blob, record, object_record_bytes);
                    ++objects;
                }
            } else if ((p[plot::flags] & plot_flag_animating_feature) == 0) {
                uint8_t record[normal_record_bytes];
                store_le16(record + 0, static_cast<uint16_t>(x));
                store_le16(record + 2, static_cast<uint16_t>(z));
                store_le16(record + 4, type);
                store_le16(record + 6, record_index);
                append_record(bank, normal_blob, record, normal_record_bytes);
                ++normal;
            } else if (f != nullptr) {
                uint8_t record[animating_record_bytes];
                store_le16(record + 0, static_cast<uint16_t>(x));
                store_le16(record + 2, static_cast<uint16_t>(z));
                store_le16(record + 4, type);
                std::memcpy(record + 6, f + feature_record::damage, 2);
                record[8] = f[feature_record::frame];
                const uint32_t sequence = detail::load_le32(f + feature_record::sequence);
                bool known = true;
                FeatureSequence playing = FeatureSequence::burn;
                if (sequence == def.seq_name_burn)
                    playing = FeatureSequence::burn;
                else if (sequence == def.seq_name_die)
                    playing = FeatureSequence::die;
                else if (sequence == def.seq_name_reclamate)
                    playing = FeatureSequence::reclamate;
                else
                    known = false;
                if (known) {
                    record[9] = static_cast<uint8_t>(
                        (f[feature_record::spread_countdown] & countdown_high_mask) |
                        static_cast<uint8_t>(playing)
                    );
                    append_record(bank, animating_blob, record, animating_record_bytes);
                    ++animating;
                }
            }
        }
        if (++x >= game->map_width) {
            x = 0;
            ++z;
        }
    }
    bank_set_int(bank, normal_count_field, normal);
    bank_set_int(bank, object_count_field, objects);
    bank_set_int(bank, animating_count_field, animating);
}

void save_read_features(SaveContext* save, Bank* bank) {
    const SaveHooks* hooks = save->hooks;
    void* context = hooks->context;
    bank_open_account(bank, features_account);
    hooks->load_feature_set(context);

    // Saved feature type index -> current FeatureDef index.
    int16_t* types = nullptr;
    uint32_t type_count = 0;
    if (!bank_open_blob_name(bank, type_names_blob)) {
        type_count = static_cast<uint32_t>(
            save->world->game.feature_def_count > 0 ? save->world->game.feature_def_count : 0
        );
        types =
            static_cast<int16_t*>(std::calloc(type_count != 0 ? type_count : 1, sizeof(int16_t)));
        if (types == nullptr)
            return;
        for (uint32_t i = 0; i < type_count; ++i)
            types[i] = static_cast<int16_t>(i);
    } else {
        type_count = static_cast<uint32_t>(bank_blob_size(bank)) >> 7;
        types =
            static_cast<int16_t*>(std::calloc(type_count != 0 ? type_count : 1, sizeof(int16_t)));
        auto* names = static_cast<char*>(
            std::calloc(static_cast<std::size_t>(type_count) * type_name_bytes + 1, 1)
        );
        if (types == nullptr || names == nullptr) {
            std::free(types);
            std::free(names);
            return;
        }
        bank_blob_read(bank, names, type_count << 7);
        for (uint32_t i = 0; i < type_count; ++i) {
            char name[type_name_bytes + 1] = {};
            std::memcpy(
                name, names + static_cast<std::size_t>(i) * type_name_bytes, type_name_bytes
            );
            const FeatureDef* defs = feature_defs(save);
            const auto count = static_cast<uint32_t>(save->world->game.feature_def_count);
            if (i < count && detail::equal_nocase(name, defs[i].name)) {
                types[i] = static_cast<int16_t>(i);
                continue;
            }
            bool found = false;
            for (uint32_t j = 0; j < count; ++j) {
                if (detail::equal_nocase(name, defs[j].name)) {
                    types[i] = static_cast<int16_t>(j);
                    found = true;
                    break;
                }
            }
            if (!found)
                types[i] = hooks->find_or_load_feature(context, name);
        }
        std::free(names);
    }
    hooks->link_feature_set(context);

    // Each blob is read only as far as its count and its whole records reach.
    uint8_t record[object_record_bytes];
    int32_t count = bank_get_int(bank, normal_count_field, 0);
    bank_open_blob_name(bank, normal_blob);
    count = whole_records(bank, count, normal_record_bytes);
    for (int32_t k = 0; k < count; ++k) {
        bank_blob_seek(bank, k * static_cast<int32_t>(normal_record_bytes));
        if (bank_blob_read(bank, record, normal_record_bytes) < normal_record_bytes)
            continue;
        uint8_t* p = save_plot_at(save, load_le16(record), load_le16(record + 2));
        if (p == nullptr)
            continue;
        hooks->place_feature(
            context, p, remap(types, type_count, load_le16(record + 4)), nullptr, nullptr
        );
        std::memcpy(p + plot::feature_record, record + 6, 2);
    }

    count = bank_get_int(bank, animating_count_field, 0);
    bank_open_blob_name(bank, animating_blob);
    count = whole_records(bank, count, animating_record_bytes);
    for (int32_t k = 0; k < count; ++k) {
        bank_blob_seek(bank, k * static_cast<int32_t>(animating_record_bytes));
        if (bank_blob_read(bank, record, animating_record_bytes) < animating_record_bytes)
            continue;
        const uint16_t x = load_le16(record), z = load_le16(record + 2);
        uint8_t* p = save_plot_at(save, x, z);
        if (p == nullptr)
            continue;
        hooks->place_feature(
            context, p, remap(types, type_count, load_le16(record + 4)), nullptr, nullptr
        );
        const auto sequence = static_cast<FeatureSequence>(record[9] & sequence_mask);
        if (sequence == FeatureSequence::burn)
            hooks->burn_feature(context, x, z);
        else if (sequence == FeatureSequence::die)
            hooks->queue_feature_event(context, x, z, 0);
        else if (sequence == FeatureSequence::reclamate)
            hooks->queue_feature_event(context, x, z, 1);
        uint8_t* f = restored_record(save, p);
        if (f == nullptr)
            continue;
        std::memcpy(f + feature_record::damage, record + 6, 2);
        store_le16(f + feature_record::frame, record[8]);
        f[feature_record::spread_countdown] = static_cast<uint8_t>(record[9] & countdown_high_mask);
    }

    count = bank_get_int(bank, object_count_field, 0);
    bank_open_blob_name(bank, object_blob);
    count = whole_records(bank, count, object_record_bytes);
    for (int32_t k = 0; k < count; ++k) {
        bank_blob_seek(bank, k * static_cast<int32_t>(object_record_bytes));
        if (bank_blob_read(bank, record, object_record_bytes) < object_record_bytes)
            continue;
        uint8_t* p = save_plot_at(save, load_le16(record), load_le16(record + 2));
        if (p == nullptr)
            continue;
        hooks->place_feature(
            context, p, remap(types, type_count, load_le16(record + 4)), record + 8, record + 20
        );
        uint8_t* f = restored_record(save, p);
        if (f == nullptr)
            continue;
        std::memcpy(f + feature_record::damage, record + 6, 2);
    }
    std::free(types);
}

} // namespace oa::data::persist
