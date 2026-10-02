// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The savegame's Features section over a running match: a match that resumes
// a save places only the map's markers, neither the map's features nor the
// mission schema's, the section's load places the saved
// features through the match's feature calls, fires keep only the high
// nibble of their spread countdown, and a feature on a plot the load hides
// under the map's edges does not come back.
#include "saved_game.hpp"

#include "oa/data/persist/save_sections.hpp"
#include "oa/base/text.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace combat_fixture;
namespace features = oa::sim::feature_runtime;
namespace persist = oa::data::persist;

constexpr uint16_t tree = 0, rock = 1, crisp = 2, smudge = 3, wreck = 4;
constexpr int32_t map_cells = 16;

// Sequence refs, as the app's FeatureDefHost would number them.
constexpr oa_ref32 standing = 1, burning = 2, dying = 3, reclaiming = 4;

struct Shape {
    uint16_t frames{};
    uint16_t duration{};
    uint8_t repeat{};
};

// Frame counts and per-frame ticks of each sequence; a fire burns for 150
// ticks from its first frame.
constexpr Shape shapes[] = {{}, {1, 1, 1}, {6, 25, 0}, {3, 4, 0}, {4, 3, 0}};

bool sequence_frame(oa_ref32 sequence, uint16_t frame, features::FeatureSequenceFrame& out) {
    if (sequence == 0 || sequence >= std::size(shapes))
        return false;
    const auto& shape = shapes[sequence];
    out.frame_count = shape.frames;
    out.repeat = shape.repeat;
    if (frame >= shape.frames)
        return false;
    out.duration = shape.duration;
    out.width = 16;
    out.height = 32;
    out.origin_x = 8;
    out.origin_y = 30;
    return true;
}

std::vector<FeatureDef> feature_table() {
    std::vector<FeatureDef> table(5);
    for (auto& def : table) {
        def.footprint_x = def.footprint_z = 1;
        def.flags = OA_FEATURE_FLAG_SPRITE;
        def.seq_name = standing;
        def.dead_feature = def.burnt_feature = def.reclamate_feature = features::no_feature;
    }
    auto& t = table[tree];
    t.flags |= OA_FEATURE_FLAG_FLAMABLE | OA_FEATURE_FLAG_RECLAIMABLE | OA_FEATURE_FLAG_BLOCKING;
    t.seq_name_burn = burning;
    t.seq_name_die = dying;
    t.seq_name_reclamate = reclaiming;
    t.spark_time = 150; // ticks: 5 seconds
    t.spread_chance = 100;
    t.damage = 50;
    t.dead_feature = smudge;
    t.burnt_feature = crisp;
    auto& r = table[rock];
    r.flags |= OA_FEATURE_FLAG_RECLAIMABLE | OA_FEATURE_FLAG_BLOCKING;
    r.seq_name_reclamate = reclaiming;
    // A 3DO unit wreck like armfast_dead.
    auto& w = table[wreck];
    w.flags = OA_FEATURE_FLAG_BLOCKING | OA_FEATURE_FLAG_RECLAIMABLE;
    w.footprint_x = w.footprint_z = 2;
    w.damage = 440;
    const char* names[] = {"Tree", "Rock", "Crisp", "Smudge", "Wreck"};
    for (std::size_t index = 0; index < table.size(); ++index)
        std::snprintf(table[index].name, sizeof table[index].name, "%s", names[index]);
    return table;
}

std::size_t plot(int32_t x, int32_t z) {
    return static_cast<std::size_t>(z * map_cells + x);
}

