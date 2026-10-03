// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The stages of the Full tier's scene builder (runtime_full.hpp), named for
// the command line: while the tier is being built stage by stage, a run
// switches the stages that exist on with --full-stages, and the processor
// keeps drawing the rest of the battlefield. Nothing here needs SDL or the
// runtime, so the command line's parser shares it with the builder.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace oa::app::full {

/// The stages, one bit each, in the order they draw within a frame: the
/// terrain under everything; the sprites, particles and lines; the models
/// and their shadows; the fog over them all.
inline constexpr uint8_t stage_terrain = 0x01;
inline constexpr uint8_t stage_sprites = 0x02;
inline constexpr uint8_t stage_models = 0x04;
inline constexpr uint8_t stage_fog = 0x08;
/// Every stage.
inline constexpr uint8_t every_stage = stage_terrain | stage_sprites | stage_models | stage_fog;
/// The stages this build draws on the card; a stage asked for that is not
/// among them is left to the processor. The terrain is not a stage of the
/// builder: the Full tier draws it from the terrain atlas whatever the
/// stages asked for.
inline constexpr uint8_t stages_built = stage_sprites;

/// The name of each stage on the command line, in bit order.
struct StageName {
    uint8_t stage{};
    std::string_view name{};
};

/// The stages' names.
inline constexpr StageName stage_names[] = {
    {stage_terrain, "terrain"},
    {stage_sprites, "sprites"},
    {stage_models, "models"},
    {stage_fog, "fog"},
};

/// Parses the value of --full-stages: stage names joined by commas, or
/// "none" for no stage.
///
/// @param text the value, such as "sprites" or "terrain,sprites"
/// @param[out] stages the stages named; unchanged when the text is refused
/// @param[out] refused the word that is no stage's name, when one is
/// @return true when the text named stages, or none
[[nodiscard]] inline bool
parse_stages(std::string_view text, uint8_t& stages, std::string& refused) {
    if (text == "none") {
        stages = 0;
        return true;
    }
    uint8_t parsed = 0;
    while (true) {
        const auto comma = text.find(',');
        const auto word = text.substr(0, comma);
        // An empty word, as in an empty value or beside a stray comma,
        // names nothing.
        if (word.empty()) {
            refused.clear();
            return false;
        }
        bool known = false;
        for (const auto& named : stage_names)
            if (word == named.name) {
                parsed = static_cast<uint8_t>(parsed | named.stage);
                known = true;
            }
        if (!known) {
            refused = std::string(word);
            return false;
        }
        if (comma == std::string_view::npos)
            break;
        text = text.substr(comma + 1);
    }
    stages = parsed;
    return true;
}

/// Returns the names of the stages in a word, joined by commas: "none"
/// for no stage.
///
/// @param stages the stages
/// @return their names
[[nodiscard]] inline std::string stage_text(uint8_t stages) {
    std::string text;
    for (const auto& named : stage_names)
        if ((stages & named.stage) != 0) {
            if (!text.empty())
                text += ',';
            text += named.name;
        }
    return text.empty() ? std::string("none") : text;
}

} // namespace oa::app::full
