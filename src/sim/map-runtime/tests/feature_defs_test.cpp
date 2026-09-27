// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/map_runtime/feature_defs.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace oa;
using namespace oa::sim::map_runtime;

int failures = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::vector<data::unit_definitions::TdfDocument>
documents(std::initializer_list<std::string_view> texts) {
    std::vector<data::unit_definitions::TdfDocument> result;
    for (const auto text : texts) {
        auto parsed = data::unit_definitions::parse_tdf(text);
        require(static_cast<bool>(parsed), "test feature document parses");
        result.push_back(std::move(parsed.value));
    }
    return result;
}

formats::tnt::Map map_with(std::initializer_list<std::string_view> names) {
    formats::tnt::Map map;
    for (const auto name : names) {
        formats::tnt::FeatureRecord record;
        record.name = std::string(name);
        map.features.push_back(std::move(record));
    }
    return map;
}

std::string_view name_of(const FeatureDef& def) {
    return {def.name, ::strnlen(def.name, sizeof def.name)};
}

// Records every asset request and hands out distinct refs.
struct Recorder {
    std::vector<std::string> animations, objects, weapons;
    std::vector<std::string> sequences; // "<archive>:<name>:<one_shot>"
    uint16_t frame_count = 3;
    uint8_t repeat = 1;
    uint16_t duration = 7;
};

FeatureDefHost make_host(Recorder& recorder) {
    FeatureDefHost host{};
    host.context = &recorder;
    host.load_animation = [](void* context, const char* name) -> oa_ref32 {
        auto& r = *static_cast<Recorder*>(context);
        r.animations.emplace_back(name);
        return static_cast<oa_ref32>(100 + r.animations.size());
    };
    host.find_sequence =
        [](void* context, oa_ref32 archive, const char* name, bool one_shot) -> oa_ref32 {
        auto& r = *static_cast<Recorder*>(context);
        r.sequences.push_back(std::to_string(archive) + ":" + name + ":" + (one_shot ? "1" : "0"));
        return archive == 0 ? 0 : static_cast<oa_ref32>(1000 * archive + r.sequences.size());
    };
    host.load_object = [](void* context, const char* name) -> oa_ref32 {
        auto& r = *static_cast<Recorder*>(context);
        r.objects.emplace_back(name);
        return static_cast<oa_ref32>(0x11223344u + r.objects.size());
    };
    host.find_weapon = [](void* context, const char* name) -> oa_ref32 {
        auto& r = *static_cast<Recorder*>(context);
        r.weapons.emplace_back(name);
        return std::string_view(name) == "TREEBURN" ? 77u : 0u;
    };
    host.sequence_frame =
        [](
            void* context, oa_ref32, uint16_t, uint16_t* count, uint8_t* repeat, uint16_t* duration
        ) {
            const auto& r = *static_cast<const Recorder*>(context);
            *count = r.frame_count;
            *repeat = r.repeat;
            *duration = r.duration;
        };
    return host;
}

constexpr std::string_view rocks_tdf =
    "[ROCK01]\n{\n"
    "  description=A rock with a rather long description text;\n"
    "  footprintx=2;\n  footprintz=3;\n  height=12;\n"
    "  filename=Rocks;\n  seqname=rock01;\n  seqnameshad=rock01shad;\n"
    "  seqnamedie=rock01die;\n  seqnamereclamate=rock01rec;\n"
    "  animating=1;\n  blocking=1;\n  reclaimable=1;\n  indestructible=0;\n"
    "  metal=70000;\n  energy=5;\n  damage=1200;\n  sparktime=2.9;\n"
    "  featuredead=ROCK01_DEAD;\n"
    "  flamable=1;\n  burnweapon=TREEBURN;\n  spreadchance=3;\n  reproduce=1;\n  reproducearea=9;\n"
    "}\n"
    "[ROCK02]\n{\n"
    "  footprintx=1;\n  footprintz=1;\n  filename=Rocks;\n  seqname=rock02;\n"
    "  autoreclaimable=0;\n  geothermal=1;\n  nodrawundergray=1;\n  metal=-1;\n"
    "}\n"
    "[ROCK01_DEAD]\n{\n"
    "  footprintx=2;\n  footprintz=3;\n  filename=Rocks;\n  seqname=rock01d;\n"
    "  featuredead=Rock01_Rubble;\n  featureburnt=ROCK01_DEAD;\n"
    "}\n"
    "[rock01_rubble]\n{\n  footprintx=1;\n  footprintz=1;\n  filename=Rocks;\n  "
    "seqname=rubble;\n}\n";

