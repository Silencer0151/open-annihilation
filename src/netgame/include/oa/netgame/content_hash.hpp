// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The byte hash behind the unit content checksums the 0x1a handshake
// exchanges; the checksum itself is the unit table's
// (netgame::sync::mix_unit_file_checksum).

#include "oa/netgame/wire.hpp"

#include <cstdint>

namespace oa::netgame {

/// Hashes a byte buffer into four byte-wide lanes.
///
/// For each byte v at index i (i taken as a byte): lane0 += v, lane1 ^= v,
/// lane2 += (i ^ v), lane3 ^= (i + v).
///
/// @param bytes Buffer to hash; null hashes to 0.
/// @param size Number of bytes; 0 hashes to 0.
/// @return lane3 << 24 | lane2 << 16 | lane1 << 8 | lane0.
[[nodiscard]] uint32_t content_buffer_hash(const uint8_t* bytes, std::size_t size) noexcept;

} // namespace oa::netgame
