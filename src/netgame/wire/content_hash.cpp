// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/content_hash.hpp"

#include <cstdint>

namespace oa::netgame {

uint32_t content_buffer_hash(const uint8_t* bytes, std::size_t size) noexcept {
    if (bytes == nullptr)
        return 0;
    uint8_t sum = 0, parity = 0, index_sum = 0, index_parity = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const uint8_t v = bytes[i];
        const auto index = static_cast<uint8_t>(i);
        sum = static_cast<uint8_t>(sum + v);
        parity = static_cast<uint8_t>(parity ^ v);
        index_sum = static_cast<uint8_t>(index_sum + (index ^ v));
        index_parity = static_cast<uint8_t>(index_parity ^ static_cast<uint8_t>(index + v));
    }
    return (uint32_t{index_parity} << 24) | (uint32_t{index_sum} << 16) | (uint32_t{parity} << 8) |
           sum;
}

} // namespace oa::netgame
