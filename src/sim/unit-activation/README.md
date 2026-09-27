# Unit activation transitions

`change` sets or clears bits of a unit's activation byte (`Unit.state_flags`),
derives the rising and falling bits once and runs the callbacks in this order:
Activate/Deactivate and sound; StartBuilding/StopBuilding; cloak/decloak sound
and attachment notification; selected-unit refresh; then, when the owner is
simulated here, the report of the new flags for the other players. Unknown bits
still take part in the state change.

Named script calls are deferred, with no arguments. The host provides the
script, sound, attachment, presentation and reporting services; this component
never supplies them silently. The flags and unit index are reread after the
callbacks when the report is formed, so changes a callback makes are reported.

`mark_owned_selection` walks the selection list (`Game.hot_units`, counted by
`Game.hot_unit_count`). Each id is a unit slot of `Game.units`. When that
unit's `owner_index` is the local player (`Game.local_player_index`), its
`Unit.flags` gains bit `0x40` and loses bit `0x80`; the other bits stay. Other
owners are not written, and an empty list does nothing.

`clear_cycle_marks` walks the unit table from `Game.units` through
`Game.units_last` inclusive and clears bits `0x40` and `0x80` of each
`Unit.flags`; the other bits stay. When the start slot is above the end slot,
nothing is written. It runs when no unit with both bits clear remains, and
just before `mark_owned_selection`.
