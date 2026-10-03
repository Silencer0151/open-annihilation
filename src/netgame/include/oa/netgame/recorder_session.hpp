// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The recorder's session as every machine running it keeps it: the host's
// options (autopause, commander warp, the speed lock), each player's
// recorder protocol, warp-done, readiness and shared camera, the prebuilt
// base, and the dot-commands players type as chat. The battle room fills
// it and hands it to the match.

#include "oa/core/world.h"
#include "oa/netgame/recorder_messages.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace oa::netgame {

/// The commands a recorder reads in chat lines, typed as ".name args".
enum class RecorderCommand : uint8_t {
    none,
    report,           ///< .report: every recorder answers with its program line
    players,          ///< .players: every recorder answers with the players it sees
    report_mod,       ///< .reportmod
    date,             ///< .date
    status,           ///< .status
    speed_lock,       ///< .syncon low high (host)
    speed_unlock,     ///< .syncoff (host)
    autopause,        ///< .autopause (host)
    vote_ready,       ///< .voteready
    ready,            ///< .ready
    commander_warp,   ///< .cmdwarp (host)
    vote_go,          ///< .votego
    force_go,         ///< .forcego
    units,            ///< .units (playback only)
    fake_watch,       ///< .fakewatch
    give,             ///< .give names...
    stop_give,        ///< .stopgive names...
    take,             ///< .take
    take_commander,   ///< .takecmd
    base_file,        ///< .base file (host)
    do_base,          ///< .dobase
    base_off,         ///< .baseoff
    record,           ///< .record name
    share_camera,     ///< .sharemappos
    share_sight,      ///< .sharelos
    f1_off,           ///< .f1off
    force_cd,         ///< .forcecd
    random_map,       ///< .randmap
    random_map_ex,    ///< .randmapex
    integrity_report, ///< .exereport, .tdreport, .tpreport, .gp3report, .crcreport
};

/// One command read from a chat line.
struct RecorderCommandLine {
    RecorderCommand command{RecorderCommand::none};
    int32_t first{};        ///< the first number argument, 0 when absent
    int32_t second{};       ///< the second number argument, 0 when absent
    uint8_t integrity_op{}; ///< for integrity_report, the private-channel op (3..7) it asks for
    char argument[32]{};    ///< the first word after the command, as typed
    char arguments[64]{};   ///< everything after the command word, as typed
};

/// Reads a recorder command out of a chat line.
///
/// The line is either the command itself or a chat line "<Name> .cmd args";
/// the command word is matched without case.
///
/// @param line the line; null reads as empty
/// @return the command, or RecorderCommand::none
[[nodiscard]] RecorderCommandLine parse_recorder_command(const char* line) noexcept;

/// Tells whether a command's words name a player.
///
/// The words are those after the command word, split at spaces; a word
/// names the player when it equals the name, matched without case.
///
/// @param line the command
/// @param name the player's name; need not end in a NUL
/// @param length the name's length in characters
/// @return true when one of the words is the name
[[nodiscard]] bool recorder_arguments_name(
    const RecorderCommandLine& line, const char* name, std::size_t length
) noexcept;

/// Tells whether only the host's command has an effect.
///
/// .dobase is not one: any player asks for its base, and only the host's
/// machine builds it.
///
/// @param command the command
/// @return true for the speed lock, autopause, commander warp, .base, .baseoff and the map commands
[[nodiscard]] bool recorder_command_host_only(RecorderCommand command) noexcept;

/// Tells whether the command belongs to the opt-in session commands, which
/// network.recorder-session-commands makes available.
///
/// @param command the command
/// @return true for .autopause, .voteready, .ready, .forcego, .votego, .fakewatch, .forcecd, .f1off,
///         .randmap and .randmapex
[[nodiscard]] bool recorder_session_command(RecorderCommand command) noexcept;

/// The lowest game speed the recorder allows without a lock.
inline constexpr uint8_t recorder_speed_floor = 0;
/// The highest game speed the recorder allows without a lock.
inline constexpr uint8_t recorder_speed_ceiling = 20;
/// The lock's limits are typed relative to normal speed (10).
inline constexpr uint8_t recorder_speed_lock_offset = 10;

