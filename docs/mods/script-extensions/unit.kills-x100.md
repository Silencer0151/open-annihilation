# Kill Count Times 100

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Extension id | `unit.kills-x100` |
| Mounted under | `script-extensions` → `get` |
| Usual index | 32: the list form mounts it there |
| Argument | None. Arguments written after the index are ignored. |
| Returns | The calling unit's kill count times 100 (a count, not a veterancy level). |
| Scope | sim: part of the profile's sim hash; every machine must mount it at the same index |
| Runs on | Every machine in the game. |
| Same answer on every machine | Yes, once a death has reached every machine: each machine counts the kill as it learns of the death, so for a moment the count can differ between machines. |
| Fidelity | Not affected: it reads only the calling unit. |
<!-- END GENERATED: facts -->

## Description

`unit.kills-x100` gives a unit script its own unit's kill count, multiplied by 100. A unit's kill count goes up by one each time it destroys a finished unit of another player; it is the count veterancy is worked out from. A script can use it to show a unit's experience on its model, such as stripes or insignia that appear as the kills mount up.

The value is a count, not a veterancy level: a unit with 3 kills reads 300, whatever level those kills give under [`veterancy.model`](../standard-hacks/veterancy.model.md).

In 3.1c, `get 32` reads 0, as `get` does at every index outside 1 to 20. Without a profile that mounts the extension at the index, this engine reads 0 there too.

## Syntax

```bos
#define KILLS_X100 32     // the index the profile mounts unit.kills-x100 at

kills = get KILLS_X100;   // 100 for each kill
```

`KILLS_X100` is a name the script chooses for itself. The compiled script holds only the number, so the profile must mount the extension at the index the script was compiled with. The extension takes no argument; `get KILLS_X100(0)` reads the same value.

## Usage

Show a stripe at 5, 10 and 20 kills, checking once a second:

```bos
#define KILLS_X100 32

piece base, stripe1, stripe2, stripe3;

ShowStripes()
{
	var kills;
	while (1)
	{
		kills = get KILLS_X100;
		if (kills >= 500)
		{
			show stripe1;
		}
		if (kills >= 1000)
		{
			show stripe2;
		}
		if (kills >= 2000)
		{
			show stripe3;
		}
		sleep 1000;
	}
}

Create()
{
	hide stripe1;
	hide stripe2;
	hide stripe3;
	start-script ShowStripes();
}
```

The value only grows while the unit lives, so the script never hides a stripe again. Each machine counts a kill as it learns of the death, so every machine shows the same stripes, some a moment later.

## Configuration example

```yaml
script-extensions:
  get: [unit.kills-x100]     # mounted at 32
```

```yaml
script-extensions:
  get:
    44: unit.kills-x100      # for scripts compiled to read index 44
```

## Details

**The value.** The kill count is a 16-bit count, so the value runs from 0 to 6,553,500 in steps of 100. A new unit starts at 0. The count is kept in saved games, so the value carries over a save and load.

**Fidelity.** Neither `exact` nor `safe` changes it: it reads only the calling unit.

**Veterancy.** [`veterancy.model`](../standard-hacks/veterancy.model.md) changes how levels follow from the kill count, not the count itself, so this value is the same with the hack on or off. A script that wants a level works it out from the count by the mod's own rule.

**Network play.** Kill counts update on every machine from the death of a unit. A death on another machine reaches a machine a moment after it happens there, so for that moment the killer's count, and this value, can differ between machines; then every machine reads the same. The mount is part of the sim hash: every machine must mount the extension at the same index.

### Implementation notes

- `unit_script_get_value` in `src/sim/unit-script/src/unit_value.cpp` returns the calling unit's kill count (`Unit.veteran_level`) times 100.
- Tests:
  - `unit-script-extensions` (`caller_values`: the value at 7 and at 65,535 kills);
  - `data-mod-profile` (the list form mounts it at 32).

## Related

<!-- BEGIN GENERATED: related -->
- [The OAMOD standard, section 7](../oamod-standard.md#7-script-extensions): mounting, fidelity and the table of every extension.
- [Every script extension](../README.md#every-script-extension), in the mod support overview.
- [Lowest Unit Id](unit.min-id.md): `unit.min-id`, usually at 69.
- [Highest Unit Id](unit.max-id.md): `unit.max-id`, usually at 70.
- [Own Unit Id](unit.my-id.md): `unit.my-id`, usually at 71.
- [Owner of a Unit](unit.owner-of.md): `unit.owner-of`, usually at 72.
- [Build Percent Left of a Unit](unit.build-percent-left-of.md): `unit.build-percent-left-of`, usually at 73.
- [Allied With a Unit's Owner](unit.allied-with.md): `unit.allied-with`, usually at 74.
- [Unit Played on This Machine](unit.is-local.md): `unit.is-local`, usually at 75.
<!-- END GENERATED: related -->
