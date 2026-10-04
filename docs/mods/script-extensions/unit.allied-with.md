# Allied With a Unit's Owner

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Extension id | `unit.allied-with` |
| Mounted under | `script-extensions` → `get` |
| Usual index | 74: the list form mounts it there |
| Argument | A unit id: the first argument of `get`. |
| Returns | 1 when the calling unit's owner has allied the owner of that unit's slot, else 0. Alliance is one-way: the target's owner need not have allied back. |
| Scope | sim: part of the profile's sim hash; every machine must mount it at the same index |
| Runs on | Every machine in the game. |
| Same answer on every machine | Yes, once an alliance change has reached every machine: it takes effect at once on the machine of the player who makes it, and a moment later on the others. Under `safe`, another machine's units also answer from when their creation reaches this machine. |
| Fidelity | `exact`: any id up to `unit.max-id` answers from its slot, whether or not a unit lives there. `safe`: 0 unless a live unit has the id. |
<!-- END GENERATED: facts -->

## Description

`unit.allied-with` tells a unit script whether its own player has allied the player that owns another unit, named by its id: 1 when it has, otherwise 0. Every player counts as allied with itself, so the unit's own player's units answer 1. A script walks the ids from [`unit.min-id`](unit.min-id.md) to [`unit.max-id`](unit.max-id.md) with it to find friendly units, for effects that help allies or spare them.

Alliance here is one-way, as each player sets it in the game's alliance menu: the answer is 1 as soon as the calling unit's player has allied the other player, whether or not the other player has allied back.

In 3.1c, `get 74` reads 0, as `get` does at every index outside 1 to 20. Without a profile that mounts the extension at the index, this engine reads 0 there too.

## Syntax

```bos
#define ALLIED_WITH 74            // the index the profile mounts unit.allied-with at

friendly = get ALLIED_WITH(id);   // 1 when this unit's player has allied the owner of id
```

`ALLIED_WITH` is a name the script chooses for itself. The compiled script holds only the number, so the profile must mount the extension at the index the script was compiled with.

`get ALLIED_WITH(id)` passes `id` as the first of the four arguments `get` carries; the ones a script leaves out are 0, and the extension reads only the first. Written without an argument, it reads id 0, which is never a unit, and answers 0.

## Usage

Count the live units of other players this unit's player has allied, every two seconds:

```bos
#define UNIT_XZ      9      // 3.1c's own value: a live unit's position, else 0
#define MIN_ID       69
#define MAX_ID       70
#define MY_ID        71
#define OWNER_OF     72
#define ALLIED_WITH  74

static-var AlliedUnits;

CountAlliedUnits()
{
	var id, last, mine;
	id = get MY_ID;
	mine = get OWNER_OF(id);
	AlliedUnits = 0;
	id = get MIN_ID;
	last = get MAX_ID;
	while (id <= last)
	{
		if (get ALLIED_WITH(id))
		{
			if ((get OWNER_OF(id)) != mine)
			{
				if (get UNIT_XZ(id))
				{
					AlliedUnits = AlliedUnits + 1;
				}
			}
		}
		id = id + 1;
	}
}

WatchAllies()
{
	while (1)
	{
		call-script CountAlliedUnits();
		sleep 2000;
	}
}

Create()
{
	start-script WatchAllies();
}
```

The `OWNER_OF` test leaves out the unit's own player, which always counts as allied. Under `exact` fidelity every id of an allied player's range answers 1, whether a unit lives there or not, so the walk also asks 3.1c's own `UNIT_XZ` (index 9), which reads 0 for an id without a live unit. The count may differ between machines for a moment, as the network notes below say.

## Configuration example

```yaml
script-extensions:
  get: [unit.min-id, unit.max-id, unit.my-id, unit.owner-of, unit.allied-with]   # mounted at 69 to 72 and 74
```

```yaml
script-extensions:
  get:
    40: unit.min-id                # for scripts compiled to read 40 to 44
    41: unit.max-id
    42: unit.my-id
    43: unit.owner-of
    44: unit.allied-with
```

## Details

**The answer** follows the alliances as they stand when the script asks: when a player allies or stops allying another during the game, the next read gives the new answer.

**Ids.** The extension reads the low 16 bits of its argument, as 3.1c's own values that take a unit id do.

**Under `exact` fidelity** (the default) any id from 0 to [`unit.max-id`](unit.max-id.md) answers for the player whose range holds it, whether or not a unit lives there: an empty place in an allied player's range answers 1. Id 0 belongs to no player and answers 0, and so does an id past `unit.max-id` or past the table's last id. In a multiplayer game the range of a player place nobody took belongs to no player either, and its ids answer 0; in a skirmish or a campaign mission every range belongs to its place, taken or not (see [`unit.min-id`](unit.min-id.md) for which range is whose).

**Under `safe` fidelity** an id answers only while a live unit has it; every other id answers 0.

**Network play.** A player's alliance change takes effect at once on that player's machine and a moment later on the others, which learn of it over the network; until then they answer as before. In a multiplayer game each machine simulates its own players' units and sends the results to the others, so what happens to another machine's unit, such as its creation or its death, reaches a machine a moment after it happens on the owner's. So a walk like the one above can count differently on two machines for a moment. The mount and the fidelity are part of the sim hash: every machine must mount the extension at the same index and play the same fidelity.

### Implementation notes

- `unit_script_get_value` in `src/sim/unit-script/src/unit_value.cpp` reads the calling unit's player's alliance with the player (`Unit.owner_index`) of the slot the id names (`Player.alliance`).
- Tests:
  - `unit-script-extensions` (`allied_with_is_one_way`: one-way alliance, own units, an empty place, id 0, past the table, and `safe` fidelity; `a_script_walks_every_unit_id`: a walk in the interpreter that counts allied ids);
  - `unit-script-extensions-mod-install` (an installed mod's own scripts read every mounted extension as the table says, and those that walk every id finish each walk within a tick at 1,500 units per player; it skips without a mod installation).

## Related

<!-- BEGIN GENERATED: related -->
- [The OAMOD standard, section 7](../oamod-standard.md#7-script-extensions): mounting, fidelity and the table of every extension.
- [Every script extension](../README.md#every-script-extension), in the mod support overview.
- [Kill Count Times 100](unit.kills-x100.md): `unit.kills-x100`, usually at 32.
- [Lowest Unit Id](unit.min-id.md): `unit.min-id`, usually at 69.
- [Highest Unit Id](unit.max-id.md): `unit.max-id`, usually at 70.
- [Own Unit Id](unit.my-id.md): `unit.my-id`, usually at 71.
- [Owner of a Unit](unit.owner-of.md): `unit.owner-of`, usually at 72.
- [Build Percent Left of a Unit](unit.build-percent-left-of.md): `unit.build-percent-left-of`, usually at 73.
- [Unit Played on This Machine](unit.is-local.md): `unit.is-local`, usually at 75.
<!-- END GENERATED: related -->
