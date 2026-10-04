# Owner of a Unit

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Extension id | `unit.owner-of` |
| Mounted under | `script-extensions` → `get` |
| Usual index | 72: the list form mounts it there |
| Argument | A unit id: the first argument of `get`. |
| Returns | The number of the player that owns that unit's slot, 0 for the first player to 9 for the tenth. Under `exact`, id 0 answers 255, and in a multiplayer game an id in the range of a player place nobody took answers 10. |
| Scope | sim: part of the profile's sim hash; every machine must mount it at the same index |
| Runs on | Every machine in the game. |
| Same answer on every machine | Under `exact`, yes: an id's player never changes. Under `safe`, a unit of another machine's player answers from when its creation reaches this machine, a moment after its owner's machine, until its death does. |
| Fidelity | `exact`: any id up to `unit.max-id` answers from its slot, whether or not a unit lives there. `safe`: 0 unless a live unit has the id. |
<!-- END GENERATED: facts -->

## Description

`unit.owner-of` gives a unit script the player that owns another unit, named by its id: the player's number, 0 for the first player to 9 for the tenth. With [`unit.my-id`](unit.my-id.md) a script learns its own player, and with a walk from [`unit.min-id`](unit.min-id.md) to [`unit.max-id`](unit.max-id.md) it can pick out the units of one player.

Each player's units take their ids from a range of their own (see [`unit.min-id`](unit.min-id.md) for which range is whose), so the answer for an id is the player whose range holds it. Under the default `exact` fidelity that is also the answer for an id that holds no unit at the moment; under `safe` fidelity such an id reads 0.

Under `exact` some ids answer outside 0 to 9: id 0, which is never a unit, answers 255, and in a multiplayer game the ids in the range of a player place nobody took answer 10. Neither is any player's number, so a test such as `== mine` is never true for them.

In 3.1c, `get 72` reads 0, as `get` does at every index outside 1 to 20. Without a profile that mounts the extension at the index, this engine reads 0 there too.

## Syntax

```bos
#define OWNER_OF 72              // the index the profile mounts unit.owner-of at

owner = get OWNER_OF(id);        // the player that owns the unit with that id
```

`OWNER_OF` is a name the script chooses for itself. The compiled script holds only the number, so the profile must mount the extension at the index the script was compiled with.

`get OWNER_OF(id)` passes `id` as the first of the four arguments `get` carries; the ones a script leaves out are 0, and the extension reads only the first. Written without an argument, `get OWNER_OF` reads id 0, which is never a unit: it answers 255 under `exact` fidelity and 0 under `safe`.

## Usage

Count the live units of this unit's own player, every three seconds:

```bos
#define UNIT_XZ   9      // 3.1c's own value: a live unit's position, else 0
#define MIN_ID    69
#define MAX_ID    70
#define MY_ID     71
#define OWNER_OF  72

static-var OwnUnits;

CountOwnUnits()
{
	var id, last, mine;
	id = get MY_ID;
	mine = get OWNER_OF(id);
	while (1)
	{
		OwnUnits = 0;
		id = get MIN_ID;
		last = get MAX_ID;
		while (id <= last)
		{
			if ((get OWNER_OF(id)) == mine)
			{
				if (get UNIT_XZ(id))
				{
					OwnUnits = OwnUnits + 1;
				}
			}
			id = id + 1;
		}
		sleep 3000;
	}
}

Create()
{
	start-script CountOwnUnits();
}
```

Under `exact` fidelity every id of the player's range answers the player, whether a unit lives there or not, so the walk asks 3.1c's own `UNIT_XZ` (index 9), which reads 0 for an id without a live unit, before it counts one.

## Configuration example

```yaml
script-extensions:
  get: [unit.min-id, unit.max-id, unit.my-id, unit.owner-of]   # mounted at 69, 70, 71 and 72
```

```yaml
script-extensions:
  fidelity: safe
  get:
    40: unit.my-id                   # for scripts compiled to read 40 and 41
    41: unit.owner-of
```

## Details

**Ids.** The extension reads the low 16 bits of its argument: `get OWNER_OF(65539)` reads id 3, as 3.1c's own values that take a unit id do.

**Under `exact` fidelity** (the default) any id from 0 to [`unit.max-id`](unit.max-id.md) answers from its place in the table, whether or not a unit lives there:

- an id with no unit at the moment answers the player whose range holds it;
- in a skirmish or a campaign mission every one of the ten ranges answers its place's number, 0 to 9, whether or not a player took the place;
- in a multiplayer game the range of a player place nobody took answers 10;
- id 0 answers 255;
- an id past `unit.max-id`, or past the table's last id, answers 0.

**Under `safe` fidelity** an id answers only while a live unit has it, and every other id answers 0, the same as a unit of the first player. A script that must tell the first player from no unit at all checks first that a unit lives there, as the example does with `UNIT_XZ`.

**A unit's player never changes.** A unit keeps its owner for as long as it lives, so a script may read its own player once, as the example does.

**Network play.** Under `exact` fidelity the answer for an id never changes during a game, so every machine reads the same. Under `safe` an id answers only while this machine knows a live unit has it. In a multiplayer game each machine simulates its own players' units and sends the results to the others, so what happens to another machine's unit, such as its creation or its death, reaches a machine a moment after it happens on the owner's. So under `safe` two machines can answer differently for a moment. The mount and the fidelity are part of the sim hash: every machine must mount the extension at the same index and play the same fidelity.

### Implementation notes

- `unit_script_get_value` in `src/sim/unit-script/src/unit_value.cpp` returns the player number (`Unit.owner_index`) of the slot the id names.
- Tests:
  - `unit-script-extensions` (`owner_of_reads_the_slot`: the low 16 bits, empty slots, id 0, ids past the table and past `unit.max-id`, and `safe` fidelity);
  - `unit-script-extensions-mod-install` (an installed mod's own scripts read every mounted extension as the table says; it skips without a mod installation).

## Related

<!-- BEGIN GENERATED: related -->
- [The OAMOD standard, section 7](../oamod-standard.md#7-script-extensions): mounting, fidelity and the table of every extension.
- [Every script extension](../README.md#every-script-extension), in the mod support overview.
- [Kill Count Times 100](unit.kills-x100.md): `unit.kills-x100`, usually at 32.
- [Lowest Unit Id](unit.min-id.md): `unit.min-id`, usually at 69.
- [Highest Unit Id](unit.max-id.md): `unit.max-id`, usually at 70.
- [Own Unit Id](unit.my-id.md): `unit.my-id`, usually at 71.
- [Build Percent Left of a Unit](unit.build-percent-left-of.md): `unit.build-percent-left-of`, usually at 73.
- [Allied With a Unit's Owner](unit.allied-with.md): `unit.allied-with`, usually at 74.
- [Unit Played on This Machine](unit.is-local.md): `unit.is-local`, usually at 75.
<!-- END GENERATED: related -->
