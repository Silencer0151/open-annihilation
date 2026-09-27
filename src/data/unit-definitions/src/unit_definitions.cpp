// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/unit_definitions.hpp"

#include "oa/formats/tdf.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>

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

int32_t truncate_low32(double value) {
    constexpr double signed64_limit = 9223372036854775808.0; // 2^63
    int64_t wide;
    if (!std::isfinite(value) || value >= signed64_limit || value < -signed64_limit)
        wide = std::numeric_limits<int64_t>::min(); // as for NaN and values past 64 bits
    else
        wide = static_cast<int64_t>(value); // truncation toward zero
    return std::bit_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(wide)));
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

struct Reader {
    const TdfSection& s;
    Error error{};
    std::unordered_set<std::string> used;

    explicit Reader(const TdfSection& source) : s(source) {}

    const std::string* get(std::string_view key) {
        auto k = lower(key);
        used.insert(k);
        return s.find(k);
    }

    std::string str(std::string_view key) {
        const auto* p = get(key);
        return p ? *p : std::string{};
    }

    /// Reads a field as an integer and marks the key as used.
    ///
    /// @param key field name, matched case-insensitively
    /// @param fallback value for an absent field, or for a malformed number, which
    ///     records an error
    /// @return the value; 0 for an empty field
    int32_t integer(std::string_view key, int32_t fallback = 0) {
        const auto* p = get(key);
        if (!p)
            return fallback;
        if (p->empty())
            return 0;
        // An integer is a decimal prefix after whitespace and a sign; the rest is ignored.
        auto t = trim(*p);
        const char* b = t.data();
        char* e = nullptr;
        errno = 0;
        long v = std::strtol(b, &e, 10);
        if (e == b || errno == ERANGE || v < INT32_MIN || v > INT32_MAX) {
            error = {
                ErrorCode::invalid_number, 0, "invalid integer field '" + std::string(key) + "'"
            };
            return fallback;
        }
        return static_cast<int32_t>(v);
    }

    float real(std::string_view key, float fallback = 0) {
        const auto* p = get(key);
        if (!p)
            return fallback;
        if (p->empty())
            return 0;
        auto t = trim(*p);
        const char* b = t.data();
        char* e = nullptr;
        errno = 0;
        double v = std::strtod(b, &e);
        if (e == b || errno == ERANGE || !std::isfinite(v)) {
            error = {ErrorCode::invalid_number, 0, "invalid real field '" + std::string(key) + "'"};
            return fallback;
        }
        return static_cast<float>(v);
    }

    double decimal(std::string_view key, double fallback = 0) {
        const auto* p = get(key);
        if (!p)
            return fallback;
        if (p->empty())
            return 0;
        auto t = trim(*p);
        const char* b = t.data();
        char* e = nullptr;
        errno = 0;
        const double v = std::strtod(b, &e);
        if (e == b || errno == ERANGE || !std::isfinite(v)) {
            error = {ErrorCode::invalid_number, 0, "invalid real field '" + std::string(key) + "'"};
            return fallback;
        }
        return v;
    }

    /// Reads a field as a signed 16.16 fixed-point value and marks the key as used.
    ///
    /// @param key field name, matched case-insensitively
    /// @param fallback value, unscaled, for an absent field, or for a malformed number, which
    ///     records an error
    /// @return the value times 65536; 0 for an empty field
    int32_t fixed(std::string_view key, int32_t fallback = 0) {
        const auto* p = get(key);
        if (!p)
            return fallback;
        if (p->empty())
            return 0;
        auto t = trim(*p);
        const char* b = t.data();
        char* e = nullptr;
        errno = 0;
        double v = std::strtod(b, &e);
        if (e == b || errno == ERANGE || !std::isfinite(v)) {
            error = {
                ErrorCode::invalid_number, 0, "invalid fixed field '" + std::string(key) + "'"
            };
            return fallback;
        }
        constexpr double scale = 65536.0;
        v *= scale;
        // The scaled value truncates toward zero to 64 bits and keeps the low 32, as in 3.1c.
        return truncate_low32(v);
    }

