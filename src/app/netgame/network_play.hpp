// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Network play's state for one runtime and the work it does on it: the
// network session behind the multiplayer screens and the networked match,
// .tad playback, the match report and the console's network commands.
// Network play's extension owns one for each runtime its hooks are called
// for (extension.cpp) and frees it as that runtime is destroyed
// (Extension::release_runtime). It is Runtime's one friend from network
// play; what it uses of Runtime's private part only shrinks
// (netgame-runtime-surface, tools/runtime-surface-baseline.json).
#pragma once

#include "oa/app/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace oa::app {

struct NetState;  // net_state.hpp
struct DemoState; // demo_state.hpp

/// Network play's state for one runtime, and the work network play does on that runtime.
class NetworkPlay {
  public:

    /// Creates network play's state for a runtime: no session, no playback.
    ///
    /// @param runtime The runtime it belongs to; it outlives the state.
    explicit NetworkPlay(Runtime& runtime) noexcept;

    NetworkPlay(const NetworkPlay&) = delete;
    NetworkPlay& operator=(const NetworkPlay&) = delete;

    /// Ends the playback, then the session, as the runtime is destroyed.
    ~NetworkPlay();

    /// Returns network play's state for a runtime, creating it on first use.
    ///
    /// @param runtime The runtime.
    /// @return Its state, which lives until free_for frees it.
    static NetworkPlay& of(Runtime& runtime);

    /// Returns network play's state for a runtime when it has any.
    ///
    /// @param runtime The runtime.
    /// @return Its state, or null before the first use.
    [[nodiscard]] static NetworkPlay* find(const Runtime& runtime) noexcept;

    /// Frees network play's state for a runtime that is being destroyed (Extension::release_runtime).
    ///
    /// The playback ends first, then the session's connection is destroyed.
    /// Nothing happens for a runtime with no state.
    ///
    /// @param runtime The runtime.
    static void free_for(const Runtime& runtime) noexcept;

    // Networked match (runtime_net.cpp): the session behind the multiplayer
    // screens, the launch from the battle room and the per-tick binding.

    /// Destroys a network session, releasing its connection storage when it was allocated.
    ///
    /// @param state Session to destroy; null is allowed.
    static void destroy_net_state(NetState* state) noexcept;

    /// Binds the multiplayer screens to the network session, creating the session on first use.
    ///
    /// The match report's hold beside the game is released first
    /// (release_match_report). A new session takes the
    /// "-p" send pacing when one was given; when its connection storage cannot
    /// be allocated the failure is reported on stderr and the screens get no
    /// transport. The screens' start hook is bound to this state, their
    /// translation of interface texts (translate_ui) to its runtime, and
    /// their battle room takes the "-t" player timeout either way.
    void net_bind_multiplayer();

    /// Runs one frame of the network match; nothing without an active match.
    ///
    /// While loading it runs the load barrier and finishes the load once the
    /// barrier passes (a failure aborts the match). While the match goes on
    /// (Runtime::match_running: on its screen, or beneath the preferences its
    /// menu opens) it sends local speed changes, announces a pause set on
    /// another player's machine, applies the received speed, pumps a paused
    /// frame while the pause bit is set, syncs the match timing, watches the
    /// player timeouts and steps the match report. The Pause key alone pauses (the
    /// extension's pause_changed); a menu, and a finished game holding on its
    /// outcome until its final economy settles, send no pause: the other
    /// players play on, and the timeouts still drop a player who stops
    /// answering.
    void net_frame();

    /// Runs one simulation tick through the network binding.
    ///
    /// @return True when the networked binding ran the tick; false without a
    ///         running (loaded) network match.
    bool net_simulation_step();

    /// Tests whether the local player of the active network match only watches.
    ///
    /// @return False without an active network match.
    [[nodiscard]] bool net_local_watcher() const;

    /// Tests whether a network match owns the session, loading or running.
    ///
    /// @return Whether a network match is active.
    [[nodiscard]] bool net_match_active() const;

    /// Tests whether the connection is in a session, from the multiplayer screens or a match.
    ///
    /// @return Whether the connection exists and is in a session.
    [[nodiscard]] bool net_session_open() const;

    /// Returns the session of the network match waiting on its load barrier.
    ///
    /// @return The session, or null when no network match is loading.
    [[nodiscard]] const NetState* loading_net_match() const;

    /// Returns the session of the network match that owns it, loading or running.
    ///
    /// @return The session, or null when no network match is active.
    [[nodiscard]] const NetState* session_net_match() const;

    /// Leaves the active network match and closes its transport; nothing without one.
    ///
    /// With a match built, the connection finishes with its Game block: every
    /// channel is flushed and the session closed, which tells the other
    /// machines the local players left; no record announces it. The binding
    /// and the match state are then cleared, the transport closed and the
    /// multiplayer screens reset.
    void net_leave();