/// The most entries a prebuilt base table holds: entry numbers run from 0 to 1000.
inline constexpr std::size_t recorder_base_entry_count = 1001;

/// One building of a prebuilt base (setup.recorder-prebuilt-base).
struct RecorderBaseEntry {
    uint16_t unit_type{}; ///< the unit type index its creation record carries
    int16_t offset_x{};   ///< map pixels east of the base's centre
    int16_t offset_z{};   ///< map pixels south of the base's centre
    uint16_t health{};    ///< the health it is handed over with
};

/// The prebuilt base the host's recorder builds (setup.recorder-prebuilt-base).
///
/// The table lists per_side buildings for each side: ARM's are entries 1 to
/// per_side, CORE's entries per_side + 1 to 2 * per_side.
struct RecorderBase {
    std::array<RecorderBaseEntry, recorder_base_entry_count> entries{};
    int32_t per_side{}; ///< buildings a side's base holds
    /// Prebuilt bases may still be offered this session; .baseoff turns them
    /// off until the game is hosted again.
    bool enabled{true};
    /// By slot: the player may still have its base built (.base offers one to
    /// each player in the battle room; it is used up once built).
    bool available[OA_PLAYER_COUNT]{};
    /// By slot: the whole part of the x, y and z of the second unit the
    /// player created, the base's centre; x 0 while none is known.
    std::array<std::array<uint16_t, 3>, OA_PLAYER_COUNT> centre{};
};

/// The outcome of reading a prebuilt base file.
enum class RecorderBaseRead : uint8_t {
    read,         ///< every building line was read
    bad_count,    ///< the first line is not a number of buildings
    bad_entry,    ///< a line's first number, the entry's number, is missing or past 1000
    bad_type,     ///< a line's second number, the unit type, is missing or past 65535
    bad_offset_x, ///< a line's third number is missing or outside -32768..32767
    bad_offset_z, ///< a line's fourth number is missing or outside -32768..32767
    bad_health,   ///< a line's fifth number, before its ';', is missing or past 65535
};

/// Reads a prebuilt base file into a base table.
///
/// The file is lines of text. Empty lines and lines starting with ';' are
/// skipped. The first other line is the number of buildings a side's base
/// holds; every line after it is one building, "entry type x z health;":
/// five numbers, the first four each followed by one space and the fifth by
/// ';'. A number may be signed and may be hexadecimal after '$'. Each
/// building goes to its entry number; entries the file does not list keep
/// what they held. Reading stops at the first line in error, keeping what was
/// read before it.
///
/// @param text the file's contents
/// @param[in,out] base the table; its per_side and entries change
/// @return read, or what was wrong with the first line in error
[[nodiscard]] RecorderBaseRead
recorder_read_base(std::string_view text, RecorderBase& base) noexcept;

/// Fills a base table with the standard base, used when .base names no file.
///
/// Fifteen buildings a side, by 3.1c's unit type indices: the same layout
/// for ARM (entries 1 to 15) and CORE (16 to 30).
///
/// @param[in,out] base the table
void recorder_standard_base(RecorderBase& base) noexcept;

/// Returns the line the host's recorder answers a base file in error with.
///
/// @param outcome the outcome of recorder_read_base
/// @return "Erroneous number of possible buildings", "Erroneous base file1" to "...5", or empty for read
[[nodiscard]] const char* recorder_base_read_text(RecorderBaseRead outcome) noexcept;