    bool boolean(std::string_view key) { return (integer(key) & 1) != 0; }
};

template <class T>
T low(int32_t v) {
    using U = std::make_unsigned_t<T>;
    return std::bit_cast<T>(static_cast<U>(static_cast<uint32_t>(v)));
}

std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    std::istringstream in{std::string(s)};
    for (std::string x; in >> x;)
        out.push_back(std::move(x));
    return out;
}

int32_t wrap_double(int32_t value) {
    const auto bits = static_cast<uint32_t>(value) * 2U;
    return std::bit_cast<int32_t>(bits);
}

int32_t truncate_float_low32(float value) {
    return truncate_low32(static_cast<double>(value));
}

/// Returns the type_id of the first record of the sorted unit table named `name`.
///
/// Catalog entries are that sorted table; slot zero is not among them.
///
/// @param catalog loaded unit catalog
/// @param name unitname, matched case-insensitively
/// @return the type id, or 0 when no record has the name
uint16_t type_id_for_unit_name(const UnitCatalog& catalog, std::string_view name) {
    const auto wanted = lower(name);
    for (const auto& entry : catalog.entries)
        if (lower(entry.definition.unit_name) == wanted)
            return entry.type_id;
    return 0;
}
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

Result<MovementClassTable> load_movement_classes(std::string_view source) {
    Result<MovementClassTable> result;
    auto document = parse_tdf(source);
    if (!document) {
        result.error = std::move(document.error);
        return result;
    }
    for (std::size_t index = 0; index < result.value.slots.size(); ++index) {
        const auto wanted = "class" + std::to_string(index);
        const TdfSection* section = nullptr;
        for (const auto& candidate : document.value.sections)
            if (lower(candidate.name) == wanted) {
                section = &candidate;
                break;
            }
        if (!section)
            continue;
        Reader r(*section);
        MovementClassDefinition movement;
        movement.name = r.str("name");
        movement.footprint_x = low<int16_t>(r.integer("footprintx"));
        movement.footprint_z = low<int16_t>(r.integer("footprintz"));
        movement.max_water_depth = low<int16_t>(r.integer("maxwaterdepth", 10000));
        movement.min_water_depth = low<int16_t>(r.integer("minwaterdepth", -10000));
        movement.max_slope = static_cast<uint8_t>(r.integer("maxslope", 255));
        movement.bad_slope = static_cast<uint8_t>(r.integer("badslope", movement.max_slope >> 1U));
        movement.max_water_slope = static_cast<uint8_t>(r.integer("maxwaterslope", 255));
        movement.bad_water_slope =
            static_cast<uint8_t>(r.integer("badwaterslope", movement.max_water_slope >> 1U));
        movement.max_slope = std::min(movement.max_slope, movement.max_water_slope);
        movement.bad_slope = std::min(movement.bad_slope, movement.max_slope);
        movement.bad_water_slope = std::min(movement.bad_water_slope, movement.max_water_slope);
        if (r.error.code != ErrorCode::none) {
            result.error = std::move(r.error);
            return result;
        }
        result.value.slots[index] = std::move(movement);
    }
    return result;
}

