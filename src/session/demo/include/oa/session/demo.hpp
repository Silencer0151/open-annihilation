// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Playback of a TA Demo recording (.tad) through the match's own network
// receive path. The third-party recorder kept every datagram the recording
// client sent or received, minus the condenser envelope and the frame
// sequence dword, stored unit-state (0x2c) records without their tick behind
// a tick base, and kept each player's lobby block as sent. Playback rebuilds
// each recorded packet as a broadcast frame from its sender and offers it to
// the packet layer as a transport would once the local tick reaches the
// sender ticks it carries, so frame ordering, tick spreading, admission and
// record dispatch run exactly as for a live peer. The local machine is a
// watcher slot with no units; every recorded player is a remote slot, under
// its recorded id, whose units arrive through 0x09 and 0x2c records.

#include "oa/sim/match_runtime.hpp"
#include "oa/netgame/match/match_binding.hpp"
#include "oa/formats/tad.hpp"
#include "oa/ui/frontend_multiplayer/lobby.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace oa::session::demo {

inline constexpr std::size_t sender_count = 256; // one entry per recorded player number

struct DemoFrame {
    uint64_t time_ms{};            // recorder wall clock at arrival
    uint32_t due_tick{};           // local tick from which it is delivered
    uint8_t sender{};              // recorded player number
    std::vector<uint8_t> datagram; // condenser-wrapped broadcast frame
};

// A recorded player's lobby block: from the 0x20 record of its status
// datagram, else only the side and colour of its player chunk.
struct RecordedPlayer {
    PlayerSetupInfo info{};
    bool from_status{};
};

struct PlaybackStats {
    uint32_t frames{};              // datagrams rebuilt
    uint32_t frames_delivered{};    // handed to the packet layer
    uint32_t frames_oversized{};    // larger than the receive buffer; dropped
    uint32_t empty_packets{};       // recorded packets with no game record
    uint32_t empty_ticks{};         // 0xff markers restored as minimal 0x2c records
    uint32_t bad_packets{};         // no payload marker or an unsplittable tail
    uint32_t recorder_records{};    // 0xf9..0xfe, not part of the game stream
    uint32_t untimed_unit_states{}; // 0xfd or 0xff with no tick base before it
    uint32_t sends_dropped{};       // the watcher's own outgoing frames
};

struct DemoPlayback {
    std::vector<uint8_t> file;
    formats::tad::Demo demo;                         // views file
    std::vector<RecordedPlayer> players;             // demo.players order
    std::array<uint32_t, sender_count> sender_ids{}; // recorded player number -> net id
    uint32_t watcher_id{};                           // sorts after every recorded id
    // The watcher's battleroom: a joined client's unit sync table after the
    // recorded 0x1a records arrived. Only the host's verdicts (subtype 3)
    // change a client's table.
    std::unique_ptr<ui::frontend_multiplayer::Lobby> battleroom;
    uint32_t recorded_verdicts{};    // subtype-3 records in the recording
    uint32_t recorded_definitions{}; // the recorded game's unit table, slot 0 excluded
    unsigned unit_def_bits{};        // its 0x2c index width
    std::vector<DemoFrame> frames;   // recorded order
    std::size_t cursor{};
    uint32_t tick{}; // local tick being delivered for
    PlaybackStats stats;
};

/// Tells whether bytes are a TA Demo recording by their magic.
///
/// Only the first chunk's length field and the magic that opens its data
/// are read: bytes that pass may still fail demo_load, as a truncated or
/// damaged recording does.
///
/// @param bytes Whole recording, or any other file's bytes.
/// @return True when the magic sits where a recording's header chunk holds it.
[[nodiscard]] bool demo_recognised(std::span<const uint8_t> bytes) noexcept;

/// Reads a recording file and loads it with demo_load.
///
/// @param[out] playback Playback state to fill.
/// @param path Path of the .tad file.
/// @param[out] error Reason for a failure.
/// @return False when the file cannot be opened or demo_load fails.
[[nodiscard]] bool
demo_open(DemoPlayback* playback, const std::filesystem::path& path, std::string* error);

