// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_health/veterancy.hpp"

#include <algorithm>
#include <cstddef>

namespace oa::sim::unit_health {
namespace {
namespace rules_ns = data::match_rules;

/// Returns the veterancy rule of a match.
///
/// @param rules the match's rules
/// @return its veterancy.model record
const rules_ns::VeterancyModel& model_of(const rules_ns::MatchRulesView& rules) noexcept {
    return rules.rules().veterancy.model;
}

/// Tells whether the level comes from kill thresholds.
///
/// @param model the veterancy rule
/// @return true for level-source thresholds
bool counts_thresholds(const rules_ns::VeterancyModel& model) noexcept {
    return model.level_source == rules_ns::VeterancyModelLevelSource::thresholds;
}

/// Returns the kill count as threshold comparisons read it.
///
/// @param model the veterancy rule
/// @param kills the unit's kills
/// @return the kills; under level-source thresholds read as a signed 16-bit
///         count widened to 32 bits and compared without sign, so a count past
///         32767 lies past every threshold
uint32_t compared_kills(const rules_ns::VeterancyModel& model, uint16_t kills) noexcept {
    if (counts_thresholds(model))
        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(kills)));
    return kills;
}

/// One unit type's thresholds, as unsigned kill counts.
struct Thresholds {
    const uint16_t* own{};   ///< the type's own list, or null
    const int32_t* shared{}; ///< default-thresholds, when the type has none
    size_t count{};

    /// Returns one threshold.
    ///
    /// @param at its position, below count
    /// @return the kill count
    [[nodiscard]] uint32_t operator[](size_t at) const noexcept {
        return own != nullptr ? own[at] : static_cast<uint32_t>(shared[at]);
    }
};

/// Returns the thresholds a unit type gains its levels at.
///
/// @param rules the match's rules
/// @param type_index the unit's type index
/// @return the type's own list, else default-thresholds
Thresholds thresholds_of(const rules_ns::MatchRulesView& rules, uint16_t type_index) noexcept {
    const auto& own = rules.unit_type(type_index).veterancy_thresholds;
    if (own.has_value())
        return {own->items.data(), nullptr, own->count};
    const auto& shared = model_of(rules).default_thresholds;
    return {nullptr, shared.items.data(), shared.count};
}

/// Counts the thresholds at or below a kill count.
///
/// @param list the ascending thresholds
/// @param kills the kills as compared_kills reads them
/// @return the count
uint32_t thresholds_reached(const Thresholds& list, uint32_t kills) noexcept {
    uint32_t reached = 0;
    while (reached < list.count && list[reached] <= kills)
        ++reached;
    return reached;
}

/// Applies a cap to a level.
///
/// @param level the level
/// @param cap the cap, in levels; negative counts as 0
/// @return the smaller of the two
uint32_t capped(uint32_t level, int32_t cap) noexcept {
    return std::min(level, static_cast<uint32_t>(std::max(cap, 0)));
}

/// Returns 100 less per-level percent × levels, never below 0.
///
/// @param per_level percent per level
/// @param levels the capped level
/// @return the percentage
int32_t reduced_percent(int32_t per_level, uint32_t levels) noexcept {
    const auto reduction = static_cast<int64_t>(per_level) * static_cast<int64_t>(levels);
    return static_cast<int32_t>(std::max<int64_t>(whole_percent - reduction, 0));
}
} // namespace

uint32_t veterancy_level(
    const data::match_rules::MatchRulesView& rules, uint16_t type_index, uint16_t kills
) noexcept {
    const auto& model = model_of(rules);
    if (!counts_thresholds(model))
        return kills / kills_per_veteran_level;
    return thresholds_reached(thresholds_of(rules, type_index), compared_kills(model, kills));
}

int32_t veteran_damage_taken_percent(
    const data::match_rules::MatchRulesView& rules, uint16_t type_index, uint16_t kills
) noexcept {
    const auto& model = model_of(rules);
    return reduced_percent(
        model.damage_taken_per_level,
        capped(veterancy_level(rules, type_index, kills), model.damage_taken_cap)
    );
}

int32_t veteran_damage_dealt_percent(
    const data::match_rules::MatchRulesView& rules, uint16_t type_index, uint16_t kills
) noexcept {
    const auto& model = model_of(rules);
    auto level = veterancy_level(rules, type_index, kills);
    if (model.damage_dealt_cap.has_value())
        level = capped(level, *model.damage_dealt_cap);
    return static_cast<int32_t>(
        static_cast<uint32_t>(whole_percent) +
        static_cast<uint32_t>(model.damage_dealt_per_level) * level
    );
}

int32_t veteran_reload_percent(
    const data::match_rules::MatchRulesView& rules, uint16_t type_index, uint16_t kills
) noexcept {
    const auto& model = model_of(rules);
    return reduced_percent(
        model.reload_per_level, capped(veterancy_level(rules, type_index, kills), model.reload_cap)
    );
}

bool veteran_leads(
    const data::match_rules::MatchRulesView& rules, uint16_t type_index, uint16_t kills
) noexcept {
    const auto& model = model_of(rules);
    if (model.lead_after == data::match_rules::VeterancyModelLeadAfter::kills_gt_5)
        return kills > kills_before_lead;
    const Thresholds list = thresholds_of(rules, type_index);
    return list.count != 0 && compared_kills(model, kills) > list[0];
}

int32_t veteran_accuracy_divisor(
    const data::match_rules::MatchRulesView& rules, uint16_t type_index, uint16_t kills
) noexcept {
    const auto& model = model_of(rules);
    const auto& own = rules.unit_type(type_index).veterancy_accuracy_rate;
    const int32_t rate = own.has_value() ? static_cast<int32_t>(*own) : model.accuracy_rate_default;
    if (rate <= 0)
        return 0;
    if (counts_thresholds(model))
        return static_cast<int32_t>(static_cast<int16_t>(kills)) / rate;
    return static_cast<int32_t>(static_cast<uint32_t>(kills) / static_cast<uint32_t>(rate));
}

uint32_t veteran_capture_level(
    const data::match_rules::MatchRulesView& rules, uint16_t type_index, uint16_t kills
) noexcept {
    const auto& model = model_of(rules);
    if (model.capture_level == data::match_rules::VeterancyModelCaptureLevel::kills_div_5)
        return kills / kills_per_veteran_level;
    const Thresholds list = thresholds_of(rules, type_index);
    const uint32_t compared = compared_kills(model, kills);
    const size_t count = list.count;
    if (count == 0 || compared < list[0])
        return 0;
    const uint32_t last = list[count - 1];
    if (compared <= last)
        return thresholds_reached(list, compared);
    if (count >= 2) {
        const uint32_t step = last - list[count - 2];
        if (step == 0)
            return static_cast<uint32_t>(count);
        return static_cast<uint32_t>(count) + (compared - last) / step;
    }
    if (last == 0)
        return static_cast<uint32_t>(count);
    return compared / last;
}

} // namespace oa::sim::unit_health
