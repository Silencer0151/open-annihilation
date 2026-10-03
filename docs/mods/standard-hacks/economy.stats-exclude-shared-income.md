# Statistics Exclude Shared Income

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `economy.stats-exclude-shared-income` |
| Area | Economy (`economy`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Resources a player receives from allies no longer count as "produced" in that player's statistics. Income and storage are unchanged: the shared resources still arrive and can be spent.

## Configuration example

```yaml
hacks:
  economy.stats-exclude-shared-income: true
```

## Details

### Behaviour

Resources shared by an ally reach the receiving player through the player's staging economy block, which takes them in as income of the next economy tick. With the hack on, each economy tick subtracts what the staging block took in this tick from the player's produced totals:

- metal first, then energy;
- each amount is widened to double precision and subtracted at double precision from the double-precision total.

Only the produced totals change. The player's stored resources, income rate and the staging block itself behave as in 3.1c, so a shared resource can be spent exactly as before. When nothing was shared in a tick, the totals are exactly 3.1c's.

### 3.1c behaviour

3.1c adds every amount a player takes in during a tick, shared resources included, to the produced totals. A player whose allies feed it resources therefore shows more produced than its own units made.

### Network games

The machine that simulates a player applies the hack to that player's totals, and the other machines take those totals from the economy records it sends. Every machine must agree that the hack is on; it is part of the profile hash.

### Interactions

- The end-of-game statistics (`src/ui/campaign/endgame.cpp`) show the produced totals this hack corrects; [ui.endgame-stats](ui.endgame-stats.md) changes which players that list includes.

### Implementation notes

- Applied in `src/sim/match-runtime/src/tick_economy.cpp`, at the end of the per-player economy update, before the staging block is settled. It reads the rules record field `rules.economy.stats_exclude_shared_income.enabled`.
- The staging block is the player's economy block (`world_player_economy` in `src/core/include/oa/core/world.h`); the share path credits its `produced` amounts.
- Tests: `match-economy-rules` (`src/sim/match-runtime/tests/economy_rules_test.cpp`, `shared_income_leaves_the_produced_totals`: with and without the hack, with and without shared receipts).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `economy.stats-exclude-shared-income`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `economy.stats-exclude-shared-income: true` | On. |
| `economy.stats-exclude-shared-income: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  economy.stats-exclude-shared-income: true
```
<!-- END GENERATED: schema -->
