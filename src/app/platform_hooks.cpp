// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The platform's hooks, kept for the run (platform_hooks.hpp).
#include "oa/app/platform_hooks.hpp"

namespace oa::app {
namespace {

/// Returns the hooks of the run, all null until set_platform_hooks.
///
/// @return the kept copy
PlatformHooks& installed_hooks() noexcept {
    static PlatformHooks hooks{};
    return hooks;
}

} // namespace

void set_platform_hooks(const PlatformHooks& hooks) noexcept {
    installed_hooks() = hooks;
}

const PlatformHooks& platform_hooks() noexcept {
    return installed_hooks();
}

} // namespace oa::app
