# Unit Played on This Machine

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Extension id | `unit.is-local` |
| Mounted under | `script-extensions` → `get` |
| Usual index | 75: the list form mounts it there |
| Argument | A unit id: the first argument of `get`. |
| Returns | 1 when the owner of that unit's slot is a human or computer player on this machine, else 0. |
| Scope | sim: part of the profile's sim hash; every machine must mount it at the same index |
| Runs on | Every machine in the game. |
| Same answer on every machine | No, on purpose: each machine answers 1 only for the players it plays itself. |
| Fidelity | `exact`: any id up to `unit.max-id` answers for the player whose range holds its slot, whether or not a unit lives there. `safe`: 0 unless a live unit has the id. |
<!-- END GENERATED: facts -->

## Description

`unit.is-local` tells a unit script whether the player that owns a unit, named by its id, is played on this machine: 1 for the player at this machine and for the computer players this machine runs, otherwise 0. With [`unit.my-id`](unit.my-id.md) a script asks it about its own unit, to do something that only the owner's machine should do, such as showing a marker only its owner sees.

The answer differs from machine to machine on purpose. In a multiplayer game each machine answers 1 only for its own players. In a game against computer players on one machine, every player is played on this machine, so every unit answers 1.

In 3.1c, `get 75` reads 0, as `get` does at every index outside 1 to 20. Without a profile that mounts the extension at the index, this engine reads 0 there too.

## Syntax

```bos
#define MY_ID     71
#define IS_LOCAL  75             // the index the profile mounts unit.is-local at

me = get MY_ID;
here = get IS_LOCAL(me);         // 1 when this machine plays the unit's owner
```

`IS_LOCAL` is a name the script chooses for itself. The compiled script holds only the number, so the profile must mount the extension at the index the script was compiled with.

`get IS_LOCAL(me)` passes `me` as the first of the four arguments `get` carries; the ones a script leaves out are 0, and the extension reads only the first. Written without an argument, `get IS_LOCAL` reads id 0, which is never a unit, and answers 0 on every machine; a script asking about its own unit passes its own id.

## Usage

Show a marker on the unit only on the machine that plays its owner:

```bos
#define MY_ID     71
#define IS_LOCAL  75

piece base, marker;

Create()
{
	var me;
	hide marker;
	me = get MY_ID;
	if (get IS_LOCAL(me))
	{
		show marker;          // drawn only where the owner plays
	}
}
```

The branch only changes what this machine draws, so it is safe in a network game.

**What breaks a network game.** Everything else a unit script does must happen the same way on every machine. Never let this value decide anything the game itself depends on: in a branch on it, do not `move`, `turn` or `spin` a piece, `sleep`, start or call another script, `set` a value or `explode` a piece, and do not keep the answer where code that does reads it. Two machines would then play different games, and the game breaks. Keep a branch on this value to showing and hiding pieces nothing else depends on.

## Configuration example

```yaml
script-extensions:
  get: [unit.my-id, unit.is-local]     # mounted at 71 and 75
```

```yaml
script-extensions:
  get:
    41: unit.my-id                     # for scripts compiled to read 41 and 48
    48: unit.is-local
```

## Details

**Who counts as this machine's.** The answer is 1 when the unit's owner is the human player at this machine, or a computer player this machine runs. A player played on another machine, human or computer, answers 0, and so does a player place no one has taken.

**Ids.** The extension reads the low 16 bits of its argument, as 3.1c's own values that take a unit id do.

**Fidelity.** It reads ids as the profile's fidelity says:

- under `exact`, an id with no unit at the moment answers for the player whose range holds it, so an empty place in the range of a player this machine plays answers 1; id 0, and any id past [`unit.max-id`](unit.max-id.md) or past the table's last id, answers 0;
- under `safe`, only an id that names a live unit answers for that unit's owner; any other id answers 0.

**Network play.** The extension is mounted the same way on every machine, and the mount is part of the sim hash, but its answer differs between machines by design. That is safe only while the answer changes nothing but what one machine draws, as the warning above says. The other extensions answer alike on every machine, apart from the moment news of another machine's units takes to arrive; their pages say when.

### Implementation notes

- `unit_script_get_value` in `src/sim/unit-script/src/unit_value.cpp` answers 1 when the status of the owner (`Player.status`) of the slot the id names is local or computer.
- Tests:
  - `unit-script-extensions` (`is_local_reads_the_owner_status`: a local human, a computer player on an empty slot, another machine's player, an empty player place, id 0, past the table, and under `safe` fidelity a live unit and an empty slot; `a_script_walks_every_unit_id`: a walk in the interpreter that counts the ids this machine plays);
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
- [Build Percent Left of a Unit](unit.build-percent-left-of.md): `unit.build-percent-left-of`, usually at 73.
- [Allied With a Unit's Owner](unit.allied-with.md): `unit.allied-with`, usually at 74.
<!-- END GENERATED: related -->
