// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The planner's tuning: every number that shapes the planned shots and that
// planner.hpp does not publish, in one place, so that the shots can be tuned
// after watching renders. planner.hpp holds the rest: the shot and cut
// lengths, the switch ratio, the turns, the memory, the hot spots' window
// and grid, the armies' link and advance, the safe area, the zoom limit,
// the view heights, the pan reach, the transitions, the big blast, the long
// range and the ending's hold and pull back.
//
// Ticks are game ticks; a second of game time is ticks_per_second ticks.
// Distances and heights are map pixels. Scores are whole numbers whose
// scale is the metal cost of the units they weigh.
#pragma once

#include "oa/formats/oascript.hpp"
#include "oa/media/director/planner.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oa::media::director::tuning {

// Time.

/// Game ticks a second of game time, at the game's normal speed.
inline constexpr uint32_t ticks_per_second = 30;
/// The opening shot aims to settle on the first commander this many ticks
/// after the first tick, and no follow shot interrupts it before: 8 seconds.
inline constexpr uint32_t opening_settle_ticks = 240;
/// The fewest ticks between two follow shots of one subject: 2 seconds.
inline constexpr uint32_t follow_period_ticks = 60;
/// How far ahead of its tick a follow shot, a pan or a cut frames its
/// subject: the camera arrives there as the subject does.
inline constexpr uint32_t follow_lead_ticks = 60;
/// How early the camera cuts to a moment: 2 seconds before it.
inline constexpr uint32_t moment_lead_ticks = 60;
/// How long after it a moment still holds the camera's interest.
inline constexpr uint32_t moment_after_ticks = 60;
/// Moments closer than this in time and moment_merge_pixels in space are one.
inline constexpr uint32_t moment_merge_ticks = 60;
/// How early the ending cuts to the deciding commander death: 3 seconds.
inline constexpr uint32_t ending_lead_ticks = 90;
/// A commander's builds are framed from this many ticks before the framed
/// tick ...
inline constexpr uint32_t build_frame_before_ticks = 30;
/// ... to this many after it.
inline constexpr uint32_t build_frame_after_ticks = 150;
/// The longest flight matched from a long-range shot to its detonation: 30
/// seconds.
inline constexpr uint32_t long_range_flight_ticks = 900;
/// A factory is a subject from this many ticks before a unit it builds is
/// finished ...
inline constexpr uint32_t factory_lead_max_ticks = 300;
/// ... to this many before it, so that the unit rolls out in view.
inline constexpr uint32_t factory_lead_min_ticks = 60;
/// A finished unit is framed as it rolls out this many ticks after it is
/// finished.
inline constexpr uint32_t roll_out_ticks = 120;
/// A base is framed by what its player started building this many ticks
/// before the turn, and during it.
inline constexpr uint32_t base_recent_ticks = 1800;
/// Long-range fire is framed wide when it lands this many ticks after the
/// decision or sooner.
inline constexpr uint32_t barrage_window_ticks = 300;
/// The longest a wide view of long-range fire is held: 8 seconds.
inline constexpr uint32_t barrage_shot_ticks = 240;
/// The fewest ticks between two wide views of long-range fire: 90 seconds.
inline constexpr uint32_t barrage_spacing_ticks = 2700;
/// The fewest ticks between two wide drifts from one side to the other: 3
/// minutes.
inline constexpr uint32_t crossing_spacing_ticks = 5400;
/// The shortest quiet turn of a subject that starts nothing new: 6 seconds.
inline constexpr uint32_t idle_turn_ticks = 180;
/// The shortest quiet turn of a subject that starts something: 10 seconds.
inline constexpr uint32_t busy_turn_ticks = 300;
/// A quiet turn lasts its shortest length and this many ticks times 0 to
/// turn_variations - 1, in an order that repeats every turn_variations
/// turns, so that the cuts do not keep time.
inline constexpr uint32_t turn_variation_ticks = 30;
/// The lengths a quiet turn takes, from its shortest.
inline constexpr uint32_t turn_variations = 5;
/// The step through them from one turn to the next; prime to
/// turn_variations, so that every length comes in turn.
inline constexpr uint32_t turn_variation_stride = 3;
/// Damage landing this many ticks before a decision or in its hot spots'
/// window keeps the camera on the battle: 15 seconds.
inline constexpr uint32_t battle_recent_ticks = 450;
/// The longest the camera holds action or a moment in a battle, waiting for
/// more, before a quiet turn may take it: 25 seconds.
inline constexpr uint32_t battle_hold_max_ticks = 750;
/// A commander that builds nothing scores commander_late_idle_score from
/// this many ticks after the first: 6 minutes.
inline constexpr uint32_t commander_idle_after_ticks = 10800;
/// The fewest ticks between two launches of one gun that are shown, with
/// their landing, when one view cannot show both: 30 seconds.
inline constexpr uint32_t launch_spacing_ticks = 900;
/// A gun's shot is shown from its launch only when it lands where the
/// ground is at most this much water, in percent.
inline constexpr int64_t launch_water_limit_percent = 50;

