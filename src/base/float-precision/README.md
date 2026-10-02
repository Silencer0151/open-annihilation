# Float precision

The precision each double operation rounds to, and the processor's other
floating-point settings, which the game keeps as they were when it started.
Target `oa-base-float-precision`, header `oa/base/float_precision.hpp`,
namespace `oa::base::float_precision`.

## Double precision

The simulation rounds each double operation to a 53-bit significand
([docs/conventions.md](../../../docs/development/conventions.md)). Every processor the
engine runs on does so, except a 32-bit x86 build whose double arithmetic
runs on the processor's older floating-point unit: that unit keeps each
result to the precision it is set to, and some C libraries start a program
at 64 bits. On 32-bit x86 `use_double_precision` sets that unit's precision
to a double's, and leaves its rounding and exception settings as they are;
elsewhere it does nothing.

- Every 32-bit x86 executable that links `oa-options` takes this library
  whole (`cmake/OaOptions.cmake`), and its start-up hook calls
  `use_double_precision` before any static is initialised and before
  `main()`.
- Each thread started through `oa/base/threads.hpp` calls it first.
- `rounds_to_double` reports the calling thread's setting.

## The settings kept

Each processor also keeps settings that change what an operation gives: a
rounding mode, and on most a flush-to-zero setting. The simulation's
results, and so saved and recorded games, depend on them, and code the game
does not control, such as a graphics driver, may change them on the game's
thread. `FloatControl` holds the calling thread's settings, one field per
unit, each 0 where the build has no such unit:

| Field | Unit | Settings |
| --- | --- | --- |
| `older_unit` | 32-bit x86's older unit | precision, rounding mode, exception masks |
| `vector_unit` | SSE, where the build computes with it: x86-64, and a 32-bit x86 build that targets it (`OA_X86_FLOAT=sse`, the default); not an `OA_X86_FLOAT=fpu` build | rounding mode, flush-to-zero, denormals-are-zero, exception masks |
| `arm_unit` | ARM64's unit, and 32-bit ARM's where it has one (the armhf packages) | rounding mode, flush-to-zero, default-NaN, exception enables |

The flags that only record what operations have raised are left out: they
are never compared, and putting the settings back leaves them as they are.
On 32-bit x86 the older unit is read and set through its own control word,
so its field holds that unit's settings alone, and setting it leaves SSE's
settings and flags as they are.

- `save_float_control` reads the settings, `float_control_matches` compares
  them with saved ones and `restore_float_control` sets them again.
- `restore_changed_float_control` does both for a `FloatControlGuard`: when
  any setting, or the double precision, differs from the guard's, it puts
  them back, returns true and calls the guard's `FloatControlHooks::changed`
  the first time only.
- `program_float_control` is the guard of the program's first thread,
  saved before `main()` runs, after `use_double_precision`.
  `restore_program_float_control` checks against it.

The game sets the program guard's hook to log the first change of the run,
and checks the settings after it creates a renderer, after the intro movies,
which present on its renderer, after each present, at the start of each
match frame's drawing, which rebuilds piece placements
the match reads, and before the simulation ticks. Each check costs a few
instructions and changes nothing while the settings are as they started.

## Tests

`base-float-precision` checks, on the first thread and on a started thread,
that the precision is a double's and that 1 + 2^-53 + 2^-53 rounds each step
to a double and gives 1. For each unit the build keeps, guarded per
architecture, it changes the rounding mode (and the precision on 32-bit x86,
flush-to-zero on SSE and ARM, denormals-are-zero on x86-64), and checks that
the change is found, that the sums 1 + 2^-60 and 1 + 2^-30 round upward
while it holds, and that the settings and the rounding to nearest come back;
that the older unit's and SSE's rounding each show in their own field only;
that a raised flag, on SSE and on 32-bit ARM, counts as no change and is
kept; that a guard reports the first change only; and that the program
guard matches the settings it started with. The wider precision is checked
in a result only where the compiler keeps a double expression's
intermediate results on the older unit (`FLT_EVAL_METHOD` 2).
