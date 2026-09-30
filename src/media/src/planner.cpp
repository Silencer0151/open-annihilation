// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Places a script's shots. An opening shot settles from the whole map onto
// the first commander; then a decision every decision_period_ticks either
// cuts to a moment, switches to action (a hot spot, an advancing army or
// long-range fire) that scores enough, takes the next quiet turn when the
// shot has run its course, or follows the current subject. Quiet turns go
// to each side in turn, turn_side_shots at a time, last longer when their
// subject starts something, and frame a commander, a factory, a
// construction unit or a base with a slow drift or push, or drift wide from
// one side to the other. In a battle, action is held rather than giving way
// to a quiet turn, and idle commanders take none. Every subject's score is
// lowered by the views shown in the last memory_ticks that overlap its own.
// Follow shots move the camera only once the subject nears the edge of the
// view, and then a bounded step. The deciding commander death is held, then
// the camera pulls back.

#include "oa/media/director/planner.hpp"

#include "oa/media/director/clock.hpp"
#include "planner_internal.hpp"
#include "planner_tuning.hpp"

#include <algorithm>
#include <span>
#include <utility>

namespace oa::media::director {
namespace {

using namespace planning;
namespace oascript = oa::formats::oascript;

/// Percent: the whole of something.
constexpr int64_t percent{100};
/// Decimal places of milliseconds.
constexpr uint32_t millisecond_places{3};
/// The decimal base.
constexpr int64_t decimal_base{10};
/// A spring's step, 2 pi frequency / framerate, may be at most 1/2: its
/// frequency at most framerate / (4 pi). Pi is taken as 355/113, a little
/// above it, so the check errs on the safe side.
constexpr int64_t spring_step_bound{4};
constexpr int64_t pi_above_numerator{355};
constexpr int64_t pi_above_denominator{113};

/// Returns a count of milliseconds as seconds, without trailing zeros.
///
/// @param milliseconds the count
/// @return the seconds, such as {5, 1} for 500
constexpr oascript::Decimal seconds_of_milliseconds(uint32_t milliseconds) noexcept {
    oascript::Decimal seconds{milliseconds, millisecond_places};
    while (seconds.places > 0 && seconds.mantissa % decimal_base == 0) {
        seconds.mantissa /= decimal_base;
        --seconds.places;
    }
    return seconds;
}

/// A transition and its length.
struct PlannedTransition {
    oascript::TransitionKind kind{oascript::TransitionKind::dissolve};
    uint32_t milliseconds{};
};

/// The far cut's dissolve.
constexpr PlannedTransition far_cut{oascript::TransitionKind::dissolve, far_cut_dissolve_ms};
/// The wipe to the other side of the map.
constexpr PlannedTransition side_wipe{oascript::TransitionKind::wipe, side_wipe_ms};
/// A big moment's checkerboard.
constexpr PlannedTransition moment_checkerboard{
    oascript::TransitionKind::checkerboard, moment_checkerboard_ms
};
/// The fade through black of the first action and of the ending.
constexpr PlannedTransition phase_fade{oascript::TransitionKind::fade, phase_fade_ms};

/// One planned shot.
struct PlannedShot {
    uint32_t tick{};
    std::optional<PlannedView> start{}; ///< none continues from the shot before
    PlannedView end{};
    std::optional<oascript::Decimal> spring{}; ///< its frequency; none moves linearly
    std::optional<PlannedTransition> transition{};
};

/// How the camera moves while it holds a subject.
enum class Style : uint8_t {
    follow, ///< follow shots keep the subject framed
    drift,  ///< one slow move over the turn, followed only once the subject leaves it
    still,  ///< held where it is
};

/// A subject shown, which the memory holds against overlapping subjects.
struct Shown {
    SubjectKey key{};
    PlannedView first{}; ///< the view its shot started on
    PlannedView last{};  ///< the view its last shot headed to
    uint32_t until{};    ///< the tick the camera left it; never while it holds it
};

/// A subject the camera may switch to.
struct Candidate {
    SubjectKey key{};
    int64_t score{};     ///< its score after the memory's penalty
    int64_t raw{};       ///< its own score
    uint32_t cut_tick{}; ///< when a switch to it cuts
    PlannedView view{};  ///< its view on the cut tick
};

/// Tells whether one candidate goes before another: the higher score, then
/// the earlier kind, then the lower subject numbers.
///
/// @param a a candidate
/// @param b another
/// @return true when a goes first
bool goes_before(const Candidate& a, const Candidate& b) noexcept {
    if (a.score != b.score)
        return a.score > b.score;
    if (a.key.kind != b.key.kind)
        return a.key.kind < b.key.kind;
    if (a.key.a != b.key.a)
        return a.key.a < b.key.a;
    return a.key.b < b.key.b;
}

/// Where a switch starts the camera and how it moves on.
struct Move {
    PlannedView start{};                       ///< the view at the switch
    PlannedView end{};                         ///< the view the first shot heads to
    std::optional<oascript::Decimal> spring{}; ///< its frequency; none moves linearly
    Style style{Style::follow};
};

/// Returns a view with its height scaled, kept on the map.
///
/// @param stage the stage
/// @param view the view
/// @param height_percent the new height, in percent of the old
/// @return the view
PlannedView scaled(const Stage& stage, PlannedView view, int64_t height_percent) {
    view.height = view.height * height_percent / percent;
    return clamp_planned(stage, view);
}

/// Returns a view moved by an offset as far as its points stay in its safe
/// area and it stays on the map.
///
/// @param stage the stage
/// @param framing the view and its points
/// @param dx2 the move across, in half pixels
/// @param dz2 the move down, in half pixels
/// @return the moved view
PlannedView shifted(const Stage& stage, const Framing& framing, int64_t dx2, int64_t dz2) {
    PlannedView view{framing.view};
    view.x2 += dx2;
    view.z2 += dz2;
    view = clamp_planned(stage, view);
    const bool held{std::all_of(framing.points.begin(), framing.points.end(), [&](Point point) {
        return in_safe_area(stage, view, point);
    })};
    if (held)
        return view;
    // Half the move, then none.
    if (dx2 / 2 != 0 || dz2 / 2 != 0)
        return shifted(stage, framing, dx2 / 2, dz2 / 2);
    return framing.view;
}

/// Returns a view's middle, rounded down to a whole pixel.
///
/// @param view the view
/// @return its centre
Point middle_of(const PlannedView& view) noexcept {
    return Point{
        static_cast<int32_t>(floor_div(view.x2, 2)), static_cast<int32_t>(floor_div(view.z2, 2))
    };
}

/// Tells whether a subject is a place rather than units: a base, long-range
/// fire framed wide or the whole map. The camera holds a place where it
/// framed it, or drifts across it, and never follows it.
///
/// @param kind the subject's kind
/// @return true for a place
constexpr bool place_kind(SubjectKind kind) noexcept {
    return kind == SubjectKind::base || kind == SubjectKind::barrage ||
           kind == SubjectKind::overview;
}

/// Returns the middle of points' bounding box, rounded down.
///
/// @param points the points; at least one
/// @return the middle
Point middle_of_points(std::span<const Point> points) noexcept {
    int64_t left{points.front().x};
    int64_t right{left};
    int64_t top{points.front().row};
    int64_t bottom{top};
    for (const Point& point : points) {
        left = std::min<int64_t>(left, point.x);
        right = std::max<int64_t>(right, point.x);
        top = std::min<int64_t>(top, point.row);
        bottom = std::max<int64_t>(bottom, point.row);
    }
    return Point{
        static_cast<int32_t>(floor_div(left + right, 2)),
        static_cast<int32_t>(floor_div(top + bottom, 2)),
    };
}

/// Places the shots of one plan.
class ShotPlanner {
  public:

    /// Starts a plan over an analysis.
    ///
    /// @param analysis the analysis; must outlive the planner
    explicit ShotPlanner(const Analysis& analysis) : analysis_{analysis} {}

    /// Places every shot.
    void plan();

    /// Returns the shots placed.
    ///
    /// @return the shots, in tick order
    [[nodiscard]] const std::vector<PlannedShot>& shots() const noexcept { return shots_; }

    /// Returns the notes written.
    ///
    /// @return one line per switch, in tick order
    [[nodiscard]] std::vector<std::string> take_notes() { return std::move(notes_); }

    /// Returns the tick the video ends before.
    ///
    /// @return director.endTick
    [[nodiscard]] uint32_t end_tick() const noexcept { return end_tick_; }

  private:

    /// Adds a shot.
    ///
    /// @param shot the shot, after every shot before
    void add(const PlannedShot& shot);

    /// Adds a note of a switch.
    ///
    /// @param tick the switch's tick
    /// @param key the new subject
    /// @param score its score
    void note(uint32_t tick, const SubjectKey& key, int64_t score);

    /// Makes a subject the current one from a tick.
    ///
    /// @param key the subject
    /// @param tick the tick its shot starts on
    /// @param first the view its shot starts on
    /// @param view the view its shot ends on
    /// @param score its score, for the note
    void take(
        const SubjectKey& key,
        uint32_t tick,
        const PlannedView& first,
        const PlannedView& view,
        int64_t score
    );

    /// Returns what is left of a subject's score after the memory's
    /// penalty: the views left in the last memory_ticks lower it by how much
    /// they overlap its view, the more the more recently they were left.
    ///
    /// @param key the subject
    /// @param view its view
    /// @param tick the decision's tick
    /// @return 0 to 100, in percent
    [[nodiscard]] int64_t
    freshness(const SubjectKey& key, const PlannedView& view, uint32_t tick) const noexcept;

    /// Returns a candidate for a subject: its framing on the cut tick and its
    /// score after the memory's penalty, of which action bears
    /// tuning::action_memory_percent; a hot spot whose view shows more than
    /// tuning::action_water_free_percent water scores less too, and nothing
    /// from tuning::action_water_none_percent on.
    ///
    /// @param key the subject
    /// @param raw its own score
    /// @param cut_tick when a switch to it cuts
    /// @param tick the decision's tick
    /// @param min_height the smallest height of its view; 0 for its own
    /// @return the candidate, or none when it cannot be framed
    [[nodiscard]] std::optional<Candidate> candidate(
        const SubjectKey& key, int64_t raw, uint32_t cut_tick, uint32_t tick, int64_t min_height
    ) const;

    /// Cuts to a moment due in a decision's period, when one may interrupt.
    ///
    /// @param tick the decision's tick
    /// @return true when a moment was cut to
    bool cut_to_moment(uint32_t tick);

    /// Switches to action when some scores enough.
    ///
    /// @param tick the decision's tick
    /// @return true when the camera switched
    bool decide_action(uint32_t tick);

    /// Takes the next quiet turn when the current shot has run its course,
    /// unless a battle goes on and the camera holds action or a moment for
    /// less than tuning::battle_hold_max_ticks.
    ///
    /// @param tick the decision's tick
    /// @return true when the camera switched
    bool decide_quiet(uint32_t tick);

    /// Tells whether a battle goes on: damage of at least
    /// tuning::battle_min_score landed in the last tuning::battle_recent_ticks
    /// or lands in the hot spots' window, after the first action.
    ///
    /// @param tick the decision's tick
    /// @return true in a battle
    [[nodiscard]] bool battle_on(uint32_t tick) const;

    /// Returns how long a quiet turn lasts: tuning::idle_turn_ticks for an
    /// idle commander, a base where nothing is started or the wide drift,
    /// tuning::busy_turn_ticks for the others, and then a variation that
    /// changes from turn to turn.
    ///
    /// @param key the subject
    /// @param score its score
    /// @return the ticks
    [[nodiscard]] uint32_t turn_length(const SubjectKey& key, int64_t score) const noexcept;

    /// Tells whether a shot switched to on a tick has its shortest length
    /// before the decisions stop: the ending or the end of the video.
    ///
    /// @param tick the switch's tick
    /// @return true when min_shot_ticks fit before the stop
    [[nodiscard]] bool room_for_shot(uint32_t tick) const noexcept;

    /// Takes a quiet turn: the next subject of the side whose turn it is, or
    /// the wide drift to the next side.
    ///
    /// @param tick the decision's tick
    /// @return true when the camera switched
    bool take_turn(uint32_t tick);

    /// Returns the smallest height of a subject's view on its turn: a
    /// commander's cycles through tuning::commander_heights.
    ///
    /// @param key the subject
    /// @return the height; 0 for the subject's own
    [[nodiscard]] int64_t turn_height(const SubjectKey& key) const noexcept;

    /// Returns how the camera takes a quiet subject on its turn: a push in,
    /// a drift or a pull back on a commander, a drift across a base, a
    /// spring push onto a factory, following a construction unit. A drift
    /// goes the way of tuning::drift_directions whose views show the least
    /// water.
    ///
    /// @param key the subject
    /// @param tick the turn's tick
    /// @param length the turn's ticks
    /// @return the move, or none when the subject cannot be framed
    [[nodiscard]] std::optional<Move>
    turn_move(const SubjectKey& key, uint32_t tick, uint32_t length) const;

    /// Returns the wide drift from the current view's side to another side.
    ///
    /// @param side the side's player
    /// @param tick the turn's tick
    /// @return the move
    [[nodiscard]] Move crossing_move(uint8_t side, uint32_t tick) const;