// Scores.

/// A commander's score before its builds' value is added.
inline constexpr int64_t commander_base_score = 1000;
/// A commander's score when it builds nothing, from
/// commander_idle_after_ticks on.
inline constexpr int64_t commander_late_idle_score = 600;
/// A factory's score when it finishes its first unit, before the unit's
/// value is added; each later unit halves it, down to the fourth.
inline constexpr int64_t factory_first_unit_score = 1500;
/// The units a factory finishes before its score stops halving.
inline constexpr uint32_t factory_halvings = 3;
/// A construction unit's score before the value of what it starts is added.
inline constexpr int64_t builder_base_score = 700;
/// A base's score before the value of what its player starts is added.
inline constexpr int64_t base_base_score = 800;
/// The whole map's score in the quiet parts, as the drift between sides.
inline constexpr int64_t overview_score = 700;
/// The fewest buildings that make a base.
inline constexpr size_t base_min_buildings = 3;
/// A death scores its unit's value this many times.
inline constexpr int64_t death_score_factor = 2;
/// A detonation scores its weapon's area of effect times its damage over this.
inline constexpr int64_t blast_score_divisor = 256;
/// An army's progress toward the enemy start: 0 at its own start, this at
/// the enemy's.
inline constexpr int64_t progress_full = 1000;
/// The fewest advancing armed units that make an army.
inline constexpr size_t army_min_units = 3;
/// A commander death's moment scores its commander's value this many times.
inline constexpr int64_t commander_death_moment_factor = 10;
/// A detonation that shakes the view is a big moment from this score on.
inline constexpr int64_t shaking_blast_min_score = 1000;
/// The least score, after the memory's penalty, that makes an action
/// subject (a hot spot, an army or long-range fire) take the camera; a
/// fight an army arrives at takes it with any score.
inline constexpr int64_t action_min_score = 100;
/// The fewest long-range shots landing in barrage_window_ticks that are
/// framed wide.
inline constexpr size_t barrage_min_fires = 2;
/// How much a view counts as the one shown before when it frames the same
/// subject, in percent, whatever their overlap.
inline constexpr int64_t same_subject_similarity_percent = 60;
/// The part of the memory's penalty an action subject bears, in percent: a
/// fight flares up again where it was shown, and is worth showing again.
inline constexpr int64_t action_memory_percent = 50;
/// A hot spot whose view shows more water than this, in percent, scores
/// less the more it shows ...
inline constexpr int64_t action_water_free_percent = 40;
/// ... down to nothing from this much water on.
inline constexpr int64_t action_water_none_percent = 80;
static_assert(action_water_free_percent < action_water_none_percent);
/// The least score of damage landing within battle_recent_ticks before a
/// decision or in its hot spots' window that makes a battle: while one
/// goes on, no idle commander takes a quiet turn and action is held.
inline constexpr int64_t battle_min_score = 500;

// Framing.

