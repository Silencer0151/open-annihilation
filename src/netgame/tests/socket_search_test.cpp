// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Where a search of the local networks sends its enumeration request, worked
// out from interface lists the test builds itself, the local address each
// message header carries, and how a typed TCP/IP address resolves. Nothing
// is sent.

#include "oa/netgame/socket_host.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

namespace sock = oa::netgame::sock;

namespace {

int failures = 0;
const char* current_test = "";

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            ++failures;                                                                            \
            std::fprintf(stderr, "%s: %s:%d: %s\n", current_test, __FILE__, __LINE__, #condition); \
        }                                                                                          \
    } while (0)

constexpr uint8_t any_address[4] = {0, 0, 0, 0};

/// Returns an interface that is up, running and broadcast-capable, with no broadcast address named.
///
/// @param a first byte of the address
/// @param b second byte
/// @param c third byte
/// @param d fourth byte
/// @param prefix netmask length in bits, 0 to 32
/// @return the interface
sock::LocalInterface lan(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint32_t prefix) {
    sock::LocalInterface local{};
    const uint8_t address[4] = {a, b, c, d};
    std::memcpy(local.address, address, 4);
    const uint32_t mask = prefix == 0 ? 0u : ~0u << (32 - prefix);
    local.netmask[0] = static_cast<uint8_t>(mask >> 24);
    local.netmask[1] = static_cast<uint8_t>(mask >> 16);
    local.netmask[2] = static_cast<uint8_t>(mask >> 8);
    local.netmask[3] = static_cast<uint8_t>(mask);
    local.up = true;
    local.running = true;
    local.broadcast_capable = true;
    return local;
}

/// Tells whether a target is the given address.
///
/// @param ip the target
/// @param a first byte of the expected address
/// @param b second byte
/// @param c third byte
/// @param d fourth byte
/// @return true when they are equal
bool is(const uint8_t ip[4], uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    const uint8_t expected[4] = {a, b, c, d};
    return std::memcmp(ip, expected, 4) == 0;
}

void broadcast_address_is_named_or_worked_out() {
    current_test = "broadcast_address_is_named_or_worked_out";
    sock::LocalInterface interfaces[3] = {
        lan(192, 168, 2, 26, 24), lan(10, 1, 2, 3, 16), lan(172, 16, 5, 9, 12)
    };
    const uint8_t named[4] = {192, 168, 2, 255};
    std::memcpy(interfaces[0].broadcast, named, 4);
    const auto targets = sock::search_targets(interfaces, 3, any_address);
    CHECK(targets.count == 3);
    CHECK(is(targets.ip[0], 192, 168, 2, 255));
    CHECK(is(targets.ip[1], 10, 1, 255, 255));
    CHECK(is(targets.ip[2], 172, 31, 255, 255));
}

void interfaces_a_search_cannot_use_are_left_out() {
    current_test = "interfaces_a_search_cannot_use_are_left_out";
    sock::LocalInterface loopback = lan(127, 0, 0, 1, 8);
    loopback.loopback = true;
    sock::LocalInterface down = lan(192, 168, 3, 4, 24);
    down.up = false;
    sock::LocalInterface no_link = lan(192, 168, 4, 4, 24);
    no_link.running = false;
    sock::LocalInterface tunnel = lan(10, 8, 0, 2, 24);
    tunnel.point_to_point = true;
    sock::LocalInterface no_broadcast = lan(192, 168, 5, 4, 24);
    no_broadcast.broadcast_capable = false;
    const sock::LocalInterface host_route = lan(192, 168, 6, 4, 32);
    const sock::LocalInterface no_netmask = lan(192, 168, 7, 4, 0);
    const sock::LocalInterface usable = lan(192, 168, 2, 26, 24);
    const sock::LocalInterface interfaces[8] = {
        loopback, down, no_link, tunnel, no_broadcast, host_route, no_netmask, usable
    };
    const auto targets = sock::search_targets(interfaces, 8, any_address);
    CHECK(targets.count == 1);
    CHECK(is(targets.ip[0], 192, 168, 2, 255));
    CHECK(sock::search_targets(interfaces, 7, any_address).count == 0);
    CHECK(sock::search_targets(nullptr, 0, any_address).count == 0);
}

void each_network_is_listed_once() {
    current_test = "each_network_is_listed_once";
    sock::LocalInterface interfaces[3] = {
        lan(192, 168, 2, 26, 24), lan(192, 168, 2, 27, 24), lan(192, 168, 2, 28, 24)
    };
    const uint8_t named[4] = {192, 168, 2, 255};
    std::memcpy(interfaces[2].broadcast, named, 4);
    const auto targets = sock::search_targets(interfaces, 3, any_address);
    CHECK(targets.count == 1);
    CHECK(is(targets.ip[0], 192, 168, 2, 255));
}

void a_bound_address_limits_the_search_to_its_interface() {
    current_test = "a_bound_address_limits_the_search_to_its_interface";
    sock::LocalInterface loopback = lan(127, 0, 0, 1, 8);
    loopback.loopback = true;
    const sock::LocalInterface interfaces[3] = {
        lan(192, 168, 2, 26, 24), lan(10, 1, 2, 3, 16), loopback
    };
    const uint8_t on_second[4] = {10, 1, 2, 3};
    auto targets = sock::search_targets(interfaces, 3, on_second);
    CHECK(targets.count == 1);
    CHECK(is(targets.ip[0], 10, 1, 255, 255));
    const uint8_t on_loopback[4] = {127, 0, 0, 1};
    CHECK(sock::search_targets(interfaces, 3, on_loopback).count == 0);
    const uint8_t elsewhere[4] = {192, 168, 9, 9};
    CHECK(sock::search_targets(interfaces, 3, elsewhere).count == 0);
    targets = sock::search_targets(interfaces, 3, any_address);
    CHECK(targets.count == 2);
}

