// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the engine reads from the installed game's TDF texts outside the unit
// and weapon loaders, digested: every feature definition, link and terrain
// record, the unit announcements of every sound category, both sides' HUD
// layouts, the meteor defaults, and the GlobalHeader and schema keys the
// session and the scenario read from every map. A change to how any of them
// is read shows as a changed digest; with OA_PRINT_TRANSCRIPT set, the
// transcript a mismatched digest covers is printed.

#include "oa/audio/unit_announcements.hpp"
#include "oa/data/persist/save_sections.hpp"
#include "oa/data/unit_definitions.hpp"
#include "oa/formats/tdf.hpp"
#include "oa/platform/system.hpp"
#include "oa/sim/map_runtime.hpp"
#include "oa/sim/map_runtime/feature_defs.hpp"
#include "oa/test/game_assets.hpp"
#include "oa/ui/hud/resource_bar.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Lines of what a reader produced, folded into a 64-bit FNV-1a digest.
class Transcript {
  public:

    void line(const std::string& text) {
        text_ += text;
        text_ += '\n';
    }

    [[nodiscard]] uint64_t digest() const {
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (const char c : text_) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }

    void expect(uint64_t expected, const char* what) const {
        const auto actual = digest();
        if (actual == expected)
            return;
        std::fprintf(
            stderr,
            "FAIL: %s digest 0x%016llx, expected 0x%016llx\n",
            what,
            static_cast<unsigned long long>(actual),
            static_cast<unsigned long long>(expected)
        );
        if (oa::platform::environment_value("OA_PRINT_TRANSCRIPT"))
            std::fprintf(stderr, "%s", text_.c_str());
        ++failures;
    }

  private:

    std::string text_;
};

std::string hex(const void* bytes, std::size_t size) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        const auto byte = static_cast<const unsigned char*>(bytes)[i];
        out += digits[byte >> 4];
        out += digits[byte & 15];
    }
    return out;
}

std::string text_of(const std::vector<uint8_t>& bytes) {
    return {bytes.begin(), bytes.end()};
}

class FeatureReader final : public oa::sim::map_runtime::FeatureAssetReader {
  public:

    explicit FeatureReader(const oa::AssetStore& store) : store_(store) {}

    oa::data::unit_definitions::Result<std::vector<std::string>> list_effective_recursive(
        std::string_view directory, std::string_view extension
    ) const override {
        return {store_.list_effective_recursive(directory, extension), {}};
    }

    oa::data::unit_definitions::Result<std::string> read(std::string_view path) const override {
        return {text_of(oa::test::read_game_file(store_, path)), {}};
    }

  private:

    const oa::AssetStore& store_;
};

// A name's 32-bit FNV-1a hash, never 0: the ref a resolver hands back for it.
oa::oa_ref32 name_ref(const char* name) {
    uint32_t hash = 0x811c9dc5U;
    for (const char* at = name; *at != '\0'; ++at) {
        hash ^= static_cast<unsigned char>(*at);
        hash *= 0x01000193U;
    }
    return hash != 0 ? hash : 1;
}

// Resolves every name to its hash, so each record carries the names it read.
oa::sim::map_runtime::FeatureDefHost naming_host() {
    oa::sim::map_runtime::FeatureDefHost host{};
    host.load_animation = [](void*, const char* name) { return name_ref(name); };
    host.load_object = [](void*, const char* name) { return name_ref(name); };
    host.find_weapon = [](void*, const char* name) { return name_ref(name); };
    host.find_sequence = [](void*, oa::oa_ref32 archive, const char* name, bool one_shot) {
        return name_ref(name) ^ archive ^ (one_shot ? 1U : 0U);
    };
    return host;
}

// Features whose TDF block lacks a ';', which the digests leave out; the
// checks below say what each reads. As in 3.1c, the entry before the missing
// ';' runs on to the next one and takes the following key with it: Coral20
// has no height, and Barrier13's shadow sequence name holds the rest of its
// line and the next.
bool run_on_feature(std::string_view name) {
    return name == "Coral20" || name == "Barrier13";
}

