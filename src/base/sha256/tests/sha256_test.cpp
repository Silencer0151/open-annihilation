// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// SHA-256 over the FIPS 180-4 example messages, whose digests NIST publishes:
// the empty message, "abc", the 448-bit and 896-bit two-block messages and a
// million 'a's. Each is also fed in pieces of every size, and the digest's
// hexadecimal form is read and written.
#include "oa/base/sha256.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace sha256 = oa::base::sha256;

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

std::span<const uint8_t> bytes_of(std::string_view text) {
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

std::string hex_of(const sha256::Digest& digest) {
    const auto text = sha256::to_hex(digest);
    return {text.begin(), text.end()};
}

struct Example {
    std::string_view message;
    std::string_view digest;
};

// The published examples; the 448-bit message fills 56 bytes, so its padding
// needs a second block.
constexpr Example examples[]{
    {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
    {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
     "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnop"
     "q"
     "rsmnopqrstnopqrstu",
     "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
};

constexpr std::string_view million_a_digest =
    "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";

void test_examples() {
    for (const auto& example : examples) {
        CHECK(hex_of(sha256::digest_of(bytes_of(example.message))) == example.digest);
        // Every split into two pieces gives the same digest.
        for (size_t split = 0; split <= example.message.size(); ++split) {
            sha256::Hasher hasher;
            sha256::update(hasher, bytes_of(example.message.substr(0, split)));
            sha256::update(hasher, bytes_of(example.message.substr(split)));
            CHECK(hex_of(sha256::finish(hasher)) == example.digest);
        }
    }
}

void test_million_a() {
    const std::string message(1'000'000, 'a');
    CHECK(hex_of(sha256::digest_of(bytes_of(message))) == million_a_digest);
    // Pieces that straddle block boundaries in every way: sizes 1 to 200.
    sha256::Hasher hasher;
    size_t fed = 0;
    for (size_t piece = 1; fed < message.size(); piece = piece % 200 + 1) {
        const size_t size = std::min(piece, message.size() - fed);
        sha256::update(hasher, bytes_of(std::string_view(message).substr(fed, size)));
        fed += size;
    }
    CHECK(hasher.message_size == message.size());
    CHECK(hex_of(sha256::finish(hasher)) == million_a_digest);
}

void test_finish_leaves_hasher() {
    sha256::Hasher hasher;
    sha256::update(hasher, bytes_of("ab"));
    CHECK(sha256::finish(hasher) == sha256::digest_of(bytes_of("ab")));
    sha256::update(hasher, bytes_of("c"));
    CHECK(hex_of(sha256::finish(hasher)) == examples[1].digest);
}

void test_hex() {
    constexpr auto parsed = sha256::parse_hex(million_a_digest);
    static_assert(parsed.has_value() && (*parsed)[0] == 0xcd && (*parsed)[31] == 0xd0);
    CHECK(parsed && hex_of(*parsed) == million_a_digest);
    CHECK(
        sha256::parse_hex("CDC76E5C9914FB9281A1C7E284D73E67F1809A48A497200E046D39CCC7112CD0") ==
        parsed
    );
    CHECK(!sha256::parse_hex(""));
    CHECK(!sha256::parse_hex(million_a_digest.substr(1)));
    CHECK(!sha256::parse_hex(std::string(million_a_digest) + "0"));
    std::string wrong(million_a_digest);
    wrong[10] = 'g';
    CHECK(!sha256::parse_hex(wrong));
    wrong[10] = ' ';
    CHECK(!sha256::parse_hex(wrong));
}

} // namespace

int main() {
    test_examples();
    test_million_a();
    test_finish_leaves_hasher();
    test_hex();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("sha256 checks passed");
    return 0;
}