constexpr std::string_view objects_tdf =
    "[Dragons Teeth]\n{\n  footprintx=1;\n  footprintz=1;\n  object=DTEETH;\n  blocking=1;\n  "
    "indestructible=1;\n  damage=99;\n}\n"
    "[ARMPW_DEAD]\n{\n  footprintx=1;\n  footprintz=1;\n  object=ARMPW_DEAD;\n  reclaimable=1;\n  "
    "metal=20;\n  featurereclamate=rock01_rubble;\n}\n";

void test_table_follows_tnt_order_and_fields() {
    const auto docs = documents({rocks_tdf, objects_tdf});
    const auto map = map_with({"rock01", "dragons teeth", "ROCK02"});
    Recorder recorder;
    const auto host = make_host(recorder);
    FeatureDefTable table;
    const auto error = init_feature_table(table, map, docs, &host);
    require(!error.has_value(), "TNT features load");
    require(table.defs.size() == 3, "one definition per TNT feature");
    require(name_of(table.defs[0]) == "rock01", "record keeps the TNT spelling");
    require(name_of(table.defs[1]) == "dragons teeth", "second TNT feature is index 1");
    require(name_of(table.defs[2]) == "ROCK02", "third TNT feature is index 2");

    const auto& rock = table.defs[0];
    require(
        std::string_view(rock.description, 20) == "A rock with a rather",
        "description bounded to 20 bytes"
    );
    require(
        rock.footprint_x == 2 && rock.footprint_z == 3 && rock.height == 12, "footprint and height"
    );
    require((rock.flags & OA_FEATURE_FLAG_SPRITE) != 0, "filename feature is a sprite");
    require(std::string_view(rock.animation_file, 5) == "Rocks", "sprite keeps its GAF name");
    require(rock.animation == 101, "sprite archive loaded through the host");
    require(
        recorder.animations == std::vector<std::string>{"Rocks"}, "anims/Rocks.gaf requested once"
    );
    require(
        rock.seq_name != 0 && rock.seq_name_shadow != 0 && rock.seq_name_die != 0 &&
            rock.seq_name_reclamate != 0,
        "named sequences resolve"
    );
    require(
        rock.seq_name_burn == 0 && rock.seq_name_burn_shadow == 0 &&
            rock.seq_name_die_shadow == 0 && rock.seq_name_reclamate_shadow == 0,
        "absent sequences stay null"
    );
    // The fifth lookup is ROCK02's idle sequence through the shared archive.
    require(
        recorder.sequences.size() == 5 && recorder.sequences[0] == "101:rock01:0" &&
            recorder.sequences[1] == "101:rock01shad:0" &&
            recorder.sequences[2] == "101:rock01die:1" &&
            recorder.sequences[3] == "101:rock01rec:1" && recorder.sequences[4] == "101:rock02:0",
        "die and reclaim sequences are marked one-shot, the idle ones are not"
    );
    require(rock.metal == 4464.0f, "metal is masked to 16 bits");
    require(rock.energy == 5.0f && rock.damage == 1200, "energy and damage");
    require(rock.spark_time == 87, "sparktime seconds are stored as ticks");
    require(
        rock.spread_chance == 3 && rock.reproduce == 1 && rock.reproduce_area == 9, "spread fields"
    );
    require(
        rock.burn_weapon == 77 && recorder.weapons == std::vector<std::string>{"TREEBURN"},
        "burn weapon resolved by name"
    );
    const uint16_t expected_rock_flags =
        OA_FEATURE_FLAG_SPRITE | OA_FEATURE_FLAG_ANIMATING | OA_FEATURE_FLAG_FLAMABLE |
        OA_FEATURE_FLAG_BLOCKING | OA_FEATURE_FLAG_RECLAIMABLE | OA_FEATURE_FLAG_AUTO_RECLAIMABLE;
    require(rock.flags == expected_rock_flags, "rock flag word; autoreclaimable defaults on");
    // Looping cursor: frame 0, first-frame duration, repeat, sequence.
    require(
        rock.animation_cursor.frame == 0 && rock.animation_cursor.remaining == 7 &&
            rock.animation_cursor.repeat == 1,
        "animating sprite starts its idle cursor"
    );
    require(rock.animation_cursor.sequence == rock.seq_name, "idle cursor names the idle sequence");
    require(
        rock.dead_feature == no_feature_index && burnt_feature(rock) == no_feature_index &&
            reclamate_feature(rock) == no_feature_index,
        "links wait for the link pass"
    );

    const auto& teeth = table.defs[1];
    require((teeth.flags & OA_FEATURE_FLAG_SPRITE) == 0, "object feature is not a sprite");
    uint32_t object_ref = 0;
    std::memcpy(&object_ref, teeth.animation_file, sizeof object_ref);
    require(
        object_ref == 0x11223345u && recorder.objects == std::vector<std::string>{"DTEETH"},
        "object ref stored in the animation slot"
    );
    require(
        teeth.flags == (OA_FEATURE_FLAG_BLOCKING | OA_FEATURE_FLAG_AUTO_RECLAIMABLE |
                        OA_FEATURE_FLAG_INDESTRUCTIBLE | OA_FEATURE_FLAG_NO_DRAW_UNDER_GRAY),
        "dragons teeth are forced to draw under gray"
    );
    require(teeth.damage == 99, "object feature damage");

    const auto& rock2 = table.defs[2];
    require(
        std::string_view(rock2.animation_file, 5) == "reuse" && rock2.animation == 0,
        "a second sprite on the same GAF reuses it"
    );
    require(recorder.animations.size() == 1, "reused GAF is not loaded again");
    require(
        rock2.seq_name == 1000 * 101 + 5, "reused sprite resolves sequences in the shared archive"
    );
    require(
        (rock2.flags & OA_FEATURE_FLAG_AUTO_RECLAIMABLE) == 0,
        "autoreclaimable=0 clears the default"
    );
    require(
        (rock2.flags & (OA_FEATURE_FLAG_GEOTHERMAL | OA_FEATURE_FLAG_NO_DRAW_UNDER_GRAY)) ==
            (OA_FEATURE_FLAG_GEOTHERMAL | OA_FEATURE_FLAG_NO_DRAW_UNDER_GRAY),
        "geothermal and nodrawundergray bits"
    );
    require(rock2.metal == 65535.0f, "negative metal wraps through the 16-bit mask");
}

