# Float precision

The precision each double operation rounds to. Target
`oa-base-float-precision`, header `oa/base/float_precision.hpp`, namespace
`oa::base::float_precision`.

The simulation rounds each double operation to a 53-bit significand
([docs/conventions.md](../../../docs/development/conventions.md)). Every processor the
engine runs on does so, except a 32-bit x86 build whose double arithmetic
runs on the processor's older floating-point unit: that unit keeps each
result to the precision it is set to, and some C libraries start a program
at 64 bits. On 32-bit x86 `use_double_precision` sets that unit's precision
to a double's, and leaves its rounding and exception settings as they are;
elsewhere it does nothing.

- Every executable that links `oa-options` takes this library whole
  (`cmake/OaOptions.cmake`), and its start-up hook calls
  `use_double_precision` before any static is initialised and before
  `main()`.
- Each thread started through `oa/base/threads.hpp` calls it first.
- `rounds_to_double` reports the calling thread's setting.

`base-float-precision` checks, on the first thread and on a started thread,
that the setting is a double's and that 1 + 2^-53 + 2^-53 rounds each step
to a double and gives 1.
