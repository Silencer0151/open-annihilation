# Steady clock

The C++ library's steady clock on the performance counter, for MinGW builds.
Target `oa-platform-steady-clock`, built only with MinGW. It has no header:
programs keep calling `std::chrono::steady_clock`.

A C++ run-time library built without a monotonic clock reads its steady
clock from the time of day, which on Windows XP advances in steps of
15.625 ms and on every Windows jumps when the clock is set: too coarse to
time the engine's frames and ticks, and not steady. With such a library,
[src/steady_clock.cpp](src/steady_clock.cpp) defines the steady clock
itself, on the performance counter, in nanoseconds since the system started
(whole seconds and the rest of the count converted apart, rounded down), as
other C++ run-time libraries for Windows do. It defines the system clock
too, as the time of day in nanoseconds since 1970, in steps of 100 ns,
because the library defines both in one object, which would otherwise be
linked beside these definitions. With a C++ library whose steady clock is
monotonic, the file defines nothing.

[cmake/OaOptions.cmake](../../../cmake/OaOptions.cmake) links the library,
whole, into every executable of a MinGW build, for Windows XP or not, so
that its definitions are in the program before the linker reaches the C++
library.

`platform-steady-clock` checks that the steady clock reads the performance
counter: each reading lies between the counter's own readings just before
and just after it. It passes with this module's clock and with a C++
library's own monotonic one, and fails with a clock that reads the time of
day.