// The loader's plot words: fires-to-be at (1,1), (10,1) and (6,6) each with
// a tree beside it, a tree to die at (4,6), a rock to reclaim at (2,6), a
// damaged tree at (12,6), a tree to clear at (0,7), a marker at (13,7), and a
// tree at (14,3), under the right-hand edge a load hides. The mission schema
// places a rock at (5,4).
Options map_options(bool resuming) {
    Options options;
    options.features = feature_table();
    options.feature_words = {
        {plot(1, 1), tree},
        {plot(2, 1), tree},
        {plot(10, 1), tree},
        {plot(11, 1), tree},
        {plot(6, 6), tree},
        {plot(7, 6), tree},
        {plot(4, 6), tree},
        {plot(2, 6), rock},
        {plot(12, 6), tree},
        {plot(0, 7), tree},
        {plot(13, 7), features::feature_marker},
        {plot(14, 3), tree}
    };
    features::FeaturePlacement schema_rock{};
    std::snprintf(schema_rock.name, sizeof schema_rock.name, "%s", "Rock");
    schema_rock.x = 5;
    schema_rock.z = 4;
    options.mission_features = {schema_rock};
    options.feature_sequence_frame = sequence_frame;
    options.resuming_saved_game = resuming;
    return options;
}

features::PlacedFeature* record_at(Fixture& f, int32_t x, int32_t z) {
    const auto& cell = f.match->state().plots[plot(x, z)];
    if ((cell.flags & OA_PLOT_FLAG_ANIMATING_FEATURE) == 0)
        return nullptr;
    return features::feature_record(f.match->state(), cell.feature_record);
}

bool burning_at(Fixture& f, int32_t x, int32_t z) {
    const auto* record = record_at(f, x, z);
    return record != nullptr && (record->state & features::state_burning) != 0;
}

std::size_t placed_count(Fixture& f) {
    const auto& world = f.match->state();
    std::size_t count = 0;
    for (std::size_t index = 0; index < plot(0, map_cells); ++index)
        count += world.plots[index].feature < OA_PLOT_FEATURE_RESERVED ? 1 : 0;
    return count;
}

// The Features section over a match: its canonical plots and placed-feature
// records, and the hooks bound to the match's feature calls as the app binds
// them. The feature-set hooks only log, since the match's table holds every
// type the tests name.
struct MatchSection {
    sim::match_runtime::Match* match{};
    std::vector<std::string> log;
    persist::SaveHooks hooks{};
    persist::SaveContext context{};

    explicit MatchSection(sim::match_runtime::Match& bound) : match(&bound) {
        hooks.context = this;
        hooks.load_feature_set = [](void* c) {
            static_cast<MatchSection*>(c)->log.push_back("load set");
        };
        hooks.find_or_load_feature = [](void* c, const char* name) -> int16_t {
            static_cast<MatchSection*>(c)->log.push_back(std::string("find ") + name);
            return static_cast<int16_t>(features::no_feature);
        };
        hooks.link_feature_set = [](void* c) {
            static_cast<MatchSection*>(c)->log.push_back("link set");
        };
        hooks.place_feature = [](void* c,
                                 uint8_t* plot_bytes,
                                 uint16_t type,
                                 const uint8_t* position,
                                 const uint8_t* orientation) {
            static_cast<MatchSection*>(c)->match->place_saved_feature(
                plot_bytes, type, position, orientation
            );
        };
        hooks.burn_feature = [](void* c, uint16_t x, uint16_t z) {
            static_cast<MatchSection*>(c)->match->ignite_saved_feature(x, z);
        };
        hooks.queue_feature_event = [](void* c, uint16_t x, uint16_t z, int32_t kind) {
            static_cast<MatchSection*>(c)->match->restart_saved_feature_sequence(x, z, kind);
        };
        auto& world = bound.state();
        context = {
            &world,
            reinterpret_cast<uint8_t*>(world.plots),
            nullptr,
            world.placed_features,
            nullptr,
            &hooks,
            world.placed_feature_count
        };
    }

    MatchSection(const MatchSection&) = delete;
    MatchSection& operator=(const MatchSection&) = delete;
};

// A bank read back from its file image, where every blob starts at position
// zero, as a loaded savegame's does.
void reload(persist::Bank* from, persist::Bank* to) {
    persist::ByteImage image{};
    CHECK(persist::bank_write_image(from, persist::savegame_description, true, &image));
    persist::BankError error{};
    CHECK(
        persist::bank_read_image(
            to, image.data, image.size, persist::savegame_description, nullptr, &error
        )
    );
    persist::byte_image_free(&image);
}

