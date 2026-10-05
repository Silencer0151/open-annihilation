// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Where a package installs, and what it asks first, from what the folders
// of Mods hold.

#include "files.hpp"

#include "oa/app/mod_install.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace oa::app::mod_install {

namespace fs = std::filesystem;

namespace {

/// Tells whether a folder holds the incoming mod, at any version.
///
/// @param held what the folder holds
/// @param incoming the mod
/// @return true when it holds that id
bool holds(const InstalledMod& held, const Incoming& incoming) noexcept {
    return held.kind == FolderKind::mod && held.id == incoming.id;
}

/// Returns what a folder holds, through the hooks.
///
/// @param hooks the hooks
/// @param folder the folder's name
/// @return what it holds; missing without a hook
InstalledMod look(const ModsFolderHooks& hooks, std::string_view folder) {
    return hooks.look != nullptr ? hooks.look(hooks.context, folder) : InstalledMod{};
}

/// Returns what a folder's .backup holds, through the hooks.
///
/// @param hooks the hooks
/// @param folder the folder's name
/// @return what it holds; nothing without a hook
std::optional<InstalledMod> backup_of(const ModsFolderHooks& hooks, std::string_view folder) {
    return hooks.backup_of != nullptr ? hooks.backup_of(hooks.context, folder) : std::nullopt;
}

/// Plans an update or a reinstall of a folder that holds the mod at the
/// incoming version.
///
/// @param hooks the hooks
/// @param incoming the mod
/// @param folder the folder
/// @param held what it holds
/// @return the plan
InstallPlan same_version(
    const ModsFolderHooks& hooks,
    const Incoming& incoming,
    const std::string& folder,
    const InstalledMod& held
) {
    InstallPlan plan{};
    plan.kind = held.revision == incoming.revision && held.revision != 0 ? PlanKind::ask_reinstall
                                                                         : PlanKind::ask_update;
    plan.target = folder;
    plan.installed = held;
    plan.backup = backup_of(hooks, folder);
    plan.older = plan.kind == PlanKind::ask_update && held.revision != 0 &&
                 incoming.revision < held.revision;
    return plan;
}

} // namespace

ModsFolderHooks mods_folder_hooks(const fs::path& mods) noexcept {
    ModsFolderHooks hooks{};
    hooks.context = const_cast<fs::path*>(&mods);
    hooks.look = [](void* context, std::string_view folder) {
        return read_installed_mod(*static_cast<const fs::path*>(context) / detail::path_of(folder));
    };
    hooks.backup_of = [](void* context, std::string_view folder) {
        return read_backup(*static_cast<const fs::path*>(context) / detail::path_of(folder));
    };
    return hooks;
}

std::vector<std::string> alongside_names(const Incoming& incoming) {
    const std::string first = incoming.id + "-" + version_folder_part(incoming.version);
    std::vector<std::string> names{first};
    for (uint32_t number = 2; number <= alongside_tries; ++number)
        names.push_back(first + "-" + std::to_string(number));
    return names;
}

InstallPlan plan_install(const Incoming& incoming, const ModsFolderHooks& hooks) {
    const std::string own = incoming.id;
    const InstalledMod at_own = look(hooks, own);
    // Its own folder holds it at this version: an update or a reinstall.
    if (holds(at_own, incoming) && at_own.version == incoming.version)
        return same_version(hooks, incoming, own, at_own);
    // A folder made alongside holds it at this version.
    const std::vector<std::string> alongside = alongside_names(incoming);
    for (const std::string& folder : alongside) {
        const InstalledMod held = look(hooks, folder);
        if (holds(held, incoming) && held.version == incoming.version)
            return same_version(hooks, incoming, folder, held);
    }
    InstallPlan plan{};
    plan.installed = at_own;
    if (at_own.kind == FolderKind::missing) {
        plan.kind = PlanKind::install;
        plan.target = own;
        return plan;
    }
    for (const std::string& folder : alongside)
        if (look(hooks, folder).kind == FolderKind::missing) {
            plan.alongside = folder;
            break;
        }
    if (plan.alongside.empty()) {
        plan.kind = PlanKind::refuse;
        return plan;
    }
    if (holds(at_own, incoming)) {
        // Another version: replace it, or install this one alongside.
        plan.kind = PlanKind::ask_version;
        plan.target = own;
        plan.backup = backup_of(hooks, own);
        return plan;
    }
    // The folder holds something else, which is never replaced.
    plan.kind = PlanKind::ask_alongside;
    return plan;
}

} // namespace oa::app::mod_install