// Every feature the installed TDFs define, loaded in catalog order with its
// links, and every terrain record.
void test_features(const oa::AssetStore& store) {
    namespace map_runtime = oa::sim::map_runtime;
    const FeatureReader reader(store);
    const auto catalog = map_runtime::load_feature_catalog(reader);
    check(catalog.ok(), "the feature catalog loads");
    if (!catalog.ok())
        return;
    check(catalog.value->size() > 1000, "the install defines over a thousand features");

    Transcript terrain;
    std::string barrier_archive;
    for (const auto& feature : *catalog.value) {
        const auto& t = feature.terrain;
        if (feature.name == "Coral20")
            check(t.height == 0, "Coral20's terrain height");
        if (feature.name == "Barrier13") {
            check(t.seqname == "barrier13", "Barrier13's terrain sequence");
            barrier_archive = t.filename;
        }
        if (run_on_feature(feature.name))
            continue;
        terrain.line(
            feature.name + " " + std::to_string(t.footprint_x) + "x" +
            std::to_string(t.footprint_z) + " metal=" + std::to_string(t.metal) + " overlay=" +
            std::to_string(t.metal_overlay) + " blocking=" + std::to_string(t.blocking) +
            " reclaimable=" + std::to_string(t.reclaimable) +
            " model=" + std::to_string(t.requires_model_instance) + " object=" + t.object +
            " filename=" + t.filename + " seqname=" + t.seqname +
            " geothermal=" + std::to_string(t.geothermal) +
            " animating=" + std::to_string(t.animating) + " height=" + std::to_string(t.height) +
            " nodrawundergray=" + std::to_string(t.no_draw_under_gray)
        );
    }
    terrain.expect(0xdccb2bd19a5534d7ULL, "feature terrain");

    auto documents = map_runtime::load_feature_documents(reader);
    check(static_cast<bool>(documents), "the feature documents load");
    if (!documents)
        return;
    const auto host = naming_host();
    map_runtime::FeatureDefTable table;
    for (const auto& feature : *catalog.value) {
        const auto loaded =
            map_runtime::find_or_load_feature(table, documents.value, feature.name, &host);
        check(loaded.ok(), "every catalogued feature loads");
    }
    check(!map_runtime::load_feature_links(table, documents.value, &host), "the links resolve");
    Transcript definitions;
    for (const auto& def : table.defs) {
        const std::string_view name(def.name);
        if (name == "Coral20")
            check(def.height == 0 && def.footprint_z == 10, "Coral20's height");
        if (name == "Barrier13")
            check(
                def.seq_name_shadow == (name_ref("barriershad13\r\n\t\r\n\tanimtrans=0") ^
                                        name_ref(barrier_archive.c_str())),
                "Barrier13's shadow sequence"
            );
        if (run_on_feature(name))
            continue;
        definitions.line(hex(&def, sizeof def));
    }
    definitions.expect(0x6495bc79e98d61c0ULL, "feature definitions");
}

// Every announcement of every sound category in SOUND.TDF.
void test_announcements(const oa::AssetStore& store) {
    using oa::audio::game_audio::UnitAnnouncementCategory;
    using oa::audio::game_audio::UnitSoundCatalog;
    const auto text = text_of(oa::test::read_game_file(store, "gamedata/sound.tdf"));
    check(!text.empty(), "the install holds sound.tdf");
    const auto catalog = UnitSoundCatalog::parse_sound_tdf(text);
    oa::formats::tdf::Document sound;
    oa::formats::tdf::document_init(&sound);
    check(
        oa::formats::tdf::parse_text(
            &sound, text.data(), static_cast<uint32_t>(text.size()), false, nullptr
        ),
        "sound.tdf parses"
    );
    Transcript transcript;
    for (uint32_t i = 0; i < oa::formats::tdf::child_count(sound.root); ++i) {
        const char* name = oa::formats::tdf::child_at(sound.root, i)->name;
        transcript.line(std::string("[") + name + "]");
        for (uint32_t category = 1; category < 24; ++category) {
            const auto* choices =
                catalog.choices(name, static_cast<UnitAnnouncementCategory>(category));
            if (choices == nullptr)
                continue;
            for (const auto& choice : *choices)
                transcript.line(
                    std::to_string(category) + " " + choice.sound + " | " + choice.text
                );
        }
    }
    oa::formats::tdf::document_free(&sound);
    transcript.expect(0x0134b97e82bbdd28ULL, "unit announcements");
}

