# Highest Unit Id

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Extension id | `unit.max-id` |
| Mounted under | `script-extensions` → `get` |
| Usual index | 70: the list form mounts it there |
| Argument | None. Arguments written after the index are ignored. |
| Returns | The unit limit the game recorded, times 10. A skirmish or multiplayer game records its own limit, so this is the last id of the unit table; a campaign mission records the player's Unit limit setting. |
| Scope | sim: part of the profile's sim hash; every machine must mount it at the same index |
| Runs on | Every machine in the game. |
| Same answer on every machine | Yes: every machine of a multiplayer game records the host's unit limit. |
| Fidelity | Not affected. Under `exact`, `unit.owner-of`, `unit.allied-with` and `unit.is-local` answer 0 when the low 16 bits of their argument name an id past it. |
<!-- END GENERATED: facts -->

## Description

`unit.max-id` gives a unit script the highest unit id of the game: the unit limit the game recorded, times 10. With [`unit.min-id`](unit.min-id.md) it bounds a walk over every unit id, so that a script can look at every unit in the game through the extensions that read another unit.

The unit table holds ten ranges of ids after id 0, one for each of the ten player places, whether or not ten players are playing: with a unit limit of L units per player, each range holds L ids and the last ends at 10L. [`unit.min-id`](unit.min-id.md) says which range is whose. In a skirmish or a multiplayer game, the limit the game records is the one it plays at (in a multiplayer game, the one the host chose), so the value is 10L, the last id of the table, and every machine reads the same.

In 3.1c, `get 70` reads 0, as `get` does at every index outside 1 to 20. Without a profile that mounts the extension at the index, this engine reads 0 there too.

## Syntax

```bos
#define MAX_ID 70          // the index the profile mounts unit.max-id at

last = get MAX_ID;         // 10 times the unit limit
```

`MAX_ID` is a name the script chooses for itself. The compiled script holds only the number, so the profile must mount the extension at the index the script was compiled with. The extension takes no argument; `get MAX_ID(0)` reads the same value.

## Usage

Count the live units in the game every five seconds, and show a warning light on this unit while there are more than 500:

```bos
#define UNIT_XZ  9        // 3.1c's own value: a live unit's position, else 0
#define MIN_ID   69
#define MAX_ID   70

piece base, light;

WatchCrowd()
{
	var id, last, found;
	while (1)
	{
		found = 0;
		id = get MIN_ID;
		last = get MAX_ID;
		while (id <= last)
		{
			if (get UNIT_XZ(id))
			{
				found = found + 1;
			}
			id = id + 1;
		}
		if (found > 500)
		{
			show light;
		}
		else
		{
			hide light;
		}
		sleep 5000;
	}
}

Create()
{
	hide light;
	start-script WatchCrowd();
}
```

The value does not change during a game, so a script may as well read it once in `Create`. `UNIT_XZ` (index 9) is one of 3.1c's own values: it reads 0 when no live unit has the id, so empty places in the table are not counted. [`unit.min-id`](unit.min-id.md) says how long a walk may take.

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

**Which limit.** The value is 10 times the unit limit the game recorded when it started:

- A skirmish, or a multiplayer game, records the limit it plays at: the value is the last id of the table.
- A campaign mission plays at its own unit limit but records the player's Unit limit setting. There the value can lie past the table's last id, and those ids read 0; or it can fall short of it, and then the ids past the value read 0 in `unit.owner-of`, `unit.allied-with` and `unit.is-local` under `exact` fidelity, though the table holds them.
- A loaded game records the limit its save holds.

**Fidelity.** Neither `exact` nor `safe` changes the value. Under `exact`, [`unit.owner-of`](unit.owner-of.md), [`unit.allied-with`](unit.allied-with.md) and [`unit.is-local`](unit.is-local.md) answer 0 for an id past it. Those three read only the low 16 bits of their argument, so they take an argument of 65,536 or more as the id its low 16 bits give: given 65,539, they read id 3, though 65,539 lies past this value. [`unit.build-percent-left-of`](unit.build-percent-left-of.md) is bounded by the table instead.

**Network play.** In a multiplayer game every machine records the host's unit limit, so every machine reads the same value. Each machine simulates its own players' units and sends the results to the others, so what happens to another machine's unit, such as its creation or its death, reaches a machine a moment after it happens on the owner's. So a walk that counts live units, as above, can count differently on two machines for a moment. The mount is part of the sim hash: every machine must mount the extension at the same index.

### Implementation notes

- `unit_script_get_value` in `src/sim/unit-script/src/unit_value.cpp` returns 10 times the game's recorded unit limit (`Game.max_units_setting`), or 10 times the limit the table was built for (`Game.units_per_player`) when nothing is recorded.
- The match records the limit as it starts (`Runtime::bootstrap_match` in `src/app/runtime_skirmish_start.cpp`); a loaded game takes its save's (`src/app/runtime_saveload.cpp`).
- Tests:
  - `unit-script-extensions` (`caller_values`: the value follows the recorded limit, and the table's limit when nothing is recorded; `owner_of_reads_the_slot`: ids past the value read 0);
  - `data-mod-profile` (refuses a profile that mounts it at the index `unit.min-id` already holds).

## Related

<!-- BEGIN GENERATED: related -->
- [The OAMOD standard, section 7](../oamod-standard.md#7-script-extensions): mounting, fidelity and the table of every extension.
- [Every script extension](../README.md#every-script-extension), in the mod support overview.
- [Kill Count Times 100](unit.kills-x100.md): `unit.kills-x100`, usually at 32.
- [Lowest Unit Id](unit.min-id.md): `unit.min-id`, usually at 69.
- [Own Unit Id](unit.my-id.md): `unit.my-id`, usually at 71.
- [Owner of a Unit](unit.owner-of.md): `unit.owner-of`, usually at 72.
- [Build Percent Left of a Unit](unit.build-percent-left-of.md): `unit.build-percent-left-of`, usually at 73.
- [Allied With a Unit's Owner](unit.allied-with.md): `unit.allied-with`, usually at 74.
- [Unit Played on This Machine](unit.is-local.md): `unit.is-local`, usually at 75.
<!-- END GENERATED: related -->
