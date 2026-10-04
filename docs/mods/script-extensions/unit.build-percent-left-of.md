# Build Percent Left of a Unit

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Extension id | `unit.build-percent-left-of` |
| Mounted under | `script-extensions` → `get` |
| Usual index | 73: the list form mounts it there |
| Argument | A unit id: the first argument of `get`. |
| Returns | That unit's `BUILD_PERCENT_LEFT`: 0 when it is finished, else 1 to 100. |
| Scope | sim: part of the profile's sim hash; every machine must mount it at the same index |
| Runs on | Every machine in the game. |
| Same answer on every machine | Not always: a unit's progress reaches the machines that do not play its owner a moment later, carried to within 1/255, so there the value can lag the owner's machine and, near a step, differ from it by one. That the unit is finished reaches every machine. |
| Fidelity | `exact`: the whole 32-bit argument chooses the record, whether or not a unit lives there. `safe`: 0 unless the argument is the id of a live unit. |
<!-- END GENERATED: facts -->

## Description

`unit.build-percent-left-of` gives a unit script how much of another unit, named by its id, is still to be built: 0 when the unit is finished, otherwise 1 to 100. It is 3.1c's own `BUILD_PERCENT_LEFT` (index 17), which a script reads only for its own unit, asked of any unit. A script uses it to wait for a unit to be finished, or to find units still being built.

In 3.1c, `get 73` reads 0, as `get` does at every index outside 1 to 20. Without a profile that mounts the extension at the index, this engine reads 0 there too.

## Syntax

```bos
#define BUILD_PERCENT_LEFT_OF 73          // the index the profile mounts it at

left = get BUILD_PERCENT_LEFT_OF(id);     // 0 when finished, else 1 to 100
```

`BUILD_PERCENT_LEFT_OF` is a name the script chooses for itself. The compiled script holds only the number, so the profile must mount the extension at the index the script was compiled with.

`get BUILD_PERCENT_LEFT_OF(id)` passes `id` as the first of the four arguments `get` carries; the ones a script leaves out are 0, and the extension reads only the first. Written without an argument, it reads id 0, which is never a unit, and answers 0.

## Usage

Wait for a unit to be finished, with a lamp lit while it is built. The script has the unit's id already, from a walk over the ids for example, and starts the wait with `start-script WaitForBuild(id);`:

```bos
#define UNIT_XZ                9     // 3.1c's own value: a live unit's position, else 0
#define BUILD_PERCENT_LEFT_OF  73

piece base, lamp;

WaitForBuild(target)
{
	var left, waited;
	show lamp;
	waited = 0;
	left = get BUILD_PERCENT_LEFT_OF(target);
	while (left > 0)
	{
		sleep 500;
		waited = waited + 500;
		left = get BUILD_PERCENT_LEFT_OF(target);
		if ((get UNIT_XZ(target)) == 0)
		{
			left = 0;          // destroyed before it was finished
		}
		if (waited >= 60000)
		{
			left = 0;          // give up after a minute
		}
	}
	hide lamp;
}
```

Light the lamp while any unit of this unit's own player is being built:

```bos
#define UNIT_XZ                9
#define MIN_ID                 69
#define MAX_ID                 70
#define MY_ID                  71
#define OWNER_OF               72
#define BUILD_PERCENT_LEFT_OF  73

piece base, lamp;

WatchBuilds()
{
	var id, last, mine, building;
	id = get MY_ID;
	mine = get OWNER_OF(id);
	while (1)
	{
		building = 0;
		id = get MIN_ID;
		last = get MAX_ID;
		while (id <= last)
		{
			if ((get OWNER_OF(id)) == mine)
			{
				if (get BUILD_PERCENT_LEFT_OF(id))
				{
					if (get UNIT_XZ(id))
					{
						building = 1;
					}
				}
			}
			id = id + 1;
		}
		if (building)
		{
			show lamp;
		}
		else
		{
			hide lamp;
		}
		sleep 2000;
	}
}

Create()
{
	hide lamp;
	start-script WatchBuilds();
}
```

Both scripts ask 3.1c's own `UNIT_XZ`, which reads 0 when no live unit has the id, because under `exact` fidelity a destroyed unit's place keeps the percentage it had when it died.

## Configuration example

```yaml
script-extensions:
  get: [unit.build-percent-left-of]     # mounted at 73
```

```yaml
script-extensions:
  get:
    46: unit.build-percent-left-of      # for scripts compiled to read index 46
```

## Details

**The value.** 0 when the unit is finished. Otherwise 1 plus 99 times the part still to build, rounded down: 100 for a unit just begun, 50 for one half built, 1 for one nearly done. This is how 3.1c works out `BUILD_PERCENT_LEFT`.

**Under `exact` fidelity** (the default) the extension reads the unit table's place for the argument whether or not a unit lives there:

- a place no unit has used yet reads 0, and a destroyed unit's place keeps what it held when it died until a new unit takes it;
- the argument counts in full, not only its low 16 bits as the other extensions read it, and is taken modulo 536,870,912 (2 to the power 29): `get BUILD_PERCENT_LEFT_OF(3 + 536870912)` reads id 3, while `get BUILD_PERCENT_LEFT_OF(65539)`, id 3 with bit 16 set, reads 0;
- the table's last id reads 0, even while a unit being built has it, and so does any argument that lies past it after the modulo, such as −1;
- the read is bounded by the table, not by [`unit.max-id`](unit.max-id.md), which matters only in a campaign mission where the two differ.

**Under `safe` fidelity** the argument must be exactly the id of a live unit, other than the table's last id; anything else reads 0.

**Network play.** In a multiplayer game each machine builds its own players' units and sends their progress to the others, carried to within 1/255 of the whole. On the machines that do not play a unit's owner, the percentage therefore changes a moment after it does on the owner's machine, and near a step from one value to the next it can differ from the owner's by one; that the unit is finished reaches every machine as 0. A wait for 0, as above, ends on every machine, though not in the same tick. The mount and the fidelity are part of the sim hash: every machine must mount the extension at the same index and play the same fidelity.

### Implementation notes

- `unit_script_get_value` in `src/sim/unit-script/src/unit_value.cpp` works the record out from the argument times the 280-byte size of a unit record, wrapped to 32 bits, and returns its `BUILD_PERCENT_LEFT` from `Unit.build_remaining`, as the base value 17 does for the calling unit.
- Tests:
  - `unit-script-extensions` (`build_percent_left_of_reads_another_unit`: finished and half-built units, a free slot's leftover value, the table's last id, −1, the wrapped argument, and `safe` fidelity);
  - `unit-script-extensions-mod-install` (an installed mod's own scripts read every mounted extension as the table says; it skips without a mod installation).

## Related

<!-- BEGIN GENERATED: related -->
- [The OAMOD standard, section 7](../oamod-standard.md#7-script-extensions): mounting, fidelity and the table of every extension.
- [Every script extension](../README.md#every-script-extension), in the mod support overview.
- [Kill Count Times 100](unit.kills-x100.md): `unit.kills-x100`, usually at 32.
- [Lowest Unit Id](unit.min-id.md): `unit.min-id`, usually at 69.
- [Highest Unit Id](unit.max-id.md): `unit.max-id`, usually at 70.
- [Own Unit Id](unit.my-id.md): `unit.my-id`, usually at 71.
- [Owner of a Unit](unit.owner-of.md): `unit.owner-of`, usually at 72.
- [Allied With a Unit's Owner](unit.allied-with.md): `unit.allied-with`, usually at 74.
- [Unit Played on This Machine](unit.is-local.md): `unit.is-local`, usually at 75.
<!-- END GENERATED: related -->
