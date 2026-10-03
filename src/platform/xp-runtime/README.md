# Windows XP run-time functions

The functions that Windows XP lacks and the C++ run-time library calls.
Target `oa-platform-xp-runtime`, header `oa/platform/xp_runtime.hpp`,
namespace `oa::platform::xp_runtime`.

A Windows build made with `-DOA_WINDOWS_XP=ON` (see
[OaWindowsXp.cmake](../../../cmake/OaWindowsXp.cmake)) compiles engine code
against the declarations of Windows XP and links this library, whole, into
every executable. The engine's own code calls nothing XP lacks: its locks
and threads are those of [oa-base-threads](../../base/threads/README.md).
The C++ run-time library linked into each executable does, and MinGW's
import libraries would make Windows refuse to start a program that imports
any function its system DLLs do not export. This library therefore defines
those functions itself, under the names a program imports them by, so the
program imports none of them:

| Functions | With the system's own | Without |
|---|---|---|
| slim reader/writer locks and their condition variables | Windows 7 and later have all of them | the stand-ins below |
| `InitOnceExecuteOnce` | Vista and later | the stand-in below |
| `FlsAlloc`, `FlsGetValue`, `FlsSetValue` | Vista and later | thread-local storage, whose slots no callback frees when a thread ends |
| `GetThreadId` | Vista and later | the thread's native basic information |
| `GetActiveProcessorCount` | 7 and later | the processor count `GetSystemInfo` gives |
| `GetLocaleInfoEx` | Vista and later | `GetLocaleInfoW` for the user's, the invariant and the system's locale; other names fail |
| `K32EnumProcessModules` | 7 and later | the process-status library's `EnumProcessModules` |
| `GetFileInformationByHandleEx`, `SetFileInformationByHandle` | Vista and later | the native file information calls, for the classes whose records they share (basic, standard, name, stream, compression and attribute tag; for setting, basic, disposition, allocation and end of file) |
| `GetFinalPathNameByHandleW` | Vista and later | the file's device path, with the device of a drive letter named `\\?\X:` and a shared folder on another computer named `\\?\UNC`; the path keeps the spelling the file was opened by, short (8.3) names included |
| `CreateSymbolicLinkW` | Vista and later | fails with `ERROR_NOT_SUPPORTED`; XP has no symbolic links |
| the C library's `_l` character, collation and number functions, `wcrtomb_s`, `_putenv_s` | always the stand-ins | the same functions without a locale argument, in the thread's current locale (the engine's is "C" throughout) |

The slim locks and condition variables use the system's only when it has
every one of them, so a lock is never taken by one implementation and
released by the other. The stand-ins keep the system's layout, one
pointer-sized word that is zero when free:

- a slim lock's word holds bit 0 for a writer and counts readers in steps
  of two;
- a condition's word counts wake-ups: a waiter notes the count while it holds
  the lock, releases the lock, and waits for the count to change or its time
  to run out. Waking one thread wakes all, which the system allows; each
  checks its condition again;
- a one-time initialisation's word holds its state in the low two bits (not
  run, running, done) and the result pointer above them.

A thread that finds a word busy waits on the processor 64 times, yields its
time slice 16 times and then sleeps a millisecond at a time. The C++ run-time library holds these locks
only briefly (static initialisation, locales, exception unwinding), so this
costs nothing measurable; it would not suit heavy contention.

The definitions are built only with MinGW, whose naming of imports they
follow (`__imp_` pointers, and on 32-bit x86 a leading underscore and the
argument size). The slim locks, conditions and one-time initialisation are
plain C++ and built everywhere. They sleep through
[oa-base-threads](../../base/threads/README.md), except on Windows, where
they call the system themselves: an XP build links them into every program
after the libraries it names, where only a program that sleeps itself would
have the sleep from oa-base-threads.

The steady clock an XP build needs, on the performance counter rather than
the time of day, is not here: every MinGW build takes it from
[src/platform/steady-clock](../steady-clock/README.md), for Windows XP or
not.

`platform-xp-runtime` checks, on every system, that a slim lock keeps four
threads' increments whole, that readers share it and exclude a writer, that
a condition hands work between two threads and times out, and that a
one-time initialisation runs once for four threads and runs again after it
fails. With MinGW on Windows it also checks that the file stand-ins read the
same size, times, attributes and final path as the system's calls for a file
it writes, and that the functions under the system's names lock, wait, run
once and keep fiber-local values.
