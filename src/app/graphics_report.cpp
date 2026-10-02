// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "graphics_report.hpp"

#include "oa/app/render_policy.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <iostream>
#include <limits>

namespace oa::app {

namespace render_probe = oa::platform::render_probe;

uint32_t corrected_texture_limit(const render_probe::AdapterFacts& facts) noexcept {
    render_policy::DriverTraits driver{};
    driver.software = facts.renderer == render_probe::software_renderer;
    driver.texture_limit_source = render_probe::reports_fixed_texture_limit(facts.renderer)
                                      ? render_policy::TextureLimitSource::fixed_report
                                      : render_policy::TextureLimitSource::reported;
    const uint32_t reported =
        facts.reported_texture_limit <= 0
            ? render_policy::unlimited_texture_size
            : static_cast<uint32_t>(std::min<int64_t>(
                  facts.reported_texture_limit, std::numeric_limits<uint32_t>::max()
              ));
    return render_policy::texture_limit(driver, reported, facts.device_texture_limit);
}

std::string graphics_log_line(const render_probe::AdapterFacts& facts) {
    std::string line = "open-annihilation: graphics: ";
    line += facts.renderer;
    if (!facts.video_driver.empty()) {
        line += " on ";
        line += facts.video_driver;
    }
    const std::string adapter = stats_adapter_name(facts);
    if (!adapter.empty()) {
        line += " (";
        line += adapter;
        line += ')';
    }
    const uint32_t limit = corrected_texture_limit(facts);
    if (limit == render_policy::unlimited_texture_size) {
        line += ", textures of any size";
    } else {
        line += ", textures up to ";
        line += std::to_string(limit);
    }
    line += "; ";
    line += standard_tier_name;
    line += " tier: ";
    line += standard_tier_text;
    return line;
}

std::string stats_adapter_name(const render_probe::AdapterFacts& facts) {
    switch (facts.adapter_state) {
    case render_probe::AdapterState::read:
        return facts.adapter;
    case render_probe::AdapterState::unknown:
        return std::string(unknown_adapter_name);
    case render_probe::AdapterState::skipped:
    case render_probe::AdapterState::none:
        break;
    }
    return {};
}

render_probe::AdapterRead adapter_read_for(std::string_view named_driver) noexcept {
    return named_driver.empty() ? render_probe::AdapterRead::read : render_probe::AdapterRead::skip;
}

render_probe::AdapterFacts report_game_renderer(SDL_Renderer* renderer) {
    const char* named = SDL_GetHint(SDL_HINT_RENDER_DRIVER);
    auto facts = render_probe::describe(
        renderer, adapter_read_for(named != nullptr ? std::string_view(named) : std::string_view())
    );
    std::cout << graphics_log_line(facts) << '\n';
    return facts;
}

} // namespace oa::app