void targets_stop_at_the_limit() {
    current_test = "targets_stop_at_the_limit";
    constexpr std::size_t listed = sock::max_search_targets + 4;
    sock::LocalInterface interfaces[listed]{};
    for (std::size_t i = 0; i < listed; ++i)
        interfaces[i] = lan(10, static_cast<uint8_t>(i), 0, 1, 16);
    const auto targets = sock::search_targets(interfaces, listed, any_address);
    CHECK(targets.count == sock::max_search_targets);
    CHECK(is(targets.ip[0], 10, 0, 255, 255));
    constexpr uint8_t last = static_cast<uint8_t>(sock::max_search_targets - 1);
    CHECK(is(targets.ip[last], 10, last, 255, 255));
}

void each_target_names_its_interface_address() {
    current_test = "each_target_names_its_interface_address";
    const sock::LocalInterface interfaces[3] = {
        lan(192, 168, 2, 26, 24), lan(192, 168, 2, 27, 24), lan(10, 1, 2, 3, 16)
    };
    const auto targets = sock::search_targets(interfaces, 3, any_address);
    CHECK(targets.count == 2);
    // A network two interfaces share is searched from the first.
    CHECK(is(targets.ip[0], 192, 168, 2, 255) && is(targets.source[0], 192, 168, 2, 26));
    CHECK(is(targets.ip[1], 10, 1, 255, 255) && is(targets.source[1], 10, 1, 2, 3));
}

void the_local_address_toward_a_destination_is_found() {
    current_test = "the_local_address_toward_a_destination_is_found";
    auto host = std::make_unique<sock::Host>();
    uint8_t ip[4] = {9, 9, 9, 9};
    // No address reaches every network or none.
    const uint8_t everywhere[4] = {255, 255, 255, 255};
    CHECK(!sock::local_address_toward(host.get(), everywhere, ip));
    CHECK(!sock::local_address_toward(host.get(), any_address, ip));
    CHECK(is(ip, 9, 9, 9, 9));
    // The system names 127.0.0.1 for this machine, and the answer is kept.
    const uint8_t this_machine[4] = {127, 0, 0, 1};
    CHECK(sock::local_address_toward(host.get(), this_machine, ip) && is(ip, 127, 0, 0, 1));
    CHECK(
        host->route_count == 1 && is(host->routes[0].destination, 127, 0, 0, 1) &&
        is(host->routes[0].local, 127, 0, 0, 1)
    );
    CHECK(sock::local_address_toward(host.get(), this_machine, ip) && host->route_count == 1);
    // A host bound to one address always uses it.
    const uint8_t bound[4] = {10, 1, 2, 3};
    std::memcpy(host->config.bind_ip, bound, 4);
    const uint8_t elsewhere[4] = {192, 168, 2, 1};
    CHECK(sock::local_address_toward(host.get(), elsewhere, ip) && is(ip, 10, 1, 2, 3));
    CHECK(host->route_count == 1);
    // The oldest remembered destination makes way once the table is full.
    std::memcpy(host->config.bind_ip, any_address, 4);
    for (uint8_t last = 2; last < 2 + sock::max_route_entries; ++last) {
        const uint8_t loopback[4] = {127, 0, 0, last};
        CHECK(sock::local_address_toward(host.get(), loopback, ip) && ip[0] == 127);
    }
    CHECK(
        host->route_count == sock::max_route_entries &&
        host->routes[0].destination[3] == 2 + sock::max_route_entries - 1
    );
}

void addresses_and_host_names_resolve() {
    current_test = "addresses_and_host_names_resolve";
    uint8_t ip[4]{};
    CHECK(sock::resolve_ipv4("192.168.2.26", ip) && is(ip, 192, 168, 2, 26));
    CHECK(sock::resolve_ipv4("127.1", ip) && is(ip, 127, 0, 0, 1));
    CHECK(sock::resolve_ipv4("10.1.2", ip) && is(ip, 10, 1, 0, 2));
    CHECK(sock::resolve_ipv4("0x7f.1", ip) && is(ip, 127, 0, 0, 1));
    CHECK(sock::resolve_ipv4("017.0.0.1", ip) && is(ip, 15, 0, 0, 1));
    CHECK(sock::resolve_ipv4("3232236058", ip) && is(ip, 192, 168, 2, 26));
    ip[0] = 0;
    CHECK(sock::resolve_ipv4("localhost", ip) && is(ip, 127, 0, 0, 1));
    // An empty label is not a host name: it fails without asking a name server.
    const uint8_t before[4] = {1, 2, 3, 4};
    std::memcpy(ip, before, 4);
    CHECK(!sock::resolve_ipv4("bad..address", ip));
    CHECK(!sock::resolve_ipv4("", ip));
    CHECK(!sock::resolve_ipv4(nullptr, ip));
    CHECK(is(ip, 1, 2, 3, 4));
}

} // namespace

int main() {
    broadcast_address_is_named_or_worked_out();
    interfaces_a_search_cannot_use_are_left_out();
    each_network_is_listed_once();
    a_bound_address_limits_the_search_to_its_interface();
    targets_stop_at_the_limit();
    each_target_names_its_interface_address();
    the_local_address_toward_a_destination_is_found();
    addresses_and_host_names_resolve();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("socket search: all tests passed");
    return 0;
}
