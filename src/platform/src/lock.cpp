// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/lock.hpp"

#include <cstdint>

namespace oa::platform {

void token_lock_reset(TokenLock* lock) noexcept {
    lock->word.store(0);
    lock->released.reset();
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
        lock->released.wait();
    }
}

void token_lock_leave(TokenLock* lock, const TokenLockHold* hold) noexcept {
    if (hold->previous != 0) {
        return;
    }
    lock->owner.store(0);
    lock->word.store(0);
    lock->released.signal();
}

void wake_event_signal(WakeEvent* event) noexcept {
    event->event.signal();
}

void wake_event_wait(WakeEvent* event) noexcept {
    event->event.wait();
}

} // namespace oa::platform