/// Parses a recording and rebuilds its packets as condenser-wrapped broadcast frames.
///
/// Binds each recorded player to an id and lobby block, replays the
/// recorded 0x1a unit checks into the watcher's battle room, sizes the 0x2c
/// index width from the recorded unit table (slot 0 included), and gives
/// each frame the local tick from which it is due: the last sender tick it
/// carries, or the previous frame's for frames without unit states.
///
/// @param[out] playback Playback state to fill; it takes the bytes.
/// @param bytes Whole recording.
/// @param[out] error Reason for a failure.
/// @return False when the recording does not parse, has no players, cannot bind them or announces no unit
///         definitions.
[[nodiscard]] bool
demo_load(DemoPlayback* playback, std::vector<uint8_t> bytes, std::string* error);

/// Returns a transport over the recording: receive yields, in recorded order, every frame whose due tick
/// has come; send drops the frame.
///
/// @param playback Loaded playback, passed back to the transport as its context.
/// @return The transport.
[[nodiscard]] netgame::NetTransport demo_transport(DemoPlayback* playback) noexcept;

/// Tells whether every rebuilt frame has been delivered.
///
/// @param playback Loaded playback.
/// @return True once the cursor is past the last frame.
[[nodiscard]] bool demo_exhausted(const DemoPlayback& playback) noexcept;

/// Returns the recording's length on the recorder's clock.
///
/// @param playback Loaded playback.
/// @return Milliseconds up to the last packet; 0 without packets.
[[nodiscard]] uint64_t demo_duration_ms(const DemoPlayback& playback) noexcept;

/// Returns the watcher's unit sync table for the match's unit table.
///
/// @param playback Loaded playback.
/// @return The table, or null when the recording holds no verdicts; the match then keeps every local type.
[[nodiscard]] const ui::frontend_multiplayer::UnitSync*
demo_unit_sync(const DemoPlayback& playback) noexcept;

// The recorded game's unit table against the match's unit definitions' FBI
// hashes (local_keys[i] belongs to definition index i + 1). The recorded
// table holds the types whose verdict has both sides set, else, without
// verdicts, the recorder's own subtype-2 list. Unit-definition indices
// travel in 0x09 and 0x2c records, so playback needs the identical table.
struct UnitTableCheck {
    uint32_t recorded{};
    uint32_t matched{};
    uint32_t missing{}; // recorded definitions absent here
    uint32_t extra{};   // local definitions the recording lacks
    bool identical{};
};

/// Compares the recorded game's unit table with the match's unit definitions.
///
/// @param playback Loaded playback.
/// @param local_keys FBI hashes of the match's unit definitions; local_keys[i] belongs to definition index i + 1.
/// @param[out] extra_indices Indices into local_keys of definitions the recording lacks, appended when not null.
/// @return Counts of recorded, matched, missing and extra definitions, and whether the tables are identical.
[[nodiscard]] UnitTableCheck demo_check_unit_table(
    const DemoPlayback& playback,
    std::span<const uint32_t> local_keys,
    std::vector<std::size_t>* extra_indices
);

/// Builds the player table for playback.
///
/// Recorded players become remote slots in recorded order with their
/// recorded ids and lobby blocks, one local watcher slot follows them, and
/// free slots take the -1 id. The host block's options apply as at a
/// multiplayer launch, and the watcher, local and viewing, sees the whole
/// map as a launched watcher does.
///
/// @param playback Loaded playback.
/// @param[in,out] world Match world whose player records and Game fields are set.
/// @param[out] watcher_slot The watcher's slot.
/// @return False for a null world, no players or no free watcher slot.
[[nodiscard]] bool
demo_bind_players(const DemoPlayback& playback, World* world, uint8_t* watcher_slot) noexcept;

// Full records of one kind of unit that placed a unit an earlier record
// already placed.
struct FullRecordDrift {
    uint32_t measured{};
    uint32_t moved{};     // measured placements whose recorded position changed
    uint64_t drift_sum{}; // world units over measured placements
    uint32_t drift_max{};
};