std::string rect_text(const oa::ui::hud::Rect& rect) {
    return std::to_string(rect.x) + "," + std::to_string(rect.y) + "," +
           std::to_string(rect.width) + "," + std::to_string(rect.height);
}

// Both sides' HUD layouts in SIDEDATA.TDF.
void test_side_layouts(const oa::AssetStore& store) {
    const auto text = text_of(oa::test::read_game_file(store, "gamedata/sidedata.tdf"));
    Transcript transcript;
    for (int32_t side = 0; side < 2; ++side) {
        oa::ui::hud::SideLayout l;
        check(oa::ui::hud::parse_side_layout(text, side, l), "each side has a layout");
        transcript.line(
            std::to_string(l.metal_color) + " " + std::to_string(l.energy_color) + " " +
            rect_text(l.metal_bar) + " " + rect_text(l.energy_bar)
        );
        for (const int32_t value :
             {l.metal_num_x,       l.metal_num_y,       l.metal_max_x,       l.metal_max_y,
              l.metal_zero_x,      l.metal_zero_y,      l.metal_produced_x,  l.metal_produced_y,
              l.metal_consumed_x,  l.metal_consumed_y,  l.energy_num_x,      l.energy_num_y,
              l.energy_max_x,      l.energy_max_y,      l.energy_zero_x,     l.energy_zero_y,
              l.energy_produced_x, l.energy_produced_y, l.energy_consumed_x, l.energy_consumed_y})
            transcript.line(std::to_string(value));
        for (const auto* rect :
             {&l.unit_name,
              &l.damage_bar,
              &l.unit_metal_make,
              &l.unit_metal_use,
              &l.unit_energy_make,
              &l.unit_energy_use,
              &l.logo2,
              &l.mission_text,
              &l.unit_name2,
              &l.damage_bar2,
              &l.name,
              &l.description})
            transcript.line(rect_text(*rect));
    }
    transcript.expect(0xc0c35cecac10a5c5ULL, "side layouts");
}

// The engine's reads of an OTA's GlobalHeader and of one of its sections.
struct ScenarioKeys {
    const oa::formats::tdf::Block* section{};

    [[nodiscard]] int32_t integer(std::string_view key, int32_t fallback) const {
        return oa::formats::tdf::get_int(section, std::string(key).c_str(), fallback);
    }

    [[nodiscard]] std::optional<std::string> text(std::string_view key) const {
        const char* value = oa::formats::tdf::find_value(section, std::string(key).c_str());
        if (value == nullptr)
            return std::nullopt;
        return std::string(value);
    }

    [[nodiscard]] double real(std::string_view key) const {
        return oa::formats::tdf::get_double(section, std::string(key).c_str(), 0.0);
    }
};

