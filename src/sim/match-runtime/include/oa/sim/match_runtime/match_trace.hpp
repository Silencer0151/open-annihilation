// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Trace stream of an offline match: at the head of every tick, before the
// unit sweep, the tick record and the section digests of
// oa/sim/trace.hpp, and optionally a text line per occupied unit slot for a
// field-level diff.
#pragma once

#include "oa/sim/trace.hpp"

#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace oa::sim::match_runtime {
class Match;

/// Fills the movement object's speed and presence and the head of the
/// primary order queue of every unit slot, from the match's side tables.
///
/// @param match Match whose units are sampled.
/// @param[out] sides One entry per unit slot.
void fill_trace_sides(const Match& match, std::span<sim::trace::UnitSide> sides);

class TraceRecorder {
  public:

    TraceRecorder() = default;
    TraceRecorder(const TraceRecorder&) = delete;
    TraceRecorder& operator=(const TraceRecorder&) = delete;
    /// Closes both streams.
    ~TraceRecorder();

    /// Starts the stream at `path` with its header.
    ///
    /// @param path File the trace stream is written to.
    /// @param unit_path File of the per-unit text dump; empty writes none.
    /// @param seed Match random seed written in the header.
    /// @return False when either file cannot be created.
    bool open(const std::string& path, const std::string& unit_path, uint32_t seed);
    /// Appends one tick: the tick record, then the section records.
    ///
    /// @param match Match sampled at the head of its tick.
    void sample(const Match& match);

  private:

    /// Closes both stream files, if open.
    void close() noexcept;

    std::FILE* stream_{};
    std::FILE* units_{};
    std::vector<sim::trace::UnitSide> sides_;
};
} // namespace oa::sim::match_runtime
