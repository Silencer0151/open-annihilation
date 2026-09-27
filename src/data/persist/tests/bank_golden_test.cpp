// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The savegame bank container, pinned byte for byte: a small synthetic save
// of three accounts holding integer, double and string fields and blobs
// addressed by id and by name, written with packed and with plain account
// payloads, and its audit listing, must equal the values below on every
// platform, and both images must read back to the same accounts.
#include "check.hpp"

#include "oa/data/persist/hapibank.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

using namespace oa::data::persist;

namespace {

using Bytes = std::vector<uint8_t>;

constexpr uint64_t fnv_basis = 0xcbf29ce484222325ull;
constexpr uint64_t fnv_prime = 0x100000001b3ull;

constexpr int32_t players = 3;
constexpr double game_time = 1234.5;
constexpr int32_t unit_count = 2;
constexpr int32_t first_unit_blob = 0x10;
constexpr int32_t second_unit_blob = 0x11;
constexpr size_t unit_blob_bytes = 96;
constexpr size_t radar_blob_bytes = 300;
constexpr uint8_t unit_blob_step = 7;
constexpr uint8_t radar_blob_step = 3;

struct PinnedBytes {
    uint64_t size{};
    uint64_t hash{}; // 64-bit FNV-1a
};

constexpr PinnedBytes pinned_packed_image{821, 0x52235122c67c1e54ull};
constexpr PinnedBytes pinned_plain_image{850, 0x9c45fb6ffd289037ull};
constexpr PinnedBytes pinned_audit{516, 0x8a1c595c18530e39ull};

struct ScopedBank {
    Bank bank{};

    ScopedBank() { bank_init(&bank); }

    ~ScopedBank() { bank_destroy(&bank); }

    ScopedBank(const ScopedBank&) = delete;
    ScopedBank& operator=(const ScopedBank&) = delete;
};

struct ScopedImage {
    ByteImage image{};
    ScopedImage() = default;

    ~ScopedImage() { byte_image_free(&image); }