Result<RuntimeDefinitionMetadata>
resolve_runtime_metadata(const UnitDefinition& u, const MovementClassTable& table) {
    Result<RuntimeDefinitionMetadata> result;
    auto& out = result.value;
    const MovementClassDefinition* movement = nullptr;
    if (!u.movement_class.empty()) {
        const auto wanted = lower(u.movement_class);
        for (std::size_t i = 0; i < table.slots.size(); ++i) {
            if (table.slots[i] && lower(table.slots[i]->name) == wanted) {
                movement = &*table.slots[i];
                out.movement_class_handle = static_cast<uint8_t>(i);
                break;
            }
        }
    }
    if (movement) {
        out.footprint_x = movement->footprint_x;
        out.footprint_z = movement->footprint_z;
        out.max_water_depth = movement->max_water_depth;
        out.min_water_depth = movement->min_water_depth;
        out.max_slope = movement->max_slope;
        out.bad_slope = movement->bad_slope;
        out.max_water_slope = movement->max_water_slope;
        out.bad_water_slope = movement->bad_water_slope;
    } else {
        out.footprint_x = u.footprint_x;
        out.footprint_z = u.footprint_z;
        out.max_water_depth = u.max_water_depth;
        out.min_water_depth = u.min_water_depth;
        out.max_slope = u.max_slope;
        out.bad_slope = u.bad_slope;
        out.max_water_slope = u.max_water_slope;
        out.bad_water_slope = u.bad_water_slope;
    }
    out.slope_speed_step_fixed = u.max_velocity_fixed / static_cast<int32_t>(out.max_slope + 1U);
    out.sight_distance = u.sight_distance;
    out.radar_distance = u.radar_distance;
    out.sonar_distance = u.sonar_distance;
    out.radar_distance_jam = u.radar_distance_jam;
    out.sonar_distance_jam = u.sonar_distance_jam;
    if (u.bm_code == 0) {
        if (out.footprint_x < 0 || out.footprint_z < 0) {
            result.error = {ErrorCode::malformed, 0, "negative building footprint"};
            return result;
        }
        const auto cells =
            static_cast<std::size_t>(out.footprint_x) * static_cast<std::size_t>(out.footprint_z);
        if (cells > limit::value_bytes) {
            result.error = {ErrorCode::limit, 0, "building yard exceeds bounded cell limit"};
            return result;
        }
        out.yard_cells.reserve(cells);
        std::size_t cursor = 0;
        while (out.yard_cells.size() < cells) {
            if (cursor >= u.yard_map.size()) {
                result.error = {
                    ErrorCode::malformed, 0, "yardmap has no recognized cell to repeat"
                };
                return result;
            }
            const char c = u.yard_map[cursor];
            std::optional<uint8_t> value;
            switch (c) {
            case '.':
                value = 0x00;
                break;
            case 'G':
                value = 0x8f;
                break;
            case 'O':
                value = 0x2b;
                break;
            case 'Y':
                value = 0x31;
                break;
            case 'C':
                value = 0x35;
                break;
            case 'c':
                value = 0x2d;
                break;
            case 'f':
                value = 0x6f;
                break;
            case 'o':
                value = 0x2f;
                break;
            case 'w':
                value = 0x37;
                break;
            case 'y':
                value = 0x29;
                break;
            default:
                break; // unrecognised separators are skipped, as in 3.1c
            }
            if (value) {
                out.yard_cells.push_back(*value);
                // Reading moves past a cell only when another byte follows
                // it, so the last cell repeats to fill a short yard map.
                if (cursor + 1 < u.yard_map.size())
                    ++cursor;
            } else {
                ++cursor;
            }
        }
    }
    return result;
}