/// The recorder's state of one session.
struct RecorderSession {
    RecorderHostOptions options{}; ///< as the host set them
    uint8_t
        peer_protocol[OA_PLAYER_COUNT]{}; ///< by slot: each player's recorder protocol, 0 for none
    bool host_options_sent[OA_PLAYER_COUNT]{}; ///< host: the options went to that slot's machine
    bool warp_done[OA_PLAYER_COUNT]{};
    bool ready[OA_PLAYER_COUNT]{}; ///< by slot: the player voted ready
    /// By slot: the centre of the player's camera its machine last sent, in map pixels.
    uint16_t camera_x[OA_PLAYER_COUNT]{};
    uint16_t camera_y[OA_PLAYER_COUNT]{};
    /// By slot: the player's machine sent its camera and has not turned sharing off since.
    bool camera_shared[OA_PLAYER_COUNT]{};
    /// .sharemappos turned this machine's camera sharing off; sharing starts on.
    bool local_camera_hidden{};
    /// This machine's camera centre, in map pixels, once the view has reported one.
    bool local_camera_known{};
    uint16_t local_camera_x{};
    uint16_t local_camera_y{};
    /// The camera centre this machine last sent, and when; local_camera_sent
    /// is false until one goes out again.
    bool local_camera_sent{};
    uint16_t sent_camera_x{};
    uint16_t sent_camera_y{};
    uint32_t sent_camera_time{};
    uint32_t cheat_mask[OA_PLAYER_COUNT]{};
    bool start_paused{};        ///< the game started paused by autopause or commander warp
    bool autopause_holding{};   ///< only the host may unpause until the host first does
    bool warp_released{};       ///< every player's warp is done and the game was unpaused
    bool fake_watch{};          ///< the local player asked to watch while seated
    uint32_t fake_watch_tick{}; ///< the tick that watch starts at
    char record_name[32]{};     ///< the name .record asked to record to; empty for none
    bool watchers_ready{};      ///< after .votego or .forcego, watchers count as ready
    /// The lines this machine answers .report with, such as "*** Name uses Program 1.0".
    char program[48]{};
    RecorderBase base{}; ///< the prebuilt base the host offers (.base)
};

/// Where the recorder's claim on a silent player's units stands on this
/// machine (sharing.recorder-take-give).
enum class RecorderTakeState : uint8_t {
    idle = 0,          ///< nobody claims
    claimed_older = 1, ///< a player whose recorder is protocol 3 or 4 claims
    /// A player whose recorder is older than protocol 3 claims; one with no
    /// recorder has its empty slots die at the next reject.
    claimed_plain = 2,
    claimed = 3, ///< another player's recorder claims, or this machine's claim was taken over
    taking = 4,  ///< this machine's player is taking a silent player's units
};

/// The recorder's claim on a silent player's units, as this machine keeps it
/// (sharing.recorder-take-give).
struct RecorderTake {
    RecorderTakeState state{RecorderTakeState::idle};
    char claimant[32]{}; ///< the player the claim is under
    int32_t takes{};     ///< takes this machine still carries out
    /// By slot: that player lets this machine's player take its units, by
    /// .give or by allying with it.
    bool granted[OA_PLAYER_COUNT]{};
    bool allied
        [OA_PLAYER_COUNT]{}; ///< by slot: the player's last alliance record allied this machine
    /// By slot: the next place in that player's unit block a take hands
    /// over, counted from 1 at the block's first unit; 0 while none is taken.
    uint32_t cursor[OA_PLAYER_COUNT]{};
    /// Damage records that kill the unused unit slots of a player with no
    /// recorder that asked to take, sent with the next reject record.
    std::vector<uint8_t> kill_records;
};

/// Returns the speed range the session's lock leaves of the rules' range.
///
/// Without the lock that is the rules' range. With it each of the lock's
/// limits, typed relative to normal, becomes the speed 10 above it, held
/// within the rules' range, and the highest is raised to the lowest when it
/// lies below it.
///
/// @param session the session
/// @param slowest the lowest speed the rules allow (WireRules::speed_min)
/// @param fastest the highest speed the rules allow, at least slowest (WireRules::speed_max)
/// @param[out] low the lowest speed; may be null
/// @param[out] high the highest speed; may be null
void recorder_speed_range(
    const RecorderSession& session, uint8_t slowest, uint8_t fastest, uint8_t* low, uint8_t* high
) noexcept;

/// Tells whether a command is the speed lock's (.syncon or .syncoff), which
/// only WireRules::speed_lock makes available.
///
/// @param command the command
/// @return true for speed_lock and speed_unlock
[[nodiscard]] bool recorder_speed_command(RecorderCommand command) noexcept;

/// Applies a host command to the session's options.
///
/// @param[in,out] session the session
/// @param line the command, from the host
/// @return true when the options changed
bool recorder_apply_host_command(
    RecorderSession& session, const RecorderCommandLine& line
) noexcept;

} // namespace oa::netgame