std::vector<uint8_t> blob(persist::Bank* bank, const char* name) {
    persist::bank_open_account(bank, "Features");
    if (!persist::bank_open_blob_name(bank, name))
        return {};
    std::vector<uint8_t> out(static_cast<std::size_t>(persist::bank_blob_size(bank)));
    persist::bank_blob_seek(bank, 0);
    persist::bank_blob_read(bank, out.data(), static_cast<uint32_t>(out.size()));
    return out;
}

void put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void put32(std::vector<uint8_t>& out, uint32_t value) {
    put16(out, static_cast<uint16_t>(value));
    put16(out, static_cast<uint16_t>(value >> 16));
}

// A match resuming a save starts with the map's markers and nothing else:
// neither the map's own features nor the mission schema's rock.
void resuming_match_places_only_markers() {
    Fixture fresh(map_options(false));
    Fixture resumed(map_options(true));
    CHECK(placed_count(fresh) == 12);
    CHECK(placed_count(resumed) == 0);
    CHECK(fresh.match->state().plots[plot(5, 4)].feature == rock);
    CHECK(fresh.match->spatial().plots[plot(5, 4)].blocking_feature);
    CHECK(resumed.match->state().plots[plot(5, 4)].feature == features::no_feature);
    CHECK(!resumed.match->spatial().plots[plot(5, 4)].blocking_feature);
    CHECK(resumed.match->state().plots[plot(13, 7)].feature == features::feature_marker);
    CHECK(resumed.match->spatial().plots[plot(13, 7)].blocking_feature);
    // The load hides the right-hand columns the fresh map's tree keeps.
    CHECK(fresh.match->state().plots[plot(14, 3)].feature == tree);
    CHECK(resumed.match->state().plots[plot(14, 3)].feature == features::hidden_edge);
    std::cout << "resuming match places only markers passed\n";
}

