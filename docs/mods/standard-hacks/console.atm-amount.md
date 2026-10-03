# ATM Amount

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `console.atm-amount` |
| Area | Console (`console`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Sets how much metal and energy the `+ATM` cheat gives. The default fills any storage at once; 3.1c gives 1000 of each.

## Configuration example

```yaml
hacks:
  console.atm-amount: true           # about 4.3 x 10^12: fills storage
```

```yaml
hacks:
  console.atm-amount: 5000           # +ATM gives 5000 metal and 5000 energy
```

## Details

### Behaviour

`+ATM` adds `amount` to both the metal and the energy of the player being viewed. The amount is held as a single-precision number, and each sum is single precision too, so a value that single precision cannot hold exactly is rounded (for example 1 plus a very small fraction becomes 1).

The default, 4294967296000, is far above any storage. The economy then clamps both resources to the player's storage, so `+ATM` leaves both full.

With the hack off, the amount is 1000 whatever the parameter says.

`+ATM` stays a cheat: it runs only when cheats are allowed.

### 3.1c baseline

`+ATM` adds 1000 metal and 1000 energy.

### Network games

`+ATM` is a cheat line that is sent to every player and run on every machine, each with its own console. Every machine must use the same amount; the hack is part of the profile hash.

### Interactions

None.

### Implementation notes

- Applied in `console_apply_rules` in `src/ui/console/src/commands.cpp` (module `src/ui/console`), which reads `MatchRules::console.atm_amount` and stores it in `Console::atm_amount`; the command itself is `add_atm_resources` in the same file.
- Tested by `test_mod_console_rules` in `src/ui/console/tests/console_test.cpp` (ctest `ui-console`): the default amount, the single-precision rounding, a small amount, and 1000 with the hack off. The application's console checks hold under any profile: `src/app/runtime_console_cheats.cpp` reads the amount from the console, and the network game's check in `src/app/netgame/runtime_session_cheats.cpp` takes each machine's amount from its own profile's rules through `console_atm_amount` in `src/ui/console/src/commands.cpp`, from which `console_apply_rules` also sets `Console::atm_amount`.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `console.atm-amount`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `console.atm-amount: true` | On, every parameter at its default. |
| `console.atm-amount: {amount: 4294967296000}` | On, the parameters named set and the rest at their defaults. |
| `console.atm-amount: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `console.atm-amount: 4294967296000` | On, the shorthand: a bare value sets `amount`. |
| `console.atm-amount: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `amount` | `decimal` | metal and energy | `0` to `1000000000000000.0` | - | `1000` | `4294967296000` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{amount: 1000}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `amount`: `console.atm-amount: 4294967296000` is `console.atm-amount: {amount: 4294967296000}`.

### Every parameter at its default

```yaml
hacks:
  console.atm-amount:
    amount: 4294967296000
```
<!-- END GENERATED: schema -->
