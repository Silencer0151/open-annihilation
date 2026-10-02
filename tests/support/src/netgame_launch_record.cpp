// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Network play's launch bound to a record (oa/test/netgame_launch_record.hpp).
#include "oa/test/netgame_launch_record.hpp"

#include "launch_binding.hpp"

namespace oa::test::netgame {

namespace {

// The record bound; null while none is.
LaunchRecord* g_record = nullptr;

/// Counts a leave into the bound record (LaunchBinding::leave_game).
///
/// @param context unused
/// @param with_reason whether the disconnect reason would show first
void record_leave(void*, bool with_reason) {
    if (g_record == nullptr)
        return;
    if (with_reason)
        ++g_record->leaves_with_reason;
    else
        ++g_record->leaves_without_reason;
}

} // namespace

void bind_launch_record(LaunchRecord* record) noexcept {
    g_record = record;
    oa::app::LaunchBinding binding{};
    if (record != nullptr) {
        binding.switches = &record->switches;
        binding.app_mode_pending = &record->app_mode;
        binding.leave_game = record_leave;
    }
    oa::app::bind_launch(binding);
}

} // namespace oa::test::netgame