// Fires with 117 ticks left, a fire from another player's machine with 37
// and one with 9, a die and a reclamate sequence, a damaged tree, a 3DO
// wreck, a cleared tree, a fire under the map's right-hand edge and the
// mission schema's rock are saved and loaded into a match resuming the save,
// which places the rock once, from the save. Each animating record's last
// byte is the countdown's high nibble with the sequence below it; the load
// keeps that nibble, so the first fire spreads 112 ticks on, the other
// player's fire, now ordinary, 32 ticks on, and the fire with 9 ticks left
// never does. The cleared tree stays gone, and the fire under the edge a
// load hides is lost without touching any other record. Saved again, the
// loaded match writes the same section but for that fire.
void features_round_trip() {
    Fixture a(map_options(false));
    auto& world = a.match->state();
    const auto host = a.match->feature_host();
    features::ignite_feature(world, host, 1, 1, false);
    record_at(a, 1, 1)->spread_countdown = 0x75;
    record_at(a, 1, 1)->sprite.animation.frame = 1;
    features::ignite_feature(world, host, 10, 1, true);
    record_at(a, 10, 1)->spread_countdown = 0x25;
    CHECK((record_at(a, 10, 1)->state & features::state_no_spread) != 0);
    features::ignite_feature(world, host, 6, 6, false);
    record_at(a, 6, 6)->spread_countdown = 9;
    features::start_feature_sequence(world, host, 4, 6, false);
    record_at(a, 4, 6)->sprite.animation.frame = 2;
    record_at(a, 4, 6)->damage = 7;
    record_at(a, 4, 6)->spread_countdown = 0x4c;
    features::start_feature_sequence(world, host, 2, 6, true);
    record_at(a, 2, 6)->sprite.animation.frame = 1;
    record_at(a, 2, 6)->spread_countdown = 0x31;
    world.plots[plot(12, 6)].feature_record = 30;
    features::ignite_feature(world, host, 14, 3, false);
    record_at(a, 14, 3)->spread_countdown = 0x55;
    record_at(a, 14, 3)->sprite.animation.frame = 3;
    record_at(a, 14, 3)->damage = 9;
    const FixedVec3 at{(8 * 16 + 16) << 16, 5 << 16, (3 * 16 + 16) << 16};
    const int16_t turn[3] = {100, -200, 300};
    auto* wrecked = features::place_feature(world, host, plot(8, 3), wreck, &at, turn, 2);
    CHECK(wrecked != nullptr);
    wrecked->damage = 123;
    CHECK(features::clear_plot_feature(world, host, plot(0, 7), false));

    MatchSection saved(*a.match);
    saved_game::ScopedBank written;
    persist::save_write_features(&saved.context, written.get());
    persist::bank_open_account(written.get(), "Features");
    CHECK(persist::bank_get_int(written.get(), "Number of Normal Features", -1) == 5);
    CHECK(persist::bank_get_int(written.get(), "Number of Animating Features", -1) == 6);
    CHECK(persist::bank_get_int(written.get(), "Number of 3D Features", -1) == 1);
    // Row order: the fires at (1,1) and (10,1), the fire at (14,3), then the
    // rock at (2,6), the dying tree at (4,6) and the fire at (6,6).
    const auto animating = blob(written.get(), "Animating Features");
    CHECK(animating.size() == 6 * 10);
    const uint8_t first[] = {1, 0, 1, 0, tree, 0, 0, 0, 1, 0x70};
    CHECK(std::memcmp(animating.data(), first, sizeof first) == 0);
    const uint8_t edge_fire[] = {14, 0, 3, 0, tree, 0, 9, 0, 3, 0x50};
    CHECK(std::memcmp(animating.data() + 20, edge_fire, sizeof edge_fire) == 0);
    CHECK(animating[19] == 0x20 && animating[39] == 0x32);
    const uint8_t dying_tree[] = {4, 0, 6, 0, tree, 0, 7, 0, 2, 0x41};
    CHECK(std::memcmp(animating.data() + 40, dying_tree, sizeof dying_tree) == 0);
    CHECK(animating[59] == 0x00);
    std::vector<uint8_t> object = {8, 0, 3, 0, wreck, 0, 123, 0};
    put32(object, static_cast<uint32_t>(at.x));
    put32(object, static_cast<uint32_t>(at.y));
    put32(object, static_cast<uint32_t>(at.z));
    for (const int16_t angle : turn)
        put16(object, static_cast<uint16_t>(angle));
    CHECK(blob(written.get(), "3D Features") == object);
    const auto normal = blob(written.get(), "Normal Features");
    const uint8_t damaged_tree[] = {12, 0, 6, 0, tree, 0, 30, 0};
    CHECK(normal.size() == 5 * 8);
    CHECK(std::memcmp(normal.data() + 32, damaged_tree, sizeof damaged_tree) == 0);
    // The schema's rock, in row order among the map's own features; its
    // record word is whatever the placement left.
    const uint8_t schema_rock[] = {5, 0, 4, 0, rock, 0};
    CHECK(std::memcmp(normal.data() + 16, schema_rock, sizeof schema_rock) == 0);

    Fixture b(map_options(true));
    MatchSection load(*b.match);
    saved_game::ScopedBank loaded;
    reload(written.get(), loaded.get());
    persist::save_read_features(&load.context, loaded.get());
    CHECK((load.log == std::vector<std::string>{"load set", "link set"}));
    auto& restored = b.match->state();
    CHECK(burning_at(b, 1, 1) && record_at(b, 1, 1)->spread_countdown == 0x70);
    CHECK(record_at(b, 1, 1)->sprite.animation.frame == 1);
    CHECK(burning_at(b, 10, 1) && record_at(b, 10, 1)->spread_countdown == 0x20);
    CHECK((record_at(b, 10, 1)->state & features::state_no_spread) == 0);
    CHECK(burning_at(b, 6, 6) && record_at(b, 6, 6)->spread_countdown == 0);
    const auto* dying_record = record_at(b, 4, 6);
    CHECK(dying_record && dying_record->sprite.animation.sequence == dying);
    CHECK(dying_record->sprite.animation.frame == 2 && dying_record->damage == 7);
    CHECK(dying_record->spread_countdown == 0x40);
    const auto* rock_record = record_at(b, 2, 6);
    CHECK(rock_record && rock_record->sprite.animation.sequence == reclaiming);
    CHECK((rock_record->state & features::state_reclaimed) != 0);
    CHECK(rock_record->spread_countdown == 0x30 && rock_record->sprite.animation.frame == 1);
    const auto* wreck_record = record_at(b, 8, 3);
    CHECK(wreck_record && wreck_record->damage == 123);
    CHECK(
        wreck_record->model.position.x == at.x && wreck_record->model.position.y == at.y &&
        wreck_record->model.position.z == at.z
    );
    CHECK(std::memcmp(wreck_record->orientation, turn, sizeof turn) == 0);
    CHECK(restored.plots[plot(9, 4)].feature == features::feature_continuation);
    CHECK(restored.plots[plot(12, 6)].feature == tree);
    CHECK(restored.plots[plot(12, 6)].feature_record == 30);
    CHECK(restored.plots[plot(0, 7)].feature == features::no_feature);
    CHECK(restored.plots[plot(14, 3)].feature == features::hidden_edge);
    CHECK(restored.plots[plot(13, 7)].feature == features::feature_marker);
    CHECK(restored.plots[plot(5, 4)].feature == rock && record_at(b, 5, 4) == nullptr);
    CHECK(b.match->spatial().plots[plot(5, 4)].blocking_feature);
    CHECK(placed_count(b) == placed_count(a) - 1);

    // Saved again: the same section, less the fire under the hidden edge.
    saved_game::ScopedBank rewritten;
    persist::save_write_features(&load.context, rewritten.get());
    auto expected_animating = animating;
    expected_animating.erase(expected_animating.begin() + 20, expected_animating.begin() + 30);
    CHECK(blob(rewritten.get(), "Animating Features") == expected_animating);
    CHECK(blob(rewritten.get(), "3D Features") == object);
    CHECK(blob(rewritten.get(), "Normal Features") == normal);

    b.run(31);
    CHECK(!burning_at(b, 11, 1));
    b.run(1);
    CHECK(burning_at(b, 11, 1));
    b.run(111 - 32);
    CHECK(!burning_at(b, 2, 1));
    b.run(1);
    CHECK(burning_at(b, 2, 1));
    b.run(60);
    CHECK(restored.plots[plot(6, 6)].feature == crisp);
    CHECK(restored.plots[plot(7, 6)].feature == tree && record_at(b, 7, 6) == nullptr);
    std::cout << "features round trip passed\n";
}

