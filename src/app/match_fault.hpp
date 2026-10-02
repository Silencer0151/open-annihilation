// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The faults a match notes (Match::fault) turned into the app's errors: a
// load, a mission start or a check stops with the fault's text, and the
// fault is cleared so the next one is noted.
#pragma once

#include "oa/sim/match_runtime.hpp"

#include <stdexcept>
#include <string>

namespace oa::app {

/// Throws the fault the match noted, after clearing it; does nothing when
/// none is noted.
///
/// @param match The match.
/// @throws std::runtime_error with the fault's text.
inline void raise_match_fault(oa::sim::match_runtime::Match& match) {
    const char* fault = match.fault();
    if (fault == nullptr)
        return;
    std::string text(fault);
    match.clear_fault();
    throw std::runtime_error(text);
}

/// Runs one tick of the match and throws the fault it noted, as a check or
/// a console command that steps the match reports it.
///
/// @param match The match.
/// @throws std::runtime_error with the fault's text.
inline void tick_or_raise(oa::sim::match_runtime::Match& match) {
    match.tick();
    raise_match_fault(match);
}

} // namespace oa::app
