// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The automation endpoint's side of what it reports of a match and of the
// battle room (reports.hpp) and of its events (events.hpp): the match, room
// and subscribe requests, and the work that sends the events of each frame,
// of a match's loading and of a match's end to the client.
#pragma once

#include "endpoint.hpp"
#include "events.hpp"

#include <cstdint>
#include <span>

namespace oa::app::automation {

/// Answers match: the running match, as write_match writes it; with
/// "state_hash": true, its saved-state digest too.
///
/// @param[in,out] endpoint the endpoint
/// @param request the request
/// @param[in,out] answer the answer
void answer_match(Endpoint& endpoint, const Request& request, Answer& answer);

/// Answers room: the battle room, as write_room writes it.
///
/// @param[in,out] endpoint the endpoint
/// @param request the request
/// @param[in,out] answer the answer
void answer_room(Endpoint& endpoint, const Request& request, Answer& answer);

/// Answers subscribe: the client is sent the kinds of event its events
/// names ("all", or a list of kinds) from now on, in place of those it
/// took before; the answer's events lists them.
///
/// @param[in,out] endpoint the endpoint
/// @param request the request
/// @param[in,out] answer the answer
void answer_subscribe(Endpoint& endpoint, const Request& request, Answer& answer);

/// Returns the process's event watch, which the endpoint's work fills.
///
/// @return the one watch
[[nodiscard]] EventWatch& endpoint_event_watch() noexcept;

/// Sends the events of one frame (a row of the endpoint's frame work): at
/// the pump stage, what changed since the last one.
///
/// @param[in,out] endpoint the endpoint, which gives the running game
/// @param stage the stage of the frame
void send_frame_events(Endpoint& endpoint, FrameStage stage);

/// Sends the events of a match's loading, while no frame runs.
///
/// @param[in,out] endpoint the endpoint, bound to the running game
/// @param rows the loading screen's rows, each 0 to 100 percent
void send_loading_events(Endpoint& endpoint, std::span<const uint8_t> rows);

/// Sends the end of the running match, as it is torn down.
///
/// @param[in,out] endpoint the endpoint, bound to the running game
void send_match_gone(Endpoint& endpoint);

} // namespace oa::app::automation
