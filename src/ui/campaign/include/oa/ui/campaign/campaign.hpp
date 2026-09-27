// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// MSNBRIEF.GUI mission briefing panel and campaign unit restrictions.
#pragma once

#include "oa/data/campaign/campaign_file.hpp"
#include "oa/ui/campaign/briefing_text.hpp"
#include "oa/ui/campaign/frontend_host.hpp"

#include <cstddef>
#include <cstdint>

namespace oa {
struct UnitDef;
}

namespace oa::ui::campaign {

// Planet artwork: the briefing GAF and its panorama and rotating-planet
// sequence names.
struct PlanetArt {
    const char* planet;
    const char* gaf;
    const char* panorama;
    const char* rotation;
};

inline constexpr std::size_t kPlanetCount = 15;

/// Looks up a planet table entry.
///
/// @param index Table index.
/// @return Entry `index`, or entry 0 past the table.
[[nodiscard]] const PlanetArt& planet_art(std::size_t index);

/// Finds the planet table entry of a mission's Planet value.
///
/// Names compare case-sensitively; CORE players see "Lunar" as "Lunar2".
///
/// @param planet Planet value of the mission; null is empty.
/// @param side Player side (0 ARM, anything else CORE).
/// @return The table index; 0 for an unknown planet.
[[nodiscard]] std::size_t briefing_planet_index(const char* planet, int32_t side);

using RandomFn = int32_t (*)(void* context); // random source returning 0..0x7fff

inline constexpr std::size_t kBriefingTextBytes = 64 * 1024;
inline constexpr uint32_t kTickerIntervalMs = 25;
inline constexpr uint32_t kCycleTicks = 2;
inline constexpr uint32_t kRotationTickDivisor = 3;
// Engine clock ticks (30 a second) between a narration's request and its
// first sound; the narration counts as playing from the request.
inline constexpr uint32_t kNarrationDelay = 60;

struct BriefingPanel {
    std::size_t planet;
    int32_t wind_walk; // shown wind speed
    int32_t wind_regen_countdown;
    int32_t panorama_scroll;    // pixels scrolled through the panorama strip
    uint32_t panorama_deadline; // game tick of the next scroll step
    uint32_t ticker_next_ms;
    uint32_t ticker_last_tick;
    int32_t rotation_frame;
    bool narration_on; // SHUTUP button state
    char wrapped[kBriefingTextBytes];
    char text[kBriefingTextBytes + kReflowSlack];
    BriefingPager pager;
    BriefingPage page;
    char wind_label[52];
    char gravity_label[52];
};

/// Sets up the briefing panel for the bound mission.
///
/// SHUTUP starts on; the shown wind starts at a random speed within the
/// mission's range and its first change comes after a random 0..63
/// SOLARSYSTEM updates; the planet art follows the mission's Planet; the briefing is wrapped
/// and its first page laid out; narration starts; the ready cursor is selected.
///
/// @param[out] panel Briefing panel state.
/// @param campaign Campaign object with the bound mission; must not be null.
/// @param side Player side (0 ARM, else CORE).
/// @param region TextRegion geometry in pixels.
/// @param wrap_width Width the briefing wraps to, in pixels.
/// @param measure Measures text in pixels.
/// @param measure_context Context passed to `measure`.
/// @param random Random source (0..0x7fff); null rolls 0.
/// @param random_context Context passed to `random`.
/// @param host Frontend services.
/// @param narrate Whether narration may play; the game plays none while a match runs.
void briefing_panel_enter(
    BriefingPanel* panel,
    const oa::data::campaign::CampaignFile* campaign,
    int32_t side,
    const BriefingRegion* region,
    int32_t wrap_width,
    MeasureText measure,
    void* measure_context,
    RandomFn random,
    void* random_context,
    const FrontendHost* host,
    bool narrate
);

/// Wraps the briefing to the TextRegion width, reflows its colour spans and lays out the first page.
///
/// @param[in,out] panel Briefing panel state; a campaign without briefing text
///                      leaves it with no text and an empty page.
/// @param campaign Campaign object with the bound mission; may be null.
/// @param region TextRegion geometry in pixels.
/// @param wrap_width Width the briefing wraps to, in pixels.
/// @param measure Measures text in pixels.
/// @param measure_context Context passed to `measure`.
void briefing_update_text(
    BriefingPanel* panel,
    const oa::data::campaign::CampaignFile* campaign,
    const BriefingRegion* region,
    int32_t wrap_width,
    MeasureText measure,
    void* measure_context
);

/// Streams the mission's narration WAV where narration may play.
///
/// It is heard kNarrationDelay engine clock ticks later, from its start.
///
/// @param campaign Campaign object with the bound mission; null plays nothing.
/// @param host Frontend services.
/// @param narrate Whether narration may play; the game plays none while a match runs.
void briefing_start_narration(
    const oa::data::campaign::CampaignFile* campaign, const FrontendHost* host, bool narrate
);

/// Updates the SOLARSYSTEM info: the wandering wind speed, its labels and the panorama scroll.
///
/// When its countdown runs out the shown wind moves by -2..+2 inside the
/// mission range and the next change comes after a random 0..62 updates. The
/// wind and gravity lines are formatted; the panorama scrolls one pixel once
/// its deadline has passed and the next step waits two game ticks.
///
/// @param[in,out] panel Briefing panel state.
/// @param campaign Campaign object with the bound mission; must not be null.
/// @param random Random source (0..0x7fff); null rolls 0.
/// @param random_context Context passed to `random`.
/// @param game_tick Current game tick.
/// @param panorama_width Panorama strip width in pixels; 0 or less does not scroll.
/// @param host Translates the labels.
void briefing_solar_system_tick(
    BriefingPanel* panel,
    const oa::data::campaign::CampaignFile* campaign,
    RandomFn random,
    void* random_context,
    uint32_t game_tick,
    int32_t panorama_width,
    const FrontendHost* host
);

/// Runs the briefing ticker every kTickerIntervalMs: SHUTUP and the PLANET rotation.
///
/// SHUTUP switches off once narration has finished; the rotation advances
/// one frame when the game tick has moved.
///
/// @param[in,out] panel Briefing panel state.
/// @param now_ms Current time in milliseconds.
/// @param game_tick Current game tick.
/// @param narration_playing Whether the narration still plays.
/// @param frame_count Frames of the rotation sequence; 0 or less draws nothing.
/// @param host Frontend services.
/// @return Whether the frame should be redrawn.
bool briefing_ticker(
    BriefingPanel* panel,
    uint32_t now_ms,
    uint32_t game_tick,
    bool narration_playing,
    int32_t frame_count,
    const FrontendHost* host
);

/// Handles a click on MSNBRIEF.GUI.
///
/// Start checks the Campaign CD, rescans the archives, stops narration and
/// raises the mission-start signal; SHUTUP toggles narration; PrevMenu stops
/// narration and goes back; TextRegion or MOREBAR turns the page. A closing
/// panel drops the text.
///
/// @param[in,out] panel Briefing panel state.
/// @param campaign Campaign object with the bound mission (for narration).
/// @param host Frontend services.
/// @param control Clicked control name, compared case-insensitively; null when the panel closes.
/// @param region TextRegion geometry in pixels.
/// @param measure Measures text in pixels.
/// @param measure_context Context passed to `measure`.
/// @param narrate Whether narration may play; the game plays none while a match runs.
void briefing_click(
    BriefingPanel* panel,
    const oa::data::campaign::CampaignFile* campaign,
    const FrontendHost* host,
    const char* control,
    const BriefingRegion* region,
    MeasureText measure,
    void* measure_context,
    bool narrate
);

/// Sets up BRIEFING.GUI over a paused game.
///
/// MOREBAR and TextRegion lose the text_list attribute, so TextRegion takes
/// clicks and MOREBAR stops being a hold button; the mission briefing is
/// wrapped and its first page laid out.
///
/// @param[in,out] panel Briefing panel state.
/// @param campaign Campaign object with the bound mission; may be null.
/// @param region TextRegion geometry in pixels.
/// @param wrap_width Width the briefing wraps to, in pixels.
/// @param measure Measures text in pixels.
/// @param measure_context Context passed to `measure`.
/// @param host Frontend services.
void ingame_briefing_enter(
    BriefingPanel* panel,
    const oa::data::campaign::CampaignFile* campaign,
    const BriefingRegion* region,
    int32_t wrap_width,
    MeasureText measure,
    void* measure_context,
    const FrontendHost* host
);

/// Handles a click on BRIEFING.GUI.
///
/// TextRegion and MOREBAR turn the page, OK only plays its sound; a closing
/// panel drops the text.
///
/// @param[in,out] panel Briefing panel state.
/// @param host Frontend services.
/// @param control Clicked control name, compared case-insensitively; null when the panel closes.
/// @param region TextRegion geometry in pixels.
/// @param measure Measures text in pixels.
/// @param measure_context Context passed to `measure`.
void ingame_briefing_click(
    BriefingPanel* panel,
    const FrontendHost* host,
    const char* control,
    const BriefingRegion* region,
    MeasureText measure,
    void* measure_context
);

/// Restricts building to the units listed in the mission's use-only TDF.
///
/// Every record past slot 0 loses OA_UNIT_DEF_FLAG_AVAILABLE, then the first
/// record whose unit name matches each TDF entry (case-insensitively) gets it.
///
/// @param[in,out] table Unit definition table.
/// @param count Number of records in `table`.
/// @param campaign Campaign object with the bound mission.
/// @param files Campaign file services.
/// @return false, with the table untouched, when the file is missing, too
///         large or does not parse.
bool load_unit_availability(
    UnitDef* table,
    uint32_t count,
    const oa::data::campaign::CampaignFile* campaign,
    const oa::data::campaign::CampaignFiles* files
);

} // namespace oa::ui::campaign