void test_missing_definition_is_an_error() {
    const auto docs = documents({rocks_tdf});
    FeatureDefTable table;
    const auto error =
        init_feature_table(table, map_with({"ROCK01", "NOSUCHTHING"}), docs, nullptr);
    require(
        error.has_value() && error->code == ErrorCode::missing_feature_definition,
        "missing TNT feature fails"
    );
    require(table.defs.size() == 1, "loading stops at the missing feature");
}

void test_find_or_load_and_links() {
    const auto docs = documents({rocks_tdf, objects_tdf});
    FeatureDefTable table;
    require(
        !init_feature_table(table, map_with({"ROCK01", "ROCK02"}), docs, nullptr).has_value(),
        "table loads"
    );
    require(
        find_feature_index(table, "rock02") == 1 &&
            find_feature_index(table, "ROCK01_DEAD") == no_feature_index,
        "index lookup is case-insensitive and reports absence"
    );

    // A unit corpse appends past the map's features and is found afterwards.
    const auto corpse = find_or_load_feature(table, docs, "armpw_dead", nullptr);
    require(
        corpse.ok() && corpse.index == 2 && name_of(table.defs[2]) == "armpw_dead",
        "corpse appended as index 2"
    );
    require(
        find_or_load_feature(table, docs, "ARMPW_DEAD", nullptr).index == 2,
        "existing corpse is not reloaded"
    );
    require(table.defs.size() == 3, "table holds map features plus the corpse");
    require(!find_or_load_feature(table, docs, "GHOST", nullptr).ok(), "unknown corpse name fails");

    require(!load_feature_links(table, docs, nullptr).has_value(), "links resolve");
    require(table.defs.size() == 5, "featuredead chain appends ROCK01_DEAD and its rubble");
    // The corpse (index 2) links before ROCK01_DEAD (index 3), so the rubble
    // carries the corpse's spelling of the name.
    require(
        name_of(table.defs[3]) == "ROCK01_DEAD" && name_of(table.defs[4]) == "rock01_rubble",
        "appended links keep the spelling of the first link naming them"
    );
    require(
        table.defs[0].dead_feature == 3 && reclamate_feature(table.defs[0]) == no_feature_index &&
            burnt_feature(table.defs[0]) == no_feature_index,
        "rock links to its dead remnant only"
    );
    require(table.defs[1].dead_feature == no_feature_index, "unlinked feature stays unlinked");
    require(
        reclamate_feature(table.defs[2]) == 4,
        "corpse reclamate resolves to the rubble appended later"
    );
    require(
        table.defs[3].dead_feature == 4 && burnt_feature(table.defs[3]) == 3,
        "appended definitions are linked in turn, self-links included"
    );
    require(table.defs[4].dead_feature == no_feature_index, "rubble ends the chain");
    require(sizeof(FeatureDef) == 0x100, "record size");

    // A link naming a definition no document has fails the pass.
    const auto broken = documents({"[LONELY]\n{\n  footprintx=1;\n  footprintz=1;\n  filename=x;\n "
                                   " featuredead=MISSING;\n}\n"});
    FeatureDefTable lonely;
    require(
        !init_feature_table(lonely, map_with({"LONELY"}), broken, nullptr).has_value(),
        "lonely loads"
    );
    const auto error = load_feature_links(lonely, broken, nullptr);
    require(
        error.has_value() && error->code == ErrorCode::missing_feature_definition,
        "missing link fails"
    );
}

