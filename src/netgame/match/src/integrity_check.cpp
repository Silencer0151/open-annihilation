// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/match/integrity_check.hpp"
#include "oa/base/sha256.hpp"

#include <cstring>
#include <span>

namespace oa::netgame::match {

namespace {

constexpr std::size_t hmac_block_bytes = 64;
constexpr uint8_t hmac_inner_pad = 0x36;
constexpr uint8_t hmac_outer_pad = 0x5c;

} // namespace

void integrity_answer(
    const uint8_t (&nonce)[integrity_nonce_bytes],
    const uint8_t (&digest)[integrity_answer_bytes],
    uint8_t (&out)[integrity_answer_bytes]
) noexcept {
    uint8_t inner_key[hmac_block_bytes]{};
    uint8_t outer_key[hmac_block_bytes]{};
    for (std::size_t i = 0; i < hmac_block_bytes; ++i) {
        const uint8_t key = i < integrity_nonce_bytes ? nonce[i] : 0;
        inner_key[i] = static_cast<uint8_t>(key ^ hmac_inner_pad);
        outer_key[i] = static_cast<uint8_t>(key ^ hmac_outer_pad);
    }
    base::sha256::Hasher inner{};
    base::sha256::update(inner, inner_key);
    base::sha256::update(inner, digest);
    const auto inner_digest = base::sha256::finish(inner);
    base::sha256::Hasher outer{};
    base::sha256::update(outer, outer_key);
    base::sha256::update(outer, inner_digest);
    const auto answer = base::sha256::finish(outer);
    std::memcpy(out, answer.data(), integrity_answer_bytes);
}

bool integrity_challenge_due(IntegrityCheck form, uint32_t tick) noexcept {
    switch (form) {
    case IntegrityCheck::single_challenge:
        return tick == integrity_challenge_tick;
    case IntegrityCheck::periodic_challenge:
        return tick >= integrity_challenge_tick && tick <= integrity_periodic_end_tick &&
               (tick - integrity_challenge_tick) % integrity_period_ticks == 0;
    case IntegrityCheck::off:
        break;
    }
    return false;
}

IntegrityPeer* integrity_peer(IntegrityCheckState& state, uint32_t player_id) noexcept {
    if (player_id == 0)
        return nullptr;
    for (auto& peer : state.peers)
        if (peer.player_id == player_id)
            return &peer;
    for (auto& peer : state.peers)
        if (peer.player_id == 0) {
            peer = IntegrityPeer{};
            peer.player_id = player_id;
            return &peer;
        }
    return nullptr;
}

void integrity_note_answer(
    IntegrityPeer& peer, uint8_t op, const uint8_t* answer, const IntegrityIdentity& identity
) noexcept {
    if (!peer.challenged || answer == nullptr)
        return;
    uint8_t expected[integrity_answer_bytes]{};
    if (op == integrity_op_module_reply) {
        integrity_answer(peer.nonce, identity.program, expected);
        peer.program_answered = true;
        peer.program_matches = std::memcmp(expected, answer, integrity_answer_bytes) == 0;
    } else if (op == integrity_op_data_reply) {
        integrity_answer(peer.nonce, identity.game_data, expected);
        peer.data_answered = true;
        peer.data_matches = std::memcmp(expected, answer, integrity_answer_bytes) == 0;
    }
}

int32_t integrity_issue_count(const IntegrityCheckState& state) noexcept {
    int32_t count = 0;
    for (const auto& peer : state.peers)
        if (peer.player_id != 0 && peer.challenged &&
            !(peer.program_answered && peer.data_answered && peer.program_matches &&
              peer.data_matches))
            ++count;
    return count;
}

} // namespace oa::netgame::match
