// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The automation endpoint's frame request: the frame the game presented in
// its window, or the one it composed, whole or a region of it, as raw RGB,
// as a PNG file or as a hash of its pixels. The request is taken at a
// frame's pump stage and answered at the same frame's presented stage, from
// the frame drawn and shown in between.
#pragma once

#include "endpoint.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace oa::app::automation {

/// Frames a frame request waits for the game to present one before it is
/// refused: about a second or two, at the rates the game draws.
inline constexpr uint32_t presented_frame_wait_limit = 60;

/// The 64-bit FNV-1a hash's starting value.
inline constexpr uint64_t fnv1a_offset_basis = 0xcbf29ce484222325ULL;
/// The 64-bit FNV-1a hash's multiplier.
inline constexpr uint64_t fnv1a_prime = 0x100000001b3ULL;

/// Returns the 64-bit FNV-1a hash of bytes.
///
/// @param bytes the bytes
/// @return the hash
[[nodiscard]] uint64_t fnv1a_64(std::span<const uint8_t> bytes) noexcept;

/// Takes a frame request: checks its fields and holds it until the frame
/// it asks for has been presented. Fields: `source`, `presented` (the
/// default) or `composed`; `format`, `rgb` (the default), `png` or `hash`;
/// `region`, `[x, y, w, h]`, and its `space`, `game` (the default, the
/// canvas's pixels) or `window` (the presented frame's own pixels).
///
/// @param[in,out] endpoint the endpoint
/// @param request the request
/// @param[in,out] answer the answer: held, or refused
void answer_frame(Endpoint& endpoint, const Request& request, Answer& answer);

/// Answers a held frame request at the presented stage of the frame that
/// took it, or as soon after as the game presents a frame; keeps the game
/// copying the frames it presents only while such a request waits.
///
/// @param[in,out] endpoint the endpoint
/// @param stage the stage of the frame
void serve_frame_request(Endpoint& endpoint, FrameStage stage);

} // namespace oa::app::automation
