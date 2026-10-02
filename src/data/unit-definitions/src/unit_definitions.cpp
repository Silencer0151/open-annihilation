// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/unit_definitions.hpp"

#include "oa/formats/tdf.hpp"

#include <algorithm>
#include <cstdint>

namespace oa::data::unit_definitions {
namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    return out;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
                             text.front() == '\n'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '\n'))
        text.remove_suffix(1);
    return text;
}

class Parser {
  public:

    explicit Parser(std::string_view source) : source_(source) {}

    Result<TdfDocument> run() {
        Result<TdfDocument> result;
        if (source_.size() > limit::input_bytes)
            return fail<TdfDocument>(ErrorCode::limit, "TDF input exceeds 16 MiB");
        skip();
        if (error_.code != ErrorCode::none) {
            result.error = error_;
            return result;
        }
        while (pos_ < source_.size()) {
            TdfSection section;
            if (!section_into(section, 1)) {
                result.error = error_;
                return result;
            }
            result.value.sections.push_back(std::move(section));
            skip();
            if (error_.code != ErrorCode::none) {
                result.error = error_;
                return result;
            }
        }
        return result;
    }

  private:

    template <class T>
    Result<T> fail(ErrorCode code, std::string message) {
        Result<T> r;
        r.error = {code, pos_, std::move(message)};
        return r;
    }

    void skip() {
        for (;;) {
            while (pos_ < source_.size() && (source_[pos_] == ' ' || source_[pos_] == '\t' ||
                                             source_[pos_] == '\r' || source_[pos_] == '\n'))
                ++pos_;
            if (pos_ + 1 < source_.size() && source_[pos_] == '/' && source_[pos_ + 1] == '/') {
                pos_ += 2;
                while (pos_ < source_.size() && source_[pos_] != '\n')
                    ++pos_;
            } else if (
                pos_ + 1 < source_.size() && source_[pos_] == '/' && source_[pos_ + 1] == '*'
            ) {
                pos_ += 2;
                while (pos_ + 1 < source_.size() &&
                       !(source_[pos_] == '*' && source_[pos_ + 1] == '/'))
                    ++pos_;
                if (pos_ + 1 < source_.size())
                    pos_ += 2;
                else {
                    error_ = {ErrorCode::malformed, pos_, "unterminated block comment"};
                    return;
                }
            } else
                return;
        }
    }

    /// Parses the section at the cursor, with its fields and nested sections.
    ///
    /// @param[out] out the section read
    /// @param depth nesting depth of the section, 1 at the top level
    /// @return false, with the error recorded, for malformed text or a passed limit
    bool section_into(TdfSection& out, std::size_t depth) {
        if (depth > limit::nesting_depth || ++sections_ > limit::sections)
            return set_limit("TDF section limit exceeded");
        if (pos_ >= source_.size() || source_[pos_] != '[')
            return malformed("expected '['");
        const auto begin = ++pos_;
        while (pos_ < source_.size() && source_[pos_] != ']')
            ++pos_;
        if (pos_ == source_.size())
            return malformed("unterminated section name");
        const auto name = trim(source_.substr(begin, pos_ - begin));
        if (name.empty() || name.size() > limit::name_bytes)
            return set_limit("invalid TDF section name");
        out.name.assign(name);
        ++pos_;
        skip();
        if (pos_ >= source_.size() || source_[pos_] != '{')
            return malformed("expected '{' after section name");
        ++pos_;
        for (;;) {
            skip();
            if (error_.code != ErrorCode::none)
                return false;
            if (pos_ >= source_.size())
                return malformed("unterminated section body");
            if (source_[pos_] == '}') {
                ++pos_;
                return true;
            }
            if (source_[pos_] == '[') {
                TdfSection child;
                if (!section_into(child, depth + 1))
                    return false;
                out.children.push_back(std::move(child));
                continue;
            }
            if (out.fields.size() >= limit::fields_per_section)
                return set_limit("too many fields in TDF section");
            const auto key_begin = pos_;
            while (pos_ < source_.size() && source_[pos_] != '=' && source_[pos_] != '\n' &&
                   source_[pos_] != '}')
                ++pos_;
            // Some shipped FBI records contain text after an empty `value=;`
            // (ARMSCORP's `ItalianDescription=;Scorpione`). 3.1c discards
            // that bare remainder at end of line.
            if (pos_ < source_.size() && source_[pos_] == '\n') {
                ++pos_;
                continue;
            }
            if (pos_ >= source_.size() || source_[pos_] != '=')
                return malformed("expected '=' after field name");
            const auto key_view = trim(source_.substr(key_begin, pos_ - key_begin));
            if (key_view.empty() || key_view.size() > limit::name_bytes)
                return set_limit("invalid TDF field name");
            ++pos_;
            const auto value_begin = pos_;
            while (pos_ < source_.size() && source_[pos_] != ';' && source_[pos_] != '\n' &&
                   source_[pos_] != '}')
                ++pos_;
            const auto value = trim(source_.substr(value_begin, pos_ - value_begin));
            if (value.size() > limit::value_bytes)
                return set_limit("TDF field value exceeds limit");
            out.fields.insert_or_assign(lower(key_view), std::string(value));
            if (pos_ < source_.size() && source_[pos_] == ';')
                ++pos_;
        }
    }