Result<UnitDefinition> load_fbi(std::string_view source, std::string source_name) {
    Result<UnitDefinition> result;
    auto doc = parse_tdf(source);
    if (!doc) {
        result.error = std::move(doc.error);
        return result;
    }
    const TdfSection* section = nullptr;
    for (const auto& x : doc.value.sections)
        if (lower(x.name) == "unitinfo") {
            section = &x;
            break;
        }
    if (!section) {
        result.error = {ErrorCode::missing_unitinfo, 0, "FBI has no [UNITINFO] section"};
        return result;
    }
    Reader r(*section);
    auto& u = result.value;
    u.source_name = std::move(source_name);
#define S(member, key) u.member = r.str(key)
#define I(member, key) u.member = r.integer(key)
#define H(member, key) u.member = low<int16_t>(r.integer(key))
#define B(member, key) u.member = r.boolean(key)
#define F(member, key) u.member = r.real(key)
    S(unit_name, "unitname");
    {
        const auto* object = r.get("objectname");
        u.object_name = object ? *object : u.unit_name;
    }
    u.version = r.decimal("version");
    S(copyright, "copyright");
    S(display_name, "name");
    S(description, "description");
    S(designation, "designation");
    S(side, "side");
    S(ted_class, "tedclass");
    S(default_mission_type, "defaultmissiontype");
    S(movement_class, "movementclass");
    S(sound_category, "soundcategory");
    S(corpse, "corpse");
    S(yard_map, "yardmap");
    S(weapon1, "weapon1");
    S(weapon2, "weapon2");
    S(weapon3, "weapon3");
    S(explode_as, "explodeas");
    S(self_destruct_as, "selfdestructas");
    S(bad_target_category, "badtargetcategory");
    {
        const auto* p = r.get("wpri_badtargetcategory");
        u.wpri_bad_target_category = p ? *p : "none";
    }
    {
        const auto* p = r.get("wsec_badtargetcategory");
        u.wsec_bad_target_category = p ? *p : "none";
    }
    {
        const auto* p = r.get("wspe_badtargetcategory");
        u.wspe_bad_target_category = p ? *p : "none";
    }
    {
        const auto* p = r.get("nochasecategory");
        u.no_chase_category = p ? *p : "none";
    }
    u.categories = words(r.str("category"));
    I(build_cost_energy, "buildcostenergy");
    I(build_cost_metal, "buildcostmetal");
    I(build_time, "buildtime");
    I(max_damage, "maxdamage");
    u.max_velocity_fixed = r.fixed("maxvelocity");
    u.brake_rate_fixed = r.fixed("brakerate");
    u.acceleration_fixed = r.fixed("acceleration");
    u.bank_scale_fixed = r.fixed("bankscale", 65536);
    u.pitch_scale_fixed = r.fixed("pitchscale");
    u.damage_modifier_fixed = r.fixed("damagemodifier", 65536);
    u.move_rate1_fixed = r.fixed("moverate1", wrap_double(u.max_velocity_fixed));
    u.move_rate2_fixed = r.fixed("moverate2", wrap_double(u.max_velocity_fixed));
    H(turn_rate, "turnrate");
    H(worker_time, "workertime");
    H(heal_time, "healtime");
    H(sight_distance, "sightdistance");
    H(radar_distance, "radardistance");
    H(sonar_distance, "sonardistance");
    H(radar_distance_jam, "radardistancejam");
    H(sonar_distance_jam, "sonardistancejam");
    H(min_cloak_distance, "mincloakdistance");
    H(build_angle, "buildangle");
    H(build_distance, "builddistance");
    H(sort_bias, "sortbias");
    H(cruise_altitude, "cruisealt");
    H(maneuver_leash_length, "maneuverleashlength");
    H(attack_run_length, "attackrunlength");
    H(kamikaze_distance, "kamikazedistance");
    H(footprint_x, "footprintx");
    H(footprint_z, "footprintz");
    u.max_water_depth = low<int16_t>(r.integer("maxwaterdepth", 10000));
    u.min_water_depth = low<int16_t>(r.integer("minwaterdepth", -10000));
    u.max_slope = static_cast<uint8_t>(r.integer("maxslope", 255));
    u.bad_slope =
        static_cast<uint8_t>(r.integer("badslope", static_cast<int32_t>(u.max_slope >> 1U)));
    u.max_water_slope = static_cast<uint8_t>(r.integer("maxwaterslope", 255));
    u.bad_water_slope = static_cast<uint8_t>(
        r.integer("badwaterslope", static_cast<int32_t>(u.max_water_slope >> 1U))
    );
    u.max_slope = std::min(u.max_slope, u.max_water_slope);
    u.bad_slope = std::min(u.bad_slope, u.max_slope);
    u.bad_water_slope = std::min(u.bad_water_slope, u.max_water_slope);
    u.waterline = low<int8_t>(r.integer("waterline"));
    u.transport_size = low<int8_t>(r.integer("transportsize"));
    u.transport_capacity = low<int8_t>(r.integer("transportcapacity"));
    u.bm_code = low<int8_t>(r.integer("bmcode"));
    u.makes_metal = low<int8_t>(r.integer("makesmetal"));
    F(energy_make, "energymake");
    F(energy_use, "energyuse");
    F(metal_make, "metalmake");
    F(extracts_metal, "extractsmetal");
    F(wind_generator, "windgenerator");
    F(tidal_generator, "tidalgenerator");
    F(energy_storage, "energystorage");
    F(metal_storage, "metalstorage");
    u.cloak_cost = static_cast<float>(r.integer("cloakcost"));
    u.cloak_cost_moving =
        static_cast<float>(r.integer("cloakcostmoving", truncate_float_low32(u.cloak_cost)));
    u.can_cloak = u.cloak_cost > 0.0F;
    u.standing_move_order = static_cast<uint8_t>(r.integer("standingmoveorder", 2) & 3);
    u.standing_fire_order = static_cast<uint8_t>(r.integer("standingfireorder", 2) & 3);
    {
        const auto* p = r.get("selfdestructcountdown");
        u.self_destruct_countdown =
            p ? static_cast<uint8_t>(r.integer("selfdestructcountdown") & 7) : 5;
    }
    B(init_cloaked, "init_cloaked");
    B(downloadable, "downloadable");
    B(builder, "builder");
    B(stealth, "stealth");
    B(z_buffer, "zbuffer");
    B(is_airbase, "isairbase");
    B(targeting_upgrade, "istargetingupgrade");
    B(teleporter, "teleporter");
    B(hide_damage, "hidedamage");
    B(shoot_me, "shootme");
    B(armored_state, "armoredstate");
    B(activate_when_built, "activatewhenbuilt");
    B(can_fly, "canfly");
    B(can_hover, "canhover");
    B(upright, "upright");
    B(floater, "floater");
    B(amphibious, "amphibious");
    B(is_feature, "isfeature");
    B(no_shadow, "noshadow");
    B(immune_to_paralyzer, "immunetoparalyzer");
    B(hover_attack, "hoverattack");
    B(anti_weapons, "antiweapons");
    B(digger, "digger");
    B(on_offable, "onoffable");
    B(mobile_stand_orders, "mobilestandorders");
    B(fire_stand_orders, "firestandorders");
    B(can_stop, "canstop");
    B(can_attack, "canattack");
    B(can_guard, "canguard");
    B(can_patrol, "canpatrol");
    B(can_move, "canmove");
    B(can_load, "canload");
    B(can_reclamate, "canreclamate");
    B(can_resurrect, "canresurrect");
    B(can_capture, "cancapture");
    B(can_dgun, "candgun");
    B(kamikaze, "kamikaze");
    B(no_restrict, "norestrict");
    B(show_player_name, "showplayername");
    B(commander, "commander");
    B(cant_be_transported, "cantbetransported");
#undef S
#undef I
#undef H
#undef B
#undef F
    if (r.error.code != ErrorCode::none) {
        result.error = std::move(r.error);
        return result;
    }
    for (const auto& [k, v] : section->fields)
        if (!r.used.contains(k))
            u.unknown_fields.emplace(k, v);
    return result;
}