// The keys the session and the scenario read from every map's OTA.
void test_scenarios(const oa::AssetStore& store) {
    static constexpr std::string_view header_integers[] = {
        "lavaworld",
        "MinWindSpeed",
        "MaxWindSpeed",
        "TidalStrength",
        "KillEnemyCommander",
        "DestroyAllUnits",
        "KillAllMobileUnits",
        "CommanderKilled",
        "AllUnitsKilled",
        "VictoryTimerRunsOut",
        "DeathTimerRunsOut",
        "AnyUnitPassesX",
        "AnyUnitPassesZ",
    };
    static constexpr std::string_view header_texts[] = {
        "BuildUnitType",
        "CaptureUnitType",
        "KillAllOfType",
        "KillUnitType",
        "MoveUnitToRadius",
        "UnitTypePassesX",
        "UnitTypePassesZ",
        "AllUnitsKilledOfType",
        "UnitTypeKilled",
    };
    static constexpr std::string_view header_reals[] = {"killmul", "timemul"};
    static constexpr std::string_view schema_integers[] = {"SurfaceMetal", "MeteorRadius"};
    static constexpr std::string_view schema_reals[] = {
        "MeteorDensity", "MeteorDuration", "MeteorInterval"
    };
    const auto maps = store.list_effective_recursive("maps", ".ota");
    check(maps.size() > 50, "the install holds the maps");
    Transcript transcript;
    for (const auto& path : maps) {
        transcript.line(path);
        const auto text = text_of(oa::test::read_game_file(store, path));
        oa::formats::tdf::OwnedDocument document;
        const bool parsed = document.parse(text);
        check(parsed, "every OTA parses");
        if (!parsed)
            continue;
        const ScenarioKeys header{oa::formats::tdf::find_child(document.root(), "GlobalHeader")};
        check(header.section != nullptr, "every OTA has a GlobalHeader");
        if (header.section == nullptr)
            continue;
        for (const auto key : header_integers)
            transcript.line(
                std::string(key) + "=" + std::to_string(header.integer(key, 0)) + "/" +
                std::to_string(header.integer(key, -1))
            );
        for (const auto key : header_texts)
            transcript.line(std::string(key) + "=" + header.text(key).value_or("(none)"));
        for (const auto key : header_reals)
            transcript.line(std::string(key) + "=" + std::to_string(header.real(key)));
        for (uint32_t index = 0; index < oa::formats::tdf::child_count(header.section); ++index) {
            const ScenarioKeys keys{oa::formats::tdf::child_at(header.section, index)};
            transcript.line(std::string("[") + keys.section->name + "]");
            for (const auto key : schema_integers)
                transcript.line(std::string(key) + "=" + std::to_string(keys.integer(key, 0)));
            transcript.line("MeteorWeapon=" + keys.text("MeteorWeapon").value_or("(none)"));
            for (const auto key : schema_reals)
                transcript.line(std::string(key) + "=" + std::to_string(keys.real(key)));
        }
    }
    transcript.expect(0x4214c00fd0029824ULL, "scenario keys");
}

// The [Default] block of METEOR.TDF, as a match without meteor keys reads it.
void test_meteor_defaults(const oa::AssetStore& store) {
    const auto text = text_of(oa::test::read_game_file(store, "gamedata/meteor.tdf"));
    check(!text.empty(), "the install holds meteor.tdf");
    oa::formats::tdf::OwnedDocument document;
    check(document.parse(text), "meteor.tdf parses");
    const auto* defaults = oa::formats::tdf::find_child(document.root(), "Default");
    check(defaults != nullptr, "meteor.tdf has a [Default] block");
    if (defaults == nullptr)
        return;
    oa::data::persist::MeteorTdf tdf{};
    tdf.context = const_cast<oa::formats::tdf::Block*>(defaults);
    tdf.text = [](void* context, const char* key, char* out, std::size_t out_bytes) {
        return oa::formats::tdf::get_string(
            static_cast<const oa::formats::tdf::Block*>(context), key, out, out_bytes, nullptr
        );
    };
    tdf.integer = [](void* context, const char* key, int32_t fallback) {
        return oa::formats::tdf::get_int(
            static_cast<const oa::formats::tdf::Block*>(context), key, fallback
        );
    };
    tdf.real = [](void* context, const char* key, double fallback) {
        return oa::formats::tdf::get_double(
            static_cast<const oa::formats::tdf::Block*>(context), key, fallback
        );
    };
    oa::sim::world_environment::MeteorSettings settings{};
    check(
        oa::data::persist::load_meteor_config(&tdf, &settings) ==
            oa::data::persist::MeteorConfigResult::loaded,
        "the meteor defaults load"
    );
    Transcript transcript;
    transcript.line(hex(&settings, sizeof settings));
    transcript.expect(0xc7ca1a5c1bc9377bULL, "meteor defaults");
}

} // namespace

int main() {
    const auto store = oa::test::require_game_assets("the installed game's TDF readers");
    test_features(store);
    test_announcements(store);
    test_side_layouts(store);
    test_scenarios(store);
    test_meteor_defaults(store);
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("installed TDF reader tests passed\n");
    return 0;
}