    bool malformed(std::string message) {
        error_ = {ErrorCode::malformed, pos_, std::move(message)};
        return false;
    }

    bool set_limit(std::string message) {
        error_ = {ErrorCode::limit, pos_, std::move(message)};
        return false;
    }

    std::string_view source_;
    std::size_t pos_ = 0, sections_ = 0;
    Error error_{};
};

} // namespace

const TdfSection* TdfSection::child(std::string_view wanted) const {
    const auto key = lower(wanted);
    for (const auto& c : children)
        if (lower(c.name) == key)
            return &c;
    return nullptr;
}

const std::string* TdfSection::find(std::string_view key) const {
    const auto it = fields.find(lower(key));
    return it == fields.end() ? nullptr : &it->second;
}

Result<TdfDocument> parse_tdf(std::string_view source) {
    return Parser(source).run();
}

uint32_t pack_unit_flags(const UnitDefinition& u) noexcept {
    uint32_t flags = static_cast<uint32_t>(u.standing_move_order & 3U) |
                     (static_cast<uint32_t>(u.standing_fire_order & 3U) << 2U);
#define PACK(member)                                                                               \
    if (u.member)                                                                                  \
    flags |= flag_mask(UnitFlag::member)
    PACK(init_cloaked);
    PACK(downloadable);
    PACK(builder);
    PACK(z_buffer);
    PACK(stealth);
    PACK(is_airbase);
    PACK(targeting_upgrade);
    PACK(can_fly);
    PACK(can_hover);
    PACK(teleporter);
    PACK(hide_damage);
    PACK(shoot_me);
    PACK(armored_state);
    PACK(activate_when_built);
    PACK(floater);
    PACK(upright);
    PACK(amphibious);
    PACK(is_feature);
    PACK(no_shadow);
    PACK(immune_to_paralyzer);
    PACK(hover_attack);
    PACK(kamikaze);
    PACK(anti_weapons);
    PACK(digger);
#undef PACK
    return flags;
}

uint32_t pack_unit_abilities(const UnitDefinition& u) noexcept {
    uint32_t flags = 0;
    const auto bit = [&](bool set, unsigned shift) {
        if (set)
            flags |= 1u << shift;
    };
    bit(u.mobile_stand_orders, 0);
    bit(u.fire_stand_orders, 1);
    bit(u.on_offable, 2);
    bit(u.can_stop, 3);
    bit(u.can_attack, 4);
    bit(u.can_guard, 5);
    bit(u.can_patrol, 6);
    bit(u.can_move, 7);
    bit(u.can_load, 8);
    bit(u.can_reclamate, 10);
    // As in 3.1c, canreclamate also sets bit 9.
    if (u.can_reclamate)
        flags |= 1u << 9;
    bit(u.can_resurrect, 11);
    bit(u.can_capture, 12);
    bit(u.can_cloak, 13);
    bit(u.can_dgun, 14);
    bit(u.no_restrict, 15);
    bit(u.show_player_name, 17);
    bit(u.commander, 18);
    bit(u.cant_be_transported, 19);
    flags |= static_cast<uint32_t>(u.self_destruct_countdown & 7u) << 20;
    return flags;
}

SpawnDefinitionFields project_for_spawn(const UnitDefinition& u) noexcept {
    return {
        pack_unit_flags(u),
        u.footprint_x,
        u.footprint_z,
        u.max_damage,
        u.heal_time,
        u.build_angle,
        u.makes_metal,
        u.bm_code
    };
}