    /// Returns how the camera follows a subject from a switch: from its view
    /// on the switch a step toward its view a lead later, with a spring.
    ///
    /// @param key the subject
    /// @param tick the switch's tick
    /// @param show_launches a hot spot is framed from the launches of the
    ///        long-range fire landing there too, when one view shows both
    /// @return the move, or none when the subject cannot be framed
    [[nodiscard]] std::optional<Move>
    follow_move(const SubjectKey& key, uint32_t tick, bool show_launches = false) const;

    /// Returns where a follow shot takes the camera from a view toward a
    /// subject's framing: nowhere while every point of the subject lies in
    /// the safe area, their middle in the middle
    /// tuning::follow_deadband_percent and the view is no more than
    /// tuning::follow_zoom_in_percent of the height the framing needs;
    /// otherwise a step of at most tuning::follow_step_percent of the view
    /// across and down and tuning::follow_zoom_step_percent of its height
    /// toward it. For damage scattered over a fight only the points' middle
    /// counts, which may lie anywhere in the safe area, and the height they
    /// need, up to a step above the view's.
    ///
    /// @param from the view the camera holds
    /// @param aim the subject's framing
    /// @param scattered the points are where damage lands
    /// @return the view to head to; `from` for none
    [[nodiscard]] PlannedView
    reframe(const PlannedView& from, const Framing& aim, bool scattered) const;

    /// Returns the transition of a cut from the current view.
    ///
    /// @param start the view cut to
    /// @param tick the cut's tick
    /// @return none for a hard cut
    [[nodiscard]] std::optional<PlannedTransition>
    cut_transition(const PlannedView& start, uint32_t tick) const;

    /// Switches to a subject: a pan when its view is near the current one,
    /// else a cut.
    ///
    /// @param key the subject
    /// @param tick the tick the switch happens on
    /// @param score its score, for the note
    /// @param move how the camera takes it
    /// @param forced a transition that makes the switch a cut; none chooses
    void switch_to(
        const SubjectKey& key,
        uint32_t tick,
        int64_t score,
        const Move& move,
        std::optional<PlannedTransition> forced = std::nullopt
    );

    /// Returns the view the current drift passes on a tick: x and z evenly,
    /// and one over the height.
    ///
    /// @param tick the tick, in the drift
    /// @return the view
    [[nodiscard]] PlannedView drift_view(uint32_t tick) const noexcept;

    /// Tells whether the current drift still holds its subject on a tick.
    ///
    /// @param tick the decision's tick
    /// @return true while the drift runs and, unless the subject is a
    ///         place, every point of the subject lies in the middle
    ///         tuning::drift_hold_percent of the view the drift passes then
    [[nodiscard]] bool drift_holds(uint32_t tick) const;

    /// Follows the current subject: a spring a step toward where it will
    /// be, when it nears the edge of the view. A drift that no longer holds
    /// its subject stops where it is; a drift across a place runs its
    /// course, and a place is never followed. A hot spot whose damage moves
    /// out of the view its shot settled on is spent: the camera holds, and
    /// a new decision may cut to where the damage went.
    ///
    /// @param tick the decision's tick
    void follow(uint32_t tick);

    /// Places the ending: the deciding commander death held, then a pull back.
    void end_on_deciding_death();

