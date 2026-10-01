// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/lock.hpp"

#include <cstdint>

namespace oa::platform {

void token_lock_reset(TokenLock* lock) noexcept {
    lock->word.store(0);
    std::lock_guard guard(lock->event_mutex);
    lock->signaled = false;
}

TokenLockHold token_lock_enter(TokenLock* lock, int32_t token) noexcept {
    for (;;) {
        const int32_t previous = lock->word.exchange(token);
        if (previous == 0) {
            lock->owner.store(token);
            return {0, token};
        }
        if (lock->owner.load() == token) {
            return {previous, token};
        }
        std::unique_lock guard(lock->event_mutex);
        lock->event.wait(guard, [lock] { return lock->signaled; });
        lock->signaled = false;
    }
}

void token_lock_leave(TokenLock* lock, const TokenLockHold* hold) noexcept {
    if (hold->previous != 0) {
        return;
    }
    lock->owner.store(0);
    lock->word.store(0);
    {
        std::lock_guard guard(lock->event_mutex);
        lock->signaled = true;
    }
    lock->event.notify_one();
}

void wake_event_signal(WakeEvent* event) noexcept {
    {
        std::lock_guard guard(event->mutex);
        event->signalled = true;
    }
    event->condition.notify_one();
}

void wake_event_wait(WakeEvent* event) noexcept {
    std::unique_lock guard(event->mutex);
    event->condition.wait(guard, [event] { return event->signalled; });
    event->signalled = false;
}

} // namespace oa::platform
