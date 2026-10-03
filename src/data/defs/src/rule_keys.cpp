// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/defs/rule_keys.hpp"

#include <cstring>

namespace oa::data::defs {
namespace {

/// Tells whether a key name binds anything.
///
/// @param name the name, or null
/// @return true when it is set and not empty
bool bound(const char* name) noexcept {
    return name != nullptr && name[0] != '\0';
}

/// Returns a bound key's value text.
///
/// @param block the block searched
/// @param name the key's name, or null when unbound
/// @return the text, or null when unbound or missing
const char* value_of(const formats::tdf::Block* block, const char* name) noexcept {
    return bound(name) ? formats::tdf::find_value(block, name) : nullptr;
}

/// Tells whether a character separates the items of a list.
bool separator(char c) noexcept {
    return c == ' ' || c == '\t' || c == ',' || c == '\r' || c == '\n';
}

/// Reads one whole number of at most highest, digits only.
///
/// @param[in,out] at the text, moved past the digits
/// @param highest the largest value allowed
/// @param[out] value the number
/// @return none, not_a_number when there are no digits or a non-digit
///     follows them, or out_of_range
RuleKeyProblem read_number(const char*& at, uint32_t highest, uint32_t& value) noexcept {
    if (*at < '0' || *at > '9')
        return RuleKeyProblem::not_a_number;
    uint32_t total = 0;
    bool too_big = false;
    while (*at >= '0' && *at <= '9') {
        total = total * 10U + static_cast<uint32_t>(*at - '0');
        if (total > highest)
            too_big = true;
        ++at;
    }
    if (*at != '\0' && !separator(*at))
        return RuleKeyProblem::not_a_number;
    value = total;
    return too_big ? RuleKeyProblem::out_of_range : RuleKeyProblem::none;
}

/// Reads a list of ascending kill counts.
RuleKeyProblem read_thresholds(const char* text, match_rules::UnitTypeRules& data) noexcept {
    match_rules::FixedList<uint16_t, match_rules::max_veterancy_thresholds> values{};
    const char* at = text;
    for (;;) {
        while (separator(*at))
            ++at;
        if (*at == '\0')
            break;
        uint32_t value = 0;
        if (const RuleKeyProblem problem = read_number(at, highest_veterancy_threshold, value);
            problem != RuleKeyProblem::none)
            return problem;
        if (values.count == match_rules::max_veterancy_thresholds)
            return RuleKeyProblem::too_many_values;
        if (values.count != 0 && value < values.items[values.count - 1U])
            return RuleKeyProblem::not_ascending;
        values.push(static_cast<uint16_t>(value));
    }
    if (values.count == 0)
        return RuleKeyProblem::none; // an empty list is a missing key
    data.veterancy_thresholds = values;
    return RuleKeyProblem::none;
}

/// Reads one whole number, allowing space around it.
RuleKeyProblem read_single(const char* text, uint32_t highest, uint32_t& value) noexcept {
    const char* at = text;
    while (separator(*at))
        ++at;
    if (const RuleKeyProblem problem = read_number(at, highest, value);
        problem != RuleKeyProblem::none)
        return problem;
    while (separator(*at))
        ++at;
    return *at == '\0' ? RuleKeyProblem::none : RuleKeyProblem::not_a_number;
}

/// Reads facing letters from S, E, N and W.
RuleKeyProblem read_facings(const char* text, match_rules::UnitTypeRules& data) noexcept {
    uint8_t facings = 0;
    for (const char* at = text; *at != '\0'; ++at) {
        switch (*at) {
        case 'S':
        case 's':
            facings |= match_rules::build_facing::south;
            break;
        case 'E':
        case 'e':
            facings |= match_rules::build_facing::east;
            break;
        case 'N':
        case 'n':
            facings |= match_rules::build_facing::north;
            break;
        case 'W':
        case 'w':
            facings |= match_rules::build_facing::west;
            break;
        default:
            return RuleKeyProblem::unknown_facing;
        }
    }
    if (facings == 0)
        return RuleKeyProblem::unknown_facing;
    data.build_facings = facings;
    return RuleKeyProblem::none;
}

/// Copies a bound key's text into a preview field, or leaves it empty.
void copy_text(
    const formats::tdf::Block* block, const char* name, std::array<char, preview_text_capacity>& out
) noexcept {
    if (bound(name))
        formats::tdf::get_string(block, name, out.data(), out.size(), "");
}

} // namespace

bool UnitDataKeys::any() const noexcept {
    return bound(veterancy_thresholds) || bound(veterancy_accuracy_rate) || bound(build_facings);
}

bool WeaponDataKeys::any() const noexcept {
    return bound(not_to_air) || bound(surface_fire) || bound(not_to_underwater) ||
           bound(no_map_alert);
}

bool UnitPreviewDataKeys::any() const noexcept {
    return bound(pieces) || bound(pieces_by_facing) || bound(object) || bound(face_opponent);
}

bool RuleKeyIssues::any() const noexcept {
    return veterancy_thresholds != RuleKeyProblem::none ||
           veterancy_accuracy_rate != RuleKeyProblem::none || build_facings != RuleKeyProblem::none;
}

const char* rule_key_problem_text(RuleKeyProblem problem) noexcept {
    switch (problem) {
    case RuleKeyProblem::none:
        return "no problem";
    case RuleKeyProblem::not_a_number:
        return "not a whole number";
    case RuleKeyProblem::too_many_values:
        return "more than 32 values";
    case RuleKeyProblem::out_of_range:
        return "a number past 65535";
    case RuleKeyProblem::not_ascending:
        return "values not in ascending order";
    case RuleKeyProblem::unknown_facing:
        return "facings other than S, E, N and W";
    }
    return "unknown problem";
}

RuleKeyIssues read_unit_rule_keys(
    const formats::tdf::Block* unit_info, const UnitDataKeys& keys, match_rules::UnitTypeRules& data
) noexcept {
    data = {};
    RuleKeyIssues issues{};
    if (const char* text = value_of(unit_info, keys.veterancy_thresholds))
        issues.veterancy_thresholds = read_thresholds(text, data);
    if (const char* text = value_of(unit_info, keys.veterancy_accuracy_rate)) {
        uint32_t rate = 0;
        issues.veterancy_accuracy_rate = read_single(text, highest_veterancy_accuracy_rate, rate);
        if (issues.veterancy_accuracy_rate == RuleKeyProblem::none) {
            data.veterancy_accuracy_rate = static_cast<uint16_t>(rate);
        }
    }
    if (const char* text = value_of(unit_info, keys.build_facings))
        issues.build_facings = read_facings(text, data);
    return issues;
}

void read_weapon_rule_keys(
    const formats::tdf::Block* section,
    const WeaponDataKeys& keys,
    match_rules::WeaponTypeRules& data
) noexcept {
    const auto low_bit = [section](const char* name) {
        const char* text = value_of(section, name);
        return text != nullptr && (static_cast<uint32_t>(formats::tdf::parse_int(text)) & 1U) != 0;
    };
    data.not_to_air = low_bit(keys.not_to_air);
    data.surface_fire = low_bit(keys.surface_fire);
    data.not_to_underwater = low_bit(keys.not_to_underwater);
    data.no_map_alert = low_bit(keys.no_map_alert);
}

void read_unit_preview_keys(
    const formats::tdf::Block* unit_info, const UnitPreviewDataKeys& keys, UnitPreviewKeys& data
) noexcept {
    data = {};
    copy_text(unit_info, keys.pieces, data.pieces);
    copy_text(unit_info, keys.object, data.object);
    if (bound(keys.pieces_by_facing)) {
        constexpr char letters[4] = {'S', 'E', 'N', 'W'};
        char name[preview_text_capacity];
        const size_t stem = std::strlen(keys.pieces_by_facing);
        if (stem + 2 <= sizeof name) {
            std::memcpy(name, keys.pieces_by_facing, stem);
            for (size_t facing = 0; facing < 4; ++facing) {
                name[stem] = letters[facing];
                name[stem + 1] = '\0';
                copy_text(unit_info, name, data.pieces_by_facing[facing]);
            }
        }
    }
    if (const char* text = value_of(unit_info, keys.face_opponent))
        data.face_opponent = (static_cast<uint32_t>(formats::tdf::parse_int(text)) & 1U) != 0;
}

bool load_unit_rule_keys(
    const Files* files,
    const char* path,
    const UnitDataKeys& keys,
    const UnitPreviewDataKeys& preview_keys,
    match_rules::UnitTypeRules& data,
    UnitPreviewKeys* preview,
    RuleKeyIssues* issues
) noexcept {
    data = {};
    if (preview != nullptr)
        *preview = {};
    if (issues != nullptr)
        *issues = {};
    formats::tdf::Document document;
    formats::tdf::document_init(&document);
    if (!load_tdf_file(files, path, &document, nullptr) ||
        !formats::tdf::select_section(&document, "UNITINFO")) {
        formats::tdf::document_free(&document);
        return false;
    }
    const formats::tdf::Block* block = formats::tdf::cursor(&document);
    const RuleKeyIssues found = read_unit_rule_keys(block, keys, data);
    if (issues != nullptr)
        *issues = found;
    if (preview != nullptr)
        read_unit_preview_keys(block, preview_keys, *preview);
    formats::tdf::document_free(&document);
    return true;
}

} // namespace oa::data::defs
