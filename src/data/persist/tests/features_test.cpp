// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The saved game's Features section, byte for byte: what save_write_features
// writes for a pool of placed features, what save_read_features hands the
// feature hooks and restores into the placed-feature records, for sections
// this engine writes, for one laid out by hand as 3.1c writes it, and for
// malformed ones. A malformed section loads what can be applied and skips the
// rest, and nothing is written outside the records the save context holds.
//
// With --count-bounds the program instead loads blobs under counts far past
// the records they hold, up to the largest a count can be.

#include "check.hpp"

#include "oa/data/persist/hapibank.hpp"
#include "oa/data/persist/save_sections.hpp"
#include "oa/base/text.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

using namespace oa;
using namespace oa::data::persist;

namespace {

using Bytes = std::vector<uint8_t>;

constexpr int32_t map_w = 5, map_h = 4;

// The Features account, its record counts and its blobs.
constexpr const char* features_account = "Features";
constexpr const char* type_names_blob = "Feature Type Names";
constexpr const char* normal_blob = "Normal Features";
constexpr const char* animating_blob = "Animating Features";
constexpr const char* object_blob = "3D Features";
constexpr const char* normal_count_field = "Number of Normal Features";
constexpr const char* animating_count_field = "Number of Animating Features";
constexpr const char* object_count_field = "Number of 3D Features";

// Entry and record sizes of the section, words little-endian: a type name;
// a normal record (cell x, cell z, type, the plot's record word); an
// animating record (cell x, cell z, type, damage, frame byte, packed byte); a
// 3D record (cell x, cell z, type, damage, position, orientation).
constexpr std::size_t type_name_bytes = 0x80;
constexpr std::size_t normal_record_bytes = 8;
constexpr std::size_t animating_record_bytes = 10;
constexpr std::size_t object_record_bytes = 0x1a;

// The last byte of an animating record: the playing sequence in the low
// nibble, under the high nibble of the spread countdown.
constexpr uint8_t sequence_burn = 0, sequence_die = 1, sequence_reclamate = 2;

// queue_feature_event kinds.
constexpr int32_t event_die = 0, event_reclamate = 1;

// The type a record whose saved type the name table lacks is placed with.
constexpr uint16_t no_feature = 0xffff;

// The fixture's feature types, in the order of its FeatureDef table, and the
// sequence references its sprite types animate with.
constexpr uint16_t tree = 0, wreck = 1, rock = 2;
constexpr oa_ref32 tree_burn = 0x101, tree_die = 0x102, tree_reclamate = 0x103;
constexpr oa_ref32 rock_burn = 0x201, rock_die = 0x202, rock_reclamate = 0x203;

// The placed-feature records a match holds, and the guard records after them
// that the save context does not count and nothing may touch.
constexpr uint32_t pool_records = 8;
constexpr uint32_t guard_records = 2;

// Bytes of a record nothing has taken, of a record the fixture's feature
// runtime has just taken, and the spread countdown its ignite sets.
constexpr uint8_t unused_record_fill = 0xee;
constexpr uint8_t taken_record_fill = 0x5a;
constexpr uint8_t ignite_countdown = 0x75;

/// Stores a little-endian 16-bit word.
void store16(uint8_t* p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
}

/// Stores a little-endian 32-bit word.
void store32(uint8_t* p, uint32_t value) {
    store16(p, static_cast<uint16_t>(value));
    store16(p + 2, static_cast<uint16_t>(value >> 16));
}

/// Reads a little-endian 16-bit word.
uint16_t load16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

/// Formats bytes as lower-case hexadecimal pairs.
std::string hex(const uint8_t* bytes, std::size_t count) {
    std::string out;
    char pair[3] = {};
    for (std::size_t i = 0; i < count; ++i) {
        std::snprintf(pair, sizeof pair, "%02x", bytes[i]);
        out += pair;
    }
    return out;
}

/// Returns the fixture's FeatureDef table: tree and rock sprites that burn,
/// die and are reclaimed, and a wreck object.
std::vector<FeatureDef> fixture_defs() {
    std::vector<FeatureDef> defs(3);
    oa::base::text::copy_terminated(defs[tree].name, "Tree");
    defs[tree].flags = feature_def_flag_sprite;
    defs[tree].seq_name_burn = tree_burn;
    defs[tree].seq_name_die = tree_die;
    defs[tree].seq_name_reclamate = tree_reclamate;
    oa::base::text::copy_terminated(defs[wreck].name, "Wreck");
    oa::base::text::copy_terminated(defs[rock].name, "Rock");
    defs[rock].flags = feature_def_flag_sprite;
    defs[rock].seq_name_burn = rock_burn;
    defs[rock].seq_name_die = rock_die;
    defs[rock].seq_name_reclamate = rock_reclamate;
    return defs;
}

enum class Sequence : uint8_t { burn, die, reclamate };

/// A map, a placed-feature pool and feature hooks that place features, ignite
/// them and start their die and reclamate sequences much as the feature
/// runtime does, logging every call.
///
/// A sprite's placement sets its plot's record word to 0 and clears its
/// animating flag; an object's takes a record, as do ignite and the sequence
/// starts, sets the plot's word to it and sets the flag. A new record is
/// filled with taken_record_fill, so the bytes the load leaves alone keep that
/// fill. The hooks leave the plot's player bits as they are.
struct Match {
    std::unique_ptr<World> world_state = std::make_unique<World>();
    std::vector<FeatureDef> defs = fixture_defs();
    Bytes plots = Bytes(static_cast<std::size_t>(map_w * map_h) * plot_bytes);
    Bytes pool;              // the counted records, then the guard records
    uint32_t record_count{}; // records the save context holds
    uint32_t next_record{};  // the record the next placement or sequence start takes
    bool runtime_refuses{};  // every placement and sequence start fails, as with no free record
    bool loads_missing_types = true;
    SaveHooks hooks{};
    SaveContext save{};
    std::vector<std::string> log;

    explicit Match(uint32_t records = pool_records, uint32_t guard = guard_records)
        : pool(
              static_cast<std::size_t>(records + guard) * feature_record_bytes, unused_record_fill
          ),
          record_count(records) {
        world_state->game.map_width = map_w;
        world_state->game.map_height = map_h;
        bind_defs();
        for (std::size_t cell = 0; cell < plots.size() / plot_bytes; ++cell)
            store16(plots.data() + cell * plot_bytes + plot::feature, no_feature);
        hooks.context = this;
        hooks.load_feature_set = [](void* c) { of(c).log.push_back("load set"); };
        hooks.find_or_load_feature = find_or_load;
        hooks.link_feature_set = [](void* c) { of(c).log.push_back("link set"); };
        hooks.place_feature = place;
        hooks.burn_feature = [](void* c, uint16_t x, uint16_t z) {
            of(c).log.push_back("burn " + cell_name(x, z));
            of(c).start_sequence(x, z, Sequence::burn);
        };
        hooks.queue_feature_event = [](void* c, uint16_t x, uint16_t z, int32_t kind) {
            Match& m = of(c);
            if (kind == event_die) {
                m.log.push_back("die " + cell_name(x, z));
                m.start_sequence(x, z, Sequence::die);
            } else if (kind == event_reclamate) {
                m.log.push_back("reclamate " + cell_name(x, z));
                m.start_sequence(x, z, Sequence::reclamate);
            } else {
                m.log.push_back("event " + cell_name(x, z) + " " + std::to_string(kind));
            }
        };
        save.world = world_state.get();
        save.plots = plots.data();
        save.feature_records = pool.empty() ? nullptr : pool.data();
        save.hooks = &hooks;
        save.feature_record_count = records;
    }

