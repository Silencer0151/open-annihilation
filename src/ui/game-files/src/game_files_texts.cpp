// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Game files screen's texts (game_files.hpp): every string through the
// interface catalogue, the platform's words with the engine's neutral ones,
// sizes in decimal units and the time a copy has left.
#include "game_files_internal.hpp"

#include "oa/data/languages/interface_text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace oa::ui::game_files {

namespace detail {

std::string tr(std::string_view english) {
    return std::string(oa::data::languages::interface_text(english));
}

std::string fill(std::string_view english, std::initializer_list<Place> places) {
    const std::string pattern = tr(english);
    std::string text;
    text.reserve(pattern.size() + 32);
    std::size_t at = 0;
    while (at < pattern.size()) {
        const std::size_t open = pattern.find('{', at);
        if (open == std::string::npos) {
            text.append(pattern, at, std::string::npos);
            break;
        }
        text.append(pattern, at, open - at);
        const std::size_t close = pattern.find('}', open + 1);
        if (close == std::string::npos) {
            text.append(pattern, open, std::string::npos);
            break;
        }
        const std::string_view name(pattern.data() + open + 1, close - open - 1);
        bool filled = false;
        for (const Place& place : places) {
            if (place.first == name) {
                text += place.second;
                filled = true;
                break;
            }
        }
        if (!filled)
            text.append(pattern, open, close - open + 1);
        at = close + 1;
    }
    return text;
}

namespace {

/// Lists words with a last joining pattern ("{a} and {b}" or "{a} or {b}").
///
/// @param words the words
/// @param last the pattern joining the last two
/// @return the list
std::string join_with(const std::vector<std::string>& words, std::string_view last) {
    if (words.empty())
        return {};
    std::string text = words.back();
    if (words.size() == 1)
        return text;
    text = fill(last, {{"a", words[words.size() - 2]}, {"b", text}});
    for (std::size_t index = words.size() - 2; index-- > 0;)
        text = fill("{a}, {b}", {{"a", words[index]}, {"b", text}});
    return text;
}

/// The engine's neutral wording of each platform word, by Model::words index.
constexpr std::array<std::string_view, word_count> neutral_words{
    "device",
    "Copy your Total Annihilation folder into Open Annihilation's own folder with your file "
    "manager.",
    "Into Open Annihilation's own folder",
    "on this device or anywhere the system's file picker reaches",
    "anywhere the file picker reaches",
    "Free up space on this device",
    "In your file manager: Open Annihilation's own folder › Total Annihilation",
    "the cloud",
};

/// The decimal units, from a thousand bytes up, as size texts write them.
constexpr std::array<std::string_view, 4> unit_patterns{"{n} KB", "{n} MB", "{n} GB", "{n} TB"};

/// Writes a number with a number of decimals, dropping trailing zeros and the point.
///
/// @param value the number
/// @param decimals digits after the point
/// @return the number's text
std::string decimal_text(double value, int decimals) {
    std::array<char, 48> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.*f", decimals, value);
    std::string text(buffer.data());
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0')
            text.pop_back();
        if (!text.empty() && text.back() == '.')
            text.pop_back();
    }
    return text;
}

/// Gives the decimals a value keeps for a number of significant digits; values of 100 and
/// more keep none, whatever the digits.
///
/// @param value the value, at least 1
/// @param digits significant digits wanted
/// @return digits after the point
int decimals_for(double value, int digits) {
    int whole_digits = 1;
    if (value >= 100.0)
        whole_digits = 3;
    else if (value >= 10.0)
        whole_digits = 2;
    return std::max(0, digits - whole_digits);
}

} // namespace

std::string join_and(const std::vector<std::string>& words) {
    return join_with(words, "{a} and {b}");
}

std::string join_or(const std::vector<std::string>& words) {
    return join_with(words, "{a} or {b}");
}

} // namespace detail

std::string platform_word(const Model& model, uint8_t which) {
    if (which >= detail::word_count)
        return {};
    if (!model.words[which].empty())
        return model.words[which];
    return detail::tr(detail::neutral_words[which]);
}

std::string size_text(uint64_t bytes, bool precise) {
    if (bytes < 1000)
        return detail::fill(bytes == 1 ? "{n} byte" : "{n} bytes", {{"n", std::to_string(bytes)}});
    const int digits = precise ? 3 : 2;
    double unit = 1000.0;
    std::size_t unit_index = 0;
    while (unit_index + 1 < detail::unit_patterns.size() &&
           static_cast<double>(bytes) >= unit * 1000.0) {
        unit *= 1000.0;
        ++unit_index;
    }
    double value = static_cast<double>(bytes) / unit;
    int decimals = detail::decimals_for(value, digits);
    double scale = std::pow(10.0, decimals);
    double rounded = std::round(value * scale) / scale;
    // Rounding may carry into another digit (9.96 to 10.0) or another unit (999.6 to 1000).
    if (rounded >= 1000.0 && unit_index + 1 < detail::unit_patterns.size()) {
        ++unit_index;
        value = rounded / 1000.0;
        decimals = detail::decimals_for(value, digits);
        scale = std::pow(10.0, decimals);
        rounded = std::round(value * scale) / scale;
    } else if (detail::decimals_for(rounded, digits) < decimals) {
        decimals = detail::decimals_for(rounded, digits);
        scale = std::pow(10.0, decimals);
        rounded = std::round(value * scale) / scale;
    }
    return detail::fill(
        detail::unit_patterns[unit_index], {{"n", detail::decimal_text(rounded, decimals)}}
    );
}

