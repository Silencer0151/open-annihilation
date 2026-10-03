# Unit health

Health event construction, resource-funded repair and construction, and
economy debit gating.

Kind-10 healing is applied locally. `DamageHost` is mandatory because other
event kinds own damage scripts and death behaviour downstream. `RecoveryHost`
additionally selects the repairer's energy accumulator. This component does not
guess those world-level policies.

`scale_computer_credit` and `credit_metal` apply the game's computer-player
resource scaling: owner status 2 on easy (0) or medium (1) scales credits by 0.5
or 0.7, and `credit_metal` writes the scaled amount into the target economy
metal accumulator (`UnitEconomy.metal`). Their overloads with
`ComputerIncomeScales` apply a mod's income multipliers instead: a present
computer owner's credit is multiplied, as a double, by the entry of its
difficulty (easy, medium, then every other difficulty); the multipliers at
3.1c's values (`computer_credit_scales`) credit exactly what 3.1c does. Weapon store debits live on the player economy record
in `oa-sim-unit-spawn`.

Veterancy (`veterancy.hpp`) turns a unit's kills (`Unit.veteran_level`)
into its level and each effect: the share of damage a victim takes, the
bonus a shooter deals, its reload, its aim lead, its spread divisor and the
level that lengthens its capture. Each reads the match's `veterancy.model`
rule and the unit type's own thresholds and accuracy rate through a
`MatchRulesView`; with the rule at its baseline each gives 3.1c's result
(a level per 5 kills, capped at 5). The shooter's machine scales both sides
of a hit, the victim's reduction by the victim's own type
(`make_health_event`).

`recover_health` runs one repair step under `repair.rate`: 3.1c's one point
a step (`clamp-max-1`), at least one point (`clamp-min-1`), or the exact heal
(`exact-remainder`), trunc(rate) x maximum / build time with the remainder
carried per repairer for its two latest targets in the host's
`repair_remainders` table, which the match keeps as rule state. Natural
regeneration uses the same step; its cadence and work follow
`repair.healtime-self-heal` (`src/sim/simulation-state`).

`paralysis.hpp` covers paralyzer damage: a live, locally simulated, non-immune
target extends the paralyze order at the head of its queue or receives a new one
carrying the damage amount as ticks. `paralyzed_mission_step` is the Paralyzed
mission handler: each step clamps the pending duration to 0x708 ticks, clears
every weapon target (tracked targets first, then each slot), sets state flag 0x10
and waits; a zero duration clears the flag and finishes the order. The match
runtime still carries its own explicit paralyze handling.