// Every detached full unit record the replay applied to a live unit. Between
// two records a remote unit moves only by its remote driver, so drift is the
// distance the replay failed to reproduce.
struct FullRecordStats {
    uint32_t placements{};
    FullRecordDrift ground{}; // units that cannot fly
    FullRecordDrift air{};
};

struct FullRecordSample {
    uint32_t tick{};
    FixedVec3 before{};
    FixedVec3 recorded{};
};

struct DemoSession {
    DemoPlayback playback;
    sim::match_runtime::Match* match{};
    netgame::match::NetConnection connection{}; // packets over demo_transport, no host
    netgame::match::NetMatch net{};
    netgame::match::MatchBinding binding{};
    uint8_t watcher_slot{};
    uint32_t tick_errors{};
    std::string last_error;
    std::vector<std::string> lines; // chat and notices since the last drain
    FullRecordStats full_records;
    std::vector<uint16_t> placed_type; // by unit slot: type at its last full record
    std::vector<FixedVec3> placed_at;  // by unit slot: position of that record
    uint16_t tracked_unit{};           // first recorded player's first unit
    std::vector<FullRecordSample> tracked;
};

/// Binds a match to the recording: player table, unit ranges, the packet layer over the recording and the
/// in-game pump.
///
/// The match must be built at the recording's unit limit with its outcomes
/// already configured with defeat disabled. demo_session_end releases the
/// packet layer.
///
/// @param[in,out] session Session holding a loaded playback; any previous binding is ended first.
/// @param match Match runtime to drive.
/// @param[out] error Reason for a failure.
/// @return False for a null session or match, a different unit limit or index width, or no free watcher slot.
[[nodiscard]] bool
demo_session_begin(DemoSession* session, sim::match_runtime::Match* match, std::string* error);

/// Runs one multiplayer tick: pumps the frames due by then, simulates unless paused and flushes.
///
/// A pause the recording carries is cleared once the tick has run, so
/// playback runs through the recorded pauses. Simulation exceptions are
/// counted in tick_errors and the last one kept.
///
/// @param[in,out] session Bound session; ignored when unbound.
void demo_session_frame(DemoSession* session);

/// Releases the packet layer and unbinds the match.
///
/// @param[in,out] session Session to end; null is ignored.
void demo_session_end(DemoSession* session) noexcept;

/// Tells whether every frame was delivered and no record is still held by the packet layer.
///
/// @param session Session to test.
/// @return True when playback is complete.
[[nodiscard]] bool demo_session_finished(const DemoSession& session) noexcept;

// How far a gap between two of the tracked unit's full records may stray
// from units_per_player ticks: delivery spreads a frame's records over up to
// 30 ticks after a send interval of 6.
inline constexpr int64_t full_record_slack_ticks = 36;

// A replay's verdict so far.
struct DemoVerdict {
    // No record failed (forgiven when the unit table differs), no unit was
    // created past the unit table (likewise forgiven), no tick failed and no
    // create was refused.
    bool clean{};
    // Every gap between the tracked unit's consecutive full records is
    // units_per_player ticks, give or take full_record_slack_ticks.
    bool paced{};
};

/// Judges a replay so far: whether it ran clean and whether the tracked unit's full records came paced.
///
/// Delta layouts follow the receiving unit's definition, so over a unit
/// table that differs from the recording's, unit states cannot decode
/// reliably and record errors do not count, nor do creates of indices a
/// larger recorded table reached. The spacing is the bound match's
/// units_per_player, or the recording's unit limit while unbound.
///
/// @param session Session to judge, bound or not.
/// @param unit_table_differs Whether the match's unit table differs from the recording's.
/// @return The verdict.
[[nodiscard]] DemoVerdict
demo_session_verdict(const DemoSession& session, bool unit_table_differs) noexcept;

/// Hashes the live units in slot order and the player economies with FNV-1a.
///
/// @param world World to hash, or null.
/// @return The digest; the FNV offset basis for a null world.
[[nodiscard]] uint64_t demo_world_digest(const World* world) noexcept;

/// Counts the occupied unit slots.
///
/// @param world World to count, or null.
/// @return Units with a nonzero type; 0 for a null world.
[[nodiscard]] uint32_t demo_live_units(const World* world) noexcept;

} // namespace oa::session::demo
