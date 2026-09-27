// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Token lock shared by the display thread, the cursor redraw thread and the
// worker thread. A caller acquires it with a nonzero token; re-entering with
// the token that already owns it returns immediately without taking a second
// hold, and only the call that actually acquired it releases it.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace oa::platform {

struct TokenLock {
    std::atomic<int32_t> word{0};  // exchanged with the caller's token
    std::atomic<int32_t> owner{0}; // token of the current holder
    std::mutex event_mutex;        // with event and signaled, an auto-reset event
    std::condition_variable event;
    bool signaled{false};
};

// Result of token_lock_enter kept by the caller for token_lock_leave.
struct TokenLockHold {
    int32_t previous{}; // 0 when this call acquired the lock
    int32_t token{};
};

/// Clears the lock word and the wake-up event.
///
/// @param[out] lock lock to reset
void token_lock_reset(TokenLock* lock) noexcept;

/// Acquires the lock for a token, or re-enters it when the token already owns it.
///
/// Swaps the token into the lock word until the word was free or already held
/// this token; otherwise waits for a release and retries.
///
/// @param[in,out] lock lock to take
/// @param token caller's nonzero token
/// @return the hold to pass to token_lock_leave; previous is 0 when this call acquired the lock
/// @quirk A failed swap leaves the caller's token in the word; only the
///        holder's release clears it.
[[nodiscard]] TokenLockHold token_lock_enter(TokenLock* lock, int32_t token) noexcept;

/// Releases the lock when this hold acquired it.
///
/// When hold->previous is 0 the owner and word are cleared and one waiter is
/// signalled; a re-entrant hold releases nothing.
///
/// @param[in,out] lock lock to release
/// @param hold result of the matching token_lock_enter
void token_lock_leave(TokenLock* lock, const TokenLockHold* hold) noexcept;

} // namespace oa::platform
