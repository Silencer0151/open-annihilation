// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The simulation hash the run plays under, for an extension, and the check
// that the hash follows the profile and a Developer Mode override.
#include "oa/app/runtime.hpp"

#include "engine_settings_state.hpp"

#include "oa/data/mod_profile.hpp"
#include "oa/data/mod_profile/overrides.hpp"
#include "oa/data/mod_profile/value.hpp"

#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace oa::app {

namespace {

namespace profiles = oa::data::mod_profile;

/// Resolves the plain baseline and returns its sim hash, as text.
///
/// @return 64 lower-case hexadecimal digits
/// @throws std::runtime_error when the baseline does not resolve
std::string resolve_baseline_hash() {
    const std::string text = profiles::base_game_profile_text();
    const auto resolved = profiles::resolve_profile(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()),
        profiles::base_game_id
    );
    if (!resolved.resolution)
        throw std::runtime_error("the plain baseline does not resolve");
    return profiles::digest_text(resolved.resolution->profile.sim_hash);
}

/// The plain baseline's sim hash, as text, resolved once for the process.
///
/// The baseline is made from the rule table built into the program alone.
/// The mod played, a game mode, Developer Mode and a soft restart for a mod
/// switch change which profile simulation_hash reads, never this. A resolve
/// that throws is tried again on the next call.
///
/// @return 64 lower-case hexadecimal digits
/// @throws std::runtime_error when the baseline does not resolve
const std::string& baseline_hash() {
    static const std::string hash = resolve_baseline_hash();
    return hash;
}

} // namespace

std::string simulation_hash(const Runtime& runtime) {
    if (const auto* profile = runtime.mod_profile())
        return profiles::digest_text(profile->sim_hash);
    if (runtime.engine_settings_ && runtime.engine_settings_->layered)
        return profiles::digest_text(runtime.engine_settings_->base_sim_hash);
    if (runtime.options_.mod_profile)
        return profiles::digest_text(runtime.options_.mod_profile->sim_hash);
    return baseline_hash();
}

void Runtime::check_simulation_hash() {
    const std::string played = simulation_hash(*this);
    if (played.size() != 64)
        throw std::runtime_error("the simulation hash is not 64 hexadecimal digits");
    const std::string baseline = baseline_hash();
    if (!options_.mod_profile) {
        if (played != baseline)
            throw std::runtime_error("with no mod the simulation hash is not the base game's");
    } else {
        if (played != profiles::digest_text(options_.mod_profile->sim_hash))
            throw std::runtime_error("under a mod the simulation hash is not that profile's");
        if (played == baseline)
            throw std::runtime_error("a mod that changes the simulation kept the base game's hash");
    }

    const auto original = engine_settings();
    auto settings = original;
    settings.developer_mode = true;
    settings.hack_overrides.push_back(
        profiles::HackOverride{"ai.attack-wave-size", true, {{"units", profiles::make_integer(40)}}}
    );
    apply_engine_settings(settings);
    const std::string changed = simulation_hash(*this);
    if (changed == played)
        throw std::runtime_error(
            "a simulation-changing Developer Mode override left the simulation hash unchanged"
        );

    // Putting the settings back plays the profile the run started with, and
    // the kept baseline still agrees with a fresh resolve.
    apply_engine_settings(original);
    if (simulation_hash(*this) != played)
        throw std::runtime_error("turning Developer Mode off did not bring the hash back");
    if (baseline_hash() != resolve_baseline_hash())
        throw std::runtime_error("the kept baseline hash differs from a fresh resolve");
    std::cout << "simulation hash check: passed\n";
}

} // namespace oa::app
