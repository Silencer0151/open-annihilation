// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/ota.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <cstdint>

namespace oa::formats::ota {
namespace {
struct Node {
    std::string name;
    std::vector<std::pair<std::string, std::string>> values;
    std::vector<Node> children;
};

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

bool equal(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool starts(std::string_view a, std::string_view b) {
    return a.size() >= b.size() && equal(a.substr(0, b.size()), b);
}

class Parser {
  public:

    std::string_view text;
    std::size_t at{}, count{};
    Error error{};

    void space() {
        for (;;) {
            while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])))
                ++at;
            if (at + 1 < text.size() && text[at] == '/' && text[at + 1] == '/') {
                at += 2;
                while (at < text.size() && text[at] != '\n')
                    ++at;
                continue;
            }
            if (at + 1 < text.size() && text[at] == '/' && text[at + 1] == '*') {
                const auto end = text.find("*/", at + 2);
                if (end == text.npos) {
                    fail(ErrorCode::malformed, "unterminated comment");
                    return;
                }
                at = end + 2;
                continue;
            }
            break;
        }
    }

    bool fail(ErrorCode c, const char* m) {
        if (error.code == ErrorCode::none)
            error = {c, at, m};
        return false;
    }

    bool section(Node& out, std::size_t depth) {
        if (depth > limit::nesting)
            return fail(ErrorCode::nesting_limit, "OTA section nesting exceeds limit");
        if (++count > limit::sections)
            return fail(ErrorCode::section_limit, "OTA section count exceeds limit");
        space();
        if (at >= text.size() || text[at] != '[')
            return fail(ErrorCode::malformed, "expected OTA section name");
        const auto close = text.find(']', ++at);
        if (close == text.npos)
            return fail(ErrorCode::malformed, "unterminated OTA section name");
        out.name = std::string(trim(text.substr(at, close - at)));
        at = close + 1;
        space();
        if (at >= text.size() || text[at++] != '{')
            return fail(ErrorCode::malformed, "expected OTA section body");
        for (;;) {
            space();
            if (error.code != ErrorCode::none)
                return false;
            if (at >= text.size())
                return fail(ErrorCode::malformed, "unterminated OTA section body");
            if (text[at] == '}') {
                ++at;
                return true;
            }
            if (text[at] == '[') {
                out.children.emplace_back();
                if (!section(out.children.back(), depth + 1))
                    return false;
                continue;
            }
            const auto key_at = at;
            while (at < text.size() && text[at] != '=' && text[at] != '}' && text[at] != '[')
                ++at;
            if (at >= text.size() || text[at] != '=')
                return fail(ErrorCode::malformed, "expected OTA property assignment");
            auto key = trim(text.substr(key_at, at - key_at));
            ++at;
            const auto value_at = at;
            while (at < text.size() && text[at] != ';')
                ++at;
            if (at >= text.size())
                return fail(ErrorCode::malformed, "unterminated OTA property value");
            auto value = trim(text.substr(value_at, at - value_at));
            ++at;
            if (key.size() > limit::value_bytes || value.size() > limit::value_bytes)
                return fail(ErrorCode::value_limit, "OTA property exceeds limit");
            out.values.emplace_back(std::string(key), std::string(value));
        }
    }
};

std::string_view value(const Node& n, std::string_view key) {
    for (const auto& v : n.values)
        if (equal(v.first, key))
            return v.second;
    return {};
}

const Node* child(const Node& n, std::string_view name) {
    for (const auto& c : n.children)
        if (equal(c.name, name))
            return &c;
    return nullptr;
}

bool integer(std::string_view s, int32_t& out) {
    s = trim(s);
    if (s.empty())
        return false;
    if (s.front() == '+') {
        s.remove_prefix(1);
        if (s.empty())
            return false;
    }
    const auto result = std::from_chars(s.data(), s.data() + s.size(), out);
    return result.ec == std::errc{};
}

bool explicit_start_suffix(std::string_view s, int32_t& out) {
    if (s.empty() || s.front() < '0' || s.front() > '9')
        return false;
    std::size_t digits = 0;
    while (digits < s.size() && s[digits] >= '0' && s[digits] <= '9')
        ++digits;
    const auto result = std::from_chars(s.data(), s.data() + digits, out);
    return result.ec == std::errc{};
}

