# Deterministic Wind

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `economy.deterministic-wind` |
| Area | Economy (`economy`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine, each reaching the same result. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Every machine draws the wind from one generator, seeded the same way everywhere, instead of from its own random numbers. Wind generators then earn the same energy on every machine, and the wind's timing no longer depends on the machine's local random stream.

## Configuration example

```yaml
hacks:
  economy.deterministic-wind: true
```

The shorthand `local-random` switches the hack on but keeps the 3.1c draws, which is what the `baseline` preset does:

```yaml
hacks:
  # On, but the wind is drawn as 3.1c draws it.
  economy.deterministic-wind: local-random
```

## Details

### Behaviour

With `rng: mt19937-host-id` the wind comes from one generator, the standard 32-bit Mersenne Twister (MT19937), kept for the whole match.

- **Seed.** The generator is seeded once, at the first wind change. The seed is the network player id of the host player: the player in use whose machine is the host and whose setup record is the host's. When no player is the host, or the host's id is not positive, the seed is the match's random seed, so a skirmish, a saved game and a replay get the same wind every time.
- **Next change.** At each change the next change is set 150 + 30 x (r mod 10) ticks later, where r is the next draw: 150 to 420 ticks.
- **Strength.** When the map's maximum wind is above its minimum, the strength is minimum + (r mod (maximum - minimum)) with a fresh draw r. Otherwise the strength is the minimum and nothing is drawn.
- **Direction.** A new 16-bit direction is drawn only when the strength is not zero; a calm wind keeps its last direction.
- **Streams.** The generator takes no draws from the match's own random streams, so the wind no longer shifts the random numbers other parts of the game use.

With `rng: local-random` the hack is on but plays exactly as 3.1c.

### 3.1c behaviour

3.1c times each change from the machine's local random stream and draws the strength and direction from the shared simulation stream. The interval therefore can differ between machines, and how many shared draws the wind takes depends on the map's wind limits.

### Network games

Every machine runs the generator itself and reaches the same wind, because every machine finds the same host player and seed. All machines must have the hack on with the same `rng`; the hack is part of the profile hash.

### Interactions

- [economy.stats-exclude-shared-income](economy.stats-exclude-shared-income.md) is the other economy hack; the two are independent.

### Implementation notes

- The generator, the host-seed lookup and the scheduler are in `src/sim/world-environment/src/wind.cpp` (`seed_wind_generator`, `draw_wind_generator`, `shared_wind_seed`, `refresh_shared_wind`, `initialize_shared_wind`), declared in `src/sim/world-environment/include/oa/sim/world_environment/wind.hpp` (`shared_cadence_bias_ticks`, `shared_cadence_steps`).
- The match chooses the generator in `src/sim/match-runtime/src/match.cpp` (`Match::keep_wind_generator`, `Match::refresh_wind`, `Match::shared_wind_seed`). It reads the rules record field `rules.economy.deterministic_wind` (`enabled` and `rng`).
- The generator's state is kept as the match rule state `wind-generator`: it is written to saves, restored from them (a state that is not one valid generator is refused) and folded into the match hash.
- Tests: `world-environment-shared-wind` (`src/sim/world-environment/tests/shared_wind_test.cpp`: the generator against the standard library's MT19937, seeding once, the host-seed lookup, flat and calm ranges) and `match-economy-rules` (`src/sim/match-runtime/tests/economy_rules_test.cpp`: `shared_wind_without_a_host`, `shared_wind_leaves_the_match_streams`, `shared_wind_seeded_by_the_host`, `shared_wind_generator_restores`, `local_wind_baseline_plays_3_1c`).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `economy.deterministic-wind`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `economy.deterministic-wind: true` | On, every parameter at its default. |
| `economy.deterministic-wind: {rng: mt19937-host-id}` | On, the parameters named set and the rest at their defaults. |
| `economy.deterministic-wind: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `economy.deterministic-wind: mt19937-host-id` | On, the shorthand: a bare value sets `rng`. |
| `economy.deterministic-wind: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `rng` | `enum` | - | `local-random`, `mt19937-host-id` | - | `local-random` | `mt19937-host-id` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{rng: local-random}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `rng`: `economy.deterministic-wind: mt19937-host-id` is `economy.deterministic-wind: {rng: mt19937-host-id}`.

### Every parameter at its default

```yaml
hacks:
  economy.deterministic-wind:
    rng: mt19937-host-id
```
<!-- END GENERATED: schema -->