// sparktime is read as a TDF number in seconds and stored as ticks: times 30,
// rounded to a 53-bit significand, truncated toward zero at 64 bits, with the
// low 16 bits kept. A product outside the signed 64-bit range stores 0.
void test_spark_time_is_stored_in_ticks() {
    struct Case {
        const char* text; // the sparktime value, or null for no key
        int16_t ticks;
        const char* message;
    };

    const Case cases[] = {
        {"5", 150, "stock sparktime 5 is 150 ticks"},
        {"2.9", 87, "2.9 seconds is 87 ticks"},
        {"4.1", 122, "a product just below 123 at 53 bits truncates to 122"},
        {"0.0333", 0, "under a tick truncates to 0"},
        {".5", 15, "a fraction without an integer part"},
        {"7.5e-1", 22, "an exponent"},
        {"1d1", 300, "d introduces an exponent"},
        {"  3", 90, "leading space is skipped"},
        {"0", 0, "zero"},
        {"-1", -30, "negative seconds are kept"},
        {"-2.9", -87, "negative values truncate toward zero"},
        {"1092.2", 32766, "largest tick count below the int16 limit"},
        {"1093", -32746, "32790 ticks wrap to the low 16 bits"},
        {"2184.5", -1, "65535 ticks keep their low 16 bits"},
        {"2184.54", 0, "65536.2 ticks keep only zero bits"},
        {"1234567.8", 9194, "37037034 ticks keep their low 16 bits"},
        {"-1234567.8", -9194, "a large negative product keeps its low 16 bits"},
        {"3.074457345618258e17", -2048, "largest product below 2^63 keeps its low 16 bits"},
        {"3.0744573456182586e17", 0, "a product of 2^63 is out of range and stores 0"},
        {"1e400", 0, "an overflowing number stores 0"},
        {"-1e400", 0, "an overflowing negative number stores 0"},
        {"0x10", 0, "a hexadecimal prefix reads as 0"},
        {"inf", 0, "inf is not a number"},
        {"nan", 0, "nan is not a number"},
        {"abc", 0, "text is not a number"},
        {nullptr, 0, "an absent sparktime is 0"},
    };
    std::string text;
    std::vector<std::string> names;
    for (std::size_t index = 0; index < std::size(cases); ++index) {
        names.push_back("SPARK" + std::to_string(index));
        text += "[" + names.back() + "]\n{\n  footprintx=1;\n  footprintz=1;\n  filename=x;\n";
        if (cases[index].text != nullptr)
            text += std::string("  sparktime=") + cases[index].text + ";\n";
        text += "}\n";
    }
    const auto docs = documents({text});
    FeatureDefTable table;
    for (const auto& name : names)
        require(load_feature_def(table, docs, name, nullptr).ok(), "spark case loads");
    if (table.defs.size() != std::size(cases))
        return;
    for (std::size_t index = 0; index < std::size(cases); ++index)
        require(table.defs[index].spark_time == cases[index].ticks, cases[index].message);
}

void test_null_host_leaves_refs_clear() {
    const auto docs = documents({rocks_tdf, objects_tdf});
    FeatureDefTable table;
    require(
        !init_feature_table(table, map_with({"ROCK01", "Dragons Teeth"}), docs, nullptr)
             .has_value(),
        "loads without a host"
    );
    const auto& rock = table.defs[0];
    require(
        rock.animation == 0 && rock.seq_name == 0 && rock.seq_name_die == 0 &&
            rock.burn_weapon == 0,
        "no host: asset refs are null"
    );
    require(
        rock.metal == 4464.0f && (rock.flags & OA_FEATURE_FLAG_RECLAIMABLE),
        "no host: values still load"
    );
    uint32_t object_ref = 1;
    std::memcpy(&object_ref, table.defs[1].animation_file, sizeof object_ref);
    require(object_ref == 0, "no host: object ref is null");
}

} // namespace

int main() {
    test_table_follows_tnt_order_and_fields();
    test_missing_definition_is_an_error();
    test_find_or_load_and_links();
    test_spark_time_is_stored_in_ticks();
    test_null_host_leaves_refs_clear();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("feature-defs: ok");
    return 0;
}
