// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The subjects the camera can film, each with its score and its framing on
// a tick: a commander with what it builds, a factory finishing a unit, a
// construction unit with what it starts, a player's base, a player's army
// advancing on an enemy start, a hot spot where damage is about to land,
// long-range fire framed wide, a moment and the whole map. Every framing
// but a moment's is placed to show as little water as its subject allows.

#include "planner_internal.hpp"
#include "planner_tuning.hpp"

#include <algorithm>
#include <numeric>

namespace oa::media::director::planning {
namespace {

/// Percent: the whole of something.
constexpr int64_t percent{100};

/// A point with a weight.
struct WeightedPoint {
    Point at{};
    int64_t weight{};
};

/// Calls a function with each building a player started in [from, to)
/// within a builder's reach of a point, the builder itself left out.
///
/// @param analysis the analysis
/// @param player the player
/// @param builder the builder's life
/// @param at where the builder is
/// @param from the first tick
/// @param to the tick after the last
/// @param visit called with the created unit's life and where it was created
template <typename Visit>
void visit_builds(
    const Analysis& analysis,
    uint8_t player,
    uint32_t builder,
    Point at,
    uint32_t from,
    uint32_t to,
    Visit&& visit
) {
    const auto& created{analysis.created[player]};
    const auto& events{analysis.timeline->events};
    const int64_t reach{analysis.lives[builder].build_reach};
    const auto first{
        std::lower_bound(created.begin(), created.end(), from, [&](uint32_t index, uint32_t tick) {
            return events[index].tick < tick;
        })
    };
    for (auto next{first}; next != created.end() && events[*next].tick < to; ++next) {
        const uint32_t life{analysis.event_lives[*next].unit};
        if (life == builder)
            continue;
        const Point built{ground_of(events[*next].at)};
        if (distance_squared(built, at) <= reach * reach)
            visit(life, built);
    }
}

/// Returns points framed around the heaviest of them: sorted by their
/// distance from the weighted middle, each is kept while the view that
/// holds the kept ones is at most a height.
///
/// @param stage the stage
/// @param points the points and their weights; at least one weighs above 0
///        for the middle to be weighted
/// @param max_height the largest height the kept points may need
/// @return the kept points, the nearest first; the nearest is always kept
std::vector<Point> gather_around_weight(
    const Stage& stage, std::span<const WeightedPoint> points, int64_t max_height
) {
    std::vector<Point> kept{};
    if (points.empty())
        return kept;
    int64_t sum_x{};
    int64_t sum_row{};
    int64_t weight{};
    for (const WeightedPoint& point : points) {
        const int64_t each{std::max<int64_t>(point.weight, 0)};
        sum_x += int64_t{point.at.x} * each;
        sum_row += int64_t{point.at.row} * each;
        weight += each;
    }
    if (weight == 0) {
        for (const WeightedPoint& point : points) {
            sum_x += point.at.x;
            sum_row += point.at.row;
        }
        weight = static_cast<int64_t>(points.size());
    }
    const Point middle{
        static_cast<int32_t>(floor_div(sum_x, weight)),
        static_cast<int32_t>(floor_div(sum_row, weight)),
    };
    std::vector<Point> order{};
    order.reserve(points.size());
    for (const WeightedPoint& point : points)
        order.push_back(point.at);
    std::sort(order.begin(), order.end(), [&](Point a, Point b) {
        const int64_t first{distance_squared(a, middle)};
        const int64_t second{distance_squared(b, middle)};
        if (first != second)
            return first < second;
        return a.x != b.x ? a.x < b.x : a.row < b.row;
    });
    for (const Point& point : order) {
        kept.push_back(point);
        if (kept.size() > 1u && framed_height(stage, kept) > max_height)
            kept.pop_back();
    }
    return kept;
}

/// Returns the framing of points: framed, then placed to show less water;
/// a view that still shows more than tuning::water_tolerance_percent of it
/// comes closer, tuning::closer_step_percent at a time, while it holds the
/// points and is no closer than close_view_height, and the one with the
/// least water wins, the widest on a tie.
///
/// @param analysis the analysis
/// @param points the points
/// @param min_height the smallest height, unless water brings the view closer
/// @param max_height the largest height
/// @param keep_percent the middle part of the view the points stay in as it
///        is placed: safe_area_percent, or tuning::lone_subject_percent for
///        a lone subject
/// @return the framing, or none for no point
std::optional<Framing> frame_on_land(
    const Analysis& analysis,
    std::vector<Point> points,
    int64_t min_height,
    int64_t max_height,
    int64_t keep_percent = safe_area_percent
) {
    const Stage& stage{analysis.stage};
    const std::optional<PlannedView> view{frame_points(stage, points, min_height, max_height)};
    if (!view)
        return std::nullopt;
    PlannedView best{settle_on_land(analysis, points, *view, keep_percent)};
    int64_t best_water{water_percent(analysis, best)};
    const int64_t closest{std::max<int64_t>(framed_height(stage, points), close_view_height)};
    for (int64_t height{view->height * tuning::closer_step_percent / percent};
         best_water > tuning::water_tolerance_percent && height >= closest;
         height = height * tuning::closer_step_percent / percent) {
        PlannedView closer{*view};
        closer.height = height;
        closer = settle_on_land(analysis, points, clamp_planned(stage, closer), keep_percent);
        const int64_t water{water_percent(analysis, closer)};
        if (water < best_water) {
            best = closer;
            best_water = water;
        }
    }
    return Framing{best, std::move(points)};
}

/// Returns a commander's score: commander_base_score and the value of what
/// it builds over the next turn. One that builds nothing scores
/// tuning::commander_late_idle_score from tuning::commander_idle_after_ticks
/// on; one under the sea scores none.
///
/// @param analysis the analysis
/// @param player the commander's player
/// @param tick the tick
/// @return the score, or none when the player has no framable commander
std::optional<int64_t> commander_score(const Analysis& analysis, uint8_t player, uint32_t tick) {
    const uint32_t commander{commander_life(analysis, player, tick)};
    if (commander == no_index || !framable(analysis, commander, tick))
        return std::nullopt;
    const Point at{ground_point(analysis, commander, tick)};
    if (water_at(analysis, at) > tuning::commander_water_limit_percent)
        return std::nullopt;
    int64_t built{};
    visit_builds(
        analysis, player, commander, at, tick, later(tick, turn_ticks), [&](uint32_t life, Point) {
            built += analysis.lives[life].value;
        }
    );
    if (built > 0)
        return tuning::commander_base_score + built;
    if (tick - std::min(tick, analysis.first_tick) >= tuning::commander_idle_after_ticks)
        return tuning::commander_late_idle_score;
    return tuning::commander_base_score;
}

/// Returns the framing of a builder and what it builds around a tick.
///
/// @param analysis the analysis
/// @param player the builder's player
/// @param builder the builder's life
/// @param tick the tick
/// @param min_height the smallest height
/// @return the framing
std::optional<Framing> frame_builds(
    const Analysis& analysis, uint8_t player, uint32_t builder, uint32_t tick, int64_t min_height
) {
    const Point at{ground_point(analysis, builder, tick)};
    std::vector<Point> points{at};
    visit_builds(
        analysis,
        player,
        builder,
        at,
        earlier(tick, tuning::build_frame_before_ticks),
        later(tick, tuning::build_frame_after_ticks),
        [&](uint32_t, Point built) { points.push_back(built); }
    );
    return frame_on_land(
        analysis,
        std::move(points),
        min_height,
        max_subject_view_height,
        tuning::lone_subject_percent
    );
}

/// Returns the framing of a commander and what it builds around a tick.
///
/// @param analysis the analysis
/// @param player the commander's player
/// @param tick the tick
/// @param require_framable give none when the commander is not framable
/// @param min_height the smallest height
/// @return the framing, or none without a commander
std::optional<Framing> frame_commander(
    const Analysis& analysis,
    uint8_t player,
    uint32_t tick,
    bool require_framable,
    int64_t min_height
) {
    uint32_t commander{commander_life(analysis, player, tick)};
    if (commander == no_index && !require_framable) {
        // The opening frames the player's first commander where it will be.
        const auto& commanders{analysis.commanders[player]};
        if (commanders.empty())
            return std::nullopt;
        commander = commanders.front();
    }
    if (commander == no_index || (require_framable && !framable(analysis, commander, tick)))
        return std::nullopt;
    return frame_builds(analysis, player, commander, tick, min_height);
}

/// Returns the production a factory is filmed for on a tick: its first unit
/// still to roll out.
///
/// @param analysis the analysis
/// @param factory the factory's life
/// @param tick the tick
/// @return the production's index, or no_index for none
uint32_t factory_production(const Analysis& analysis, uint32_t factory, uint32_t tick) {
    const auto& productions{analysis.productions};
    const uint32_t from{earlier(tick, tuning::roll_out_ticks)};
    const auto first{std::lower_bound(
        productions.begin(), productions.end(), from, [](const Production& made, uint32_t wanted) {
            return made.tick < wanted;
        }
    )};
    for (auto next{first}; next != productions.end(); ++next)
        if (next->factory == factory)
            return static_cast<uint32_t>(next - productions.begin());
    return no_index;
}

/// Returns a factory's score for a unit it finishes: a score that halves
/// with each unit before it, and the unit's value.
///
/// @param analysis the analysis
/// @param made the production
/// @return the score
int64_t production_score(const Analysis& analysis, const Production& made) {
    const uint32_t halvings{std::min(made.order, tuning::factory_halvings)};
    return (tuning::factory_first_unit_score >> halvings) + analysis.lives[made.unit].value;
}

/// Returns a player's factory with the best-scoring unit finished between
/// factory_lead_min_ticks and factory_lead_max_ticks after a tick.
///
/// @param analysis the analysis
/// @param player the player
/// @param tick the tick
/// @return the subject; score 0 for none
SideSubject best_factory(const Analysis& analysis, uint8_t player, uint32_t tick) {
    SideSubject best{};
    const auto& productions{analysis.productions};
    const uint32_t from{later(tick, tuning::factory_lead_min_ticks)};
    const uint32_t to{later(tick, tuning::factory_lead_max_ticks)};
    const auto first{std::lower_bound(
        productions.begin(), productions.end(), from, [](const Production& made, uint32_t wanted) {
            return made.tick < wanted;
        }
    )};
    for (auto next{first}; next != productions.end() && next->tick <= to; ++next) {
        const Life& factory{analysis.lives[next->factory]};
        if (factory.owner != player || !framable(analysis, next->factory, tick))
            continue;
        const int64_t score{production_score(analysis, *next)};
        if (score > best.score)
            best = SideSubject{
                SubjectKey{SubjectKind::factory, player, static_cast<int32_t>(next->factory)},
                score,
            };
    }
    return best;
}

/// Returns the framing of a factory and the unit it finishes, as that unit
/// rolls out.
///
/// @param analysis the analysis
/// @param factory the factory's life
/// @param tick the tick
/// @return the framing, or none when the factory is gone or finishes nothing
std::optional<Framing> frame_factory(const Analysis& analysis, uint32_t factory, uint32_t tick) {
    if (!framable(analysis, factory, tick))
        return std::nullopt;
    const uint32_t index{factory_production(analysis, factory, tick)};
    std::vector<Point> points{analysis.lives[factory].created_at};
    if (index != no_index) {
        const Production& made{analysis.productions[index]};
        points.push_back(made.at);
        points.push_back(
            ground_point(analysis, made.unit, later(made.tick, tuning::roll_out_ticks))
        );
    }
    return frame_on_land(
        analysis,
        std::move(points),
        tuning::factory_view_height,
        max_subject_view_height,
        tuning::lone_subject_percent
    );
}

/// Returns a player's construction unit that starts the most value of
/// buildings over the next turn, each building counted for the framable
/// construction unit nearest it that reaches it.
///
/// @param analysis the analysis
/// @param player the player
/// @param tick the tick
/// @return the subject; score 0 for none
SideSubject best_builder(const Analysis& analysis, uint8_t player, uint32_t tick) {
    SideSubject best{};
    const auto& created{analysis.created[player]};
    const auto& events{analysis.timeline->events};
    const auto& builders{analysis.builders[player]};
    if (builders.empty())
        return best;
    const uint32_t to{later(tick, turn_ticks)};
    std::vector<int64_t> started(builders.size(), 0);
    const auto first{std::lower_bound(
        created.begin(), created.end(), tick, [&](uint32_t index, uint32_t wanted) {
            return events[index].tick < wanted;
        }
    )};
    for (auto next{first}; next != created.end() && events[*next].tick < to; ++next) {
        const uint32_t life{analysis.event_lives[*next].unit};
        if (analysis.lives[life].mobile)
            continue;
        const uint32_t when{events[*next].tick};
        const Point built{ground_of(events[*next].at)};
        size_t nearest{builders.size()};
        int64_t nearest_distance{};
        for (size_t index{}; index < builders.size(); ++index) {
            const uint32_t builder{builders[index]};
            if (!framable(analysis, builder, when) || !framable(analysis, builder, tick))
                continue;
            const int64_t reach{analysis.lives[builder].build_reach};
            const int64_t apart{distance_squared(ground_point(analysis, builder, when), built)};
            if (apart > reach * reach)
                continue;
            if (nearest == builders.size() || apart < nearest_distance) {
                nearest = index;
                nearest_distance = apart;
            }
        }
        if (nearest != builders.size())
            started[nearest] += analysis.lives[life].value;
    }
    for (size_t index{}; index < builders.size(); ++index) {
        if (started[index] == 0)
            continue;
        const int64_t score{tuning::builder_base_score + started[index]};
        if (score > best.score)
            best = SideSubject{
                SubjectKey{SubjectKind::builder, player, static_cast<int32_t>(builders[index])},
                score,
            };
    }
    return best;
}

/// Returns a player's buildings started from base_recent_ticks before a
/// tick to a turn after it and alive on the tick, weighted by their value
/// times the part of their water square that is land.
///
/// @param analysis the analysis
/// @param player the player
/// @param tick the tick
/// @return the buildings, in order of birth
std::vector<WeightedPoint>
recent_buildings(const Analysis& analysis, uint8_t player, uint32_t tick) {
    std::vector<WeightedPoint> points{};
    const uint32_t from{earlier(tick, tuning::base_recent_ticks)};
    const uint32_t to{later(tick, turn_ticks)};
    for (const uint32_t life : analysis.buildings[player]) {
        const Life& record{analysis.lives[life]};
        if (record.born < from)
            continue;
        if (record.born >= to)
            break;
        if (record.died > tick)
            points.push_back(
                WeightedPoint{
                    record.created_at,
                    record.value * (percent - water_at(analysis, record.created_at)) / percent,
                }
            );
    }
    return points;
}

/// Returns a base's score: base_base_score and the value of what its player
/// starts over the next turn.
///
/// @param analysis the analysis
/// @param player the player
/// @param tick the tick
/// @return the score, or 0 without base_min_buildings recent buildings
int64_t base_score(const Analysis& analysis, uint8_t player, uint32_t tick) {
    if (recent_buildings(analysis, player, tick).size() < tuning::base_min_buildings)
        return 0;
    int64_t score{tuning::base_base_score};
    const uint32_t to{later(tick, turn_ticks)};
    for (const uint32_t life : analysis.buildings[player]) {
        const Life& record{analysis.lives[life]};
        if (record.born >= to)
            break;
        if (record.born >= tick)
            score += record.value;
    }
    return score;
}

/// Returns the framing of a player's base: its recent buildings around the
/// most valued of them on land, as many as tuning::base_gather_height
/// holds.
///
/// @param analysis the analysis
/// @param player the player
/// @param tick the tick
/// @return the framing, or none without base_min_buildings recent buildings
std::optional<Framing> frame_base(const Analysis& analysis, uint8_t player, uint32_t tick) {
    const std::vector<WeightedPoint> buildings{recent_buildings(analysis, player, tick)};
    if (buildings.size() < tuning::base_min_buildings)
        return std::nullopt;
    return frame_on_land(
        analysis,
        gather_around_weight(analysis.stage, buildings, tuning::base_gather_height),
        tuning::base_view_height,
        max_subject_view_height
    );
}

/// Returns the framing of a player's leading army, with lead room ahead of
/// it the way it moves.
///
/// @param analysis the analysis
/// @param player the player
/// @param tick the tick
/// @return the framing, or none without an army
std::optional<Framing> frame_army(const Analysis& analysis, uint8_t player, uint32_t tick) {
    ArmyGroup army{best_army(analysis, player, tick)};
    if (!army.found)
        return std::nullopt;
    const std::optional<PlannedView> plain{
        frame_points(analysis.stage, army.points, close_view_height, max_subject_view_height)
    };
    const int64_t moved{distance(Point{}, army.heading)};
    if (plain && moved > 0) {
        const int64_t lead{
            view_width(analysis.stage, plain->height) * tuning::army_lead_percent / percent
        };
        army.points.push_back(
            Point{
                static_cast<int32_t>(army.centre.x + int64_t{army.heading.x} * lead / moved),
                static_cast<int32_t>(army.centre.row + int64_t{army.heading.row} * lead / moved),
            }
        );
    }
    return frame_on_land(
        analysis, std::move(army.points), close_view_height, max_subject_view_height
    );
}

/// Tells whether a hot event lies in a cell's neighbourhood.
///
/// @param event the event
/// @param cell_x the cell's column
/// @param cell_z the cell's row
/// @param cells the neighbourhood's reach, in cells
/// @return true when both of its cell coordinates are within reach
bool near_cell(const HotEvent& event, int32_t cell_x, int32_t cell_z, int32_t cells) noexcept {
    const int32_t across{event.cell_x - cell_x};
    const int32_t down{event.cell_z - cell_z};
    return across >= -cells && across <= cells && down >= -cells && down <= cells;
}

/// Returns the hot events of a decision's look-ahead window.
///
/// @param analysis the analysis
/// @param tick the decision's tick
/// @return the events from hot_window_first_ticks to hot_window_last_ticks
///         after it, both included
std::span<const HotEvent> window_events(const Analysis& analysis, uint32_t tick) {
    const uint32_t from{later(tick, hot_window_first_ticks)};
    const uint32_t to{later(tick, hot_window_last_ticks)};
    const auto& events{analysis.hot_events};
    const auto first{std::lower_bound(
        events.begin(), events.end(), from, [](const HotEvent& event, uint32_t wanted) {
            return event.tick < wanted;
        }
    )};
    const auto last{
        std::upper_bound(first, events.end(), to, [](uint32_t wanted, const HotEvent& event) {
            return wanted < event.tick;
        })
    };
    return std::span<const HotEvent>{first, last};
}

/// Returns a hot spot's score on a tick: the scores of its neighbourhood's
/// events in the look-ahead window.
///
/// @param analysis the analysis
/// @param cell_x the cell's column
/// @param cell_z the cell's row
/// @param tick the decision's tick
/// @return the score
int64_t hot_spot_score(const Analysis& analysis, int32_t cell_x, int32_t cell_z, uint32_t tick) {
    int64_t score{};
    for (const HotEvent& event : window_events(analysis, tick))
        if (near_cell(event, cell_x, cell_z, tuning::hot_neighbourhood_cells))
            score += event.score;
    return score;
}

/// Returns the framing of a hot spot: where its damage lands in the
/// look-ahead window and the units hit, where they are as the window opens,
/// gathered around the heaviest damage within tuning::hot_view_height; then
/// the attackers near it that fit that height too; then placed to show
/// little water. Long-range fire landing there may be framed from its
/// launch too when one view can show both.
///
/// @param analysis the analysis
/// @param cell_x the cell's column
/// @param cell_z the cell's row
/// @param tick the decision's tick
/// @param show_launches frame the launches of long-range fire landing there
/// @return the framing, or none when nothing happens there
std::optional<Framing> frame_hot_spot(
    const Analysis& analysis, int32_t cell_x, int32_t cell_z, uint32_t tick, bool show_launches
) {
    const uint32_t opens{later(tick, hot_window_first_ticks)};
    std::vector<WeightedPoint> struck{};
    std::vector<WeightedPoint> attackers{};
    std::vector<uint32_t> fires{};
    for (const HotEvent& event : window_events(analysis, tick)) {
        if (!near_cell(event, cell_x, cell_z, tuning::hot_neighbourhood_cells))
            continue;
        struck.push_back(WeightedPoint{event.at, event.score});
        if (event.life != no_index && framable(analysis, event.life, opens))
            struck.push_back(WeightedPoint{ground_point(analysis, event.life, opens), event.score});
        if (event.attacker != no_index && framable(analysis, event.attacker, opens)) {
            const Point at{ground_point(analysis, event.attacker, opens)};
            HotEvent where{};
            where.cell_x = static_cast<int32_t>(floor_div(at.x, hot_cell_pixels));
            where.cell_z = static_cast<int32_t>(floor_div(at.row, hot_cell_pixels));
            if (near_cell(where, cell_x, cell_z, tuning::attacker_reach_cells))
                attackers.push_back(WeightedPoint{at, event.score});
        }
        if (event.fire != no_index)
            fires.push_back(event.fire);
    }
    if (struck.empty())
        return std::nullopt;
    const Stage& stage{analysis.stage};
    std::vector<Point> points{gather_around_weight(stage, struck, tuning::hot_view_height)};
    // The attackers, the heaviest first, where they fit.
    std::stable_sort(attackers.begin(), attackers.end(), [](const auto& a, const auto& b) {
        return a.weight > b.weight;
    });
    for (const WeightedPoint& attacker : attackers) {
        points.push_back(attacker.at);
        if (framed_height(stage, points) > tuning::hot_view_height)
            points.pop_back();
    }
    const std::optional<PlannedView> view{
        frame_points(stage, points, close_view_height, tuning::hot_view_height)
    };
    if (!view)
        return std::nullopt;
    if (show_launches && !fires.empty()) {
        const int64_t width{view_width(stage, view->height)};
        std::vector<Point> wide{points};
        for (const uint32_t fire : fires) {
            const LongRangeFire& shot{analysis.fires[fire]};
            if (distance(shot.origin, shot.impact) > width)
                wide.push_back(shot.origin);
        }
        if (wide.size() != points.size() && framed_height(stage, wide) <= stage.fit)
            return frame_on_land(analysis, std::move(wide), close_view_height, stage.fit);
    }
    return frame_on_land(analysis, std::move(points), close_view_height, tuning::hot_view_height);
}

/// Returns the framing of long-range fire landing soon: its launches and
/// impacts in the safe area of a wide view, or, when no view holds them
/// there, within tuning::wide_safe_area_percent of it.
///
/// @param analysis the analysis
/// @param tick the decision's tick
/// @return the framing, or none without enough fire
std::optional<Framing> frame_barrage(const Analysis& analysis, uint32_t tick) {
    Barrage barrage{find_barrage(analysis, tick)};
    if (!barrage.found)
        return std::nullopt;
    const Stage& stage{analysis.stage};
    const int64_t area{
        framed_height(stage, barrage.points) <= stage.fit ? int64_t{safe_area_percent}
                                                          : tuning::wide_safe_area_percent
    };
    const std::optional<PlannedView> view{
        frame_points_within(stage, barrage.points, area, max_subject_view_height, stage.fit)
    };
    if (!view)
        return std::nullopt;
    return Framing{*view, std::move(barrage.points)};
}

/// Returns the framing of one point at a height.
///
/// @param analysis the analysis
/// @param at the point
/// @param height_percent the height, in percent of close_view_height
/// @return the framing, kept on the map
Framing frame_still(const Analysis& analysis, Point at, int64_t height_percent) {
    const int64_t height{int64_t{close_view_height} * height_percent / percent};
    return frame_on_land(analysis, std::vector<Point>{at}, height, height)
        .value_or(Framing{whole_map(analysis.stage), {}});
}

/// Returns the framing of a moment: centred on it at
/// tuning::moment_height_percent of close_view_height, whatever the water;
/// where the map's edge keeps it out of the middle
/// tuning::lone_subject_percent of the view, the view comes closer,
/// tuning::closer_step_percent at a time down to close_view_height, so that
/// it shows larger.
///
/// @param analysis the analysis
/// @param at the moment's place
/// @return the framing, kept on the map
Framing frame_moment(const Analysis& analysis, Point at) {
    const Stage& stage{analysis.stage};
    const std::vector<Point> points{at};
    int64_t height{int64_t{close_view_height} * tuning::moment_height_percent / percent};
    PlannedView view{frame_points(stage, points, height, height).value_or(whole_map(stage))};
    while (!within_part(stage, view, at, tuning::lone_subject_percent) &&
           height > close_view_height) {
        height =
            std::max<int64_t>(height * tuning::closer_step_percent / percent, close_view_height);
        view = frame_points(stage, points, height, height).value_or(view);
    }
    return Framing{view, points};
}

} // namespace

std::string key_text(const Analysis& analysis, const SubjectKey& key) {
    switch (key.kind) {
    case SubjectKind::hot:
        return "hot:" + std::to_string(key.a) + "," + std::to_string(key.b);
    case SubjectKind::army:
        return "army:" + std::to_string(key.a);
    case SubjectKind::barrage:
        return "barrage";
    case SubjectKind::commander:
        return "commander:" + std::to_string(key.a);
    case SubjectKind::factory:
        return "factory:" + std::to_string(key.a) + "," + std::to_string(key.b);
    case SubjectKind::builder:
        return "builder:" + std::to_string(key.a) + "," + std::to_string(key.b);
    case SubjectKind::base:
        return "base:" + std::to_string(key.a);
    case SubjectKind::moment:
        return "moment:" + std::to_string(analysis.moments[static_cast<size_t>(key.a)].tick);
    case SubjectKind::overview:
        return "overview";
    case SubjectKind::ending:
        return "ending:" + std::to_string(analysis.deciding_tick);
    }
    return "overview";
}

ArmyGroup best_army(const Analysis& analysis, uint8_t player, uint32_t tick) {
    ArmyGroup best{};
    const auto& samples{analysis.timeline->samples};
    const auto& order{analysis.sample_order};
    const auto after{
        std::upper_bound(order.begin(), order.end(), tick, [&](uint32_t wanted, uint32_t index) {
            return wanted < samples[index].tick;
        })
    };
    if (after == order.begin())
        return best;
    const uint32_t sampled{samples[*(after - 1)].tick};
    if (tick - sampled >= 2 * sample_period_ticks)
        return best;
    const auto first{
        std::lower_bound(order.begin(), after, sampled, [&](uint32_t index, uint32_t wanted) {
            return samples[index].tick < wanted;
        })
    };
    const uint32_t ahead{later(tick, army_look_ahead_ticks)};
    const uint32_t soon{later(tick, tuning::follow_lead_ticks)};

    // The armed units of the player, not builders or commanders, drawn,
    // free and advancing on the enemy start nearest them.
    struct Member {
        Point at{};
        Point soon{};  ///< where it is follow_lead_ticks later
        Point moved{}; ///< its move over army_look_ahead_ticks
        int64_t value{};
        uint32_t life{};
    };

    std::vector<Member> members{};
    constexpr uint8_t hidden{sample_flag::cloaked | sample_flag::carried | sample_flag::unfinished};
    for (auto next{first}; next != after; ++next) {
        const UnitSample& sample{samples[*next]};
        const uint32_t life{analysis.sample_lives[*next]};
        if (sample.owner != player || life == no_index)
            continue;
        const Life& record{analysis.lives[life]};
        if (!record.mobile || !record.armed || record.builder || record.commander ||
            (sample.flags & sample_flag::visible) == 0 || (sample.flags & hidden) != 0 ||
            record.died <= ahead)
            continue;
        const Point at{ground_of(sample.at)};
        bool has_goal{};
        Point goal{};
        int64_t nearest{};
        for (const uint8_t other : analysis.combatants) {
            if (!enemies(analysis, player, other) || !analysis.has_start[other])
                continue;
            const int64_t apart{distance_squared(at, analysis.start[other])};
            if (!has_goal || apart < nearest) {
                has_goal = true;
                goal = analysis.start[other];
                nearest = apart;
            }
        }
        const int64_t apart{distance(at, goal)};
        if (!has_goal || apart == 0)
            continue;
        const Point later_at{ground_point(analysis, life, ahead)};
        const Point moved{later_at.x - at.x, later_at.row - at.row};
        const int64_t toward{
            (int64_t{moved.x} * (int64_t{goal.x} - at.x) +
             int64_t{moved.row} * (int64_t{goal.row} - at.row)) /
            apart
        };
        if (toward < army_min_advance_pixels)
            continue;
        members.push_back(
            Member{at, ground_point(analysis, life, soon), moved, record.value, life}
        );
    }
    if (members.size() < tuning::army_min_units)
        return best;
    std::sort(members.begin(), members.end(), [](const Member& a, const Member& b) {
        if (a.at.x != b.at.x)
            return a.at.x < b.at.x;
        if (a.at.row != b.at.row)
            return a.at.row < b.at.row;
        return a.life < b.life;
    });
    // Single-link groups: units within group_link_pixels of each other join.
    std::vector<size_t> parent(members.size());
    std::iota(parent.begin(), parent.end(), size_t{0});
    const auto root{[&](size_t member) {
        while (parent[member] != member) {
            parent[member] = parent[parent[member]];
            member = parent[member];
        }
        return member;
    }};
    constexpr int64_t link{int64_t{group_link_pixels} * group_link_pixels};
    for (size_t a{}; a < members.size(); ++a)
        for (size_t b{a + 1u};
             b < members.size() && int64_t{members[b].at.x} - members[a].at.x <= group_link_pixels;
             ++b)
            if (distance_squared(members[a].at, members[b].at) <= link) {
                const size_t first_root{root(a)};
                const size_t second_root{root(b)};
                if (first_root != second_root)
                    parent[std::max(first_root, second_root)] = std::min(first_root, second_root);
            }
    std::vector<std::vector<size_t>> groups{};
    std::vector<size_t> group_of(members.size(), members.size());
    for (size_t member{}; member < members.size(); ++member) {
        const size_t top{root(member)};
        if (group_of[top] == members.size()) {
            group_of[top] = groups.size();
            groups.emplace_back();
        }
        groups[group_of[top]].push_back(member);
    }
    for (const auto& group : groups) {
        if (group.size() < tuning::army_min_units)
            continue;
        int64_t sum_x{};
        int64_t sum_row{};
        int64_t moved_x{};
        int64_t moved_row{};
        int64_t value{};
        for (const size_t member : group) {
            sum_x += members[member].at.x;
            sum_row += members[member].at.row;
            moved_x += members[member].moved.x;
            moved_row += members[member].moved.row;
            value += members[member].value;
        }
        const int64_t count{static_cast<int64_t>(group.size())};
        const Point centre{
            static_cast<int32_t>(floor_div(sum_x, count)),
            static_cast<int32_t>(floor_div(sum_row, count)),
        };
        // Progress toward the enemy start nearest the group, the lower
        // player on a tie.
        bool has_goal{};
        Point goal{};
        int64_t nearest{};
        for (const uint8_t other : analysis.combatants) {
            if (!enemies(analysis, player, other) || !analysis.has_start[other])
                continue;
            const int64_t apart{distance_squared(centre, analysis.start[other])};
            if (!has_goal || apart < nearest) {
                has_goal = true;
                goal = analysis.start[other];
                nearest = apart;
            }
        }
        int64_t progress{};
        if (has_goal && analysis.has_start[player]) {
            const int64_t whole{distance(analysis.start[player], goal)};
            if (whole > 0) {
                const int64_t left{distance(centre, goal)};
                progress = std::clamp<int64_t>(
                    tuning::progress_full - tuning::progress_full * left / whole,
                    0,
                    tuning::progress_full
                );
            }
        }
        const int64_t score{
            value * (tuning::progress_full + progress) / (2 * tuning::progress_full)
        };
        if (best.found && score <= best.score)
            continue;
        best.found = true;
        best.score = score;
        best.centre = centre;
        best.heading = Point{
            static_cast<int32_t>(floor_div(moved_x, count)),
            static_cast<int32_t>(floor_div(moved_row, count)),
        };
        best.points.clear();
        for (const size_t member : group) {
            best.points.push_back(members[member].at);
            best.points.push_back(members[member].soon);
        }
    }
    return best;
}

Barrage find_barrage(const Analysis& analysis, uint32_t tick) {
    Barrage barrage{};
    const uint32_t to{later(tick, tuning::barrage_window_ticks)};
    size_t count{};
    for (const LongRangeFire& fire : analysis.fires) {
        if (fire.detonation_tick < tick || fire.detonation_tick > to)
            continue;
        ++count;
        barrage.score += fire.score;
        barrage.points.push_back(fire.origin);
        barrage.points.push_back(fire.impact);
    }
    barrage.found = count >= tuning::barrage_min_fires;
    if (!barrage.found) {
        barrage.score = 0;
        barrage.points.clear();
    }
    return barrage;
}

std::vector<SideSubject> side_subjects(const Analysis& analysis, uint8_t player, uint32_t tick) {
    std::vector<SideSubject> subjects{};
    if (const std::optional<int64_t> score{commander_score(analysis, player, tick)})
        subjects.push_back(SideSubject{SubjectKey{SubjectKind::commander, player, 0}, *score});
    if (const SideSubject factory{best_factory(analysis, player, tick)}; factory.score > 0)
        subjects.push_back(factory);
    if (const SideSubject builder{best_builder(analysis, player, tick)}; builder.score > 0)
        subjects.push_back(builder);
    if (const int64_t base{base_score(analysis, player, tick)}; base > 0)
        subjects.push_back(SideSubject{SubjectKey{SubjectKind::base, player, 0}, base});
    return subjects;
}

int64_t damage_between(const Analysis& analysis, uint32_t from, uint32_t to) {
    const auto& events{analysis.hot_events};
    const auto first{std::lower_bound(
        events.begin(), events.end(), from, [](const HotEvent& event, uint32_t wanted) {
            return event.tick < wanted;
        }
    )};
    int64_t score{};
    for (auto next{first}; next != events.end() && next->tick <= to; ++next)
        score += next->score;
    return score;
}

bool neighbourhoods_overlap(
    int32_t first_x, int32_t first_z, int32_t second_x, int32_t second_z
) noexcept {
    const int32_t reach{2 * tuning::hot_neighbourhood_cells};
    const int32_t across{first_x - second_x};
    const int32_t down{first_z - second_z};
    return across >= -reach && across <= reach && down >= -reach && down <= reach;
}

HotSpots find_hot_spots(
    const Analysis& analysis,
    uint32_t tick,
    const std::optional<std::array<int32_t, 2>>& avoid,
    std::vector<int64_t>& grid
) {
    HotSpots spots{};
    const std::span<const HotEvent> events{window_events(analysis, tick)};
    if (events.empty())
        return spots;
    const int32_t columns{analysis.cells_x};
    const int32_t rows{analysis.cells_z};
    grid.assign(static_cast<size_t>(columns) * static_cast<size_t>(rows), 0);
    for (const HotEvent& event : events)
        grid
            [static_cast<size_t>(event.cell_z) * static_cast<size_t>(columns) +
             static_cast<size_t>(event.cell_x)] += event.score;
    const int32_t reach{tuning::hot_neighbourhood_cells};
    const auto consider{[](HotSpot& spot, int32_t column, int32_t row, int64_t sum) {
        if (sum <= spot.score)
            return;
        spot.found = true;
        spot.cell_x = column;
        spot.cell_z = row;
        spot.score = sum;
    }};
    for (int32_t row{}; row < rows; ++row)
        for (int32_t column{}; column < columns; ++column) {
            int64_t sum{};
            for (int32_t z{std::max(0, row - reach)}; z <= std::min(rows - 1, row + reach); ++z)
                for (int32_t x{std::max(0, column - reach)};
                     x <= std::min(columns - 1, column + reach);
                     ++x)
                    sum += grid
                        [static_cast<size_t>(z) * static_cast<size_t>(columns) +
                         static_cast<size_t>(x)];
            consider(spots.best, column, row, sum);
            if (!avoid || !neighbourhoods_overlap(column, row, (*avoid)[0], (*avoid)[1]))
                consider(spots.away, column, row, sum);
        }
    for (HotSpot* spot : {&spots.best, &spots.away}) {
        if (!spot->found)
            continue;
        for (const HotEvent& event : events)
            if (near_cell(event, spot->cell_x, spot->cell_z, reach)) {
                spot->first_tick = event.tick;
                break;
            }
    }
    return spots;
}

uint32_t commander_life(const Analysis& analysis, uint8_t player, uint32_t tick) noexcept {
    if (player >= player_slots)
        return no_index;
    for (const uint32_t life : analysis.commanders[player])
        if (alive(analysis.lives[life], tick))
            return life;
    return no_index;
}

int64_t subject_score(const Analysis& analysis, const SubjectKey& key, uint32_t tick) {
    const auto player{static_cast<uint8_t>(key.a)};
    switch (key.kind) {
    case SubjectKind::hot:
        return hot_spot_score(analysis, key.a, key.b, tick);
    case SubjectKind::army: {
        const ArmyGroup army{best_army(analysis, player, tick)};
        return army.found ? army.score : 0;
    }
    case SubjectKind::barrage:
        return find_barrage(analysis, tick).score;
    case SubjectKind::commander:
        return commander_score(analysis, player, tick).value_or(0);
    case SubjectKind::factory: {
        const auto factory{static_cast<uint32_t>(key.b)};
        if (factory >= analysis.lives.size() || !framable(analysis, factory, tick))
            return 0;
        const uint32_t index{factory_production(analysis, factory, tick)};
        return index != no_index && analysis.productions[index].tick <=
                                        later(tick, tuning::factory_lead_max_ticks)
                   ? production_score(analysis, analysis.productions[index])
                   : 0;
    }
    case SubjectKind::builder: {
        const auto builder{static_cast<uint32_t>(key.b)};
        if (builder >= analysis.lives.size() || !framable(analysis, builder, tick))
            return 0;
        int64_t score{tuning::builder_base_score};
        visit_builds(
            analysis,
            player,
            builder,
            ground_point(analysis, builder, tick),
            tick,
            later(tick, turn_ticks),
            [&](uint32_t life, Point) { score += analysis.lives[life].value; }
        );
        return score;
    }
    case SubjectKind::base:
        return base_score(analysis, player, tick);
    case SubjectKind::moment: {
        const Moment& moment{analysis.moments[static_cast<size_t>(key.a)]};
        return tick <= later(moment.tick, tuning::moment_after_ticks) ? moment.score : 0;
    }
    case SubjectKind::overview:
        return tuning::overview_score;
    case SubjectKind::ending:
        return 0;
    }
    return 0;
}

std::optional<Framing> frame_subject(
    const Analysis& analysis,
    const SubjectKey& key,
    uint32_t tick,
    bool require_framable,
    int64_t min_height,
    bool show_launches
) {
    const auto player{static_cast<uint8_t>(key.a)};
    switch (key.kind) {
    case SubjectKind::hot:
        return frame_hot_spot(analysis, key.a, key.b, tick, show_launches);
    case SubjectKind::army:
        return frame_army(analysis, player, tick);
    case SubjectKind::barrage:
        return frame_barrage(analysis, tick);
    case SubjectKind::commander:
        return frame_commander(
            analysis,
            player,
            tick,
            require_framable,
            min_height > 0 ? min_height : int64_t{close_view_height}
        );
    case SubjectKind::factory: {
        const auto factory{static_cast<uint32_t>(key.b)};
        if (factory >= analysis.lives.size())
            return std::nullopt;
        return frame_factory(analysis, factory, tick);
    }
    case SubjectKind::builder: {
        const auto builder{static_cast<uint32_t>(key.b)};
        if (builder >= analysis.lives.size() || !framable(analysis, builder, tick))
            return std::nullopt;
        return frame_builds(analysis, player, builder, tick, tuning::builder_view_height);
    }
    case SubjectKind::base:
        return frame_base(analysis, player, tick);
    case SubjectKind::moment:
        return frame_moment(analysis, analysis.moments[static_cast<size_t>(key.a)].at);
    case SubjectKind::overview:
        return Framing{whole_map(analysis.stage), {}};
    case SubjectKind::ending:
        return frame_still(analysis, analysis.deciding_at, tuning::ending_hold_height_percent);
    }
    return std::nullopt;
}

} // namespace oa::media::director::planning