Result<UnitDefinition> load_fbi_file(const std::filesystem::path& path) {
    Result<UnitDefinition> result;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        result.error = {ErrorCode::io, 0, "cannot open FBI: " + path.string()};
        return result;
    }
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0) {
        result.error = {ErrorCode::io, 0, "cannot size FBI: " + path.string()};
        return result;
    }
    if (static_cast<uintmax_t>(size) > limit::input_bytes) {
        result.error = {ErrorCode::limit, 0, "FBI input exceeds 16 MiB"};
        return result;
    }
    std::string buffer(static_cast<std::size_t>(size), '\0');
    file.seekg(0, std::ios::beg);
    if (!buffer.empty())
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (!file) {
        result.error = {ErrorCode::io, 0, "cannot read FBI: " + path.string()};
        return result;
    }
    return load_fbi(buffer, path.string());
}

Result<UnitCatalog>
load_unit_catalog(const CatalogAssetReader& assets, const CatalogOptions& options) {
    Result<UnitCatalog> result;
    auto paths = assets.list_effective("units", ".FBI");
    if (!paths) {
        result.error = std::move(paths.error);
        return result;
    }
    if (paths.value.size() >= static_cast<std::size_t>(std::numeric_limits<uint16_t>::max())) {
        result.error = {ErrorCode::limit, 0, "unit catalog exceeds the 16-bit type-id space"};
        return result;
    }
    result.value.entries.reserve(paths.value.size());
    for (const auto& path : paths.value) {
        auto bytes = assets.read(path);
        if (!bytes) {
            result.error = std::move(bytes.error);
            return result;
        }
        auto definition = load_fbi(bytes.value, path);
        if (!definition) {
            result.error = std::move(definition.error);
            return result;
        }
        if (options.compatible && !options.compatible(definition.value))
            continue;
        result.value.entries.push_back({0, path, std::move(definition.value)});
    }
    // Types sort by unitname (UnitDef.unit_name) without regard to ASCII
    // case. Enumeration order is not the final type order.
    std::sort(
        result.value.entries.begin(),
        result.value.entries.end(),
        [](const CatalogEntry& a, const CatalogEntry& b) {
            return lower(a.definition.unit_name) < lower(b.definition.unit_name);
        }
    );
    uint16_t id = 1;
    for (auto& entry : result.value.entries)
        entry.type_id = id++;
    return result;
}

