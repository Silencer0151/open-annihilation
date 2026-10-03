# Income Multipliers

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ai.income-multipliers` |
| Area | AI (`ai`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 2 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Computer players earn resources at a rate the profile sets for each difficulty. 3.1c gives an Easy computer player half its income, a Medium one 70 percent and a Hard one its full income; with the hack on, the default rates are half, full and four times the income. Production and feature reclaim have separate lists.

## Configuration example

```yaml
hacks:
  ai.income-multipliers: true   # Easy 0.5x, Medium 1x, Hard 4x, for production and reclaim
```

```yaml
hacks:
  ai.income-multipliers:
    production: [0.5, 1.0, 2.0]   # a Hard computer player produces at twice the rate
    # reclaim is not named, so it stays at its default, [0.5, 1.0, 4.0]
```

## Details

### Behaviour

Each list holds three multipliers, indexed by the game's difficulty number: entry 0 for difficulty 0, entry 1 for difficulty 1 and entry 2 for difficulty 2. A difficulty number outside 0 and 1 takes entry 2.

- `production` scales every energy and metal credit of a computer player's economy update: what its generators, extractors, metal makers and other producing units add each tick.
- `reclaim` scales the energy and metal a computer player's unit gains by reclaiming a map feature.

A credit is scaled only when the owning player's slot is in use and the player is a computer player. Human players are never scaled. The amount is widened to double precision, multiplied, and rounded back to a single-precision float, as 3.1c does with its own factors.

The other places where 3.1c applies its difficulty scale to a computer player keep 3.1c's factors whatever the lists say: the resources drawn while building (nanolathing and build power), resources given or shared between players, and resources returned when a unit dies.

The lists follow the difficulty number, not its name. Under [ai.difficulty-names](ai.difficulty-names.md) the names move but the numbers do not, so entry 0 always belongs to difficulty 0, whatever name the menus show for it. A list written for 3.1c's names reads Easy, Medium, Hard; a profile that swaps the names writes its list in the swapped order.

### Baseline (3.1c)

A computer player's credit is multiplied by 0.5 on difficulty 0 (Easy) and by 0.7 on difficulty 1 (Medium), and left whole on Hard, both for production and for feature reclaim. The `baseline` preset, `[0.5, 0.7, 1.0]` for both lists, plays exactly as 3.1c.

### Network play

The economy of a computer player runs on the machine that hosts it, and that machine applies the multipliers. Every machine must load the same lists: the hack is part of the profile hash the machines compare.

### Interactions

- [ai.difficulty-names](ai.difficulty-names.md) changes which name each difficulty number carries; the lists stay indexed by number.

### Implementation notes

- The rules record is `AiIncomeMultipliers` (`rules.ai.income_multipliers`, fields `production` and `reclaim`) in `src/data/match-rules/include/oa/data/match_rules/records.inc`.
- `Match::update_player_economy` in `src/sim/match-runtime/src/tick_economy.cpp` passes `production` to every production credit.
- The feature host's `credit_reclaim` in `src/sim/match-runtime/src/features.cpp` passes `reclaim` to the reclaim credit.
- The scaling itself is `scale_computer_income`, with the overloads of `credit_metal` and `credit_energy` that take `ComputerIncomeScales`, in `src/sim/unit-health/include/oa/sim/unit_health.hpp` and `src/sim/unit-health/src/unit_health.cpp`. `computer_income_index` maps a difficulty number to its entry. With the hack off, `scale_computer_credit` applies 3.1c's factors.
- Tests: ctest `match-economy-rules` (`production_baseline_plays_3_1c`, `production_multipliers_scale_every_site`, `reclaim_multipliers_scale_feature_reclaim`), `unit-health` (3.1c's factors) and `data-match-rules` (the record's baseline).
- Known limit: the multipliers apply at the economy update and at feature reclaim only, as described above.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ai.income-multipliers`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ai.income-multipliers: true` | On, every parameter at its default. |
| `ai.income-multipliers: {production: [0.5, 1.0, 4.0]}` | On, the parameters named set and the rest at their defaults. |
| `ai.income-multipliers: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `ai.income-multipliers: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `production` | `list<decimal>` | x income | `0` to `100` | 3 | `[0.5, 0.7, 1.0]` | `[0.5, 1.0, 4.0]` | `fixed` | `sim` | - |
| `reclaim` | `list<decimal>` | x income | `0` to `100` | 3 | `[0.5, 0.7, 1.0]` | `[0.5, 1.0, 4.0]` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{production: [0.5, 0.7, 1.0], reclaim: [0.5, 0.7, 1.0]}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

This hack takes no shorthand: write `true`, `false` or a parameter map.

### Every parameter at its default

```yaml
hacks:
  ai.income-multipliers:
    production: [0.5, 1.0, 4.0]
    reclaim: [0.5, 1.0, 4.0]
```
<!-- END GENERATED: schema -->
