// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The mod profile's files: finding a folder's oamod.yaml, reading it, and
// the --print-profile and --mod options.

#include "oa/app/mod_profile_loader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace oa::app {

namespace {

namespace fs = std::filesystem;
namespace mod_profile = data::mod_profile;

/// Lower-cases the ASCII letters of a file name.
///
/// @param name the name, UTF-8
/// @return the name with A-Z made a-z
std::string folded(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return name;
}

/// Writes a path as UTF-8.
///
/// @param path the path
/// @return its text
std::string path_text(const fs::path& path) {
    const auto text = path.u8string();
    return std::string{text.begin(), text.end()};
}

} // namespace

std::optional<fs::path> find_mod_profile(const fs::path& folder, std::string& error) {
    error.clear();
    std::vector<fs::path> found;
    std::error_code listing;
    for (fs::directory_iterator entry{folder, listing}, end; !listing && entry != end;
         entry.increment(listing)) {
        const auto name = entry->path().filename().u8string();
        if (folded(std::string{name.begin(), name.end()}) == mod_profile_name)
            found.push_back(entry->path());
    }
    if (listing) {
        error = path_text(folder) + ": cannot list the folder: " + listing.message();
        return std::nullopt;
    }
    if (found.size() > 1) {
        std::sort(found.begin(), found.end());
        error = path_text(folder) + ": ";
        for (size_t index = 0; index < found.size(); ++index)
            error += (index > 0 ? ", " : "") + path_text(found[index].filename());
        error += " differ only in case; keep one";
        return std::nullopt;
    }
    if (found.empty())
        return std::nullopt;
    return found.front();
}

mod_profile::ResolveResult load_mod_profile(
    const fs::path& file, bool accept_unimplemented_hacks, const mod_profile::Settings& settings
) {
    mod_profile::ResolveResult result{};
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        result.errors.push_back(mod_profile::Diagnostic{path_text(file), {}, {}, "cannot be read"});
        return result;
    }
    // One byte past the limit is enough for the reader to refuse a file too large.
    std::vector<uint8_t> bytes;
    bytes.reserve(formats::oamod::max_input_bytes + 1);
    for (std::istreambuf_iterator<char> at{in}, end;
         at != end && bytes.size() <= formats::oamod::max_input_bytes;
         ++at)
        bytes.push_back(static_cast<uint8_t>(*at));
    mod_profile::ResolveOptions options{};
    options.accept_unimplemented_hacks = accept_unimplemented_hacks;
    options.settings = settings;
    return mod_profile::resolve_profile(bytes, path_text(file), options);
}

int print_mod_profile(
    const fs::path& mod_file,
    const fs::path& game_dir,
    bool accept_unimplemented_hacks,
    std::ostream& out,
    std::ostream& err
) {
    fs::path file = mod_file;
    if (file.empty()) {
        std::string error;
        const auto found = find_mod_profile(game_dir, error);
        if (!error.empty()) {
            err << "open-annihilation: " << error << '\n';
            return 1;
        }
        if (!found) {
            out << path_text(game_dir) << ": no " << mod_profile_name
                << "; the folder plays base 3.1c\n";
            return 0;
        }
        file = *found;
    }
    auto result = load_mod_profile(file, accept_unimplemented_hacks);
    // The player's settings in the mod's INI file, in the folder, apply as in a game.
    if (result.resolution && !game_dir.empty()) {
        const auto settings = mod_settings_of(result.resolution->profile, {game_dir}, nullptr);
        if (!settings.ini.empty())
            result = load_mod_profile(file, accept_unimplemented_hacks, settings);
    }
    for (const auto& warning : result.warnings)
        err << "warning: " << mod_profile::format_diagnostic(warning) << '\n';
    for (const auto& error : result.errors)
        err << "error: " << mod_profile::format_diagnostic(error) << '\n';
    if (!result.resolution)
        return 1;
    out << mod_profile::describe_resolution(*result.resolution);
    return 0;
}

mod_profile::ModProfile
check_mod_profile(const fs::path& mod_file, bool accept_unimplemented_hacks, std::ostream& out) {
    const auto result = load_mod_profile(mod_file, accept_unimplemented_hacks);
    for (const auto& warning : result.warnings)
        out << "open-annihilation: warning: " << mod_profile::format_diagnostic(warning) << '\n';
    if (!result.resolution) {
        std::string message = "the mod profile cannot be used:";
        for (const auto& error : result.errors)
            message += "\n  " + mod_profile::format_diagnostic(error);
        throw std::runtime_error(message);
    }
    const auto& profile = result.resolution->profile;
    report_mod_profile(profile, {}, out);
    return profile;
}

void report_mod_profile(
    const mod_profile::ModProfile& profile,
    const std::vector<std::string>& warnings,
    std::ostream& out
) {
    for (const auto& warning : warnings)
        out << "open-annihilation: warning: " << warning << '\n';
    out << "open-annihilation: mod profile " << profile.id << ' ' << profile.version
        << " resolved, sim hash " << mod_profile::digest_text(profile.sim_hash)
        << "; its layout, identity, limits, rules and script extensions apply\n";
}

} // namespace oa::app
