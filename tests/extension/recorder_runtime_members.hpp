// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The recorder extension's members of oa::app::Runtime, included inside the
// class through OA_RUNTIME_EXTENSION_MEMBERS, so that the engine's own
// builds exercise that mechanism while it exists: one function, which the
// recorder's RuntimeExtension calls, and the count it keeps.

/// Counts a call that reached the runtime through the extension's members.
///
/// @return the calls counted so far, this one included
uint32_t count_recorder_member_call();

uint32_t recorder_member_calls_{};