    ScopedImage(const ScopedImage&) = delete;
    ScopedImage& operator=(const ScopedImage&) = delete;
};

/// Returns blob bytes that step by a fixed amount from zero.
///
/// @param size number of bytes
/// @param step difference between neighbouring bytes, modulo 256
/// @return the bytes
Bytes stepped(size_t size, uint8_t step) {
    Bytes bytes(size);
    for (size_t i = 0; i < size; ++i)
        bytes[i] = static_cast<uint8_t>(i * step);
    return bytes;
}

/// Returns the 64-bit FNV-1a of bytes.
///
/// @param bytes bytes to hash
/// @return the hash
uint64_t fnv1a(std::span<const uint8_t> bytes) {
    uint64_t hash = fnv_basis;
    for (const uint8_t byte : bytes) {
        hash ^= byte;
        hash *= fnv_prime;
    }
    return hash;
}

/// Tells whether bytes match a pinned size and hash, printing the values found when they differ.
///
/// @param what name printed
/// @param bytes bytes produced
/// @param expected pinned size and hash
/// @return whether they match
bool matches_pinned(const char* what, std::span<const uint8_t> bytes, const PinnedBytes& expected) {
    const PinnedBytes found{bytes.size(), fnv1a(bytes)};
    if (found.size != expected.size || found.hash != expected.hash)
        std::fprintf(
            stderr,
            "%s is {%llu, 0x%016llxull}\n",
            what,
            static_cast<unsigned long long>(found.size),
            static_cast<unsigned long long>(found.hash)
        );
    return found.size == expected.size && found.hash == expected.hash;
}

/// Writes a blob of the open account in one piece.
///
/// @param[in,out] bank bank with the blob open
/// @param bytes blob contents
/// @return whether every byte was written
bool write_blob(Bank* bank, const Bytes& bytes) {
    return bank_blob_write(bank, bytes.data(), static_cast<uint32_t>(bytes.size())) == bytes.size();
}

/// Reads the whole open blob.
///
/// @param[in,out] bank bank with the blob open
/// @return its bytes
Bytes read_blob(Bank* bank) {
    Bytes bytes(static_cast<size_t>(bank_blob_size(bank)));
    bank_blob_seek(bank, 0);
    const uint32_t read = bank_blob_read(bank, bytes.data(), static_cast<uint32_t>(bytes.size()));
    bytes.resize(read);
    return bytes;
}

/// Fills a bank with the synthetic save: Summary (integer, double and
/// string fields), Units (a count and two blobs by id) and Radar Image (a
/// blob by name).
///
/// @param[out] bank bank to fill; reset first
void fill(Bank* bank) {
    CHECK(bank_reset(bank));
    CHECK(!bank_open_account(bank, "Summary"));
    CHECK(bank_set_int(bank, "Players", players));
    CHECK(bank_set_real(bank, "Game Time", game_time));
    CHECK(bank_set_text(bank, "Map", "Golden Plains"));
    CHECK(bank_set_text(bank, "Description", "a synthetic save"));
    CHECK(!bank_open_account(bank, "Units"));
    CHECK(bank_set_int(bank, "Number of Units", unit_count));
    CHECK(!bank_open_blob_id(bank, first_unit_blob));
    CHECK(write_blob(bank, stepped(unit_blob_bytes, unit_blob_step)));
    CHECK(!bank_open_blob_id(bank, second_unit_blob));
    CHECK(write_blob(bank, stepped(unit_blob_bytes, unit_blob_step + 1)));
    CHECK(!bank_open_account(bank, "Radar"));
    CHECK(!bank_open_blob_name(bank, "Radar Image"));
    CHECK(write_blob(bank, stepped(radar_blob_bytes, radar_blob_step)));
}

/// Checks that a bank read back holds the synthetic save.
///
/// @param[in,out] bank bank read from an image
void holds_the_save(Bank* bank) {
    CHECK(bank_open_account(bank, "Summary"));
    CHECK(bank_get_int(bank, "Players", 0) == players);
    CHECK(bank_get_real(bank, "Game Time", 0.0) == game_time);
    CHECK(std::strcmp(bank_get_text(bank, "Map", ""), "Golden Plains") == 0);
    CHECK(std::strcmp(bank_get_text(bank, "Description", ""), "a synthetic save") == 0);
    CHECK(bank_open_account(bank, "Units"));
    CHECK(bank_get_int(bank, "Number of Units", 0) == unit_count);
    CHECK(bank_open_blob_id(bank, first_unit_blob));
    CHECK(read_blob(bank) == stepped(unit_blob_bytes, unit_blob_step));
    CHECK(bank_open_blob_id(bank, second_unit_blob));
    CHECK(read_blob(bank) == stepped(unit_blob_bytes, unit_blob_step + 1));
    CHECK(bank_open_account(bank, "Radar"));
    CHECK(bank_open_blob_name(bank, "Radar Image"));
    CHECK(read_blob(bank) == stepped(radar_blob_bytes, radar_blob_step));
}

/// Writes the save as an image, checks the image against its pin and reads it back.
///
/// @param what name printed
/// @param bank bank holding the save
/// @param pack_accounts whether account payloads are LZ77-packed
/// @param expected pinned size and hash of the image
void image_matches_pin(
    const char* what, const Bank* bank, bool pack_accounts, const PinnedBytes& expected
) {
    ScopedImage image;
    CHECK(bank_write_image(bank, savegame_description, pack_accounts, &image.image));
    CHECK(
        matches_pinned(what, std::span<const uint8_t>(image.image.data, image.image.size), expected)
    );
    ScopedBank back;
    BankError error{};
    CHECK(bank_read_image(
        &back.bank, image.image.data, image.image.size, savegame_description, nullptr, &error
    ));
    holds_the_save(&back.bank);
}

/// Checks the packed and plain images and the audit listing of the synthetic save.
void save_matches_pins() {
    ScopedBank bank;
    fill(&bank.bank);
    image_matches_pin("packed image", &bank.bank, true, pinned_packed_image);
    image_matches_pin("plain image", &bank.bank, false, pinned_plain_image);
    ScopedImage audit;
    CHECK(bank_format_audit(&bank.bank, &audit.image));
    CHECK(matches_pinned(
        "audit", std::span<const uint8_t>(audit.image.data, audit.image.size), pinned_audit
    ));
}

} // namespace

int main() {
    save_matches_pins();
    return oa::data::persist::test::finish("persist-bank-golden");
}
