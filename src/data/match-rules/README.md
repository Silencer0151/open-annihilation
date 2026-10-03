# Match rules

The rules a mod profile sets for the simulation, as plain records. Target
`oa-data-match-rules` (header only), header `oa/data/match_rules.hpp`,
namespace `oa::data::match_rules`.

`MatchRules` holds one record per area of the engine that owns rules (`ai`,
`orders`, `air`, `weapons`, `repair`, `veterancy`, `economy`, `intel`,
`setup`, `teams`, `sharing`, `units`, `console`), and in each one record
per hack: `enabled`, whether the profile turns the hack on, and one field
per parameter, named after it. `script_get` and `script_set` are the
unit-script extensions mounted at get and set indices, and
`script_fidelity` how they answer reads the original left undefined.
A match keeps them (`OfflineInputs::rules`, `Match::rules()`), and a unit
script's GET at an index outside 1..20 reads the extension `script_get`
mounts there ([src/sim/unit-script](../../sim/unit-script/include/oa/sim/unit_script.hpp),
`unit_script_get_value`). The engine's limits (units per player, unit types, effects, path-search
budget, build lists, model composite, category masks) are not here: they
are the one `Limits` record of [src/data/limits](../limits/README.md).

Every field starts at its 3.1c baseline, so `MatchRules{}` is base 3.1c: a match given them plays exactly as one without a profile.
A hack's parameters keep their baselines while it is off. The records are
fixed-size and trivially copyable: integers, decimals as `double`, enums,
`std::array`, `std::optional` for an integer or none, and `FixedText`,
`FixedList` and `EnumSet` for text, lists and sets, so simulation code can
keep and read them without the heap.

The records are generated from the OAMOD registry by
`tools/gen_mod_registry.py` (`match_rules/records.inc`,
`match_rules/script_extensions.inc`); do not edit them by hand.
[src/data/mod-profile](../mod-profile/README.md) fills them from a resolved
profile, and its `data-mod-profile-bindings` test checks that every field
can hold what its parameter allows and starts at its baseline.

A profile's data keys give single unit types and weapons values of their
own, read from their files: `UnitTypeRules` (veterancy thresholds and
accuracy rate, the facings a building may be built in) and
`WeaponTypeRules` (the low bit of each of the four weapon keys). A
default record is a type or weapon without the keys, as in 3.1c.

`MatchRulesView` is how simulation code reads all of them: the match-wide
`MatchRules` and the per-type and per-weapon records, by pointer and span.
`rules()`, `unit_type(index)` and `weapon(id)` answer with the record given,
or with `baseline_match_rules`, `baseline_unit_type_rules` and
`baseline_weapon_rules` when the view was not given one, so a module whose
host leaves its view unset plays 3.1c. The match owns the records and hands
out views (`Match::rules_view`).

`data-match-rules` checks the defaults, the helpers, the extension table
and the view.

## Where each rule is read