bool UnitCategoryMask::contains(uint16_t type_id) const noexcept {
    const auto index = static_cast<std::size_t>(type_id);
    return index < category_mask_bits && (words[index >> 5U] & (1U << (index & 31U))) != 0;
}

Result<ResolvedCategoryRegistry> resolve_unit_categories(const UnitCatalog& catalog) {
    Result<ResolvedCategoryRegistry> result;
    uint16_t maximum_id = 0;
    for (const auto& entry : catalog.entries) {
        if (entry.type_id == 0 || entry.type_id >= category_mask_bits) {
            result.error = {ErrorCode::limit, 0, "unit type id exceeds the 512-bit category mask"};
            return result;
        }
        maximum_id = std::max(maximum_id, entry.type_id);
        for (const auto& category : entry.definition.categories) {
            auto& mask = result.value.categories[lower(category)];
            const auto index = static_cast<std::size_t>(entry.type_id);
            mask.words[index >> 5U] |= 1U << (index & 31U);
        }
    }
    result.value.target_masks.resize(static_cast<std::size_t>(maximum_id) + 1U);
    const auto resolved = [&result](const std::string& category) -> UnitCategoryMask {
        // An unknown name gets a new empty mask, as in 3.1c.
        return result.value.categories[lower(category)];
    };
    for (const auto& entry : catalog.entries) {
        auto& masks = result.value.target_masks[entry.type_id];
        masks.primary_bad = resolved(entry.definition.wpri_bad_target_category);
        masks.secondary_bad = resolved(entry.definition.wsec_bad_target_category);
        masks.special_bad = resolved(entry.definition.wspe_bad_target_category);
        masks.no_chase = resolved(entry.definition.no_chase_category);
    }
    return result;
}

void append_download_build_ids(
    const UnitCatalog& catalog,
    std::vector<std::optional<std::vector<uint16_t>>>& build_lists,
    const std::vector<std::vector<DownloadMenuEntry>>& menus
) {
    for (std::size_t unit = 0; unit < catalog.entries.size(); ++unit) {
        if (unit >= build_lists.size() || !build_lists[unit])
            continue;
        auto& ids = *build_lists[unit];
        // Units are matched by array index. Once types are numbered that
        // index is UnitDef.type_id, which load_unit_catalog publishes as
        // type_id.
        const auto index = catalog.entries[unit].type_id;
        for (const auto& menu : menus) {
            for (const auto& entry : menu) {
                if (index != entry.menu_unit_index || ids.size() >= download_build_id_limit)
                    continue;
                const auto resolved = type_id_for_unit_name(catalog, entry.unit_name);
                if (resolved != 0)
                    ids.push_back(resolved);
            }
        }
    }
}
} // namespace oa::data::unit_definitions