    /// Sends a chat line from the local player to every player as "<name> text".
    ///
    /// A line that reaches everyone (a chat mode other than chosen players,
    /// allies or local-only) also goes to the match report (report_chat). Nothing is sent
    /// without a running network match or for empty text.
    ///
    /// @param text Chat text without the speaker prefix.
    void net_say(std::string_view text);

    /// Sends an already formatted chat line to every player.
    ///
    /// Nothing is sent without a running network match or for an empty line.
    ///
    /// @param line Chat line, speaker prefix included; null sends nothing.
    void net_send_chat(const char* line);

    /// Resets the traffic statistics of the connection's packet queue at the current tick.
    ///
    /// The console's "NetStats" and every console setup call it; the
    /// statistics feed the "BPS" and debug-key traffic readouts. Without a
    /// connection or packet queue there is nothing to reset.
    void net_reset_traffic_stats();

    /// Sends every local player's lobby block and team to all players; nothing without an active match.
    void net_send_player_status();

    /// Pumps a finished network game and tests its final-economy gate.
    ///
    /// A finished network game leaves once every peer holds the local final
    /// economy.
    ///
    /// @return True once the gate has passed, or when no loaded network match
    ///         runs; false keeps the outcome screen, which retries each frame.
    bool net_final_economy_settled();

    /// Gives resources between players of a network match.
    ///
    /// The match debits, credits and sends the resource record, as the share
    /// transfers do; nothing is given while the match loads.
    ///
    /// @param from Giving player index.
    /// @param to Receiving player index.
    /// @param metal True for metal, false for energy.
    /// @param amount Amount given.
    /// @return False when no network match is active, so the caller gives locally.
    bool net_give(uint8_t from, uint8_t to, bool metal, float amount);

    /// Runs --net-loopback-check: hosts, and joins from a second in-process runtime, over 127.0.0.1.
    ///
    /// The two machines publish and join a session, sync their unit tables,
    /// launch through the load barrier and run the match, which the check
    /// compares between them; with --net-loopback-watcher the joiner only
    /// watches. Progress goes to stdout.
    ///
    /// @param ticks Networked ticks to run once the match has started.
    /// @return 0 when every check passes; a failed check throws std::runtime_error.
    int run_net_loopback_check(std::size_t ticks);

    /// Selects a map by the name lobbies and recordings use (runtime_netgame.cpp).
    ///
    /// The name is first tried as a map file stem; otherwise the installed
    /// maps/*.ota files are searched for that OTA mission name, ignoring case.
    ///
    /// @param name Map file stem or OTA mission name.
    /// @return Whether a map was selected; false for an empty name.
    bool select_map_named(std::string_view name);

    /// Draws a network load's player bars and ready count over the loading screen.
    ///
    /// Each bar is drawn in the player-bar colour, filled in the done colour
    /// up to the player's progress, with the player's name over it; the names
    /// and the status text use the text colour the last category row set.
    /// 3.1c redraws the root gadget panel first; this loading screen has no
    /// such panel. Nothing is drawn unless a network match is loading.
    ///
    /// @param[in,out] target Surface of the loading screen.
    /// @param font Font of the names and the status text.
    void draw_loading_players(oa::Surface& target, const oa::present::GafSprites* font);

    // .tad playback (runtime_demo.cpp).

    /// Ends a demo playback session and frees it.
    ///
    /// @param session Session to destroy.
    static void destroy_demo_session(DemoState* session) noexcept;

    /// Opens the --play-demo recording and builds a match to watch it in.
    ///
    /// A recording the engine handed over (NetgameContext::staged_recording)
    /// is taken and opened in its place, and its strict flag stands for
    /// --demo-unit-table. The recording's map must be installed and a slot
    /// left for the watcher. The match is built for the first recorded
    /// player's view with the recorded unit-sync verdicts; unless
    /// --demo-unit-table ignore is given the installed unit table must match
    /// the recording's. The recorded players then become remote and the
    /// watcher local, the sight grids are rebuilt and the view is the
    /// watcher's. Throws std::runtime_error when the recording cannot be
    /// opened or played.
    void start_demo_playback();

    /// Delivers one tick of recorded packets to the demo match.
    ///
    /// Tick errors are reported and the recording's chat lines posted. The
    /// session ends when its match is no longer the running one.
    void step_demo_frame();

    /// Applies a recorded game speed change to the playback pacing, as it paced the recorded game.
    void demo_frame();

    /// Places the headless snapshot camera: at --camera when given, else on the followed unit.
    void place_demo_camera();

    /// Replays up to `ticks` networked ticks headlessly and reports the replay's statistics.
    ///
    /// Prints the recording, the final digest and the full unit records the
    /// recording carried for the followed unit, with how far every unit had
    /// drifted from each of its full records; writes --snapshot when given.
    /// With --headless-check it first checks that Enter opens no chat line
    /// for the watcher.
    ///
    /// @param ticks Most ticks to replay.
    /// @return 0 when the replay was clean and the followed unit's full records
    ///         came units_per_player ticks apart; 1 otherwise.
    int run_headless_demo(std::size_t ticks);

