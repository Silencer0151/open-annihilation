// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/recorder_session.hpp"
#include "oa/netgame/private_channel.hpp"

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string_view>

namespace oa::netgame {

namespace {

struct CommandName {
    std::string_view word;
    RecorderCommand command{};
    uint8_t integrity_op{};
};

constexpr CommandName command_names[] = {
    {"report", RecorderCommand::report, 0},
    {"players", RecorderCommand::players, 0},
    {"reportmod", RecorderCommand::report_mod, 0},
    {"date", RecorderCommand::date, 0},
    {"status", RecorderCommand::status, 0},
    {"syncon", RecorderCommand::speed_lock, 0},
    {"syncoff", RecorderCommand::speed_unlock, 0},
    {"autopause", RecorderCommand::autopause, 0},
    {"voteready", RecorderCommand::vote_ready, 0},
    {"ready", RecorderCommand::ready, 0},
    {"cmdwarp", RecorderCommand::commander_warp, 0},
    {"votego", RecorderCommand::vote_go, 0},
    {"forcego", RecorderCommand::force_go, 0},
    {"units", RecorderCommand::units, 0},
    {"fakewatch", RecorderCommand::fake_watch, 0},
    {"give", RecorderCommand::give, 0},
    {"stopgive", RecorderCommand::stop_give, 0},
    {"take", RecorderCommand::take, 0},
    {"takecmd", RecorderCommand::take_commander, 0},
    {"base", RecorderCommand::base_file, 0},
    {"dobase", RecorderCommand::do_base, 0},
    {"baseoff", RecorderCommand::base_off, 0},
    {"record", RecorderCommand::record, 0},
    {"sharemappos", RecorderCommand::share_camera, 0},
    {"sharelos", RecorderCommand::share_sight, 0},
    {"f1off", RecorderCommand::f1_off, 0},
    {"forcecd", RecorderCommand::force_cd, 0},
    {"randmap", RecorderCommand::random_map, 0},
    {"randmapex", RecorderCommand::random_map_ex, 0},
    // The integrity reports ask every machine for the line naming one of its parts.
    {"tdreport", RecorderCommand::integrity_report, 3},
    {"gp3report", RecorderCommand::integrity_report, 4},
    {"tpreport", RecorderCommand::integrity_report, 5},
    {"exereport", RecorderCommand::integrity_report, 6},
    {"crcreport", RecorderCommand::integrity_report, 7},
};

bool same_nocase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto x = a[i];
        auto y = b[i];
        if (x >= 'A' && x <= 'Z')
            x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = static_cast<char>(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}

/// Takes the next word off the front of a view.
///
/// @param[in,out] text the rest of the line
/// @return the word, without the spaces around it
std::string_view next_word(std::string_view& text) noexcept {
    while (!text.empty() && text.front() == ' ')
        text.remove_prefix(1);
    std::size_t end = 0;
    while (end < text.size() && text[end] != ' ')
        ++end;
    const auto word = text.substr(0, end);
    text.remove_prefix(end);
    return word;
}

/// Reads a decimal number; 0 for anything else.
int32_t number_of(std::string_view word) noexcept {
    int32_t value = 0;
    bool negative = false;
    std::size_t at = 0;
    if (!word.empty() && word[0] == '-') {
        negative = true;
        at = 1;
    }
    for (; at < word.size(); ++at) {
        if (word[at] < '0' || word[at] > '9')
            return 0;
        if (value < 100000)
            value = value * 10 + (word[at] - '0');
    }
    return negative ? -value : value;
}

} // namespace

RecorderCommandLine parse_recorder_command(const char* line) noexcept {
    RecorderCommandLine out{};
    if (line == nullptr)
        return out;
    std::string_view text{line, ::strnlen(line, private_record_bytes * 4)};
    // A chat line names its sender first: "<Name> .cmd args".
    if (!text.empty() && text.front() == '<') {
        const auto close = text.find("> ");
        if (close == std::string_view::npos)
            return out;
        text.remove_prefix(close + 2);
    }
    while (!text.empty() && text.front() == ' ')
        text.remove_prefix(1);
    if (text.size() < 2 || text.front() != '.')
        return out;
    text.remove_prefix(1);
    const auto word = next_word(text);
    for (const auto& name : command_names) {
        if (!same_nocase(word, name.word))
            continue;
        out.command = name.command;
        out.integrity_op = name.integrity_op;
        break;
    }
    if (out.command == RecorderCommand::none)
        return out;
    auto rest = text;
    const auto first = next_word(rest);
    const auto copied = first.size() < sizeof out.argument ? first.size() : sizeof out.argument - 1;
    std::memcpy(out.argument, first.data(), copied);
    out.first = number_of(first);
    out.second = number_of(next_word(rest));
    const auto all = text.size() < sizeof out.arguments ? text.size() : sizeof out.arguments - 1;
    std::memcpy(out.arguments, text.data(), all);
    return out;
}

bool recorder_arguments_name(
    const RecorderCommandLine& line, const char* name, std::size_t length
) noexcept {
    if (name == nullptr || length == 0)
        return false;
    const std::string_view wanted{name, ::strnlen(name, length)};
    std::string_view words{line.arguments, ::strnlen(line.arguments, sizeof line.arguments)};
    for (auto word = next_word(words); !word.empty(); word = next_word(words))
        if (same_nocase(word, wanted))
            return true;
    return false;
}

bool recorder_command_host_only(RecorderCommand command) noexcept {
    switch (command) {
    case RecorderCommand::speed_lock:
    case RecorderCommand::speed_unlock:
    case RecorderCommand::autopause:
    case RecorderCommand::commander_warp:
    case RecorderCommand::base_file:
    case RecorderCommand::base_off:
    case RecorderCommand::f1_off:
    case RecorderCommand::random_map:
    case RecorderCommand::random_map_ex:
        return true;
    default:
        return false;
    }
}

bool recorder_session_command(RecorderCommand command) noexcept {
    switch (command) {
    case RecorderCommand::autopause:
    case RecorderCommand::vote_ready:
    case RecorderCommand::ready:
    case RecorderCommand::force_go:
    case RecorderCommand::vote_go:
    case RecorderCommand::fake_watch:
    case RecorderCommand::force_cd:
    case RecorderCommand::f1_off:
    case RecorderCommand::random_map:
    case RecorderCommand::random_map_ex:
        return true;
    default:
        return false;
    }
}

void recorder_speed_range(
    const RecorderSession& session, uint8_t slowest, uint8_t fastest, uint8_t* low, uint8_t* high
) noexcept {
    uint8_t lo = slowest;
    uint8_t hi = fastest;
    if (session.options.speed_lock != 0) {
        // The limits travel as signed bytes relative to normal speed.
        const auto within = [slowest, fastest](int32_t v) {
            return static_cast<uint8_t>(v < slowest ? slowest : (v > fastest ? fastest : v));
        };
        lo = within(static_cast<int8_t>(session.options.speed_low) + recorder_speed_lock_offset);
        hi = within(static_cast<int8_t>(session.options.speed_high) + recorder_speed_lock_offset);
        if (hi < lo)
            hi = lo;
    }
    if (low != nullptr)
        *low = lo;
    if (high != nullptr)
        *high = hi;
}

bool recorder_speed_command(RecorderCommand command) noexcept {
    return command == RecorderCommand::speed_lock || command == RecorderCommand::speed_unlock;
}

bool recorder_apply_host_command(
    RecorderSession& session, const RecorderCommandLine& line
) noexcept {
    auto& options = session.options;
    const auto before = options;
    switch (line.command) {
    case RecorderCommand::speed_lock: {
        // Limits are typed relative to normal speed and kept within 0..20.
        auto low = line.first;
        auto high = line.second;
        const auto bound = [](int32_t v) {
            const int32_t floor = -static_cast<int32_t>(recorder_speed_lock_offset);
            const int32_t ceiling = recorder_speed_ceiling - recorder_speed_lock_offset;
            return v < floor ? floor : (v > ceiling ? ceiling : v);
        };
        low = bound(low);
        high = bound(high);
        if (high < low)
            high = low;
        options.speed_lock = 1;
        options.speed_low = static_cast<uint8_t>(static_cast<int8_t>(low));
        options.speed_high = static_cast<uint8_t>(static_cast<int8_t>(high));
        break;
    }
    case RecorderCommand::speed_unlock:
        options.speed_lock = 0;
        break;
    case RecorderCommand::autopause:
        options.autopause = 1;
        break;
    case RecorderCommand::commander_warp:
        // Each .cmdwarp turns the warp on or, when it is on, off.
        options.commander_warp = options.commander_warp != 0 ? 0 : 1;
        break;
    case RecorderCommand::f1_off:
        options.f1_off = 1;
        break;
    default:
        return false;
    }
    return std::memcmp(&before, &options, sizeof options) != 0;
}

namespace {

/// One building of the standard base, for both sides.
struct StandardBuilding {
    uint16_t arm_type{};     ///< ARM's unit type index
    uint16_t core_type{};    ///< CORE's unit type index
    int16_t offset_x{};      ///< both sides
    int16_t arm_offset_z{};  ///< ARM's
    int16_t core_offset_z{}; ///< CORE's
    uint16_t health{};       ///< both sides
};

/// The standard base: one row a building, ARM's in entries 1 to 15 and
/// CORE's in 16 to 30.
constexpr StandardBuilding standard_buildings[] = {
    {132, 274, -100, 140, 140, 2500},
    {22, 155, 100, 140, 140, 2500},
    {58, 189, 0, 40, 60, 8000},
    {87, 218, 0, -60, -60, 1000},
    {10, 205, 0, -120, -120, 4000},
    {68, 202, -100, -200, -200, 2500},
    {8, 145, 100, -200, -200, 2800},
    {64, 195, -220, 160, 160, 2000},
    {64, 195, 220, 160, 160, 2000},
    {64, 195, -220, -160, -160, 2000},
    {64, 195, 220, -160, -160, 2000},
    {52, 184, -190, 160, 160, 1700},
    {52, 184, 190, 160, 160, 1700},
    {52, 184, -190, -160, -160, 1700},
    {52, 184, 190, -160, -160, 1700},
};

/// Reads a number as a base file writes it: leading spaces, an optional
/// sign, then decimal digits or '$' and hexadecimal digits, and nothing
/// after them.
///
/// @param text the number's characters
/// @param[out] value the number
/// @return false for anything else, or a number past 32 bits
bool base_number(std::string_view text, int64_t* value) noexcept {
    std::size_t at = 0;
    while (at < text.size() && text[at] == ' ')
        ++at;
    bool negative = false;
    if (at < text.size() && (text[at] == '-' || text[at] == '+')) {
        negative = text[at] == '-';
        ++at;
    }
    int base = 10;
    if (at < text.size() && text[at] == '$') {
        base = 16;
        ++at;
    }
    if (at == text.size())
        return false;
    int64_t result = 0;
    for (; at < text.size(); ++at) {
        const char c = text[at];
        int digit = -1;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        if (digit < 0)
            return false;
        result = result * base + digit;
        if (result > 0xffffffffLL)
            return false;
    }
    *value = negative ? -result : result;
    return true;
}

/// Takes the next field of a building line: the characters before the
/// first separator, dropping them and the separator from the line. Without
/// a separator the field is empty and the line stays.
///
/// @param[in,out] line the rest of the line
/// @param separator ' ' or ';'
/// @return the field
std::string_view base_field(std::string_view& line, char separator) noexcept {
    const auto at = line.find(separator);
    if (at == std::string_view::npos)
        return {};
    const auto field = line.substr(0, at);
    line.remove_prefix(at + 1);
    return field;
}

} // namespace

RecorderBaseRead recorder_read_base(std::string_view text, RecorderBase& base) noexcept {
    bool counted = false;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.empty() || line.front() == ';')
            continue;
        int64_t value = 0;
        if (!counted) {
            if (!base_number(line, &value) || value > INT32_MAX || value < INT32_MIN)
                return RecorderBaseRead::bad_count;
            base.per_side = static_cast<int32_t>(value);
            counted = true;
            continue;
        }
        int64_t entry = 0;
        int64_t type = 0;
        int64_t offset_x = 0;
        int64_t offset_z = 0;
        int64_t health = 0;
        if (!base_number(base_field(line, ' '), &entry) || entry < 0 ||
            entry >= static_cast<int64_t>(recorder_base_entry_count))
            return RecorderBaseRead::bad_entry;
        if (!base_number(base_field(line, ' '), &type) || type < 0 || type > UINT16_MAX)
            return RecorderBaseRead::bad_type;
        if (!base_number(base_field(line, ' '), &offset_x) || offset_x < INT16_MIN ||
            offset_x > INT16_MAX)
            return RecorderBaseRead::bad_offset_x;
        if (!base_number(base_field(line, ' '), &offset_z) || offset_z < INT16_MIN ||
            offset_z > INT16_MAX)
            return RecorderBaseRead::bad_offset_z;
        if (!base_number(base_field(line, ';'), &health) || health < 0 || health > UINT16_MAX)
            return RecorderBaseRead::bad_health;
        auto& slot = base.entries[static_cast<std::size_t>(entry)];
        slot.unit_type = static_cast<uint16_t>(type);
        slot.offset_x = static_cast<int16_t>(offset_x);
        slot.offset_z = static_cast<int16_t>(offset_z);
        slot.health = static_cast<uint16_t>(health);
    }
    return RecorderBaseRead::read;
}

void recorder_standard_base(RecorderBase& base) noexcept {
    constexpr auto per_side = std::size(standard_buildings);
    base.per_side = static_cast<int32_t>(per_side);
    for (std::size_t i = 0; i < per_side; ++i) {
        const auto& building = standard_buildings[i];
        base.entries[1 + i] = {
            building.arm_type, building.offset_x, building.arm_offset_z, building.health
        };
        base.entries[1 + per_side + i] = {
            building.core_type, building.offset_x, building.core_offset_z, building.health
        };
    }
}

const char* recorder_base_read_text(RecorderBaseRead outcome) noexcept {
    switch (outcome) {
    case RecorderBaseRead::bad_count:
        return "Erroneous number of possible buildings";
    case RecorderBaseRead::bad_entry:
        return "Erroneous base file1";
    case RecorderBaseRead::bad_type:
        return "Erroneous base file2";
    case RecorderBaseRead::bad_offset_x:
        return "Erroneous base file3";
    case RecorderBaseRead::bad_offset_z:
        return "Erroneous base file4";
    case RecorderBaseRead::bad_health:
        return "Erroneous base file5";
    case RecorderBaseRead::read:
        break;
    }
    return "";
}

} // namespace oa::netgame