ParseResult failure(const Error& e) {
    return {std::nullopt, e};
}
} // namespace

ParseResult parse(std::string_view text) {
    if (text.size() > limit::input_bytes)
        return failure({ErrorCode::input_limit, 0, "OTA input exceeds 4 MiB limit"});
    Parser parser{text};
    std::vector<Node> roots;
    while (true) {
        parser.space();
        if (parser.error.code != ErrorCode::none)
            return failure(parser.error);
        if (parser.at == text.size())
            break;
        roots.emplace_back();
        if (!parser.section(roots.back(), 1))
            return failure(parser.error);
    }
    const Node* global = nullptr;
    for (const auto& n : roots)
        if (equal(n.name, "GlobalHeader")) {
            global = &n;
            break;
        }
    if (!global)
        return failure({ErrorCode::malformed, 0, "OTA has no GlobalHeader section"});
    MapMetadata output;
    output.mission_name = value(*global, "missionname");
    output.mission_description = value(*global, "missiondescription");
    output.planet = value(*global, "planet");
    output.memory_requirement = value(*global, "memory");
    output.map_size = value(*global, "size");
    output.permitted_player_counts = value(*global, "numplayers");
    for (const auto& section : global->children) {
        if (!starts(section.name, schema_section_prefix))
            continue;
        int32_t number{};
        if (!integer(std::string_view(section.name).substr(schema_section_prefix.size()), number))
            continue;
        const auto canonical = std::string(schema_section_prefix) + std::to_string(number);
        // The game looks a schema up by exactly this name, so an alias such as
        // Schema 00 is never found.
        if (!equal(section.name, canonical))
            continue;
        Schema schema;
        schema.number = number;
        schema.type = value(section, "Type");
        const auto* specials = child(section, "specials");
        int32_t implicit_start_number = 0;
        if (specials)
            for (const auto& special : specials->children) {
                const auto what = value(special, "specialwhat");
                if (!starts(what, start_position_prefix))
                    continue;
                if (schema.start_positions.size() >= limit::start_positions)
                    return failure(
                        {ErrorCode::start_position_limit,
                         0,
                         "OTA start-position count exceeds limit"}
                    );
                int32_t suffix{}, x{}, z{};
                const auto suffix_text = what.substr(start_position_prefix.size());
                if (!explicit_start_suffix(suffix_text, suffix))
                    suffix = ++implicit_start_number;
                if (suffix > 0)
                    --suffix;
                if (!integer(value(special, "XPos"), x))
                    x = 0;
                if (!integer(value(special, "ZPos"), z))
                    z = 0;
                if (x < std::numeric_limits<int16_t>::min() ||
                    x > std::numeric_limits<int16_t>::max() ||
                    z < std::numeric_limits<int16_t>::min() ||
                    z > std::numeric_limits<int16_t>::max())
                    return failure(
                        {ErrorCode::number_out_of_range,
                         0,
                         "OTA start position exceeds the signed 16-bit field"}
                    );
                schema.start_positions.push_back(
                    {suffix, static_cast<int16_t>(x), static_cast<int16_t>(z)}
                );
            }
        output.schemas.push_back(std::move(schema));
    }
    return {std::move(output), std::nullopt};
}

const Schema* select_multiplayer_schema(const MapMetadata& map, int32_t players) {
    const Schema* best = nullptr;
    std::size_t best_count = 0;
    for (const auto type : multiplayer_schema_types) {
        for (int32_t number = 0;; ++number) {
            const Schema* schema = nullptr;
            for (const auto& candidate : map.schemas)
                if (candidate.number == number) {
                    schema = &candidate;
                    break;
                }
            // The scan of a type ends at the first absent Schema N.
            if (!schema)
                break;
            if (!equal(schema->type, type))
                continue;
            const auto count = schema->start_positions.size();
            if (count != 0 &&
                (static_cast<int32_t>(count) == players || players == 0 ||
                 (best_count < count && static_cast<int32_t>(best_count) != players))) {
                best = schema;
                best_count = count;
            }
        }
    }
    return best;
}
} // namespace oa::formats::ota
