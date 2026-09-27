# Unit-order names

`oa-data-mission-types` holds the list of the 68 unit-order names (`Standby`,
`Move_Ground`, `VTOL_Patrol` and the rest, as FBI files, mission schemas and
saved orders write them) and maps a name to its index, the order kind the
simulation uses. It does not carry out orders.

- The list is sorted without regard to ASCII case, and index 0 is the empty
  name. All 68 names are unique under ASCII case folding.
- `index_for_name` finds a name without regard to ASCII case; a NUL ends the
  name, and a name not in the list gives index 0.
- `mission_flags` marks two orders, `SelfRepair` and `Standby_Mine`, with
  `unnumbered_order`. A saved order that stores its kind as a position rather
  than a name counts only the orders without that flag.
- `mission_block_count` counts a campaign's leading `MISSION0`, `MISSION1`,
  ... sections.

The tests look up every name in lower case and an unknown name, count the
unnumbered orders, and count mission sections with gaps, case differences
and embedded NULs.