    Match(const Match&) = delete;
    Match& operator=(const Match&) = delete;

    /// Returns the plot of a cell.
    uint8_t* plot_at(int32_t x, int32_t z) { return save_plot_at(&save, x, z); }

    /// Returns a record of the pool, guard records included.
    uint8_t* record(uint32_t index) {
        return pool.data() + static_cast<std::size_t>(index) * feature_record_bytes;
    }

    /// Returns a copy of a record of the pool.
    Bytes record_bytes(uint32_t index) {
        return Bytes(record(index), record(index) + feature_record_bytes);
    }

    /// Puts a type and record word on a plot, with its animating flag.
    void set_plot(int32_t x, int32_t z, uint16_t type, uint16_t record_word, bool animating) {
        uint8_t* p = plot_at(x, z);
        store16(p + plot::feature, type);
        store16(p + plot::feature_record, record_word);
        p[plot::flags] = animating ? plot_flag_animating_feature : 0;
    }

    /// Points the World and Game at the current FeatureDef table.
    void bind_defs() {
        world_state->feature_defs = defs.data();
        world_state->feature_def_count = static_cast<uint32_t>(defs.size());
        world_state->game.feature_def_count = static_cast<int32_t>(defs.size());
        world_state->game.feature_defs = oa_ref_from_index(0);
    }

    /// Returns true when no record from `first` on, guard records included,
    /// has been written.
    bool untouched_from(uint32_t first) {
        return std::all_of(
            pool.begin() + static_cast<std::ptrdiff_t>(first * feature_record_bytes),
            pool.end(),
            [](uint8_t b) { return b == unused_record_fill; }
        );
    }

    /// Returns the match a hook's context names.
    static Match& of(void* context) { return *static_cast<Match*>(context); }

    /// Names a cell as "x,z".
    static std::string cell_name(int32_t x, int32_t z) {
        return std::to_string(x) + "," + std::to_string(z);
    }

    /// Takes the next free record for a plot and fills it as a new record.
    ///
    /// @return the record, or null when none is free; the plot is then unchanged
    uint8_t* take_record(uint8_t* plot) {
        if (next_record >= record_count)
            return nullptr;
        uint8_t* taken = record(next_record);
        std::memset(taken, taken_record_fill, feature_record_bytes);
        store16(plot + plot::feature_record, static_cast<uint16_t>(next_record));
        ++next_record;
        return taken;
    }

    /// Logs a placement and places the type as the feature runtime does.
    static void place(
        void* c, uint8_t* plot, uint16_t type, const uint8_t* position, const uint8_t* orientation
    ) {
        Match& m = of(c);
        const auto cell = static_cast<int32_t>((plot - m.plots.data()) / plot_bytes);
        std::string line =
            "place " + cell_name(cell % map_w, cell / map_w) + " type " + std::to_string(type);
        if (position != nullptr)
            line += " at " + hex(position, feature_record::position_bytes);
        if (orientation != nullptr)
            line += " facing " + hex(orientation, feature_record::orientation_bytes);
        m.log.push_back(line);
        if (m.runtime_refuses || type >= m.defs.size())
            return;
        if ((m.defs[type].flags & feature_def_flag_sprite) == 0) {
            uint8_t* taken = m.take_record(plot);
            if (taken == nullptr)
                return;
            if (position != nullptr)
                std::memcpy(
                    taken + feature_record::position, position, feature_record::position_bytes
                );
            if (orientation != nullptr)
                std::memcpy(
                    taken + feature_record::orientation,
                    orientation,
                    feature_record::orientation_bytes
                );
            plot[plot::flags] =
                static_cast<uint8_t>(plot[plot::flags] | plot_flag_animating_feature);
        } else {
            store16(plot + plot::feature_record, 0);
            plot[plot::flags] =
                static_cast<uint8_t>(plot[plot::flags] & ~plot_flag_animating_feature);
        }
        store16(plot + plot::feature, type);
    }

    /// Starts a sequence on the sprite a plot holds, in a newly taken record,
    /// as ignite and the die and reclamate starts do.
    void start_sequence(uint16_t x, uint16_t z, Sequence sequence) {
        uint8_t* plot = plot_at(x, z);
        if (plot == nullptr || runtime_refuses)
            return;
        const uint16_t type = load16(plot + plot::feature);
        if (type >= defs.size())
            return;
        uint8_t* taken = take_record(plot);
        if (taken == nullptr)
            return;
        const FeatureDef& def = defs[type];
        const oa_ref32 name = sequence == Sequence::burn  ? def.seq_name_burn
                              : sequence == Sequence::die ? def.seq_name_die
                                                          : def.seq_name_reclamate;
        store32(taken + feature_record::sequence, name);
        store16(taken + feature_record::frame, 0);
        if (sequence == Sequence::burn)
            taken[feature_record::spread_countdown] = ignite_countdown;
        plot[plot::flags] = static_cast<uint8_t>(plot[plot::flags] | plot_flag_animating_feature);
    }

    /// Logs a lookup and, unless loads_missing_types is false, loads the type
    /// as a sprite at the end of a table that moves to new storage, as the
    /// runtime's table may.
    static int16_t find_or_load(void* c, const char* name) {
        Match& m = of(c);
        m.log.push_back(std::string("find ") + name);
        if (!m.loads_missing_types)
            return -1;
        std::vector<FeatureDef> grown(m.defs.size() + 1);
        std::copy(m.defs.begin(), m.defs.end(), grown.begin());
        oa::base::text::copy_padded(grown.back().name, name, sizeof grown.back().name - 1);
        grown.back().flags = feature_def_flag_sprite;
        m.defs.swap(grown); // the old table is freed on return
        m.bind_defs();
        return static_cast<int16_t>(m.defs.size() - 1);
    }
};

struct ScopedBank {
    Bank bank{};

    ScopedBank() {
        bank_init(&bank);
        bank_reset(&bank);
    }

    ~ScopedBank() { bank_destroy(&bank); }

