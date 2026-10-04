// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The mod profile's files: a folder's oamod.yaml found whatever the case of
// its name, two that differ only in case refused, --print-profile's output
// and exit status, a --mod profile's limits block filling the Limits record,
// a --mod profile the engine cannot use stopping the run with every error, and
// the player's settings in the mod's INI file reaching the parameters the
// profile binds.

#include "oa/app/mod_profile_loader.hpp"
#include "oa/data/defs/layout.hpp"
#include "oa/data/mod_profile/registry.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>

namespace {

namespace fs = std::filesystem;
using namespace oa::app;

/// A profile that changes nothing.
constexpr std::string_view base_profile = "oamod: 1\n"
                                          "id: example\n"
                                          "name: Example mod\n"
                                          "version: \"1.0\"\n"
                                          "requires: {base: ta-3.1c, catalogue: 1}\n"
                                          "author:\n"
                                          "  name: unknown\n"
                                          "packaging:\n"
                                          "  revision: 1\n"
                                          "  date: 2026-10-04\n"
                                          "  packager: Open Annihilation\n";

/// Writes a file.
///
/// @param path the file
/// @param text its contents
void write(const fs::path& path, std::string_view text) {
    std::ofstream out{path, std::ios::binary};
    out << text;
}

void test_find(const fs::path& scratch) {
    const fs::path game = scratch / "game";
    fs::create_directories(game);
    std::string error;
    OA_CHECK(!find_mod_profile(game, error) && error.empty());
    write(game / "OAMOD.YAML", base_profile);
    const auto found = find_mod_profile(game, error);
    OA_CHECK(found && found->filename() == "OAMOD.YAML" && error.empty());

    // A file system that tells case apart may hold both spellings.
    write(game / "oamod.yaml", base_profile);
    std::string both;
    const bool case_sensitive = fs::exists(game / "OAMOD.YAML") &&
                                fs::exists(game / "oamod.yaml") &&
                                !fs::equivalent(game / "OAMOD.YAML", game / "oamod.yaml");
    if (case_sensitive) {
        OA_CHECK(!find_mod_profile(game, both));
        OA_CHECK(both.find("differ only in case") != std::string::npos);
    }
    OA_CHECK(!find_mod_profile(scratch / "missing", error) && !error.empty());
}

void test_print(const fs::path& scratch) {
    const fs::path game = scratch / "plain";
    fs::create_directories(game);
    std::ostringstream out;
    std::ostringstream err;
    OA_CHECK(print_mod_profile({}, game, false, out, err) == 0);
    OA_CHECK(out.str().find("no oamod.yaml; the folder plays base 3.1c") != std::string::npos);

    const fs::path file = scratch / "example.oamod";
    write(file, base_profile);
    std::ostringstream printed;
    std::ostringstream warnings;
    OA_CHECK(print_mod_profile(file, {}, false, printed, warnings) == 0);
    OA_CHECK(printed.str().find("\"id\": \"example\"") != std::string::npos);
    OA_CHECK(printed.str().find("sha256-sim  ") != std::string::npos);
    OA_CHECK(printed.str().find("sha256-full ") != std::string::npos);
    OA_CHECK(warnings.str().empty());

    // A hack this build does not implement yet refuses the profile, unless
    // a development run accepts it.
    std::string hack;
    for (const auto& entry : oa::data::mod_profile::registry::table().entries)
        if (hack.empty() && entry.kind == oa::data::mod_profile::registry::EntryKind::hack &&
            !entry.implemented && !entry.visual)
            hack = entry.id;
    if (!hack.empty()) {
        const fs::path hacked = scratch / "hacked.oamod";
        write(hacked, std::string{base_profile} + "hacks:\n  " + hack + ": true\n");
        std::ostringstream refused_out;
        std::ostringstream refused_err;
        OA_CHECK(print_mod_profile(hacked, {}, false, refused_out, refused_err) == 1);
        OA_CHECK(refused_err.str().find("error: ") != std::string::npos);
        OA_CHECK(
            refused_err.str().find(
                ":7:3: hacks." + hack + ": this engine does not implement the hack yet"
            ) != std::string::npos
        );
        OA_CHECK(refused_out.str().empty());
        std::ostringstream accepted_out;
        std::ostringstream accepted_err;
        OA_CHECK(print_mod_profile(hacked, {}, true, accepted_out, accepted_err) == 0);
        OA_CHECK(accepted_err.str().find("warning: ") != std::string::npos);
        OA_CHECK(accepted_out.str().find("\"" + hack + "\"") != std::string::npos);
    }

    std::ostringstream missing_out;
    std::ostringstream missing_err;
    OA_CHECK(print_mod_profile(scratch / "none.oamod", {}, false, missing_out, missing_err) == 1);
    OA_CHECK(missing_err.str().find("cannot be read") != std::string::npos);
}

void test_check(const fs::path& scratch) {
    const fs::path file = scratch / "check.oamod";
    write(file, base_profile);
    std::ostringstream out;
    const auto profile = check_mod_profile(file, false, out);
    OA_CHECK(profile.id == "example");
    OA_CHECK(out.str().find("mod profile example 1.0 resolved") != std::string::npos);
    // Without a limits block the game keeps 3.1c's capacities.
    OA_CHECK(profile.limits == oa::data::limits::Limits{});

    // A limits block fills the one Limits record the game sizes its tables from.
    const fs::path raised = scratch / "raised.oamod";
    write(
        raised,
        std::string{base_profile} + "limits:\n"
                                    "  units-per-player: {default: 1000, max: 1500}\n"
                                    "  path-search-budget: true\n"
                                    "  build-list-entries: {copy: 36, overflow: dynamic}\n"
    );
    std::ostringstream raised_out;
    const auto raised_profile = check_mod_profile(raised, false, raised_out);
    OA_CHECK(raised_profile.limits.units_per_player.default_limit == 1000);
    OA_CHECK(raised_profile.limits.units_per_player.minimum == 20);
    OA_CHECK(raised_profile.limits.units_per_player.maximum == 1500);
    OA_CHECK(raised_profile.limits.path_search.nodes == 66650);
    OA_CHECK(raised_profile.limits.build_lists.copy == 36);
    OA_CHECK(
        raised_profile.limits.build_lists.overflow == oa::data::limits::BuildListOverflow::dynamic
    );
    OA_CHECK(raised_profile.limits.effects == oa::data::limits::Effects{});
    OA_CHECK(
        oa::data::limits::check_limits(raised_profile.limits) == oa::data::limits::LimitsError::none
    );

    const fs::path broken = scratch / "broken.oamod";
    write(broken, std::string{base_profile} + "colour: red\nhacks: {nope.nope: true}\n");
    std::string message;
    try {
        std::ostringstream ignored;
        std::ignore = check_mod_profile(broken, false, ignored);
    } catch (const std::runtime_error& error) {
        message = error.what();
    }
    OA_CHECK(message.find("the mod profile cannot be used") != std::string::npos);
    OA_CHECK(message.find("unknown top-level key 'colour'") != std::string::npos);
    OA_CHECK(message.find("hacks.nope.nope: unknown hack") != std::string::npos);
}

void test_settings(const fs::path& scratch) {
    const auto ini = read_ini_settings(
        "Loose = 1\n; a comment\n[Preferences] ; custom settings\n"
        "UnitLimit = 1000;\r\n"
        "SfxLimit=20480 ; experimental\n"
        "  MultiGameWeapon = FALSE;\n"
        "no value here\n"
        "[Other]\nKey = text with spaces\n"
    );
    OA_CHECK(ini.size() == 4);
    OA_CHECK(ini[0].name == "Preferences/UnitLimit" && ini[0].value == "1000");
    OA_CHECK(ini[1].name == "Preferences/SfxLimit" && ini[1].value == "20480");
    OA_CHECK(ini[2].name == "Preferences/MultiGameWeapon" && ini[2].value == "FALSE");
    OA_CHECK(ini[3].name == "Other/Key" && ini[3].value == "text with spaces");

    // A profile that binds settings, keeps its registry under a root of its
    // own and seeds two registry values.
    const fs::path game = scratch / "settings";
    fs::create_directories(game);
    write(
        game / "oamod.yaml",
        std::string{base_profile} +
            "identity: {settings-file: Mod.ini, registry-root: 'Software\\Example'}\n"
            "limits:\n"
            "  units-per-player: true\n"
            "  effects: true\n"
            "settings:\n"
            "  limits.units-per-player.default: {registry: UnitLimit}\n"
            "  limits.effects.queue: {ini: Preferences/SfxLimit}\n"
            "  registry-seeds: {UnitLimit: 900, GameSpeed: 10}\n"
    );
    write(game / "MOD.INI", "[Preferences]\nSfxLimit = 1000;\n");
    const auto seeded = resolve_folder_profile({game}, {});
    OA_CHECK(seeded.errors.empty() && seeded.profile);
    OA_CHECK(seeded.profile->limits.effects.queue == 1000);
    OA_CHECK(seeded.profile->limits.units_per_player.default_limit == 900);
    // --print-profile reads the same INI file through the same reader.
    const auto printed = mod_settings_of(*seeded.profile, {game}, nullptr);
    OA_CHECK(printed.ini.size() == 1 && printed.registry.empty());

    const auto prefix = registry_key_prefix(*seeded.profile);
    OA_CHECK(prefix == "registry:Software\\Example\\");
    oa::platform::preferences::Values values{{prefix + "Total Annihilation|gamespeed", "5"}};
    OA_CHECK(seed_registry(*seeded.profile, values));
    OA_CHECK(values.at(prefix + "Total Annihilation|UnitLimit") == "900");
    OA_CHECK(!values.contains(prefix + "Total Annihilation|GameSpeed"));
    OA_CHECK(!seed_registry(*seeded.profile, values));
    values[prefix + "Total Annihilation|UnitLimit"] = "700";
    ModChoice choice{};
    choice.preferences = &values;
    const auto chosen = resolve_folder_profile({game}, choice);
    OA_CHECK(chosen.profile && chosen.profile->limits.units_per_player.default_limit == 700);

    // The profile's source, read again for Developer Mode, resolves to the
    // same profile, and with an override laid over it to that profile with
    // the override.
    const auto source = folder_profile_source({game}, choice);
    OA_CHECK(source.has_value());
    if (source) {
        OA_CHECK(source->name.ends_with("oamod.yaml") && !source->text.empty());
        OA_CHECK(source->options.overrides.empty() && !source->options.settings.ini.empty());
        const auto again =
            oa::data::mod_profile::resolve_profile(source->text, source->name, source->options);
        OA_CHECK(
            again.resolution && again.resolution->profile.full_hash == chosen.profile->full_hash
        );
        auto options = source->options;
        options.overrides.push_back({"units.id-reuse-delay", true, {}});
        const auto laid =
            oa::data::mod_profile::resolve_profile(source->text, source->name, options);
        OA_CHECK(laid.resolution && laid.resolution->profile.rules.units.id_reuse_delay.enabled);
        OA_CHECK(laid.resolution && laid.resolution->profile.sim_hash != chosen.profile->sim_hash);
    }
    OA_CHECK(!folder_profile_source({scratch / "nothing"}, {}).has_value());
    OA_CHECK(!folder_profile_source({}, {}).has_value());

    // The base game's registry root keeps the base game's own keys.
    OA_CHECK(registry_key_prefix(oa::data::mod_profile::ModProfile{}).empty());
}

void test_layout(const fs::path& scratch) {
    OA_CHECK(data_layout_of(nullptr) == oa::data::defs::DataLayout{});
    const auto base_plan = discovery_plan_of(nullptr);
    OA_CHECK(base_plan.revision_archive == "rev31.GP3" && base_plan.folders_as_disc);
    const fs::path file = scratch / "layout.oamod";
    write(
        file,
        std::string{base_profile} +
            "identity: {network-version: [10, 2], side-names: [Red, Blue]}\n"
            "layout:\n"
            "  revision-archive: modrev.gp4\n"
            "  archive-patterns: {ufo: \"*.SWX\"}\n"
            "  directories: {units: unitsM, weapons: weaponM}\n"
            "  file-extensions: {unit-definition: UNM}\n"
            "  map-units-section: unitsM\n"
    );
    const auto result = load_mod_profile(file, false);
    OA_CHECK(result.resolution.has_value());
    const auto& profile = result.resolution->profile;
    const auto layout = data_layout_of(&profile);
    using oa::data::defs::DataDirectory;
    OA_CHECK(layout.directories[static_cast<std::size_t>(DataDirectory::units)] == "unitsM");
    OA_CHECK(layout.directories[static_cast<std::size_t>(DataDirectory::weapons)] == "weaponM");
    OA_CHECK(layout.directories[static_cast<std::size_t>(DataDirectory::guis)] == "guis");
    OA_CHECK(layout.unit_extension == "UNM" && layout.map_units_section == "unitsM");
    OA_CHECK(layout.build_version[0] == 10 && layout.build_version[1] == 2);
    OA_CHECK(layout.side_names[0] == "Red" && layout.side_names[1] == "Blue");
    const auto plan = discovery_plan_of(&profile);
    OA_CHECK(plan.revision_archive == "modrev.gp4" && plan.ufo_pattern == "*.SWX");
    OA_CHECK(plan.ccx_pattern == "*.CCX" && plan.hpi_pattern == "*.HPI");
}

void test_mod_folders(const fs::path& scratch) {
    const fs::path game = scratch / "offered";
    fs::create_directories(game / "MODS" / "beta");
    fs::create_directories(game / "MODS" / "Alpha");
    fs::create_directories(game / "MODS" / "empty");
    write(game / "MODS" / "beta" / "oamod.yaml", base_profile);
    write(game / "MODS" / "Alpha" / "OAMOD.YAML", base_profile);
    const auto offered = list_mod_folders(game);
    OA_CHECK(offered.size() == 2);
    OA_CHECK(offered.size() == 2 && offered[0].filename() == "Alpha");
    OA_CHECK(offered.size() == 2 && offered[1].filename() == "beta");
    OA_CHECK(list_mod_folders(scratch / "nothing").empty());

    // A mod folder needs its own profile, and a --mod file replaces it.
    const auto lacking = resolve_folder_profile(
        {game / "MODS" / "empty", game}, {game / "MODS" / "empty", {}, false, nullptr}
    );
    OA_CHECK(!lacking.errors.empty() && !lacking.profile);
    const auto named = resolve_folder_profile(
        {game / "MODS" / "empty", game},
        {game / "MODS" / "empty", game / "MODS" / "beta" / "oamod.yaml", false, nullptr}
    );
    OA_CHECK(named.errors.empty() && named.profile && named.profile->id == "example");
}

} // namespace

int main() {
    const fs::path scratch = oa::test::make_scratch_directory("oa-app-mod-profile");
    test_find(scratch);
    test_print(scratch);
    test_check(scratch);
    test_settings(scratch);
    test_layout(scratch);
    test_mod_folders(scratch);
    std::error_code ignored;
    fs::remove_all(scratch, ignored);
    return oa::test::check_exit_status();
}
