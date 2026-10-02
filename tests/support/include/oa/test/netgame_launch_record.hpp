// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Network play's launch bound to a record, for tests that check calls of
// network play's API: the calls of network play's API
// (oa/app/netgame/extension_api.hpp) whose effect lands in the running game (a setup
// request, an application mode asked for and a leave) are kept in the record
// instead, so a test that links no running game can check that its extension
// made them. Defined by oa-netgame-test-launch.
#pragma once

#include "oa/app/netgame/launch_switches.hpp"

#include <cstdint>

namespace oa::test::netgame {

// What an extension asked of network play while a record was bound.
struct LaunchRecord {
    // The game switches' values: request_setup marks netsetup, as "-y" does.
    oa::app::netgame::launch::LaunchSwitches switches{};
    // The application mode request_app_mode last asked for; 0 for none.
    int32_t app_mode{};
    // leave_game calls with the disconnect reason, and without it.
    int32_t leaves_with_reason{};
    int32_t leaves_without_reason{};
};

/// Binds network play's launch to a record, or unbinds it.
///
/// While `record` is bound, request_setup marks its switches,
/// request_app_mode writes its app_mode and leave_game counts into it; the
/// launch block, the launch's state and the hooks keep their behaviour
/// without a running game. A binding replaces whatever network play's launch
/// was bound to.
///
/// @param record the record, which must outlive the binding; null unbinds
void bind_launch_record(LaunchRecord* record) noexcept;

} // namespace oa::test::netgame
