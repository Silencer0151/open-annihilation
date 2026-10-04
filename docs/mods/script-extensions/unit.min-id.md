# Lowest Unit Id

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Extension id | `unit.min-id` |
| Mounted under | `script-extensions` → `get` |
| Usual index | 69: the list form mounts it there |
| Argument | None. Arguments written after the index are ignored. |
| Returns | 1, the lowest id a unit can have. |
| Scope | sim: part of the profile's sim hash; every machine must mount it at the same index |
| Runs on | Every machine in the game. |
| Same answer on every machine | Yes. |
| Fidelity | Not affected: it is always 1. |
<!-- END GENERATED: facts -->

## Description

`unit.min-id` gives a unit script the lowest id a unit can have, which is always 1. With [`unit.max-id`](unit.max-id.md) it bounds a walk over every unit id, so that a script can look at every unit in the game through the extensions that read another unit: [`unit.owner-of`](unit.owner-of.md), [`unit.allied-with`](unit.allied-with.md), [`unit.build-percent-left-of`](unit.build-percent-left-of.md) and [`unit.is-local`](unit.is-local.md).

A unit id names a place in the game's unit table. With a unit limit of L units per player, the table holds ten ranges of L ids after id 0, one for each of the ten player places, and each player's units take their ids from its own range. Which range is whose depends on the kind of game:

- In a skirmish or a campaign mission the ranges follow the player numbers: the first player's units have ids 1 to L, the second player's L + 1 to 2L, and so on to the tenth player's, which end at 10L.
- In a multiplayer game the ranges are given out in the order of the players' network ids, which every machine knows alike and which need not be the order of their player numbers, and the ranges of player places nobody took come last. So the first player's range need not be the first one.

Id 0 is never a unit. A script that needs a unit's player asks [`unit.owner-of`](unit.owner-of.md) rather than working it out from the id.

In 3.1c, `get 69` reads 0, as `get` does at every index outside 1 to 20. Without a profile that mounts the extension at the index, this engine reads 0 there too.

## Syntax

```bos
#define MIN_ID 69          // the index the profile mounts unit.min-id at

first = get MIN_ID;        // always 1
```

`MIN_ID` is a name the script chooses for itself. The compiled script holds only the number, so the profile must mount the extension at the index the script was compiled with. The extension takes no argument; `get MIN_ID(0)` reads the same value.

## Usage

The walk over every unit id, here counting the live units in the game:

```bos
#define UNIT_XZ  9        // 3.1c's own value: a live unit's position, else 0
#define MIN_ID   69
#define MAX_ID   70

static-var LiveUnits;

CountLiveUnits()
{
	var id, last;
	LiveUnits = 0;
	id = get MIN_ID;
	last = get MAX_ID;
	while (id <= last)
	{
		if (get UNIT_XZ(id))
		{
			LiveUnits = LiveUnits + 1;
		}
		id = id + 1;
	}
}
```

`UNIT_XZ` (index 9) is one of 3.1c's own values: it packs the position of the unit with that id, and reads 0 when no live unit has the id (or, in the one case that can confuse it, when the unit stands in the map's very top-left corner). [`unit.allied-with`](unit.allied-with.md) and [`unit.build-percent-left-of`](unit.build-percent-left-of.md) show walks that read the extensions for each id.

A script may run a whole walk within one game tick, without sleeping: one unit's scripts may run 16,777,216 instructions in a tick, 256 for each of the 65,536 ids a unit id can name. When a unit's scripts reach that many in one tick, they run no further in that tick and the unit's pieces do not move in it; they carry on where they stopped in the next tick. A loop that never sleeps therefore takes the whole of that unit's share every tick. A walk also costs time on every machine, so a script that repeats one sleeps between walks rather than walking every tick.

## Configuration example

```yaml
script-extensions:
  get: [unit.min-id, unit.max-id]     # mounted at 69 and 70
```

```yaml
script-extensions:
  get:
    40: unit.min-id                   # for scripts compiled to read 40 and 41
    41: unit.max-id
```

## Details

**The value.** Always 1, whatever the unit limit, the number of players or the fidelity.

**Id 0.** Id 0 names a reserved place before the first player's range. It never holds a unit: under `exact` fidelity [`unit.owner-of`](unit.owner-of.md) reads 255 there, and the other extensions read 0. A walk that starts from this value never visits it.

**Without the mount.** A script compiled for the extension still runs when nothing is mounted at its index: `get MIN_ID` and `get MAX_ID` both read 0, so the walk above visits only id 0 and counts nothing.

**Network play.** The value is the same on every machine. In a multiplayer game each machine simulates its own players' units and sends the results to the others, so what happens to another machine's unit, such as its creation or its death, reaches a machine a moment after it happens on the owner's. So a walk that counts live units, as above, can count differently on two machines for a moment. The mount is part of the sim hash: every machine must mount the extension at the same index.

### Implementation notes

- `unit_script_get_value` in `src/sim/unit-script/src/unit_value.cpp` returns 1.
- Tests:
  - `unit-script-extensions` (`caller_values`; `a_script_walks_every_unit_id` runs a walk from `unit.min-id` to `unit.max-id` in the interpreter, and `a_full_table_walk_fits_one_tick` walks the largest table a 16-bit id names in one tick);
  - `data-mod-profile` (refuses profiles that mount it wrongly: at an index another extension holds, at an index inside 3.1c's own 1 to 20, at two indices, under `set`, and at an index that is not a decimal integer).

## Related

<!-- BEGIN GENERATED: related -->
- [The OAMOD standard, section 7](../oamod-standard.md#7-script-extensions): mounting, fidelity and the table of every extension.
- [Every script extension](../README.md#every-script-extension), in the mod support overview.
- [Kill Count Times 100](unit.kills-x100.md): `unit.kills-x100`, usually at 32.
- [Highest Unit Id](unit.max-id.md): `unit.max-id`, usually at 70.
- [Own Unit Id](unit.my-id.md): `unit.my-id`, usually at 71.
- [Owner of a Unit](unit.owner-of.md): `unit.owner-of`, usually at 72.
- [Build Percent Left of a Unit](unit.build-percent-left-of.md): `unit.build-percent-left-of`, usually at 73.
- [Allied With a Unit's Owner](unit.allied-with.md): `unit.allied-with`, usually at 74.
- [Unit Played on This Machine](unit.is-local.md): `unit.is-local`, usually at 75.
<!-- END GENERATED: related -->