namespace {

/// Copies a record's fixed-size name up to its NUL or its end.
///
/// @param text the record's name buffer
/// @return the name
template <std::size_t N>
std::string text_of(const char (&text)[N]) {
    std::size_t length = 0;
    while (length < N && text[length] != '\0')
        ++length;
    return {text, length};
}

/// Returns the section name of the weapon a record's reference points at.
///
/// @param weapon_defs Game.weapon_defs
/// @param ref weapon index + 1
/// @return the name; empty for weapon 0, which stands in for none, and for no table
std::string weapon_name(const WeaponDef* weapon_defs, oa_ref32 ref) {
    if (weapon_defs == nullptr || ref <= 1 || ref > OA_WEAPON_DEF_COUNT)
        return {};
    return text_of(weapon_defs[ref - 1].key);
}

/// Returns whether a ASCII name is ALL, ignoring case.
///
/// @param name category name
/// @return true for ALL
bool is_all(const char* name) {
    return formats::tdf::compare_nocase(name, "all") == 0;
}

} // namespace

UnitDefinition unit_definition_from(const UnitDef& unit, const UnitDefinitionSources& sources) {
    UnitDefinition u;
    u.unit_name = text_of(unit.unit_name);
    u.object_name = text_of(unit.object_name);
    u.display_name = text_of(unit.name);
    u.description = text_of(unit.description);
    u.side = text_of(unit.side);
    if (sources.move_classes != nullptr && unit.move_class != 0 &&
        unit.move_class <= OA_MOVE_CLASS_COUNT)
        u.movement_class = text_of(sources.move_classes->names[unit.move_class - 1U]);
    if (sources.sound_categories != nullptr && unit.sound_category >= 0 &&
        static_cast<uint32_t>(unit.sound_category) < sources.sound_categories->count)
        u.sound_category = text_of(sources.sound_categories->categories[unit.sound_category].name);
    u.weapon1 = weapon_name(sources.weapon_defs, unit.weapon1);
    u.weapon2 = weapon_name(sources.weapon_defs, unit.weapon2);
    u.weapon3 = weapon_name(sources.weapon_defs, unit.weapon3);
    u.explode_as = weapon_name(sources.weapon_defs, unit.explode_as);
    u.self_destruct_as = weapon_name(sources.weapon_defs, unit.self_destruct_as);
    if (sources.categories != nullptr)
        for (uint32_t index = 0; index < sources.categories->count; ++index) {
            const auto& category = sources.categories->entries[index];
            if (!is_all(category.name) &&
                defs::category_mask_contains(
                    &sources.categories->masks[category.mask], unit.type_id
                ))
                u.categories.emplace_back(category.name);
        }
    u.build_cost_energy = static_cast<int32_t>(unit.build_cost_energy);
    u.build_cost_metal = static_cast<int32_t>(unit.build_cost_metal);
    u.build_time = unit.build_time;
    u.max_damage = static_cast<int32_t>(unit.max_damage);
    u.max_velocity_fixed = unit.max_velocity;
    u.brake_rate_fixed = unit.brake_rate;
    u.acceleration_fixed = unit.acceleration;
    u.bank_scale_fixed = unit.bank_scale;
    u.pitch_scale_fixed = unit.pitch_scale;
    u.damage_modifier_fixed = unit.damage_modifier;
    u.move_rate1_fixed = unit.move_rate1;
    u.move_rate2_fixed = unit.move_rate2;
    u.turn_rate = unit.turn_rate;
    u.worker_time = unit.worker_time;
    u.heal_time = unit.heal_time;
    u.sight_distance = unit.sight_distance;
    u.radar_distance = unit.radar_distance;
    u.sonar_distance = unit.sonar_distance;
    u.radar_distance_jam = unit.radar_distance_jam;
    u.sonar_distance_jam = unit.sonar_distance_jam;
    u.min_cloak_distance = unit.min_cloak_distance;
    u.build_angle = unit.build_angle;
    u.build_distance = unit.build_distance;
    u.sort_bias = unit.sort_bias;
    u.cruise_altitude = unit.cruise_alt;
    u.maneuver_leash_length = unit.maneuver_leash_length;
    u.attack_run_length = unit.attack_run_length;
    u.kamikaze_distance = unit.kamikaze_distance;
    u.footprint_x = unit.footprint_x;
    u.footprint_z = unit.footprint_z;
    u.max_water_depth = unit.max_water_depth;
    u.min_water_depth = unit.min_water_depth;
    u.max_slope = unit.max_slope;
    u.max_water_slope = unit.max_water_slope;
    u.waterline = unit.water_line;
    u.transport_size = unit.transport_size;
    u.transport_capacity = unit.transport_capacity;
    u.bm_code = unit.bm_code;
    u.makes_metal = unit.makes_metal;
    u.energy_make = unit.energy_make;
    u.energy_use = unit.energy_use;
    u.metal_make = unit.metal_make;
    u.extracts_metal = unit.extracts_metal;
    u.wind_generator = unit.wind_generator;
    u.tidal_generator = unit.tidal_generator;
    u.energy_storage = unit.energy_storage;
    u.metal_storage = unit.metal_storage;
    u.cloak_cost = unit.cloak_cost;
    u.cloak_cost_moving = unit.cloak_cost_moving;
    const uint32_t flags = unit.flags;
    const uint32_t abilities = unit.abilities;
    u.standing_move_order = static_cast<uint8_t>(flags & OA_UNIT_DEF_FLAG_MOVE_ORDER_MASK);
    u.standing_fire_order = static_cast<uint8_t>(
        (flags & OA_UNIT_DEF_FLAG_FIRE_ORDER_MASK) >> OA_UNIT_DEF_FLAG_FIRE_ORDER_SHIFT
    );
    u.self_destruct_countdown = static_cast<uint8_t>(
        (abilities & OA_UNIT_DEF_ABILITY_SELF_DESTRUCT_MASK) >>
        OA_UNIT_DEF_ABILITY_SELF_DESTRUCT_SHIFT
    );
    const auto flag = [flags](uint32_t bit) { return (flags & bit) != 0; };
    const auto ability = [abilities](uint32_t bit) { return (abilities & bit) != 0; };
    u.init_cloaked = flag(OA_UNIT_DEF_FLAG_INIT_CLOAKED);
    u.downloadable = flag(OA_UNIT_DEF_FLAG_DOWNLOADABLE);
    u.builder = flag(OA_UNIT_DEF_FLAG_BUILDER);
    u.stealth = flag(OA_UNIT_DEF_FLAG_STEALTH);
    u.can_cloak = ability(OA_UNIT_DEF_ABILITY_CAN_CLOAK);
    u.z_buffer = flag(OA_UNIT_DEF_FLAG_Z_BUFFER);
    u.is_airbase = flag(OA_UNIT_DEF_FLAG_IS_AIRBASE);
    u.targeting_upgrade = flag(OA_UNIT_DEF_FLAG_TARGETING_UPGRADE);
    u.teleporter = flag(OA_UNIT_DEF_FLAG_TELEPORTER);
    u.hide_damage = flag(OA_UNIT_DEF_FLAG_HIDE_DAMAGE);
    u.shoot_me = flag(OA_UNIT_DEF_FLAG_SHOOT_ME);
    u.armored_state = flag(OA_UNIT_DEF_FLAG_ARMORED_STATE);
    u.activate_when_built = flag(OA_UNIT_DEF_FLAG_ACTIVATE_WHEN_BUILT);
    u.can_fly = flag(OA_UNIT_DEF_FLAG_CAN_FLY);
    u.can_hover = flag(OA_UNIT_DEF_FLAG_CAN_HOVER);
    u.upright = flag(OA_UNIT_DEF_FLAG_UPRIGHT);
    u.floater = flag(OA_UNIT_DEF_FLAG_FLOATER);
    u.amphibious = flag(OA_UNIT_DEF_FLAG_AMPHIBIOUS);
    u.is_feature = flag(OA_UNIT_DEF_FLAG_IS_FEATURE);
    u.no_shadow = flag(OA_UNIT_DEF_FLAG_NO_SHADOW);
    u.immune_to_paralyzer = flag(OA_UNIT_DEF_FLAG_IMMUNE_TO_PARALYZER);
    u.hover_attack = flag(OA_UNIT_DEF_FLAG_HOVER_ATTACK);
    u.anti_weapons = flag(OA_UNIT_DEF_FLAG_ANTI_WEAPONS);
    u.digger = flag(OA_UNIT_DEF_FLAG_DIGGER);
    u.kamikaze = flag(OA_UNIT_DEF_FLAG_KAMIKAZE);
    u.on_offable = ability(OA_UNIT_DEF_ABILITY_ON_OFFABLE);
    u.mobile_stand_orders = ability(OA_UNIT_DEF_ABILITY_MOBILE_STAND_ORDERS);
    u.fire_stand_orders = ability(OA_UNIT_DEF_ABILITY_FIRE_STAND_ORDERS);
    u.can_stop = ability(OA_UNIT_DEF_ABILITY_CAN_STOP);
    u.can_attack = ability(OA_UNIT_DEF_ABILITY_CAN_ATTACK);
    u.can_guard = ability(OA_UNIT_DEF_ABILITY_CAN_GUARD);
    u.can_patrol = ability(OA_UNIT_DEF_ABILITY_CAN_PATROL);
    u.can_move = ability(OA_UNIT_DEF_ABILITY_CAN_MOVE);
    u.can_load = ability(OA_UNIT_DEF_ABILITY_CAN_LOAD);
    u.can_reclamate = ability(OA_UNIT_DEF_ABILITY_CAN_RECLAMATE);
    u.can_resurrect = ability(OA_UNIT_DEF_ABILITY_CAN_RESURRECT);
    u.can_capture = ability(OA_UNIT_DEF_ABILITY_CAN_CAPTURE);
    u.can_dgun = ability(OA_UNIT_DEF_ABILITY_CAN_DGUN);
    u.no_restrict = ability(OA_UNIT_DEF_ABILITY_NO_RESTRICT);
    u.show_player_name = ability(OA_UNIT_DEF_ABILITY_SHOW_PLAYER_NAME);
    u.commander = ability(OA_UNIT_DEF_ABILITY_COMMANDER);
    u.cant_be_transported = ability(OA_UNIT_DEF_ABILITY_CANT_BE_TRANSPORTED);
    return u;
}

