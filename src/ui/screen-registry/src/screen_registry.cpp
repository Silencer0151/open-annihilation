// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Fixed-capacity screen, overlay and dispatcher-step tables.
#include "oa/ui/screen_registry.hpp"

#include <cstdint>

namespace oa::app {

namespace {

bool reject(ScreenRegistry* registry, const char* name) {
    if (registry->rejected == nullptr)
        registry->rejected = name != nullptr ? name : "(unnamed)";
    return false;
}

} // namespace

bool screen_register(ScreenRegistry* registry, const ScreenDesc* desc) {
    if (desc->id == kScreenAny || registry->screen_count >= kMaxScreens ||
        screen_find(registry, desc->id) != nullptr)
        return reject(registry, desc->name);
    registry->screens[registry->screen_count++] = *desc;
    return true;
}

bool overlay_register(ScreenRegistry* registry, const OverlayDesc* desc) {
    if (registry->overlay_count >= kMaxOverlays)
        return reject(registry, desc->name);
    auto at = registry->overlay_count;
    while (at > 0 && registry->overlays[at - 1].z > desc->z) {
        registry->overlays[at] = registry->overlays[at - 1];
        --at;
    }
    registry->overlays[at] = *desc;
    ++registry->overlay_count;
    return true;
}

bool step_register(
    ScreenRegistry* registry, oa::ui::frontend_state::Step step, ScreenFn run, void* state
) {
    if (run == nullptr || registry->step_count >= kMaxSteps || step_find(registry, step) != nullptr)
        return reject(registry, "dispatcher step");
    registry->steps[registry->step_count++] = {step, run, state};
    return true;
}

const ScreenDesc* screen_find(const ScreenRegistry* registry, ScreenId id) {
    for (uint32_t index = 0; index < registry->screen_count; ++index)
        if (registry->screens[index].id == id)
            return &registry->screens[index];
    return nullptr;
}

const StepDesc* step_find(const ScreenRegistry* registry, oa::ui::frontend_state::Step step) {
    for (uint32_t index = 0; index < registry->step_count; ++index)
        if (registry->steps[index].step == step)
            return &registry->steps[index];
    return nullptr;
}

} // namespace oa::app