A hack is one branch at the place where its 3.1c rule lives, reading its
record from the nearest of these. The baseline values run 3.1c's arithmetic
unchanged; new state goes in a rule-state table
([src/sim/match-runtime](../../sim/match-runtime/README.md#rules)).

| Rules | Record | Read through |
| --- | --- | --- |
| `ai.*` | `rules().ai` | `ComputerPlayers::rules` (`src/sim/ai`; the match's computer host sets it on every pass); `RetaliationHooks::rules` for the damaged capturer's alert; `simulation_state::Host::rules` for the nearest-candidate search; difficulty labels in the screens through `Runtime::mod_profile()` |
| `orders.*`, `air.*` | `rules().orders`, `rules().air` | the mission handlers of `src/sim/match-runtime` (`TickHost` and its adapters hold the match: `match.rules()`), `ground_orders::Host::rules`; command input through `OrderCursorHooks::rules` (`src/sim/gameplay-input`) |
| `weapons.*` and the weapon data keys | `rules().weapons`, `weapon(id)` | `weapon_execution::Host::rules`, `RetaliationHooks::rules`, targeting and projectiles in `src/sim/match-runtime` (`Match::rules_view()`); `src/sim/ballistics` stays pure, its callers passing what a rule decides in its parameter records |
| `repair.*`, `veterancy.*` and the unit veterancy keys | `rules().repair`, `rules().veterancy`, `unit_type(t)` | the level and its effects in `oa/sim/unit_health/veterancy.hpp`, which take the view: `unit_health::DamageHost::rules` (damage taken and the repair step), `weapon_execution::Host::rules` (reload, lead, spread, damage dealt), `Match::rules_view()` (projectile damage, capture); `simulation_state::Host::rules` for the self-heal cadence and `Match::rules()` for its work; the exact repair's remainders in `Match::repair_remainders()` |
| `economy.*`, `ai.income-multipliers`, `units.init-cloaked-after-build` | `rules().economy`, `rules().ai.income_multipliers`, `rules().units.init_cloaked_after_build` | the economy tick, feature reclaim and the wind host of `src/sim/match-runtime` (`Match::rules()`); the wind scheduler of `src/sim/world-environment` and the income scaling of `src/sim/unit-health` take what the rules decide as parameters |
| `units.*` and the build-facings key | `rules().units`, `unit_type(t).build_facings` | `Match::keep_unit_rules` hands the slot reuse and start-submerged rules to unit creation (`sim::unit_spawn::SpawnRules`) and turns the yards; the movement tick's sea occupy update; building sites, construction, teardown and transfers in `src/sim/match-runtime`; the yard-map rules in the FBI loader (`data::defs::YardMapRules`), set by the app |
| `intel.*` | `rules().intel` | `src/sim/match-runtime`: the sight stamps' context (`Match::sight_context`, read by `src/sim/visibility-state`), `Match::unit_visible`, the contact scan and the alliance rebuild; the app's fog and radar draw from what these leave |
| `setup.*`, `teams.*` | `rules().setup`, `rules().teams` | the skirmish start and the lobby in `src/app` and `src/app/netgame`: `Runtime::mod_profile()->rules`; the recorder's commander warp and prebuilt base through `Lobby::rules` in the battle room and `NetMatch::match_rules` in the match (`net_match_bind_rules`), the commander's placing in `Match::begin_commander_placement` and `Runtime::commander_placement_pointer` |
| `console.*`, `sharing.*`, `teams.alliance-menu-all-game-types` | `rules().console`, `rules().sharing`, `rules().teams` | the match's `Match::rules()`: command classes, ATM, the key remaps and Tab through `console_apply_rules` (`src/ui/console`, applied as `src/app` binds the console); the speed range through `sim::speed::range_of` (`Runtime::game_speed_range`, and `WireRules::speed_min`/`speed_max`, which `wire_rules_of` fills, for network play); the team menu through `Runtime::team_menu_every_game`; structure gifts in `Match::begin_share_gift`, `end_share_gift` and `give_due_structure_gifts`; the take refusal in `Runtime::refuse_take_line`; the recorder's .give and .take through `NetMatch::match_rules` |
| `ui.*` | `ModProfile::ui` | `Runtime::mod_profile()->ui`, handed to the present and ui modules through their hosts |
| `network.*`, `recorder.*`, identity | `ModProfile::network`, `recorder`, `identity` | `Runtime::mod_profile()` in `src/app/netgame` |
| Unit-script extensions | `script_get`, `script_set`, `script_fidelity` | `UnitValueServices::rules` and `limits` (`src/sim/unit-script`), which the match fills from `UnitValueHost::value_rules` |
| Data keys | `UnitTypeRules`, `WeaponTypeRules` | read from the files by the keys `ModProfile::data_keys` binds, into `Runtime::unit_type_rules_` (indexed like `spawn_types_`) and `Runtime::weapon_rules_` (by weapon ID), which the match copies (`OfflineInputs::unit_type_rules`, `weapon_rules`) |