Result<RuntimeDefinitionMetadata> resolve_runtime_metadata(
    const UnitDef& unit,
    const defs::MoveClassTable& movement_classes,
    const defs::UnitDefBlocks& blocks
) {
    Result<RuntimeDefinitionMetadata> result;
    auto& out = result.value;
    out.footprint_x = unit.footprint_x;
    out.footprint_z = unit.footprint_z;
    out.max_water_depth = unit.max_water_depth;
    out.min_water_depth = unit.min_water_depth;
    out.max_slope = unit.max_slope;
    out.max_water_slope = unit.max_water_slope;
    out.bad_slope = static_cast<uint8_t>(unit.max_slope >> 1U);
    out.bad_water_slope = static_cast<uint8_t>(unit.max_water_slope >> 1U);
    if (unit.move_class != 0 && unit.move_class <= OA_MOVE_CLASS_COUNT) {
        const MoveClass& movement = movement_classes.classes[unit.move_class - 1U];
        out.movement_class_handle = static_cast<uint8_t>(unit.move_class - 1U);
        out.bad_slope = movement.bad_slope;
        out.bad_water_slope = movement.bad_water_slope;
    }
    out.slope_speed_step_fixed = unit.slope_speed_step;
    out.sight_distance = unit.sight_distance;
    out.radar_distance = unit.radar_distance;
    out.sonar_distance = unit.sonar_distance;
    out.radar_distance_jam = unit.radar_distance_jam;
    out.sonar_distance_jam = unit.sonar_distance_jam;
    if (unit.bm_code == 0) {
        const auto cells = static_cast<std::size_t>(std::max<int32_t>(unit.footprint_x, 0)) *
                           static_cast<std::size_t>(std::max<int32_t>(unit.footprint_z, 0));
        const uint8_t* yard = defs::unit_def_block(&blocks, unit.yard_map);
        if (cells != 0 &&
            (yard == nullptr || defs::unit_def_block_size(&blocks, unit.yard_map) < cells)) {
            result.error = {ErrorCode::malformed, 0, "building yard map did not load"};
            return result;
        }
        out.yard_cells.assign(yard, yard + cells);
    }
    return result;
}

UnitTargetCategoryMasks
target_category_masks(const UnitDef& unit, const defs::CategoryRegistry& categories) {
    UnitTargetCategoryMasks masks;
    const auto copy = [&categories](oa_ref32 ref, UnitCategoryMask& mask) {
        if (const auto* source = defs::category_registry_mask(&categories, ref))
            std::copy(std::begin(source->words), std::end(source->words), mask.words.begin());
    };
    copy(unit.primary_bad_target_category, masks.primary_bad);
    copy(unit.secondary_bad_target_category, masks.secondary_bad);
    copy(unit.special_bad_target_category, masks.special_bad);
    copy(unit.no_chase_category, masks.no_chase);
    return masks;
}

bool UnitCategoryMask::contains(uint16_t type_id) const noexcept {
    const auto index = static_cast<std::size_t>(type_id);
    return index < category_mask_bits && (words[index >> 5U] & (1U << (index & 31U))) != 0;
}

} // namespace oa::data::unit_definitions