    // The match report (runtime_report.cpp), kept with the network session.

    /// Returns the world the match report reads: the running match's, else the end-of-game screen's.
    ///
    /// @return The world, or null when neither exists.
    oa::World* reporter_world();

    /// Starts the match report for a network game.
    ///
    /// The end-of-game screen's state, which the report needs, is created here.
    /// When the lobby kept its own Game block, its provider GUID and game name
    /// are copied into the match's. The battle room launches straight into the
    /// match, so the battle-room and launch events are reported at once.
    ///
    /// @param[in,out] world Match world; its Game block receives the session details.
    /// @param lobby_game The lobby's Game block; null or the match's own block copies nothing.
    void start_reporter(oa::World& world, const oa::Game* lobby_game);

    /// Reports a game event to every route; nothing before the end-of-game state exists.
    ///
    /// @param event An extension_api::report_event value.
    void report_game_event(int32_t event);

    /// Remembers a chat line as the last one reported and reports it.
    ///
    /// @param line Chat line; null is kept as empty.
    void report_chat_line(const char* line);

    /// Runs the match report's per-frame step on the running match.
    void step_reporter_frame();

    /// Reports the closed session, then closes the match report; nothing if it never started.
    void close_reporter();

    /// Asks the extension to release what its match report holds beside the game, as opening the provider list does.
    void release_match_report();

    // Drop, Share*, Compression, Page and Senderror (runtime_console_network.cpp).

    /// Binds the console's network commands to this state.
    ///
    /// A share toggle re-sends the local players' lobby blocks, "NetStats"
    /// resets the traffic statistics and "Page" pages through the extension
    /// built on network play while a launch is active. The commands are
    /// added through the host's extend hook.
    ///
    /// @param[in,out] host Console host whose player_info_changed,
    ///                     extension_context and extend are set.
    void bind_console_network_hooks(oa::ui::console::ConsoleHost& host);

    /// Checks the console's network commands outside a live network game.
    ///
    /// The share toggles and "Compression" must do nothing; "Drop" must set
    /// its bit for 0 (or no value) and clear it for anything else; "Page" must
    /// say what it needs; the developer command "Senderror" must take 0..100
    /// from a single value and any other value as 0. A failure throws
    /// std::runtime_error.
    ///
    /// @param enter_line Types one chat line into the console.
    void check_console_network_commands(const std::function<void(const char*)>& enter_line);

    /// Checks the same commands in a live network game against `peer`.
    ///
    /// The share toggles must reach the peer's copy of this machine's lobby
    /// block, "Compression" must send every frame stored, "Senderror 100" must
    /// drop every frame before the transport (still counted as sent) and
    /// "Drop 0" must keep the stall check from naming a silent peer. A failure
    /// throws std::runtime_error.
    ///
    /// @param peer The other machine of the loopback game.
    /// @param step Runs one networked tick on this machine, and on the peer too
    ///             when passed true.
    void
    check_console_network_session(Runtime& peer, const std::function<void(bool peer_runs)>& step);

    /// Checks the console's cheat gate in a network game against `peer` (runtime_session_cheats.cpp).
    ///
    /// Every machine's cheat flag must be the host's CHEATING option, which the
    /// Game Settings sheet shows as its Cheat Codes row. The host's "+atm"
    /// must run only when the option is on, and either way its line must reach
    /// the other machine and the match report. A watching peer's Enter must
    /// open no chat line; a playing peer's "+atm" must follow the option and
    /// reach this machine the same way. A failure throws std::runtime_error.
    ///
    /// @param peer The other machine of the loopback game.
    /// @param step Runs one networked tick on this machine, and on the peer too
    ///             when passed true.
    void
    check_console_session_cheats(Runtime& peer, const std::function<void(bool peer_runs)>& step);

    /// Returns the --match-ticks run length, when one was given.
    ///
    /// @return The ticks, or nothing without --match-ticks.
    [[nodiscard]] std::optional<std::size_t> match_ticks() const;

    /// Posts a player's departure from a network game to the match's message log.
    ///
    /// @param[in,out] world Match world the departure is posted to.
    /// @param player_id Session id of the player who left.
    void post_departure(oa::World& world, uint32_t player_id);

  private:

    // The callbacks handed to the multiplayer screens and the net match, and
    // the launch steps they drive (runtime_net.cpp).
    struct NetHost;
    // Network play's hooks (extension.cpp), which reach this state.
    friend struct RuntimeExtension;

    Runtime& runtime_;
    std::unique_ptr<NetState, void (*)(NetState*) noexcept> net_{nullptr, destroy_net_state};
    bool demo_unit_table_differs_ = false;
    uint16_t demo_speed_ = 0; // last recorded game speed applied to the pacing
    // Declared after net_: the playback ends before the session.
    std::unique_ptr<DemoState, void (*)(DemoState*) noexcept> demo_{nullptr, destroy_demo_session};
    // The last chat line handed to the match report.
    std::string reported_chat_;
};

} // namespace oa::app