std::string time_left_text(std::optional<uint32_t> seconds) {
    if (!seconds)
        return detail::tr("working out the time left");
    const uint32_t left = *seconds;
    if (left < 45)
        return detail::tr("a few seconds left");
    if (left < 90)
        return detail::tr("about a minute left");
    if (left < 3570) {
        const uint32_t minutes = (left + 30) / 60;
        return detail::fill("about {n} minutes left", {{"n", std::to_string(minutes)}});
    }
    if (left < 5400)
        return detail::tr("about an hour left");
    const uint32_t hours = (left + 1800) / 3600;
    return detail::fill("about {n} hours left", {{"n", std::to_string(hours)}});
}

namespace {

/// Tells whether a part is present in a parts array (indexed by PartKind).
///
/// @param parts the parts present
/// @param kind the part
/// @return true when present
bool has(const std::array<bool, 11>& parts, PartKind kind) noexcept {
    return parts[static_cast<std::size_t>(kind)];
}

} // namespace

std::string ready_text(const std::array<bool, 11>& parts, bool demo) {
    if (demo || (has(parts, PartKind::demo) && !has(parts, PartKind::game_archives)))
        return detail::tr("The Total Annihilation demo (1997).");
    std::vector<std::string> expansions;
    if (has(parts, PartKind::core_contingency))
        expansions.push_back(detail::tr("Core Contingency"));
    if (has(parts, PartKind::battle_tactics))
        expansions.push_back(detail::tr("Battle Tactics"));
    std::vector<std::string> others;
    if (has(parts, PartKind::extra))
        others.push_back(detail::tr("extra units and maps"));
    if (has(parts, PartKind::music))
        others.push_back(detail::tr("music"));
    if (has(parts, PartKind::movies))
        others.push_back(detail::tr("movies"));
    if (has(parts, PartKind::mod))
        others.push_back(detail::tr("mods"));
    const std::string game = has(parts, PartKind::update_31c)
                                 ? detail::tr("Total Annihilation 3.1c")
                                 : detail::tr("Total Annihilation");
    if (!expansions.empty() && !others.empty())
        return detail::fill(
            "{game} with {expansions}, {others}.",
            {{"game", game},
             {"expansions", detail::join_and(expansions)},
             {"others", detail::join_and(others)}}
        );
    if (!expansions.empty())
        return detail::fill(
            "{game} with {list}.", {{"game", game}, {"list", detail::join_and(expansions)}}
        );
    if (!others.empty())
        return detail::fill(
            "{game} with {list}.", {{"game", game}, {"list", detail::join_and(others)}}
        );
    return detail::fill("{game}.", {{"game", game}});
}

std::string summary_text(const std::array<bool, 11>& parts, uint32_t mods, bool demo) {
    std::vector<std::string> items;
    if (demo || (has(parts, PartKind::demo) && !has(parts, PartKind::game_archives)))
        items.push_back(detail::tr("The Total Annihilation demo (1997)"));
    else if (has(parts, PartKind::update_31c))
        items.push_back(detail::tr("3.1c"));
    else if (has(parts, PartKind::game_archives))
        items.push_back(detail::tr("Total Annihilation"));
    if (has(parts, PartKind::core_contingency))
        items.push_back(detail::tr("Core Contingency"));
    if (has(parts, PartKind::battle_tactics))
        items.push_back(detail::tr("Battle Tactics"));
    if (has(parts, PartKind::extra))
        items.push_back(detail::tr("extra units and maps"));
    if (has(parts, PartKind::music))
        items.push_back(detail::tr("music"));
    if (has(parts, PartKind::movies))
        items.push_back(detail::tr("movies"));
    if (mods == 1)
        items.push_back(detail::tr("1 mod"));
    else if (mods > 1)
        items.push_back(detail::fill("{n} mods", {{"n", std::to_string(mods)}}));
    if (items.empty())
        return detail::tr("No game files");
    std::string text = items.front();
    for (std::size_t index = 1; index < items.size(); ++index)
        text = detail::fill("{a} · {b}", {{"a", text}, {"b", items[index]}});
    return text;
}

} // namespace oa::ui::game_files