/// Added to a builder's build distance when finding what it builds: a
/// building's centre lies beyond the distance its builder reaches.
inline constexpr int32_t build_reach_slack_pixels = 96;
/// The lead room ahead of an advancing army, in percent of the view's width.
inline constexpr int64_t army_lead_percent = 20;
/// A moment's view height, in percent of close_view_height.
inline constexpr int64_t moment_height_percent = 200;
/// The ending's held view height, in percent of close_view_height.
inline constexpr int64_t ending_hold_height_percent = 150;
/// The ending's pulled-back view height, in percent of the held one.
inline constexpr int64_t ending_pull_back_percent = 250;
/// A pan, rather than a cut, needs the two view heights within this ratio.
inline constexpr int64_t pan_height_ratio = 2;
/// A hot spot sums the cells this many cells around its own: 1 is 3 by 3.
inline constexpr int32_t hot_neighbourhood_cells = 1;
/// A hot spot frames the attackers within this many cells of its own.
inline constexpr int32_t attacker_reach_cells = 2;
/// Two moments closer than this, and than moment_merge_ticks, are one.
inline constexpr int32_t moment_merge_pixels = 512;
/// The heights a commander's turns take in turn, in map pixels.
inline constexpr std::array<int64_t, 3> commander_heights{540, 760, 1000};
/// The largest height of a hot spot's view, unless it shows long-range
/// fire from its launch: close enough to see where the damage lands.
inline constexpr int64_t hot_view_height = 1000;
/// The smallest height of a factory's view.
inline constexpr int64_t factory_view_height = 600;
/// The smallest height of a construction unit's view.
inline constexpr int64_t builder_view_height = 700;
/// The smallest height of a base's view.
inline constexpr int64_t base_view_height = 700;
/// The largest height a base's buildings are gathered into.
inline constexpr int64_t base_gather_height = 800;
/// The height of the wide drift from one side to the other.
inline constexpr int64_t crossing_view_height = 1400;
/// The part of a wide view that holds long-range fire, in percent of each
/// side.
inline constexpr int64_t wide_safe_area_percent = 90;
/// The centres tried along each axis when a view is placed to show less
/// water; odd, so that the middle is tried.
inline constexpr int64_t water_search_steps = 5;
/// A view that shows more water than this, in percent, comes closer.
inline constexpr int64_t water_tolerance_percent = 30;
/// The middle part of the view, in percent of each side, that a lone
/// subject (a commander, a factory or a construction unit and what they
/// build) stays in when the view is placed to show less water.
inline constexpr int64_t lone_subject_percent = 40;
/// A commander in a square more water than this, in percent, walks under
/// the sea, hardly seen, and takes no quiet turn.
inline constexpr int64_t commander_water_limit_percent = 50;
/// The wide drift between sides is left out when the view halfway along it
/// shows more water than this, in percent.
inline constexpr int64_t crossing_water_limit_percent = 50;
/// Each step closer takes a view to this height, in percent of the last.
inline constexpr int64_t closer_step_percent = 85;

// Motion.

/// A slow push in starts this much higher than it ends, in percent.
inline constexpr int64_t push_in_percent = 120;
/// A factory's push in starts this much higher than it ends, in percent.
inline constexpr int64_t factory_push_percent = 140;
/// A drift moves the view this far across it over its turn, in percent of
/// its width.
inline constexpr int64_t drift_percent = 12;
/// A drift's directions, tried in this order: the eight points of the
/// compass, east first, in thousandths of the drift's length across and
/// down. The drift goes the way whose views show the least water.
inline constexpr std::array<std::array<int64_t, 2>, 8> drift_directions{{
    {1000, 0},
    {707, 707},
    {0, 1000},
    {-707, 707},
    {-1000, 0},
    {-707, -707},
    {0, -1000},
    {707, -707},
}};
/// The thousandths a drift direction is given in.
inline constexpr int64_t drift_direction_scale = 1000;
/// A drift runs on while every point of its subject stays in this middle
/// part of the view it passes, in percent of each side.
inline constexpr int64_t drift_hold_percent = 80;
/// A follow shot moves the camera only once a point of its subject leaves
/// the safe area, or the middle of the subject leaves this middle part of
/// the view, in percent of each side.
inline constexpr int64_t follow_deadband_percent = 40;
/// A follow shot moves the view at most this far, in percent of its width
/// across and of its height down.
inline constexpr int64_t follow_step_percent = 25;
/// A follow shot changes the view height at most this much, in percent.
inline constexpr int64_t follow_zoom_step_percent = 20;
/// A follow shot comes closer only when the view is more than this many
/// times the height its subject needs, in percent.
inline constexpr int64_t follow_zoom_in_percent = 200;

// Motion: spring frequencies in hertz, all critically damped.

/// The opening's settle from the whole map onto the first commander.
inline constexpr oa::formats::oascript::Decimal opening_spring{25, 2};
/// A pan to a new subject near the current one.
inline constexpr oa::formats::oascript::Decimal pan_spring{4, 1};
/// A follow shot that keeps a subject framed.
inline constexpr oa::formats::oascript::Decimal follow_spring{5, 1};
/// A factory's push in.
inline constexpr oa::formats::oascript::Decimal push_spring{3, 1};
/// The ending's pull back.
inline constexpr oa::formats::oascript::Decimal ending_spring{2, 1};
/// Every spring's damping ratio: settles without overshoot.
inline constexpr oa::formats::oascript::Decimal spring_damping{1, 0};

} // namespace oa::media::director::tuning
