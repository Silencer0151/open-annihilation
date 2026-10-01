# Threads

Locks, condition variables, events and threads that run on every system the
engine supports, Windows XP included. Target `oa-base-threads`, header
`oa/base/threads.hpp`, namespace `oa::base::threads`.

The platform layer's token lock, thread start and log lock, the HPI archive
readers and the director's frame hashing use them in place of the standard
library's threads and locks. On Windows the standard library's locks and
condition variables rest on calls that Windows XP does not have; these rest
only on the ones it does: critical sections, semaphores, events and threads
started through the C library. On macOS and Linux they wrap the
standard library and POSIX threads.

- `Mutex` is a lock one thread holds at a time. It needs no constructor code,
  so one at namespace scope (`constinit`) is ready before any static is
  initialised; on Windows it makes its critical section on first use.
  `LockGuard` holds one for a scope.
- `ConditionVariable` lets threads that hold a `Mutex` wait for a signal; as
  with the standard's, a wait can end without one, and `wait(mutex, ready)`
  checks its condition again. On Windows it counts its waiters and wakes them
  through a semaphore, then waits until each woken thread has taken its
  wake-up, so that a thread that starts waiting after a signal never takes
  it.
- `Event` wakes one waiting thread and clears itself; a signal that comes
  before the wait is kept.
- `start_thread` and `join_thread` start a thread and wait for it;
  `start_detached_thread` starts one nobody waits for.
- `processor_count` and `sleep_ms` report the machine's logical processors and
  suspend the calling thread.

Nothing here throws. Should Windows fail to make a semaphore or event, a wait
becomes a short sleep that the waiter takes as a wake-up without a signal,
and its caller checks its condition again.

`base-threads` checks that a lock keeps four threads' increments whole, a
namespace-scope lock included, that `try_lock` fails while another thread
holds the lock, that a condition variable hands 200 jobs to four threads and
back, that an event keeps an early signal and relays between two threads,
and that a detached thread runs.
