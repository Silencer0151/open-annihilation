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
metal accumulator (`UnitEconomy.metal`). Weapon store debits live on the player economy record
in `oa-sim-unit-spawn`.

`paralysis.hpp` covers paralyzer damage: a live, locally simulated, non-immune
target extends the paralyze order at the head of its queue or receives a new one
carrying the damage amount as ticks. `paralyzed_mission_step` is the Paralyzed
mission handler: each step clamps the pending duration to 0x708 ticks, clears
every weapon target (tracked targets first, then each slot), sets state flag 0x10
and waits; a zero duration clears the flag and finishes the order. The match
runtime still carries its own explicit paralyze handling.