    const Analysis& analysis_;
    std::vector<PlannedShot> shots_{};
    std::vector<std::string> notes_{};
    std::vector<uint32_t> note_ticks_{};
    std::vector<int64_t> grid_{}; ///< scratch space for the hot spots
    std::vector<Shown> memory_{};
    SubjectKey current_{};
    SubjectKey taken_{};       ///< the subject last switched to, before any drift
    uint32_t subject_start_{}; ///< the tick the current subject was taken on
    uint32_t last_cut_{};      ///< the tick of the last switch
    uint32_t last_shot_{};     ///< the tick of the last shot
    uint32_t stop_{};          ///< decisions end before this tick
    uint32_t end_tick_{};
    uint32_t last_wipe_{never};     ///< the tick of the last wipe
    uint32_t last_barrage_{never};  ///< the tick long-range fire was last framed wide
    uint32_t last_crossing_{never}; ///< the tick of the last wide drift between sides
    PlannedView view_{};            ///< the view the camera is heading to
    PlannedView anchor_{};          ///< the view the current subject's first shot heads to
    Style style_{Style::follow};
    PlannedView drift_from_{};         ///< the current drift's start
    uint32_t drift_start_{};           ///< the tick it starts on
    uint32_t turn_length_{turn_ticks}; ///< the ticks of the current quiet turn
    bool spent_{};                     ///< the current hot spot's damage has moved out of anchor_
    uint8_t side_{no_player};          ///< the player whose side the quiet turns are on
    uint32_t side_turns_{};            ///< the turns taken on that side in a row
    uint32_t commander_turns_{};
    uint32_t turns_{};   ///< every quiet turn taken
    int64_t height_{};   ///< the smallest height of the current subject's view; 0 for its own
    bool opening_{};     ///< the current subject is the opening's
    bool action_seen_{}; ///< the camera has switched to action once
};

void ShotPlanner::add(const PlannedShot& shot) {
    // A drift cut short ends where it has taken the camera, at its own pace:
    // a linear shot reaches its end as the next shot starts.
    if (style_ == Style::drift && !shots_.empty() && shots_.back().tick == drift_start_ &&
        !shots_.back().spring && shot.tick > drift_start_ &&
        shot.tick - drift_start_ < turn_length_)
        shots_.back().end = drift_view(shot.tick);
    shots_.push_back(shot);
    last_shot_ = shot.tick;
    view_ = shot.end;
}

void ShotPlanner::note(uint32_t tick, const SubjectKey& key, int64_t score) {
    notes_.push_back(
        "tick " + std::to_string(tick) + ": " + key_text(analysis_, key) + " score " +
        std::to_string(score)
    );
    note_ticks_.push_back(tick);
}

void ShotPlanner::take(
    const SubjectKey& key,
    uint32_t tick,
    const PlannedView& first,
    const PlannedView& view,
    int64_t score
) {
    if (!memory_.empty())
        memory_.back().until = tick;
    memory_.push_back(Shown{key, first, view, never});
    current_ = key;
    taken_ = key;
    subject_start_ = tick;
    last_cut_ = tick;
    view_ = view;
    anchor_ = view;
    spent_ = false;
    opening_ = false;
    style_ = Style::follow;
    height_ = 0;
    note(tick, key, score);
}

int64_t ShotPlanner::freshness(
    const SubjectKey& key, const PlannedView& view, uint32_t tick
) const noexcept {
    const Stage& stage{analysis_.stage};
    int64_t penalty{};
    for (auto shown{memory_.rbegin()}; shown != memory_.rend(); ++shown) {
        // The current subject's view does not count: a subject near it is
        // where the camera is going, not where it has been.
        if (shown->until == never)
            continue;
        const uint32_t age{tick - std::min(shown->until, tick)};
        // Each view shown was left no later than the one after it.
        if (age >= memory_ticks)
            break;
        int64_t similar{std::max(
            overlap_percent(stage, view, shown->first), overlap_percent(stage, view, shown->last)
        )};
        if (shown->key == key)
            similar = std::max(similar, tuning::same_subject_similarity_percent);
        penalty = std::max(penalty, similar * (memory_ticks - age) / memory_ticks);
    }
    return percent - penalty;
}

std::optional<Candidate> ShotPlanner::candidate(
    const SubjectKey& key, int64_t raw, uint32_t cut_tick, uint32_t tick, int64_t min_height
) const {
    const std::optional<Framing> framing{frame_subject(analysis_, key, cut_tick, true, min_height)};
    if (!framing)
        return std::nullopt;
    int64_t kept{freshness(key, framing->view, tick)};
    if (action_kind(key.kind))
        kept = percent - (percent - kept) * tuning::action_memory_percent / percent;
    int64_t score{raw * kept / percent};
    // A fight at sea scores less, and soon nothing.
    if (key.kind == SubjectKind::hot) {
        constexpr int64_t free{tuning::action_water_free_percent};
        constexpr int64_t none{tuning::action_water_none_percent};
        const int64_t water{std::clamp(water_percent(analysis_, framing->view), free, none)};
        score = score * (none - water) / (none - free);
    }
    return Candidate{key, score, raw, cut_tick, framing->view};
}

bool ShotPlanner::room_for_shot(uint32_t tick) const noexcept {
    return uint64_t{tick} + min_shot_ticks <= stop_;
}

void ShotPlanner::plan() {
    const uint32_t first{analysis_.first_tick};
    const Stage& stage{analysis_.stage};
    const PlannedView whole{whole_map(stage)};
    end_tick_ = analysis_.end_tick;
    const auto opener{
        std::find_if(analysis_.combatants.begin(), analysis_.combatants.end(), [&](uint8_t player) {
            const auto& commanders{analysis_.commanders[player]};
            return !commanders.empty() && analysis_.lives[commanders.front()].born < end_tick_;
        })
    };
    if (opener == analysis_.combatants.end()) {
        add(PlannedShot{first, whole, whole});
        note(first, SubjectKey{}, 0);
        return;
    }
    const uint32_t deciding{analysis_.deciding_tick};
    stop_ = end_tick_;
    if (deciding != never) {
        stop_ = earlier(deciding, tuning::ending_lead_ticks);
        if (stop_ <= first) {
            end_on_deciding_death();
            return;
        }
    }
    const uint8_t player{*opener};
    const uint32_t born{analysis_.lives[analysis_.commanders[player].front()].born};
    const uint32_t settled{std::max(later(first, tuning::opening_settle_ticks), born)};
    const SubjectKey key{SubjectKind::commander, player, 0};
    const std::optional<Framing> framing{frame_subject(analysis_, key, settled, false)};
    const PlannedView target{framing ? framing->view : whole};
    add(PlannedShot{first, whole, target, tuning::opening_spring});
    take(key, first, whole, target, subject_score(analysis_, key, first));
    opening_ = true;
    side_ = player;
    side_turns_ = 1;
    commander_turns_ = 1;
    for (uint32_t tick{later(first, decision_period_ticks)}; tick < stop_;) {
        if (!cut_to_moment(tick) && !decide_action(tick) && !decide_quiet(tick))
            follow(tick);
        const uint32_t next{later(tick, decision_period_ticks)};
        if (next <= tick)
            break;
        tick = next;
    }
    if (deciding != never)
        end_on_deciding_death();
}

bool ShotPlanner::cut_to_moment(uint32_t tick) {
    const auto& moments{analysis_.moments};
    const uint32_t period_end{later(tick, decision_period_ticks)};
    const uint32_t horizon{later(period_end, tuning::moment_lead_ticks)};
    auto next{std::lower_bound(
        moments.begin(), moments.end(), tick, [](const Moment& moment, uint32_t wanted) {
            return moment.tick < wanted;
        }
    )};
    std::optional<size_t> chosen{};
    uint32_t chosen_due{};
    for (; next != moments.end() && next->tick < horizon; ++next) {
        const Moment& moment{*next};
        const uint32_t due{std::max(
            earlier(moment.tick, tuning::moment_lead_ticks), later(last_cut_, min_interrupt_ticks)
        )};
        if (due < tick || due >= period_end || due > moment.tick || due >= stop_ ||
            due <= last_shot_)
            continue;
        if (in_safe_area(analysis_.stage, view_, moment.at))
            continue;
        // A moment holds the camera for a shot's length, unless a commander
        // dies or the fire launched lands; another moment of the same tick
        // is the same instant.
        if (current_.kind == SubjectKind::moment) {
            const Moment& held{moments[static_cast<size_t>(current_.a)]};
            const bool landing{
                held.kind == MomentKind::launch && moment.fire != no_index &&
                moment.fire == held.fire
            };
            if (held.tick == moment.tick ||
                (due - subject_start_ < min_shot_ticks &&
                 moment.kind != MomentKind::commander_death && !landing))
                continue;
        }
        if (chosen) {
            const Moment& best{moments[*chosen]};
            const bool better{
                moment.kind != best.kind ? moment.kind > best.kind : moment.score > best.score
            };
            if (!better)
                continue;
        }
        chosen = static_cast<size_t>(next - moments.begin());
        chosen_due = due;
    }
    if (!chosen)
        return false;
    const Moment& moment{moments[*chosen]};
    const SubjectKey key{SubjectKind::moment, static_cast<int32_t>(*chosen), 0};
    const std::optional<Framing> framing{frame_subject(analysis_, key, chosen_due, false)};
    const PlannedView view{framing ? framing->view : view_};
    PlannedShot shot{chosen_due, view, view};
    // A launch and its shell landing cut hard; a big blast or a death comes
    // in with a checkerboard.
    if (moment.kind != MomentKind::launch && moment.kind != MomentKind::impact)
        shot.transition = moment_checkerboard;
    add(shot);
    take(key, chosen_due, view, view, moment.score);
    style_ = Style::still;
    return true;
}

bool ShotPlanner::decide_action(uint32_t tick) {
    // The fight the camera holds, until its damage moves out of the view.
    const bool live{current_.kind == SubjectKind::hot && !spent_};
    std::optional<std::array<int32_t, 2>> avoid{};
    if (live)
        avoid = std::array<int32_t, 2>{current_.a, current_.b};
    const HotSpots spots{find_hot_spots(analysis_, tick, avoid, grid_)};
    // A hot spot whose neighbourhood overlaps the one the shot was taken on
    // is the same fight, moved; the candidate is the best fight elsewhere.
    if (live && spots.best.found &&
        neighbourhoods_overlap(spots.best.cell_x, spots.best.cell_z, taken_.a, taken_.b)) {
        current_.a = spots.best.cell_x;
        current_.b = spots.best.cell_z;
    }
    std::vector<Candidate> candidates{};
    const auto consider{[&](const SubjectKey& key, int64_t raw, uint32_t cut_tick) {
        // Neither the current subject, unless its fight has moved on, nor the
        // one last switched to is taken again at once.
        if (key == taken_ || (key == current_ && !spent_))
            return;
        if (const std::optional<Candidate> found{candidate(key, raw, cut_tick, tick, 0)})
            candidates.push_back(*found);
    }};
    const HotSpot& hot{spots.away};
    if (hot.found)
        consider(
            SubjectKey{SubjectKind::hot, hot.cell_x, hot.cell_z},
            hot.score,
            earlier(hot.first_tick, arrive_early_ticks)
        );
    for (const uint8_t player : analysis_.combatants) {
        const ArmyGroup army{best_army(analysis_, player, tick)};
        if (army.found)
            consider(SubjectKey{SubjectKind::army, player, 0}, army.score, tick);
    }
    // Long-range fire is framed wide now and then, not whenever it flies.
    if (last_barrage_ == never || tick - last_barrage_ >= tuning::barrage_spacing_ticks)
        if (const Barrage barrage{find_barrage(analysis_, tick)}; barrage.found)
            consider(SubjectKey{SubjectKind::barrage, 0, 0}, barrage.score, tick);
    if (candidates.empty())
        return false;
    std::sort(candidates.begin(), candidates.end(), goes_before);
    const Candidate& best{candidates.front()};
    // An army that arrives where a fight is about to break out hands the
    // camera to the fight, however small.
    const bool arriving{
        current_.kind == SubjectKind::army && best.key.kind == SubjectKind::hot &&
        overlap_percent(analysis_.stage, view_, best.view) > 0
    };
    if (best.score < tuning::action_min_score && !arriving)
        return false;
    // A hot spot whose fight starts later is waited for.
    if (best.cut_tick < tick || best.cut_tick >= later(tick, decision_period_ticks) ||
        best.cut_tick >= stop_)
        return false;
    const uint32_t cut{best.cut_tick};
    if (cut <= last_shot_ || cut - last_cut_ < min_cut_ticks || !room_for_shot(cut))
        return false;
    const uint32_t age{cut - subject_start_};
    bool switching{age >= min_shot_ticks};
    // Wide long-range fire gives way to any action once its time is up.
    const bool wide_done{
        current_.kind == SubjectKind::barrage && age >= tuning::barrage_shot_ticks
    };
    if (action_kind(current_.kind) && age < max_shot_ticks && !arriving && !wide_done) {
        const int64_t current{spent_ ? 0 : subject_score(analysis_, current_, tick)};
        switching =
            switching && best.score * switch_ratio_denominator >= current * switch_ratio_numerator;
    }
    if (!switching)
        return false;
    // A hot spot shows the launches of the fire landing there, wide, only
    // as often as long-range fire is framed wide.
    const bool wide_allowed{
        last_barrage_ == never || cut - last_barrage_ >= tuning::barrage_spacing_ticks
    };
    const std::optional<Move> move{follow_move(best.key, cut, wide_allowed)};
    if (!move)
        return false;
    std::optional<PlannedTransition> forced{};
    if (!action_seen_)
        forced = phase_fade;
    action_seen_ = true;
    if (best.key.kind == SubjectKind::barrage ||
        (best.key.kind == SubjectKind::hot && move->start.height > tuning::hot_view_height))
        last_barrage_ = cut;
    switch_to(best.key, cut, best.raw, *move, forced);
    return true;
}

bool ShotPlanner::battle_on(uint32_t tick) const {
    return action_seen_ && damage_between(
                               analysis_,
                               earlier(tick, tuning::battle_recent_ticks),
                               later(tick, hot_window_last_ticks)
                           ) >= tuning::battle_min_score;
}

uint32_t ShotPlanner::turn_length(const SubjectKey& key, int64_t score) const noexcept {
    const bool idle{
        key.kind == SubjectKind::overview ||
        (key.kind == SubjectKind::commander && score <= tuning::commander_base_score) ||
        (key.kind == SubjectKind::base && score <= tuning::base_base_score)
    };
    const uint32_t variation{
        (turns_ * tuning::turn_variation_stride % tuning::turn_variations) *
        tuning::turn_variation_ticks
    };
    return (idle ? tuning::idle_turn_ticks : tuning::busy_turn_ticks) + variation;
}

bool ShotPlanner::decide_quiet(uint32_t tick) {
    const uint32_t age{tick - subject_start_};
    bool due{};
    if (current_.kind == SubjectKind::barrage)
        due = age >= tuning::barrage_shot_ticks;
    else if (action_kind(current_.kind))
        due = !spent_ && subject_score(analysis_, current_, tick) > 0 ? age >= max_shot_ticks
                                                                      : age >= min_shot_ticks;
    else if (current_.kind == SubjectKind::moment)
        due = age >= min_shot_ticks;
    else
        due = age >= turn_length_;
    if (!due || tick - last_cut_ < min_cut_ticks || !room_for_shot(tick))
        return false;
    // In a battle the camera stays with the action, waiting for more.
    if ((action_kind(current_.kind) || current_.kind == SubjectKind::moment) &&
        age < tuning::battle_hold_max_ticks && battle_on(tick))
        return false;
    // No turn starts so late that the cut to the first fight must wait.
    const uint32_t engagement{analysis_.engagement_tick};
    if (engagement != never && tick < engagement &&
        uint64_t{tick} + min_shot_ticks + arrive_early_ticks > engagement)
        return false;
    // Nor does one start just before a moment would cut it short.
    const auto& moments{analysis_.moments};
    const auto next{std::upper_bound(
        moments.begin(),
        moments.end(),
        later(tick, tuning::moment_lead_ticks),
        [](uint32_t wanted, const Moment& moment) { return wanted < moment.tick; }
    )};
    if (next != moments.end() &&
        earlier(next->tick, tuning::moment_lead_ticks) < later(tick, min_shot_ticks))
        return false;
    return take_turn(tick);
}

int64_t ShotPlanner::turn_height(const SubjectKey& key) const noexcept {
    if (key.kind != SubjectKind::commander)
        return 0;
    return tuning::commander_heights[commander_turns_ % tuning::commander_heights.size()];
}

bool ShotPlanner::take_turn(uint32_t tick) {
    const auto& combatants{analysis_.combatants};
    if (combatants.empty())
        return false;
    const auto at{std::find(combatants.begin(), combatants.end(), side_)};
    const size_t here{at != combatants.end() ? static_cast<size_t>(at - combatants.begin()) : 0u};
    const bool stay{at != combatants.end() && side_turns_ < turn_side_shots};
    // The sides in the order they are tried: this one while it has turns
    // left, then the next ones.
    std::vector<uint8_t> order{};
    for (size_t step{stay ? 0u : 1u}; order.size() < combatants.size(); ++step)
        order.push_back(combatants[(here + step) % combatants.size()]);
    // In a battle a commander takes a turn only while it builds.
    const bool battle{battle_on(tick)};
    for (const uint8_t side : order) {
        const bool crossing{side != side_};
        std::vector<Candidate> candidates{};
        for (const SideSubject& subject : side_subjects(analysis_, side, tick)) {
            // A side's turns show different kinds of subject in a row.
            if (subject.key == current_ || subject.key == taken_ ||
                (!crossing && subject.key.kind == current_.kind))
                continue;
            if (battle && subject.key.kind == SubjectKind::commander &&
                subject.score <= tuning::commander_base_score)
                continue;
            if (const std::optional<Candidate> found{
                    candidate(subject.key, subject.score, tick, tick, turn_height(subject.key))
                })
                candidates.push_back(*found);
        }
        std::sort(candidates.begin(), candidates.end(), goes_before);
        if (candidates.empty() || candidates.front().score <= 0)
            continue;
        // Crossing to a side with a subject, the wide drift there may take
        // the turn, unless it would drift over the sea.
        if (crossing && combatants.size() > 1u && current_.kind != SubjectKind::overview &&
            (last_crossing_ == never || tick - last_crossing_ >= tuning::crossing_spacing_ticks)) {
            const Move crossing_drift{crossing_move(side, tick)};
            const PlannedView halfway{
                (crossing_drift.start.x2 + crossing_drift.end.x2) / 2,
                (crossing_drift.start.z2 + crossing_drift.end.z2) / 2,
                crossing_drift.end.height,
            };
            const SubjectKey overview{SubjectKind::overview, 0, 0};
            const int64_t score{
                tuning::overview_score * freshness(overview, crossing_drift.end, tick) / percent
            };
            if (score > candidates.front().score && overview != taken_ &&
                water_percent(analysis_, halfway) <= tuning::crossing_water_limit_percent) {
                const uint32_t length{turn_length(overview, tuning::overview_score)};
                switch_to(overview, tick, tuning::overview_score, crossing_drift);
                turn_length_ = length;
                last_crossing_ = tick;
                side_ = side;
                side_turns_ = 0;
                ++turns_;
                return true;
            }
        }
        const Candidate& best{candidates.front()};
        const uint32_t length{turn_length(best.key, best.raw)};
        const std::optional<Move> move{turn_move(best.key, tick, length)};
        if (!move)
            continue;
        switch_to(best.key, tick, best.raw, *move);
        turn_length_ = length;
        if (crossing)
            side_turns_ = 0;
        side_ = side;
        ++side_turns_;
        ++turns_;
        if (best.key.kind == SubjectKind::commander)
            ++commander_turns_;
        return true;
    }
    return false;
}

std::optional<Move>
ShotPlanner::follow_move(const SubjectKey& key, uint32_t tick, bool show_launches) const {
    const std::optional<Framing> start{frame_subject(analysis_, key, tick, true, 0, show_launches)};
    if (!start)
        return std::nullopt;
    const std::optional<Framing> aim{frame_subject(
        analysis_, key, later(tick, tuning::follow_lead_ticks), true, 0, show_launches
    )};
    Move move{};
    move.start = start->view;
    move.end = aim ? reframe(start->view, *aim, key.kind == SubjectKind::hot) : start->view;
    if (move.end != move.start)
        move.spring = tuning::follow_spring;
    return move;
}

PlannedView
ShotPlanner::reframe(const PlannedView& from, const Framing& aim, bool scattered) const {
    const Stage& stage{analysis_.stage};
    const PlannedView& to{aim.view};
    const bool too_wide{from.height * percent > to.height * tuning::follow_zoom_in_percent};
    const Point middle{aim.points.empty() ? middle_of(to) : middle_of_points(aim.points)};
    bool still{};
    if (scattered) {
        // Damage lands all over a fight: the camera moves once its middle
        // leaves the safe area, or it needs a wider view by more than a step.
        still = in_safe_area(stage, from, middle) &&
                to.height * percent <= from.height * (percent + tuning::follow_zoom_step_percent) &&
                !too_wide;
    } else {
        const bool holds{std::all_of(aim.points.begin(), aim.points.end(), [&](Point point) {
            return in_safe_area(stage, from, point);
        })};
        still = holds && (!aim.points.empty() || to.height <= from.height) &&
                within_part(stage, from, middle, tuning::follow_deadband_percent) && !too_wide;
    }
    if (still)
        return from;
    const bool wide_enough{to.height <= from.height};
    PlannedView step{from};
    const int64_t reach_x2{
        2 * view_width(stage, from.height) * tuning::follow_step_percent / percent
    };
    const int64_t reach_z2{2 * from.height * tuning::follow_step_percent / percent};
    step.x2 += std::clamp(to.x2 - from.x2, -reach_x2, reach_x2);
    step.z2 += std::clamp(to.z2 - from.z2, -reach_z2, reach_z2);
    if (!wide_enough)
        step.height = std::min(
            to.height, from.height * (percent + tuning::follow_zoom_step_percent) / percent
        );
    else if (too_wide)
        step.height = std::max(
            to.height, from.height * percent / (percent + tuning::follow_zoom_step_percent)
        );
    return clamp_planned(stage, step);
}

std::optional<Move>
ShotPlanner::turn_move(const SubjectKey& key, uint32_t tick, uint32_t length) const {
    const Stage& stage{analysis_.stage};
    const int64_t height{turn_height(key)};
    const std::optional<Framing> start{frame_subject(analysis_, key, tick, true, height)};
    if (!start)
        return std::nullopt;
    // A base stays where it is framed; units may move over the turn.
    const std::optional<Framing> finish{
        frame_subject(analysis_, key, later(tick, length), true, height)
    };
    const Framing& end{finish && !place_kind(key.kind) ? *finish : *start};
    Move move{};
    move.start = start->view;
    move.end = end.view;
    move.style = Style::drift;
    // A drift runs across the view the way that shows the least water at
    // its end, then at its start, as far as its subject stays in view.
    const auto drift_along{[&](const std::array<int64_t, 2>& direction, int64_t sign) {
        // Each end lies half the drift from the framing, in half pixels.
        const auto moved{[&](const Framing& framing, int64_t away) {
            const int64_t reach{
                away * view_width(stage, framing.view.height) * tuning::drift_percent / percent
            };
            return shifted(
                stage,
                framing,
                direction[0] * reach / tuning::drift_direction_scale,
                direction[1] * reach / tuning::drift_direction_scale
            );
        }};
        return std::pair<PlannedView, PlannedView>{moved(*start, -sign), moved(end, sign)};
    }};
    const auto drift{[&]() {
        std::pair<PlannedView, PlannedView> best{move.start, move.end};
        int64_t least{};
        bool found{};
        for (const auto& direction : tuning::drift_directions) {
            const std::pair<PlannedView, PlannedView> tried{drift_along(direction, 1)};
            const int64_t water{
                2 * water_percent(analysis_, tried.second) + water_percent(analysis_, tried.first)
            };
            if (!found || water < least) {
                best = tried;
                least = water;
                found = true;
            }
        }
        move.start = best.first;
        move.end = best.second;
    }};
    switch (key.kind) {
    case SubjectKind::commander:
        switch (turns_ % 3) {
        case 0:
            move.start = scaled(stage, start->view, tuning::push_in_percent);
            break;
        case 1:
            drift();
            break;
        default:
            move.end = scaled(stage, end.view, tuning::push_in_percent);
            break;
        }
        break;
    case SubjectKind::base:
        drift();
        break;
    case SubjectKind::factory:
        move.start = scaled(stage, start->view, tuning::factory_push_percent);
        move.end = start->view;
        move.spring = tuning::push_spring;
        move.style = Style::follow;
        break;
    default:
        return follow_move(key, tick);
    }
    return move;
}

Move ShotPlanner::crossing_move(uint8_t side, uint32_t tick) const {
    const Stage& stage{analysis_.stage};
    const auto wide{[&](int64_t x2, int64_t z2) {
        return clamp_planned(stage, PlannedView{x2, z2, tuning::crossing_view_height});
    }};
    // To where the side builds, else to its start.
    Point to{analysis_.has_start[side] ? analysis_.start[side] : Point{}};
    if (const std::optional<Framing> base{
            frame_subject(analysis_, SubjectKey{SubjectKind::base, side, 0}, tick, true)
        })
        to = middle_of(base->view);
    Move move{};
    move.start = wide(view_.x2, view_.z2);
    move.end = wide(2 * int64_t{to.x}, 2 * int64_t{to.row});
    move.style = Style::drift;
    return move;
}

std::optional<PlannedTransition>
ShotPlanner::cut_transition(const PlannedView& start, uint32_t tick) const {
    const Stage& stage{analysis_.stage};
    // A cut in or out: one view's middle lies in the other.
    const auto holds_middle{[&](const PlannedView& outer, const PlannedView& inner) {
        return in_safe_area(
            stage,
            PlannedView{outer.x2, outer.z2, outer.height * percent / safe_area_percent},
            middle_of(inner)
        );
    }};
    if (holds_middle(view_, start) || holds_middle(start, view_))
        return std::nullopt;
    const uint8_t from{side_of(analysis_, middle_of(view_))};
    const uint8_t to{side_of(analysis_, middle_of(start))};
    if (from != to && (last_wipe_ == never || tick - last_wipe_ >= wipe_spacing_ticks))
        return side_wipe;
    return far_cut;
}

void ShotPlanner::switch_to(
    const SubjectKey& key,
    uint32_t tick,
    int64_t score,
    const Move& move,
    std::optional<PlannedTransition> forced
) {
    const Stage& stage{analysis_.stage};
    PlannedView first{move.start};
    PlannedView end{move.end};
    // A quiet turn's drift pans only to a view whose middle the camera
    // shows already; anything else pans within pan_reach_percent.
    const bool pan{
        !forced && near_enough_to_pan(stage, view_, move.start) &&
        (move.style != Style::drift || in_safe_area(stage, view_, middle_of(move.start)))
    };
    if (pan) {
        first = view_;
        end = limit_zoom(stage, view_, move.end, tuning::follow_lead_ticks);
        add(PlannedShot{tick, std::nullopt, end, tuning::pan_spring});
    } else {
        const std::optional<PlannedTransition> transition{
            forced ? forced : cut_transition(move.start, tick)
        };
        if (transition && transition->kind == oascript::TransitionKind::wipe)
            last_wipe_ = tick;
        end = limit_zoom(stage, move.start, move.end, tuning::follow_lead_ticks);
        add(PlannedShot{tick, move.start, end, move.spring, transition});
    }
    take(key, tick, first, end, score);
    // A pan heads for the view a drift would end on, then follows, or holds
    // a place.
    style_ = pan && move.style == Style::drift ? Style::follow : move.style;
    drift_from_ = first;
    drift_start_ = tick;
    height_ = turn_height(key);
}

PlannedView ShotPlanner::drift_view(uint32_t tick) const noexcept {
    const int64_t whole{std::max<uint32_t>(turn_length_, 1u)};
    const int64_t done{std::min<int64_t>(tick - std::min(tick, drift_start_), whole)};
    const PlannedView& from{drift_from_};
    const PlannedView& to{view_};
    PlannedView passing{};
    passing.x2 = from.x2 + (to.x2 - from.x2) * done / whole;
    passing.z2 = from.z2 + (to.z2 - from.z2) * done / whole;
    const int64_t divisor{to.height * whole + (from.height - to.height) * done};
    passing.height = divisor > 0 ? from.height * to.height * whole / divisor : to.height;
    return clamp_planned(analysis_.stage, passing);
}

bool ShotPlanner::drift_holds(uint32_t tick) const {
    if (tick < drift_start_ || tick - drift_start_ >= turn_length_)
        return false;
    if (place_kind(current_.kind))
        return true;
    const std::optional<Framing> framing{frame_subject(analysis_, current_, tick, true, height_)};
    if (!framing)
        return false;
    const PlannedView passing{drift_view(tick)};
    return std::all_of(framing->points.begin(), framing->points.end(), [&](Point point) {
        return within_part(analysis_.stage, passing, point, tuning::drift_hold_percent);
    });
}

void ShotPlanner::follow(uint32_t tick) {
    if (style_ == Style::still || spent_ || (place_kind(current_.kind) && style_ != Style::drift))
        return;
    // No follow shot comes within follow_period_ticks of the shot before,
    // so that a cut's transition plays out.
    if (tick - last_shot_ < tuning::follow_period_ticks)
        return;
    // Where the camera is: a drift that no longer holds its subject stops
    // where it has taken the camera, and is followed from there.
    PlannedView from{view_};
    bool stop{};
    if (style_ == Style::drift) {
        if (drift_holds(tick))
            return;
        if (tick - drift_start_ < turn_length_) {
            from = drift_view(tick);
            stop = true;
        } else {
            style_ = Style::follow;
        }
    }
    if (opening_ && tick < later(analysis_.first_tick, tuning::opening_settle_ticks))
        return;
    const std::optional<Framing> aim{
        frame_subject(analysis_, current_, later(tick, tuning::follow_lead_ticks), true, height_)
    };
    // Damage that has moved out of the view the shot settled on ends the
    // fight the camera holds there.
    if (aim && current_.kind == SubjectKind::hot && !aim->points.empty() &&
        !within_part(analysis_.stage, anchor_, middle_of_points(aim->points), percent))
        spent_ = true;
    const PlannedView end{
        aim && !spent_ ? reframe(from, *aim, current_.kind == SubjectKind::hot) : from
    };
    if (end == from && !stop)
        return;
    std::optional<oascript::Decimal> spring{};
    if (end != from)
        spring = tuning::follow_spring;
    add(PlannedShot{tick, std::nullopt, end, spring});
    style_ = Style::follow;
    if (!memory_.empty())
        memory_.back().last = end;
}

void ShotPlanner::end_on_deciding_death() {
    const uint32_t deciding{analysis_.deciding_tick};
    const uint32_t hold_tick{
        std::max(earlier(deciding, tuning::ending_lead_ticks), analysis_.first_tick)
    };
    while (!shots_.empty() && shots_.back().tick >= hold_tick)
        shots_.pop_back();
    while (!note_ticks_.empty() && note_ticks_.back() >= hold_tick) {
        note_ticks_.pop_back();
        notes_.pop_back();
    }
    const SubjectKey key{SubjectKind::ending, 0, 0};
    const std::optional<Framing> framing{frame_subject(analysis_, key, hold_tick, false)};
    const PlannedView hold{framing ? framing->view : whole_map(analysis_.stage)};
    PlannedShot held{hold_tick, hold, hold};
    if (!shots_.empty())
        held.transition = phase_fade;
    add(held);
    take(key, hold_tick, hold, hold, analysis_.deciding_score);
    PlannedView pulled{hold};
    pulled.height = hold.height * tuning::ending_pull_back_percent / percent;
    pulled = clamp_planned(analysis_.stage, pulled);
    const uint32_t pull_tick{later(deciding, ending_hold_ticks)};
    end_tick_ = std::min(analysis_.end_tick, later(pull_tick, ending_pull_back_ticks));
    if (pull_tick < end_tick_ && pull_tick > hold_tick)
        add(PlannedShot{pull_tick, std::nullopt, pulled, tuning::ending_spring});
}

/// Tells whether a spring stays within max_spring_step at a frame rate.
///
/// @param frequency the spring's frequency, in hertz
/// @param framerate the frame rate
/// @return true when 2 pi frequency / framerate is at most 1/2, checked
///         with pi taken a little above its value
bool spring_steady(oascript::Decimal frequency, const Rational& framerate) noexcept {
    const Rational hertz{rational_of(frequency)};
    return spring_step_bound * pi_above_numerator * hertz.numerator * framerate.denominator <=
           pi_above_denominator * framerate.numerator * hertz.denominator;
}

/// Returns the script of planned shots, fitted to the video's frames: a
/// shot no frame shows gives way to the next, which keeps its tick and, when
/// it has none, its cut; a shot starting at or after the end is dropped; a
/// transition longer than its shot is dropped; and a spring too fast for the
/// frame rate moves linearly.
///
/// @param planned the shots
/// @param end_tick the tick the video ends before
/// @param settings the plan's settings
/// @return the script
oascript::Script script_of(
    const std::vector<PlannedShot>& planned, uint32_t end_tick, const PlannerSettings& settings
) {
    oascript::Script script{};
    script.input.recording = settings.recording;
    script.input.tickrate = settings.tickrate;
    script.output.width = settings.width;
    script.output.height = settings.height;
    script.output.framerate = settings.framerate;
    std::vector<PlannedShot> kept{};
    for (const PlannedShot& shot : planned)
        if ((kept.empty() || shot.tick > kept.back().tick) && shot.tick < end_tick)
            kept.push_back(shot);
    FrameClock clock{};
    clock.first_tick = kept.empty() ? 0u : kept.front().tick;
    clock.tickrate = rational_of(settings.tickrate);
    clock.framerate = rational_of(settings.framerate);
    const bool rates_usable{
        clock.tickrate.numerator > 0 && clock.tickrate.denominator > 0 &&
        clock.framerate.numerator > 0 && clock.framerate.denominator > 0
    };
    std::vector<uint64_t> first_frames{};
    std::vector<PlannedShot> shown{};
    for (const PlannedShot& shot : kept) {
        const uint64_t frame{rates_usable ? first_frame_of_tick(clock, shot.tick) : 0u};
        if (!shown.empty() && rates_usable && frame <= first_frames.back()) {
            // The shot before shows no frame: this one takes its place, and
            // its cut when this one has none.
            PlannedShot& earlier_shot{shown.back()};
            PlannedShot merged{shot};
            merged.tick = earlier_shot.tick;
            if (!merged.start) {
                merged.start = earlier_shot.start;
                merged.transition = earlier_shot.transition;
            }
            earlier_shot = merged;
            continue;
        }
        shown.push_back(shot);
        first_frames.push_back(frame);
    }
    const uint64_t end_frame{rates_usable ? first_frame_of_tick(clock, end_tick) : 0u};
    while (shown.size() > 1u && rates_usable && end_frame <= first_frames.back()) {
        shown.pop_back();
        first_frames.pop_back();
    }
    for (size_t index{}; index < shown.size(); ++index) {
        const PlannedShot& planned_shot{shown[index]};
        oascript::Shot shot{};
        shot.tick = planned_shot.tick;
        if (planned_shot.start)
            shot.camera_start = camera_of(*planned_shot.start);
        shot.camera_end = camera_of(planned_shot.end);
        if (planned_shot.spring && rates_usable &&
            spring_steady(*planned_shot.spring, clock.framerate)) {
            shot.motion.kind = oascript::Motion::spring;
            shot.motion.frequency = *planned_shot.spring;
            shot.motion.damping_ratio = tuning::spring_damping;
        }
        if (planned_shot.transition && planned_shot.start && index > 0 && rates_usable) {
            const uint64_t next{index + 1u < shown.size() ? first_frames[index + 1u] : end_frame};
            const uint64_t frames{next - first_frames[index]};
            const oascript::Decimal duration{
                seconds_of_milliseconds(planned_shot.transition->milliseconds)
            };
            const uint64_t steps{frames_of_seconds(clock, duration)};
            if (steps > 0 && steps <= frames)
                shot.transition = oascript::Transition{planned_shot.transition->kind, duration};
        }
        script.director.shots.push_back(shot);
    }
    script.director.end_tick = end_tick;
    return script;
}

} // namespace

Plan plan_script(const Timeline& timeline, const PlannerSettings& settings) {
    const Analysis analysis{analyse(timeline, settings)};
    ShotPlanner planner{analysis};
    planner.plan();
    Plan plan{};
    plan.script = script_of(planner.shots(), planner.end_tick(), settings);
    plan.notes = planner.take_notes();
    return plan;
}

} // namespace oa::media::director
