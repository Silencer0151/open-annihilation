// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/network.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using Bytes = std::vector<uint8_t>;

struct Transport final : oa::netgame::network::ReceiveTransport {
    Bytes packet;
    bool enabled = true;
    unsigned calls{}, statistics_calls{};
    uint32_t result = oa::netgame::network::condenser::success;

    uint32_t receive(std::span<uint8_t> output, uint32_t& size) override {
        ++calls;
        size = static_cast<uint32_t>(packet.size());
        if (result == oa::netgame::network::condenser::success) {
            if (packet.size() > output.size())
                throw std::runtime_error("test transport capacity");
            std::copy(packet.begin(), packet.end(), output.begin());
        }
        return result;
    }

    bool condenser_enabled() const override { return enabled; }

    void record_received_bytes(uint32_t) override { ++statistics_calls; }
};

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

template <class F>
void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed, "invalid frame accepted");
}

int main() {
    try {
        const oa::netgame::network::UnitCreated created{
            0x1234, 0xabcd, {0x11223344, 0x80000000, 0xffffffff}, {0x0102, 0x0304, 0x0506}
        };
        const std::array<uint8_t, oa::netgame::network::unit_created_bytes> expected_created{
            9,    0x34, 0x12, 0xcd, 0xab, 0x44, 0x33, 0x22, 0x11, 0, 0, 0,
            0x80, 0xff, 0xff, 0xff, 0xff, 2,    1,    4,    3,    6, 5
        };
        require(
            oa::netgame::network::encode_unit_created(created) == expected_created,
            "unit creation payload layout"
        );
        require(
            oa::netgame::network::encode_unit_finished(0x1234, 0xabcd) ==
                std::array<uint8_t, oa::netgame::network::unit_finished_bytes>{
                    0x12, 0x34, 0x12, 0xcd, 0xab
                },
            "unit completion payload index ordering"
        );
        // Hand-derived: XOR applies only to indices [3,n-3), last three stay clear.
        const Bytes payload{0, 1, 2, 3, 4, 5, 6};
        const Bytes packet{3, 20, 0, 3, 5, 7, 5, 4, 5, 6};
        const auto stored_header = oa::netgame::network::condenser::read_frame_header(packet);
        require(stored_header.type == 3 && stored_header.checksum == 20, "stored frame header");
        require(oa::netgame::network::encode_stored_frame(payload) == packet, "stored bytes");
        require(oa::netgame::network::decode_frame(packet) == payload, "stored decode");
        require(oa::netgame::network::reencode_frame(packet) == packet, "wire preservation");
        require(
            oa::netgame::network::encode_stored_frame(Bytes{42}) == Bytes({3, 0, 0, 42}),
            "short packet"
        );
        const Bytes repetitive(1024, 'A');
        const auto compressed_output = oa::netgame::network::encode_frame(repetitive);
        require(
            compressed_output[0] == oa::netgame::network::condenser::compressed_frame,
            "send compression selection"
        );
        require(
            oa::netgame::network::decode_frame(compressed_output) == repetitive,
            "compressed send roundtrip"
        );
        require(
            oa::netgame::network::encode_frame(repetitive, false) ==
                oa::netgame::network::encode_stored_frame(repetitive),
            "compression disabled"
        );
        require(
            oa::netgame::network::encode_frame(Bytes(12, 'A')) ==
                oa::netgame::network::encode_stored_frame(Bytes(12, 'A')),
            "compression minimum payload"
        );
        // Tag: A, overlapping slot-1 match of length 5, then terminator.
        // Plain compressed body = 06 41 13 00 00 00, first three XORed by 3,4,5.
        const Bytes compressed{4, 96, 0, 5, 69, 22, 0, 0, 0};
        const auto compressed_header =
            oa::netgame::network::condenser::read_frame_header(compressed);
        require(
            compressed_header.type == 4 && compressed_header.checksum == 96,
            "compressed frame header"
        );
        const Bytes wide_checksum{3, 0x34, 0x12, 0};
        const auto wide_header = oa::netgame::network::condenser::read_frame_header(wide_checksum);
        require(
            wide_header.type == 3 && wide_header.checksum == 0x1234, "header checksum endianness"
        );
        require(
            oa::netgame::network::decode_frame(compressed) == Bytes(6, 'A'), "compressed decode"
        );
        require(
            oa::netgame::network::reencode_frame(compressed) == compressed,
            "compressed wire preservation"
        );
        auto bad = packet;
        bad[4] ^= 1;
        rejects([&] { (void)oa::netgame::network::decode_frame(bad); });
        rejects([] { (void)oa::netgame::network::decode_frame(Bytes{3, 0, 0}); });
        rejects([] { (void)oa::netgame::network::decode_frame(Bytes{2, 0, 0, 1}); });
        rejects([] { (void)oa::netgame::network::encode_stored_frame(Bytes{}); });
        rejects([] { (void)oa::netgame::network::encode_stored_frame(Bytes(65534)); });
        // Two overlapping length17 matches expand to35 bytes in an11-byte wire
        // frame. Retries must not consume a second transport packet.
        Transport transport;
        transport.packet = {4, 138, 0, 13, 69, 26, 6, 24, 0, 0, 0};
        oa::netgame::network::CondenserReceiver receiver;
        Bytes buffer(transport.packet.size(), 0xa5);
        auto size = static_cast<uint32_t>(buffer.size());
        using namespace oa::netgame::network::condenser;
        require(receiver.receive(transport, buffer, size) == buffer_too_small, "expanded capacity");
        require(size == 35 && receiver.has_pending_message(), "pending expanded payload");
        size = 1;
        require(receiver.receive(transport, buffer, size) == buffer_too_small, "small retry");
        require(
            transport.calls == 1 && transport.statistics_calls == 1, "retry transport/statistics"
        );
        buffer.assign(35, 0xa5);
        size = 35;
        require(receiver.receive(transport, buffer, size) == success, "deliver cached payload");
        require(
            buffer == Bytes(35, 'A') && !receiver.has_pending_message(), "cached payload content"
        );
        require(transport.calls == 1, "cached delivery polled transport");
        // Raw control records and disabled condenser mode bypass framing.
        transport.packet = {1, 2, 3, 4};
        buffer.assign(16, 0);
        size = 16;
        require(
            receiver.receive(transport, buffer, size) == success && size == 4, "control bypass"
        );
        transport.enabled = false;
        transport.packet = {4};
        size = 16;
        require(
            receiver.receive(transport, buffer, size) == success && size == 1, "disabled bypass"
        );
        transport.enabled = true;
        transport.packet = {3, 0, 0};
        size = 16;
        require(receiver.receive(transport, buffer, size) == no_message, "short transformed frame");
        transport.result = 123;
        size = 16;
        require(receiver.receive(transport, buffer, size) == 123, "transport status preservation");
        transport.result = success;
        // A compressed body that ends before its end marker is dropped, as is a
        // stored body larger than the receive storage.
        transport.packet = {4, 0, 0, 0};
        size = 16;
        require(receiver.receive(transport, buffer, size) == no_message, "malformed body dropped");
        require(!receiver.has_pending_message(), "a dropped body leaves nothing pending");
        transport.packet =
            oa::netgame::network::encode_stored_frame(Bytes(receive_storage_bytes + 1, 'B'));
        buffer.assign(transport.packet.size(), 0);
        size = static_cast<uint32_t>(buffer.size());
        require(receiver.receive(transport, buffer, size) == no_message, "oversized body dropped");
        // The non-throwing unwrap says what was wrong.
        const auto refused = oa::netgame::network::unwrap_frame(bad);
        require(
            !refused.ok() &&
                refused.error.detail ==
                    static_cast<uint16_t>(oa::netgame::network::FrameProblem::checksum_mismatch),
            "unwrap reports the checksum"
        );
        std::cout << "network tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