// A Features section laid out by hand as 3.1c writes it, its type names in
// another order than the match's table, loads into a resuming match as the
// features it describes.
void hand_built_section_loads() {
    Fixture f(map_options(true));
    saved_game::ScopedBank bank;
    persist::bank_open_account(bank.get(), "Features");
    std::vector<uint8_t> names(3 * 0x80);
    oa::base::text::copy_terminated(
        std::span(reinterpret_cast<char*>(names.data()), 0x80), "Wreck"
    );
    oa::base::text::copy_terminated(
        std::span(reinterpret_cast<char*>(names.data() + 0x80), 0x80), "Tree"
    );
    oa::base::text::copy_terminated(
        std::span(reinterpret_cast<char*>(names.data() + 0x100), 0x80), "Rock"
    );
    persist::bank_open_blob_name(bank.get(), "Feature Type Names");
    persist::bank_blob_write(bank.get(), names.data(), static_cast<uint32_t>(names.size()));
    // Normal: a tree at (2,3) with 30 damage taken and a rock at (5,5).
    const uint8_t normal[] = {2, 0, 3, 0, 1, 0, 30, 0, 5, 0, 5, 0, 2, 0, 0, 0};
    persist::bank_set_int(bank.get(), "Number of Normal Features", 2);
    persist::bank_open_blob_name(bank.get(), "Normal Features");
    persist::bank_blob_write(bank.get(), normal, sizeof normal);
    // Animating: a tree at (6,6) burning on frame 2 with 0x50 ticks to spread,
    // and a rock at (9,2) being reclaimed on frame 1, its record's countdown
    // nibble carried along.
    const uint8_t animating[] = {
        6, 0, 6, 0, 1, 0, 7, 0, 2, 0x50, 9, 0, 2, 0, 2, 0, 0, 0, 1, 0x12,
    };
    persist::bank_set_int(bank.get(), "Number of Animating Features", 2);
    persist::bank_open_blob_name(bank.get(), "Animating Features");
    persist::bank_blob_write(bank.get(), animating, sizeof animating);
    // 3D: a wreck at (10,4) with 100 damage, standing where it fell and turned.
    std::vector<uint8_t> object = {10, 0, 4, 0, 0, 0, 100, 0};
    const FixedVec3 at{(10 * 16 + 16) << 16, 3 << 16, (4 * 16 + 16) << 16};
    put32(object, static_cast<uint32_t>(at.x));
    put32(object, static_cast<uint32_t>(at.y));
    put32(object, static_cast<uint32_t>(at.z));
    const int16_t turn[3] = {0x100, 0, -0x200};
    for (const int16_t angle : turn)
        put16(object, static_cast<uint16_t>(angle));
    persist::bank_set_int(bank.get(), "Number of 3D Features", 1);
    persist::bank_open_blob_name(bank.get(), "3D Features");
    persist::bank_blob_write(bank.get(), object.data(), static_cast<uint32_t>(object.size()));

    MatchSection load(*f.match);
    saved_game::ScopedBank loaded;
    reload(bank.get(), loaded.get());
    persist::save_read_features(&load.context, loaded.get());
    CHECK((load.log == std::vector<std::string>{"load set", "link set"}));
    const auto& world = f.match->state();
    CHECK(world.plots[plot(2, 3)].feature == tree && world.plots[plot(2, 3)].feature_record == 30);
    CHECK((world.plots[plot(2, 3)].flags & OA_PLOT_FLAG_ANIMATING_FEATURE) == 0);
    CHECK(world.plots[plot(5, 5)].feature == rock && record_at(f, 5, 5) == nullptr);
    const auto* fire = record_at(f, 6, 6);
    CHECK(world.plots[plot(6, 6)].feature == tree && fire != nullptr);
    CHECK(
        (fire->state & features::state_burning) != 0 && fire->sprite.animation.sequence == burning
    );
    CHECK(fire->sprite.animation.frame == 2 && fire->damage == 7 && fire->spread_countdown == 0x50);
    const auto* reclaim = record_at(f, 9, 2);
    CHECK(world.plots[plot(9, 2)].feature == rock && reclaim != nullptr);
    CHECK((reclaim->state & features::state_reclaimed) != 0);
    CHECK(reclaim->sprite.animation.sequence == reclaiming && reclaim->sprite.animation.frame == 1);
    CHECK(reclaim->spread_countdown == 0x10);
    const auto* wreck_record = record_at(f, 10, 4);
    CHECK(world.plots[plot(10, 4)].feature == wreck && wreck_record != nullptr);
    CHECK(wreck_record->damage == 100);
    CHECK(
        wreck_record->model.position.x == at.x && wreck_record->model.position.y == at.y &&
        wreck_record->model.position.z == at.z
    );
    CHECK(std::memcmp(wreck_record->orientation, turn, sizeof turn) == 0);
    for (const auto cell : {plot(11, 4), plot(10, 5), plot(11, 5)})
        CHECK(world.plots[cell].feature == features::feature_continuation);
    CHECK(f.match->spatial().plots[plot(11, 5)].blocking_feature);
    CHECK(placed_count(f) == 5);
    std::cout << "hand-built section loads passed\n";
}

} // namespace

int main() {
    try {
        resuming_match_places_only_markers();
        features_round_trip();
        hand_built_section_loads();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