    ScopedBank(const ScopedBank&) = delete;
    ScopedBank& operator=(const ScopedBank&) = delete;

    Bank* get() { return &bank; }
};

/// Copies a bank through a file image, as a saved game is read back, so that
/// every blob starts at position zero.
void reload(Bank* from, ScopedBank& to) {
    ByteImage image{};
    CHECK(bank_write_image(from, savegame_description, true, &image));
    BankError error{};
    CHECK(bank_read_image(to.get(), image.data, image.size, savegame_description, nullptr, &error));
    byte_image_free(&image);
}

/// Returns a blob of the Features account, or nothing when it is absent or empty.
Bytes read_blob(Bank* bank, const char* name) {
    bank_open_account(bank, features_account);
    if (!bank_open_blob_name(bank, name))
        return {};
    Bytes out(static_cast<std::size_t>(bank_blob_size(bank)));
    bank_blob_seek(bank, 0);
    bank_blob_read(bank, out.data(), static_cast<uint32_t>(out.size()));
    return out;
}

/// Returns a count of the Features account, or -1 when it is absent.
int32_t read_count(Bank* bank, const char* field) {
    bank_open_account(bank, features_account);
    return bank_get_int(bank, field, -1);
}

/// Appends bytes to a blob of the Features account.
void put_blob(Bank* bank, const char* name, const Bytes& bytes) {
    bank_open_account(bank, features_account);
    bank_open_blob_name(bank, name);
    bank_blob_seek(bank, bank_blob_size(bank));
    bank_blob_write(bank, bytes.data(), static_cast<uint32_t>(bytes.size()));
}

/// Sets a count of the Features account.
void put_count(Bank* bank, const char* field, int32_t count) {
    bank_open_account(bank, features_account);
    bank_set_int(bank, field, count);
}

/// Reloads a built section through a file image and restores it into a match.
void load(Bank* built, Match& match) {
    ScopedBank loaded;
    reload(built, loaded);
    save_read_features(&match.save, loaded.get());
}

/// Lays out a name table: each name in a zero-filled type_name_bytes entry.
Bytes type_name_table(std::initializer_list<const char*> names) {
    Bytes table(names.size() * type_name_bytes);
    std::size_t entry = 0;
    for (const char* name : names)
        std::memcpy(table.data() + entry++ * type_name_bytes, name, std::strlen(name));
    return table;
}

/// Lays out a normal record.
Bytes normal_record(uint16_t x, uint16_t z, uint16_t type, uint16_t record_word) {
    Bytes out(normal_record_bytes);
    store16(out.data(), x);
    store16(out.data() + 2, z);
    store16(out.data() + 4, type);
    store16(out.data() + 6, record_word);
    return out;
}

/// Lays out an animating record.
Bytes animating_record(
    uint16_t x, uint16_t z, uint16_t type, uint16_t damage, uint8_t frame, uint8_t packed
) {
    Bytes out(animating_record_bytes);
    store16(out.data(), x);
    store16(out.data() + 2, z);
    store16(out.data() + 4, type);
    store16(out.data() + 6, damage);
    out[8] = frame;
    out[9] = packed;
    return out;
}

// A position (X, Y, Z, 16.16) and orientation (three angles) of a 3DO wreck,
// as the placed-feature record and the 3D record lay them out.
const Bytes wreck_position{0x56, 0x34, 0x12, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x80, 0xfe, 0xff};
const Bytes wreck_orientation{0x00, 0x40, 0x10, 0x00, 0x00, 0xc0};
// The same bytes as a placement's log line shows them.
const std::string wreck_at = "56341200000002000080feff";
const std::string wreck_facing = "0040100000c0";

/// Lays out a 3D record.
Bytes object_record(
    uint16_t x,
    uint16_t z,
    uint16_t type,
    uint16_t damage,
    const Bytes& position = wreck_position,
    const Bytes& orientation = wreck_orientation
) {
    Bytes out(object_record_bytes);
    store16(out.data(), x);
    store16(out.data() + 2, z);
    store16(out.data() + 4, type);
    store16(out.data() + 6, damage);
    std::copy(position.begin(), position.end(), out.begin() + 8);
    std::copy(orientation.begin(), orientation.end(), out.begin() + 20);
    return out;
}

/// Joins records into one blob.
Bytes join(std::initializer_list<Bytes> records) {
    Bytes out;
    for (const Bytes& r : records)
        out.insert(out.end(), r.begin(), r.end());
    return out;
}

/// Returns a record as the fixture's runtime leaves it after a sequence start
/// and the load then restores it.
Bytes restored_sprite_record(
    oa_ref32 sequence, uint8_t countdown, uint16_t damage, uint16_t frame
) {
    Bytes out(feature_record_bytes, taken_record_fill);
    store32(out.data() + feature_record::sequence, sequence);
    store16(out.data() + feature_record::frame, frame);
    out[feature_record::spread_countdown] = countdown;
    store16(out.data() + feature_record::damage, damage);
    return out;
}

/// Returns a record as the fixture's runtime places a 3DO feature and the
/// load then restores its damage.
Bytes restored_object_record(const Bytes& position, const Bytes& orientation, uint16_t damage) {
    Bytes out(feature_record_bytes, taken_record_fill);
    std::copy(position.begin(), position.end(), out.begin() + feature_record::position);
    std::copy(orientation.begin(), orientation.end(), out.begin() + feature_record::orientation);
    store16(out.data() + feature_record::damage, damage);
    return out;
}

/// Stages the pool the round trip saves: every record byte first set from
/// `noise`, then the fields the section carries.
///
/// Row order: (0,0) a tree burning with 0x75 ticks left; (1,0) no feature;
/// (2,0) a tree burning with 0x0c ticks left; (3,0) a rock dying, its record
/// keeping a countdown of 0x37 from an earlier use; (4,0) a tree being
/// reclaimed; (0,1) a tree that does not animate, whose record word holds
/// 0x40; (1,1) the first reserved type, no feature of its own; (2,2) a wreck.
void stage_pool(Match& f, uint32_t noise) {
    for (std::size_t i = 0; i < f.pool.size(); ++i)
        f.pool[i] = static_cast<uint8_t>(i * noise + 11);
    auto sprite =
        [&](uint32_t index, oa_ref32 sequence, uint16_t frame, uint16_t damage, uint8_t countdown) {
            uint8_t* r = f.record(index);
            store32(r + feature_record::sequence, sequence);
            store16(r + feature_record::frame, frame);
            store16(r + feature_record::damage, damage);
            r[feature_record::spread_countdown] = countdown;
        };
    f.set_plot(0, 0, tree, 2, true);
    sprite(2, tree_burn, 0x0105, 0x0123, 0x75);
    f.set_plot(2, 0, tree, 0, true);
    sprite(0, tree_burn, 0x0003, 0x0010, 0x0c);
    f.set_plot(3, 0, rock, 5, true);
    sprite(5, rock_die, 0x000a, 0x0200, 0x37);
    f.set_plot(4, 0, tree, 1, true);
    sprite(1, tree_reclamate, 0x0007, 0x0000, 0x00);
    f.set_plot(0, 1, tree, 0x40, false);
    f.plot_at(0, 1)[plot::flags] = plot_flags_player_features;
    f.set_plot(1, 1, plot_first_reserved_feature, 3, true);
    f.set_plot(2, 2, wreck, 4, true);
    uint8_t* object = f.record(4);
    std::copy(wreck_position.begin(), wreck_position.end(), object + feature_record::position);
    std::copy(
        wreck_orientation.begin(), wreck_orientation.end(), object + feature_record::orientation
    );
    store16(object + feature_record::damage, 0x0321);
}

/// Saves a pool of placed features and checks every byte of the section;
/// loads it and checks the hook calls and restored records; saves the loaded
/// state and gets the same section again.
void pool_round_trip() {
    Match f;
    stage_pool(f, 37);
    ScopedBank bank;
    save_write_features(&f.save, bank.get());

    CHECK(read_count(bank.get(), normal_count_field) == 1);
    CHECK(read_count(bank.get(), animating_count_field) == 4);
    CHECK(read_count(bank.get(), object_count_field) == 1);
    const Bytes names = read_blob(bank.get(), type_names_blob);
    CHECK(names == type_name_table({"Tree", "Wreck", "Rock"}));
    const Bytes normal = read_blob(bank.get(), normal_blob);
    CHECK((normal == Bytes{0, 0, 1, 0, 0, 0, 0x40, 0}));
    const Bytes animating = read_blob(bank.get(), animating_blob);
    CHECK(animating.size() == 4 * animating_record_bytes);
    // The frame keeps its low byte; the last byte packs the countdown's high
    // nibble over the sequence.
    CHECK(
        animating == join({
                         animating_record(0, 0, tree, 0x0123, 0x05, 0x70 | sequence_burn),
                         animating_record(2, 0, tree, 0x0010, 0x03, 0x00 | sequence_burn),
                         animating_record(3, 0, rock, 0x0200, 0x0a, 0x30 | sequence_die),
                         animating_record(4, 0, tree, 0x0000, 0x07, 0x00 | sequence_reclamate),
                     })
    );
    if (animating.size() == 4 * animating_record_bytes) {
        CHECK(animating[9] == 0x70);
        CHECK(animating[animating_record_bytes + 9] == 0x00);
        CHECK(animating[2 * animating_record_bytes + 9] == 0x31);
        CHECK(animating[3 * animating_record_bytes + 9] == 0x02);
    }
    const Bytes objects = read_blob(bank.get(), object_blob);
    CHECK((objects == Bytes{2,    0,    2,    0,    1,    0,    0x21, 0x03, 0x56,
                            0x34, 0x12, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x80,
                            0xfe, 0xff, 0x00, 0x40, 0x10, 0x00, 0x00, 0xc0}));

    // Only the fields the section names reach it: other record bytes differ
    // here and the section is the same.
    Match other;
    stage_pool(other, 91);
    ScopedBank other_bank;
    save_write_features(&other.save, other_bank.get());
    for (const char* blob : {type_names_blob, normal_blob, animating_blob, object_blob})
        CHECK(read_blob(other_bank.get(), blob) == read_blob(bank.get(), blob));

    Match g;
    load(bank.get(), g);
    // Normal records first, then animating ones, each placement followed by
    // its sequence start, then 3D ones, which get their position and
    // orientation.
    const std::vector<std::string> expected{
        "load set",
        "link set",
        "place 0,1 type 0",
        "place 0,0 type 0",
        "burn 0,0",
        "place 2,0 type 0",
        "burn 2,0",
        "place 3,0 type 2",
        "die 3,0",
        "place 4,0 type 0",
        "reclamate 4,0",
        "place 2,2 type 1 at " + wreck_at + " facing " + wreck_facing,
    };
    CHECK(g.log == expected);
    // The runtime took records 0..4 in that order; the load replaced ignite's
    // countdown and the stale one with the saved high nibble, and the frame
    // with its saved low byte.
    CHECK(g.record_bytes(0) == restored_sprite_record(tree_burn, 0x70, 0x0123, 0x0005));
    CHECK(g.record_bytes(1) == restored_sprite_record(tree_burn, 0x00, 0x0010, 0x0003));
    CHECK(g.record_bytes(2) == restored_sprite_record(rock_die, 0x30, 0x0200, 0x000a));
    CHECK(g.record_bytes(3) == restored_sprite_record(tree_reclamate, 0x00, 0x0000, 0x0007));
    CHECK(g.record_bytes(4) == restored_object_record(wreck_position, wreck_orientation, 0x0321));
    CHECK(g.untouched_from(5));
    // The loaded plots animate where the saved ones did, the wreck included.
    for (auto [x, z] :
         {std::array<int32_t, 2>{0, 0},
          std::array<int32_t, 2>{2, 0},
          std::array<int32_t, 2>{3, 0},
          std::array<int32_t, 2>{4, 0},
          std::array<int32_t, 2>{2, 2}})
        CHECK((g.plot_at(x, z)[plot::flags] & plot_flag_animating_feature) != 0);
    CHECK((g.plot_at(0, 1)[plot::flags] & plot_flag_animating_feature) == 0);
    CHECK(load16(g.plot_at(0, 1) + plot::feature) == tree);
    CHECK(load16(g.plot_at(0, 1) + plot::feature_record) == 0x40);
    CHECK(load16(g.plot_at(1, 0) + plot::feature) == no_feature);
    CHECK(load16(g.plot_at(1, 1) + plot::feature) == no_feature);

    ScopedBank again;
    save_write_features(&g.save, again.get());
    for (const char* blob : {type_names_blob, normal_blob, animating_blob, object_blob})
        CHECK(read_blob(again.get(), blob) == read_blob(bank.get(), blob));
    for (const char* field : {normal_count_field, animating_count_field, object_count_field})
        CHECK(read_count(again.get(), field) == read_count(bank.get(), field));
}

/// Returns the bytes of a two-dimensional table of records.
template <std::size_t Rows, std::size_t Columns>
Bytes rows(const uint8_t (&table)[Rows][Columns]) {
    return Bytes(&table[0][0], &table[0][0] + Rows * Columns);
}

/// Loads a Features section laid out by hand, record by record, as 3.1c
/// writes it, into a game whose FeatureDef table is ordered differently and
/// lacks one saved type.
///
/// The saving game's types were Tree, Boulder, Wreck and Rock; the loading
/// game holds Rock, Tree and Wreck, and loads Boulder when asked, which moves
/// its table. A burning record carries its countdown's high nibble (a fire
/// set from stock data has 75 to 149 ticks, 0x40 to 0x90); a die record
/// carries whatever countdown its record kept from an earlier use.
void section_as_3_1c_writes_it() {
    // Each name fills an entry up to its terminator; what follows is not read.
    Bytes names(4 * type_name_bytes);
    std::memcpy(names.data() + 0 * type_name_bytes, "Tree", 4);
    std::memcpy(names.data() + 1 * type_name_bytes, "Boulder", 7);
    std::memcpy(names.data() + 2 * type_name_bytes, "Wreck\0\xcd\xcd\xcd", 9);
    std::memcpy(names.data() + 3 * type_name_bytes, "Rock", 4);
    const uint8_t normal[][normal_record_bytes] = {
        {1, 0, 0, 0, 3, 0, 0x20, 0x01}, // (1,0) Rock; its record word holds 0x120 damage
        {4, 0, 3, 0, 1, 0, 0x00, 0x00}, // (4,3) Boulder
    };
    const uint8_t animating[][animating_record_bytes] = {
        {0, 0, 0, 0, 0, 0, 0x05, 0x00, 0x0c, 0x90}, // (0,0) Tree burning, 0x90..0x9f ticks left
        {2, 0, 1, 0, 0, 0, 0x00, 0x00, 0x02, 0x40}, // (2,1) Tree burning, 0x40..0x4f ticks left
        {3, 0, 1, 0, 3, 0, 0x80, 0x00, 0x04, 0x21}, // (3,1) Rock dying
        {0, 0, 2, 0, 0, 0, 0x10, 0x00, 0x00, 0x02}, // (0,2) Tree being reclaimed
    };
    // (2,2) Wreck, damage 0x400, at X 0xa0, Y 0x10, Z 0x62.8, facing 0x2000, 0, 0xffff.
    const uint8_t objects[][object_record_bytes] = {
        {2,    0,    2,    0,    2,    0,    0x00, 0x04, 0x00, 0x00, 0xa0, 0x00, 0x00,
         0x00, 0x10, 0x00, 0x00, 0x80, 0x62, 0x00, 0x00, 0x20, 0x00, 0x00, 0xff, 0xff},
    };
    ScopedBank bank;
    put_blob(bank.get(), type_names_blob, names);
    put_count(bank.get(), normal_count_field, 2);
    put_blob(bank.get(), normal_blob, rows(normal));
    put_count(bank.get(), animating_count_field, 4);
    put_blob(bank.get(), animating_blob, rows(animating));
    put_count(bank.get(), object_count_field, 1);
    put_blob(bank.get(), object_blob, rows(objects));

    Match g;
    const std::vector<FeatureDef> saved_order = fixture_defs();
    g.defs = {saved_order[rock], saved_order[tree], saved_order[wreck]};
    g.bind_defs();
    constexpr uint16_t now_rock = 0, now_tree = 1, now_wreck = 2, now_boulder = 3;
    load(bank.get(), g);
    const std::vector<std::string> expected{
        "load set",
        "find Boulder",
        "link set",
        "place 1,0 type 0",
        "place 4,3 type 3",
        "place 0,0 type 1",
        "burn 0,0",
        "place 2,1 type 1",
        "burn 2,1",
        "place 3,1 type 0",
        "die 3,1",
        "place 0,2 type 1",
        "reclamate 0,2",
        "place 2,2 type 2 at 0000a0000000100000806200 facing 00200000ffff",
    };
    CHECK(g.log == expected);
    CHECK(g.defs.size() == 4 && std::strcmp(g.defs[now_boulder].name, "Boulder") == 0);
    CHECK(g.world_state->game.feature_def_count == 4);

    // Each sequence start took the next record; the load then gave it the
    // saved damage, the frame byte and the countdown's saved high nibble,
    // and left the rest as the start made it.
    CHECK(g.record_bytes(0) == restored_sprite_record(tree_burn, 0x90, 0x0005, 0x000c));
    CHECK(g.record_bytes(1) == restored_sprite_record(tree_burn, 0x40, 0x0000, 0x0002));
    CHECK(g.record_bytes(2) == restored_sprite_record(rock_die, 0x20, 0x0080, 0x0004));
    CHECK(g.record_bytes(3) == restored_sprite_record(tree_reclamate, 0x00, 0x0010, 0x0000));
    const Bytes position{0x00, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x80, 0x62, 0x00};
    const Bytes orientation{0x00, 0x20, 0x00, 0x00, 0xff, 0xff};
    CHECK(g.record_bytes(4) == restored_object_record(position, orientation, 0x0400));
    CHECK(g.untouched_from(5));

    CHECK(load16(g.plot_at(1, 0) + plot::feature) == now_rock);
    CHECK(load16(g.plot_at(1, 0) + plot::feature_record) == 0x0120);
    CHECK(load16(g.plot_at(4, 3) + plot::feature) == now_boulder);
    CHECK(load16(g.plot_at(4, 3) + plot::feature_record) == 0);
    for (auto [x, z, type, index] :
         {std::array<int32_t, 4>{0, 0, now_tree, 0},
          std::array<int32_t, 4>{2, 1, now_tree, 1},
          std::array<int32_t, 4>{3, 1, now_rock, 2},
          std::array<int32_t, 4>{0, 2, now_tree, 3}}) {
        const uint8_t* p = g.plot_at(x, z);
        CHECK(load16(p + plot::feature) == type && load16(p + plot::feature_record) == index);
        CHECK((p[plot::flags] & plot_flag_animating_feature) != 0);
    }
    CHECK(load16(g.plot_at(2, 2) + plot::feature) == now_wreck);
    CHECK(load16(g.plot_at(2, 2) + plot::feature_record) == 4);
}

/// Loads normal records that give cells record words below, at, just past
/// and far past the pool's record count, then animating and 3D records for
/// those cells whose placements fail, so the words stay as the file set them.
///
/// Every placement is still asked for and every sequence still starts. Where
/// each cell already holds a tree that the refused placements leave in
/// place, as an indestructible feature stays, only records below the count
/// take the saved damage, frame and countdown. Where the cells hold no
/// feature, as over plots a load hides under the map's edges, no record
/// takes them: a plot without a feature of its own names no record of its
/// own, so the load writes no record data through its word.
///
/// @param records records the save context holds; 0 gives it none at all
/// @param guard records after them that nothing may touch; with 0 the pool's
///     storage ends right after its last record
/// @param cells_hold_features each cell holds a tree before the load
void record_index_bounds(uint32_t records, uint32_t guard, bool cells_hold_features) {
    ScopedBank bank;
    put_blob(bank.get(), type_names_blob, type_name_table({"Tree", "Wreck", "Rock"}));
    put_count(bank.get(), normal_count_field, 8);
    put_blob(
        bank.get(),
        normal_blob,
        join({
            normal_record(0, 0, tree, 7), // the last record
            normal_record(1, 0, tree, 8), // at the count
            normal_record(2, 0, tree, 9), // just past it
            normal_record(3, 0, tree, 0xffff),
            normal_record(0, 1, tree, 6),
            normal_record(1, 1, tree, 8),
            normal_record(2, 1, tree, 9),
            normal_record(3, 1, tree, 0x8000),
        })
    );
    put_count(bank.get(), animating_count_field, 4);
    put_blob(
        bank.get(),
        animating_blob,
        join({
            animating_record(0, 0, tree, 0x0101, 1, 0x70 | sequence_burn),
            animating_record(1, 0, tree, 0x0202, 2, 0x50 | sequence_die),
            animating_record(2, 0, tree, 0x0303, 3, 0x60 | sequence_reclamate),
            animating_record(3, 0, tree, 0x0404, 4, 0x30 | sequence_burn),
        })
    );
    put_count(bank.get(), object_count_field, 4);
    put_blob(
        bank.get(),
        object_blob,
        join({
            object_record(0, 1, wreck, 0x0a0a),
            object_record(1, 1, wreck, 0x0b0b),
            object_record(2, 1, wreck, 0x0c0c),
            object_record(3, 1, wreck, 0x0d0d),
        })
    );

    Match g(records, guard);
    g.runtime_refuses = true;
    if (cells_hold_features)
        for (int32_t i = 0; i < 8; ++i)
            g.set_plot(i % 4, i / 4, tree, 0, false);
    load(bank.get(), g);
    const std::string object_tail = " type 1 at " + wreck_at + " facing " + wreck_facing;
    const std::vector<std::string> expected{
        "load set",
        "link set",
        "place 0,0 type 0",
        "place 1,0 type 0",
        "place 2,0 type 0",
        "place 3,0 type 0",
        "place 0,1 type 0",
        "place 1,1 type 0",
        "place 2,1 type 0",
        "place 3,1 type 0",
        "place 0,0 type 0",
        "burn 0,0",
        "place 1,0 type 0",
        "die 1,0",
        "place 2,0 type 0",
        "reclamate 2,0",
        "place 3,0 type 0",
        "burn 3,0",
        "place 0,1" + object_tail,
        "place 1,1" + object_tail,
        "place 2,1" + object_tail,
        "place 3,1" + object_tail,
    };
    CHECK(g.log == expected);
    const uint16_t words[] = {7, 8, 9, 0xffff, 6, 8, 9, 0x8000};
    for (int32_t i = 0; i < 8; ++i)
        CHECK(load16(g.plot_at(i % 4, i / 4) + plot::feature_record) == words[i]);
    for (int32_t i = 0; i < 8; ++i)
        CHECK(
            load16(g.plot_at(i % 4, i / 4) + plot::feature) ==
            (cells_hold_features ? tree : no_feature)
        );

    if (records == pool_records && cells_hold_features) {
        Bytes last(feature_record_bytes, unused_record_fill);
        store16(last.data() + feature_record::damage, 0x0101);
        store16(last.data() + feature_record::frame, 1);
        last[feature_record::spread_countdown] = 0x70;
        CHECK(g.record_bytes(7) == last);
        Bytes object(feature_record_bytes, unused_record_fill);
        store16(object.data() + feature_record::damage, 0x0a0a);
        CHECK(g.record_bytes(6) == object);
        CHECK(std::all_of(g.pool.begin(), g.pool.begin() + 6 * feature_record_bytes, [](uint8_t b) {
            return b == unused_record_fill;
        }));
        CHECK(g.untouched_from(pool_records));
    } else {
        // No record in the context, or no cell with a feature to restore onto.
        CHECK(g.untouched_from(0));
    }
}

/// Saves plots whose animating features, then object features, name records
/// below, at, just past and far past the pool's record count. Only those
/// below the count are written, though the guard records past it hold what a
/// live fire or wreck would; a normal record carries its plot's word whatever
/// it holds.
void writer_record_index_bounds() {
    Match fires;
    for (uint32_t index : {7u, 8u, 9u}) {
        uint8_t* r = fires.record(index);
        store32(r + feature_record::sequence, tree_burn);
        store16(r + feature_record::frame, static_cast<uint16_t>(index));
        store16(r + feature_record::damage, static_cast<uint16_t>(0x100 + index));
        r[feature_record::spread_countdown] = 0x75;
    }
    fires.set_plot(0, 0, tree, 7, true);
    fires.set_plot(1, 0, tree, 8, true);
    fires.set_plot(2, 0, tree, 9, true);
    fires.set_plot(3, 0, tree, 0xffff, true);
    fires.set_plot(4, 0, tree, 0xffff, false);
    ScopedBank bank;
    save_write_features(&fires.save, bank.get());
    CHECK(read_count(bank.get(), normal_count_field) == 1);
    CHECK(read_count(bank.get(), animating_count_field) == 1);
    CHECK(read_count(bank.get(), object_count_field) == 0);
    CHECK(read_blob(bank.get(), normal_blob) == normal_record(4, 0, tree, 0xffff));
    CHECK(
        read_blob(bank.get(), animating_blob) ==
        animating_record(0, 0, tree, 0x0107, 7, 0x70 | sequence_burn)
    );

    Match wrecks;
    for (uint32_t index : {6u, 8u, 9u}) {
        uint8_t* r = wrecks.record(index);
        std::copy(wreck_position.begin(), wreck_position.end(), r + feature_record::position);
        std::copy(
            wreck_orientation.begin(), wreck_orientation.end(), r + feature_record::orientation
        );
        store16(r + feature_record::damage, static_cast<uint16_t>(0x600 + index));
    }
    wrecks.set_plot(0, 1, wreck, 6, true);
    wrecks.set_plot(1, 1, wreck, 8, true);
    wrecks.set_plot(2, 1, wreck, 9, true);
    wrecks.set_plot(3, 1, wreck, 0xffff, true);
    ScopedBank objects;
    save_write_features(&wrecks.save, objects.get());
    CHECK(read_count(objects.get(), normal_count_field) == 0);
    CHECK(read_count(objects.get(), animating_count_field) == 0);
    CHECK(read_count(objects.get(), object_count_field) == 1);
    CHECK(read_blob(objects.get(), object_blob) == object_record(0, 1, wreck, 0x0606));
}

/// Saves plots whose feature words name no FeatureDef: the first past the
/// table and the last below the reserved words. Neither writes a record, and
/// the tree beside them is written as usual.
void writer_words_past_the_table() {
    Match f;
    f.set_plot(0, 0, static_cast<uint16_t>(f.defs.size()), 0, false);
    f.set_plot(1, 0, static_cast<uint16_t>(plot_first_reserved_feature - 1), 0, true);
    f.set_plot(2, 0, tree, 0x0033, false);
    ScopedBank bank;
    save_write_features(&f.save, bank.get());
    CHECK(read_count(bank.get(), normal_count_field) == 1);
    CHECK(read_count(bank.get(), animating_count_field) == 0);
    CHECK(read_count(bank.get(), object_count_field) == 0);
    CHECK(read_blob(bank.get(), normal_blob) == normal_record(2, 0, tree, 0x0033));
}

/// Loads sections cut short: a name table ending inside its second entry,
/// and normal, animating and 3D blobs ending inside a record. Whole records
/// load; a partial record does not, and a saved type past the whole entries
/// places no feature.
void truncated_sections() {
    ScopedBank bank;
    Bytes names = type_name_table({"Tree", "Wreck"});
    names.pop_back();
    put_blob(bank.get(), type_names_blob, names);
    Bytes normal = join({
        normal_record(0, 0, tree, 0x0011),
        normal_record(3, 3, wreck, 0x0022),
        normal_record(2, 0, tree, 0x0033),
    });
    normal.resize(2 * normal_record_bytes + 5);
    put_count(bank.get(), normal_count_field, 3);
    put_blob(bank.get(), normal_blob, normal);
    Bytes animating = join({
        animating_record(1, 0, tree, 0x0044, 6, 0x50 | sequence_burn),
        animating_record(2, 1, tree, 0x0055, 7, 0x60 | sequence_burn),
    });
    animating.pop_back();
    put_count(bank.get(), animating_count_field, 2);
    put_blob(bank.get(), animating_blob, animating);
    Bytes objects = object_record(3, 2, wreck, 0x0066);
    objects.pop_back();
    put_count(bank.get(), object_count_field, 1);
    put_blob(bank.get(), object_blob, objects);

    Match g;
    load(bank.get(), g);
    const std::vector<std::string> expected{
        "load set",
        "link set",
        "place 0,0 type 0",
        "place 3,3 type 65535",
        "place 1,0 type 0",
        "burn 1,0",
    };
    CHECK(g.log == expected);
    CHECK(load16(g.plot_at(0, 0) + plot::feature_record) == 0x0011);
    CHECK(load16(g.plot_at(3, 3) + plot::feature_record) == 0x0022);
    CHECK(load16(g.plot_at(2, 0) + plot::feature) == no_feature);
    CHECK(load16(g.plot_at(2, 0) + plot::feature_record) == 0);
    CHECK(g.record_bytes(0) == restored_sprite_record(tree_burn, 0x50, 0x0044, 6));
    CHECK(g.untouched_from(1));
}

/// Loads counts that disagree with their blobs: a normal count far past its
/// two records, a negative animating count and no 3D count. Each record a
/// count covers and its blob holds loads once; nothing else is placed.
void counts_past_the_data() {
    ScopedBank bank;
    put_blob(bank.get(), type_names_blob, type_name_table({"Tree", "Wreck", "Rock"}));
    put_count(bank.get(), normal_count_field, 1000);
    put_blob(
        bank.get(),
        normal_blob,
        join({normal_record(0, 0, tree, 0x0011), normal_record(1, 0, rock, 0x0022)})
    );
    put_count(bank.get(), animating_count_field, -1);
    put_blob(bank.get(), animating_blob, animating_record(2, 0, tree, 0x0033, 1, sequence_burn));
    put_blob(bank.get(), object_blob, object_record(3, 0, wreck, 0x0044));

    Match g;
    load(bank.get(), g);
    const std::vector<std::string> expected{
        "load set", "link set", "place 0,0 type 0", "place 1,0 type 2"
    };
    CHECK(g.log == expected);
    CHECK(load16(g.plot_at(0, 0) + plot::feature_record) == 0x0011);
    CHECK(load16(g.plot_at(1, 0) + plot::feature_record) == 0x0022);
    CHECK(load16(g.plot_at(2, 0) + plot::feature) == no_feature);
    CHECK(load16(g.plot_at(3, 0) + plot::feature) == no_feature);
    CHECK(g.untouched_from(0));
}

/// Loads records for cells past the map's edges among records for cells on
/// it. Those off the map get no placement and no sequence start, and the
/// load goes on to the next record.
void cells_outside_the_map() {
    ScopedBank bank;
    put_blob(bank.get(), type_names_blob, type_name_table({"Tree", "Wreck", "Rock"}));
    put_count(bank.get(), normal_count_field, 4);
    put_blob(
        bank.get(),
        normal_blob,
        join({
            normal_record(map_w, 0, tree, 1),
            normal_record(0, map_h, tree, 2),
            normal_record(0xffff, 0, tree, 3),
            normal_record(1, 1, tree, 4),
        })
    );
    put_count(bank.get(), animating_count_field, 2);
    put_blob(
        bank.get(),
        animating_blob,
        join({
            animating_record(0xffff, 0xffff, tree, 0x0011, 1, 0x70 | sequence_burn),
            animating_record(2, 1, tree, 0x0022, 2, 0x70 | sequence_burn),
        })
    );
    put_count(bank.get(), object_count_field, 2);
    put_blob(
        bank.get(),
        object_blob,
        join({object_record(map_w, map_h - 1, wreck, 0x0033), object_record(4, 3, wreck, 0x0044)})
    );

    Match g;
    const Bytes before = g.plots;
    load(bank.get(), g);
    const std::vector<std::string> expected{
        "load set",
        "link set",
        "place 1,1 type 0",
        "place 2,1 type 0",
        "burn 2,1",
        "place 4,3 type 1 at " + wreck_at + " facing " + wreck_facing,
    };
    CHECK(g.log == expected);
    CHECK(g.record_bytes(0) == restored_sprite_record(tree_burn, 0x70, 0x0022, 2));
    CHECK(g.record_bytes(1) == restored_object_record(wreck_position, wreck_orientation, 0x0044));
    CHECK(g.untouched_from(2));
    // Only the three cells on the map changed.
    for (int32_t cell = 0; cell < map_w * map_h; ++cell) {
        if (cell == 1 * map_w + 1 || cell == 1 * map_w + 2 || cell == 3 * map_w + 4)
            continue;
        const auto at = static_cast<std::ptrdiff_t>(cell) * plot_bytes;
        CHECK(
            std::equal(g.plots.begin() + at, g.plots.begin() + at + plot_bytes, before.begin() + at)
        );
    }
}

/// Loads a bank with no Features account: the feature set is still loaded
/// and linked, and nothing is placed.
void missing_account() {
    ScopedBank bank;
    bank_open_account(bank.get(), "Summary");
    bank_set_int(bank.get(), "Gametype", 1);
    Match g;
    const Bytes before = g.plots;
    load(bank.get(), g);
    CHECK((g.log == std::vector<std::string>{"load set", "link set"}));
    CHECK(g.plots == before);
    CHECK(g.untouched_from(0));
}

/// Loads a section with no name table: a saved type is the current table's
/// index, and one past the table places no feature.
void missing_name_table() {
    ScopedBank bank;
    put_count(bank.get(), normal_count_field, 2);
    put_blob(
        bank.get(), normal_blob, join({normal_record(0, 0, rock, 0), normal_record(1, 0, 3, 0)})
    );
    Match g;
    load(bank.get(), g);
    const std::vector<std::string> expected{
        "load set", "link set", "place 0,0 type 2", "place 1,0 type 65535"
    };
    CHECK(g.log == expected);
}

/// Loads a name entry with no terminator: the name looked up is the whole
/// entry, type_name_bytes characters, and a record of that type gets the type
/// the lookup returns. A lookup that finds nothing places no feature.
void unterminated_name() {
    Bytes names = type_name_table({"Tree"});
    names.resize(2 * type_name_bytes, 'x');
    const std::string long_name(type_name_bytes, 'x');
    for (bool loads : {true, false}) {
        ScopedBank bank;
        put_blob(bank.get(), type_names_blob, names);
        put_count(bank.get(), normal_count_field, 1);
        put_blob(bank.get(), normal_blob, normal_record(0, 0, 1, 0));
        Match g;
        g.loads_missing_types = loads;
        load(bank.get(), g);
        const std::vector<std::string> expected{
            "load set",
            "find " + long_name,
            "link set",
            loads ? "place 0,0 type 3" : "place 0,0 type 65535",
        };
        CHECK(g.log == expected);
    }
}

/// Loads an animating record whose low nibble names no sequence: none
/// starts, and the record the plot's word names, the 0 a sprite's placement
/// leaves, takes the saved damage, frame and countdown.
void unknown_sequence() {
    ScopedBank bank;
    put_blob(bank.get(), type_names_blob, type_name_table({"Tree", "Wreck", "Rock"}));
    put_count(bank.get(), animating_count_field, 1);
    put_blob(bank.get(), animating_blob, animating_record(0, 0, tree, 0x0042, 9, 0x75));
    Match g;
    load(bank.get(), g);
    CHECK((g.log == std::vector<std::string>{"load set", "link set", "place 0,0 type 0"}));
    Bytes first(feature_record_bytes, unused_record_fill);
    store16(first.data() + feature_record::damage, 0x0042);
    store16(first.data() + feature_record::frame, 9);
    first[feature_record::spread_countdown] = 0x70;
    CHECK(g.record_bytes(0) == first);
    CHECK(g.untouched_from(1));
}

/// Returns the smallest count of records of a size whose last record would
/// start past the largest byte offset a 32-bit signed position holds.
constexpr int32_t count_past_offset_range(std::size_t record_bytes) {
    return static_cast<int32_t>(INT32_MAX / record_bytes + 2);
}

/// Loads a section whose one blob holds a single record under a count.
void load_one_record_under_count(
    Match& match, const char* count_field, int32_t count, const char* blob, const Bytes& record
) {
    ScopedBank bank;
    put_blob(bank.get(), type_names_blob, type_name_table({"Tree", "Wreck", "Rock"}));
    put_count(bank.get(), count_field, count);
    put_blob(bank.get(), blob, record);
    load(bank.get(), match);
}

/// Loads blobs under counts that disagree with them. A count below the
/// records a blob holds reads that many. Then each of the normal, animating
/// and 3D blobs, loaded on its own and holding one record, has counts far
/// past it: the smallest count whose last record would start past the
/// largest byte offset a 32-bit signed position holds, then INT32_MAX. The
/// load reads only the records a blob holds whole, so the record is placed
/// once, an animating one starts its sequence once, and the count costs no
/// more than that one record.
void counts_bounded_by_blobs() {
    ScopedBank bank;
    put_blob(bank.get(), type_names_blob, type_name_table({"Tree", "Wreck", "Rock"}));
    put_count(bank.get(), normal_count_field, 1);
    put_blob(
        bank.get(),
        normal_blob,
        join({normal_record(0, 0, tree, 0x0011), normal_record(1, 0, rock, 0x0022)})
    );
    Match fewer;
    load(bank.get(), fewer);
    CHECK((fewer.log == std::vector<std::string>{"load set", "link set", "place 0,0 type 0"}));
    CHECK(load16(fewer.plot_at(1, 0) + plot::feature) == no_feature);

    for (bool largest : {false, true}) {
        const auto count = [largest](std::size_t record_bytes) {
            return largest ? INT32_MAX : count_past_offset_range(record_bytes);
        };
        Match normal;
        load_one_record_under_count(
            normal,
            normal_count_field,
            count(normal_record_bytes),
            normal_blob,
            normal_record(1, 0, rock, 0x0120)
        );
        CHECK((normal.log == std::vector<std::string>{"load set", "link set", "place 1,0 type 2"}));
        CHECK(load16(normal.plot_at(1, 0) + plot::feature_record) == 0x0120);
        CHECK(normal.untouched_from(0));

        Match animating;
        load_one_record_under_count(
            animating,
            animating_count_field,
            count(animating_record_bytes),
            animating_blob,
            animating_record(0, 0, tree, 0x0123, 0x05, 0x70 | sequence_burn)
        );
        const std::vector<std::string> burning{
            "load set", "link set", "place 0,0 type 0", "burn 0,0"
        };
        CHECK(animating.log == burning);
        CHECK(animating.record_bytes(0) == restored_sprite_record(tree_burn, 0x70, 0x0123, 0x0005));
        CHECK(animating.untouched_from(1));

        Match objects;
        load_one_record_under_count(
            objects,
            object_count_field,
            count(object_record_bytes),
            object_blob,
            object_record(2, 2, wreck, 0x0321)
        );
        const std::vector<std::string> wrecked{
            "load set", "link set", "place 2,2 type 1 at " + wreck_at + " facing " + wreck_facing
        };
        CHECK(objects.log == wrecked);
        CHECK(
            objects.record_bytes(0) ==
            restored_object_record(wreck_position, wreck_orientation, 0x0321)
        );
        CHECK(objects.untouched_from(1));
    }
}

} // namespace

int main(int argc, char** argv) {
    using oa::data::persist::test::finish;
    if (argc > 1 && std::strcmp(argv[1], "--count-bounds") == 0) {
        counts_bounded_by_blobs();
        return finish("persist-features-count-bounds");
    }
    pool_round_trip();
    section_as_3_1c_writes_it();
    record_index_bounds(pool_records, guard_records, true);
    record_index_bounds(pool_records, 0, true);
    record_index_bounds(0, 0, true);
    record_index_bounds(pool_records, guard_records, false);
    record_index_bounds(pool_records, 0, false);
    writer_record_index_bounds();
    writer_words_past_the_table();
    truncated_sections();
    counts_past_the_data();
    cells_outside_the_map();
    missing_account();
    missing_name_table();
    unterminated_name();
    unknown_sequence();
    return finish("persist-features");
}
