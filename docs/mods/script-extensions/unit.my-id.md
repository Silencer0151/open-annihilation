# Own Unit Id

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Extension id | `unit.my-id` |
| Mounted under | `script-extensions` → `get` |
| Usual index | 71: the list form mounts it there |
| Argument | None. Arguments written after the index are ignored. |
| Returns | The calling unit's own id. |
| Scope | sim: part of the profile's sim hash; every machine must mount it at the same index |
| Runs on | Every machine in the game. |
| Same answer on every machine | Yes: every machine gives a unit the same id. |
| Fidelity | Not affected: it reads only the calling unit. |
<!-- END GENERATED: facts -->

## Description

`unit.my-id` gives a unit script its own unit's id: the unit's place in the game's unit table, from 1 to the table's last id (see [`unit.max-id`](unit.max-id.md)). It is the same id the game passes to a transport's `TransportPickup` and `TransportDrop` scripts for the unit it carries.

A script needs its own id to ask the extensions that read a unit by id about itself, such as [`unit.owner-of`](unit.owner-of.md) for its own player or [`unit.is-local`](unit.is-local.md) for whether this machine plays it, and to leave itself out of a walk over every unit id.

In 3.1c, `get 71` reads 0, as `get` does at every index outside 1 to 20. Without a profile that mounts the extension at the index, this engine reads 0 there too.

## Syntax

```bos
#define MY_ID 71           // the index the profile mounts unit.my-id at

me = get MY_ID;            // this unit's id
```

`MY_ID` is a name the script chooses for itself. The compiled script holds only the number, so the profile must mount the extension at the index the script was compiled with. The extension takes no argument; `get MY_ID(0)` reads the same value.

## Usage

Remember the unit's own player when it is created:

```bos
#define MY_ID     71
#define OWNER_OF  72

static-var Me, MyPlayer;

Create()
{
	Me = get MY_ID;
	MyPlayer = get OWNER_OF(Me);    // 0 for the first player to 9 for the tenth
}
```

A unit keeps its id and its player for as long as it lives, so reading them once is enough. A walk over every unit id leaves the unit itself out with `if (id != Me)`; [`unit.allied-with`](unit.allied-with.md) shows one.

## Configuration example

```yaml
script-extensions:
  get: [unit.my-id, unit.owner-of]     # mounted at 71 and 72
```

```yaml
script-extensions:
  get:
    40: unit.my-id                     # for scripts compiled to read 40 and 41
    41: unit.owner-of
```

## Details

**The value.** From 1 to the table's last id; never 0. It lies in the range of ids of the unit's player (see [`unit.min-id`](unit.min-id.md)).

**Reused ids.** Once the unit dies, its id is free, and a unit built later may be given the same id. A script that keeps another unit's id should not take that id to mean the same unit for ever. [`units.id-reuse-delay`](../standard-hacks/units.id-reuse-delay.md) holds a dead unit's id back for a while before it is given out again.

**Fidelity.** Neither `exact` nor `safe` changes it: it reads only the calling unit.

**Network play.** Every machine gives a unit the same id, so every machine reads the same value. The mount is part of the sim hash: every machine must mount the extension at the same index.

### Implementation notes

- `unit_script_get_value` in `src/sim/unit-script/src/unit_value.cpp` returns the calling unit's id (`Unit.id`).
- Tests:
  - `unit-script-extensions` (`caller_values`; `extensions_mount_at_any_index`: indices 33, 45 and 60,000, and an index nothing is mounted at);
  - `data-mod-profile` (list form at 71; mapping form at 33 under `safe` fidelity).

## Related

<!-- BEGIN GENERATED: related -->
- [The OAMOD standard, section 7](../oamod-standard.md#7-script-extensions): mounting, fidelity and the table of every extension.
- [Every script extension](../README.md#every-script-extension), in the mod support overview.
- [Kill Count Times 100](unit.kills-x100.md): `unit.kills-x100`, usually at 32.
- [Lowest Unit Id](unit.min-id.md): `unit.min-id`, usually at 69.
- [Highest Unit Id](unit.max-id.md): `unit.max-id`, usually at 70.
- [Owner of a Unit](unit.owner-of.md): `unit.owner-of`, usually at 72.
- [Build Percent Left of a Unit](unit.build-percent-left-of.md): `unit.build-percent-left-of`, usually at 73.
- [Allied With a Unit's Owner](unit.allied-with.md): `unit.allied-with`, usually at 74.
- [Unit Played on This Machine](unit.is-local.md): `unit.is-local`, usually at 75.
<!-- END GENERATED: related -->
