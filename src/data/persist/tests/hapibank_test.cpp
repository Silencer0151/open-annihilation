// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"
#include "oa/base/bytes.hpp"

#include "oa/data/persist/hapibank.hpp"
#include "oa/test/scratch_directory.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

using namespace oa::data::persist;
using oa::base::bytes::load_le32;

namespace {

struct ScopedBank {
    Bank bank{};

    ScopedBank() {
        bank_init(&bank);
        bank_reset(&bank);
    }

    ~ScopedBank() { bank_destroy(&bank); }

    Bank* operator->() { return &bank; }

    Bank* get() { return &bank; }
};

struct ScopedImage {
    ByteImage image{};

    ~ScopedImage() { byte_image_free(&image); }

    std::vector<uint8_t> bytes() const { return {image.data, image.data + image.size}; }
};

void fields() {
    ScopedBank b;
    CHECK(!bank_set_int(b.get(), "x", 1)); // no open account
    CHECK(bank_get_int(b.get(), "x", 9) == 9);
    CHECK(!bank_open_account(b.get(), "Summary"));
    CHECK(bank_open_account(b.get(), "SUMMARY"));
    CHECK(b->accounts->count == 1);
    CHECK(bank_set_int(b.get(), "Game Time", 1234));
    CHECK(bank_set_real(b.get(), "Ratio", 0.25));
    CHECK(bank_set_text(b.get(), "Mission", "cormis1"));
    CHECK(!bank_set_text(b.get(), "Null", nullptr));
    CHECK(bank_get_int(b.get(), "game time", 0) == 1234);
    CHECK(bank_get_real(b.get(), "RATIO", 0) == 0.25);
    CHECK(std::strcmp(bank_get_text(b.get(), "mission", ""), "cormis1") == 0);
    CHECK(bank_get_int(b.get(), "Mission", -5) == -5); // type mismatch
    CHECK(bank_get_text(b.get(), "Game Time", nullptr) == nullptr);
    CHECK(bank_has_field(b.get(), "Ratio"));
    CHECK(!bank_has_field(b.get(), "Absent"));
    CHECK(bank_set_int(b.get(), "Mission", 7)); // replaces the string
    CHECK(bank_get_int(b.get(), "Mission", 0) == 7);
    CHECK(bank_find_field(b.get(), "Mission", false) == 2);
    CHECK(b->accounts->items[0].field_count == 3);
    bank_open_account(b.get(), "Other");
    CHECK(bank_get_int(b.get(), "Game Time", -1) == -1);
}

void blobs() {
    ScopedBank b;
    CHECK(!bank_open_blob_id(b.get(), 1)); // no account
    bank_open_account(b.get(), "Units");
    CHECK(!bank_open_blob_id(b.get(), 3));
    CHECK(bank_blob_size(b.get()) == 0);
    const char data[] = "abcdef";
    CHECK(bank_blob_write(b.get(), data, 6) == 6);
    CHECK(bank_blob_size(b.get()) == 6);
    CHECK(bank_open_blob_id(b.get(), 3));
    bank_blob_seek(b.get(), 4);
    char out[8] = {};
    CHECK(bank_blob_read(b.get(), out, 8) == 2);
    CHECK(out[0] == 'e' && out[1] == 'f');
    CHECK(bank_blob_read(b.get(), out, 8) == 0);
    bank_blob_seek(b.get(), -3);
    CHECK(bank_blob_read(b.get(), out, 1) == 1 && out[0] == 'a');
    bank_blob_seek(b.get(), 100);
    CHECK(bank_blob_write(b.get(), "gh", 2) == 2);
    CHECK(bank_blob_size(b.get()) == 8);
    CHECK(!bank_open_blob_name(b.get(), "Plotmap"));
    bank_blob_write(b.get(), "zz", 2);
    CHECK(bank_find_blob_name(b.get(), "PLOTMAP", false) == 1);
    CHECK(bank_find_blob_id(b.get(), 3, false) == 0);
    CHECK(bank_find_blob_id(b.get(), 4, false) == -1);
    // Reopening an account clears its open blob.
    bank_open_account(b.get(), "Units");
    CHECK(b->accounts->items[0].open_blob == -1);
    CHECK(bank_blob_size(b.get()) == 0);
    CHECK(bank_blob_write(b.get(), "q", 1) == 0);
}

// Byte layout of a small unpacked bank.
void image_layout() {
    ScopedBank b;
    bank_open_account(b.get(), "A");
    bank_set_int(b.get(), "x", 5);
    bank_open_blob_id(b.get(), 3);
    bank_blob_write(b.get(), "\x11\x22", 2);
    ScopedImage image;
    CHECK(bank_write_image(b.get(), savegame_description, false, &image.image));
    const auto bytes = image.bytes();
    const uint32_t account = bank_header_bytes;
    const uint32_t record = account_header_bytes + int_entry_bytes + blob_entry_bytes + 2;
    CHECK(std::memcmp(bytes.data(), "HAPIBANK", 8) == 0);
    CHECK(load_le32(bytes.data() + bank_header::description) == 0);
    CHECK(load_le32(bytes.data() + bank_header::pool_offset) == account + record);
    CHECK(load_le32(bytes.data() + bank_header::first_account) == bank_header_bytes);
    CHECK(load_le32(bytes.data() + bank_header::version) == 1);
    CHECK(bytes[bank_header::pool_packed] == 0);
    for (std::size_t i = bank_header::pool_packed + 1; i < bank_header_bytes; ++i)
        CHECK(bytes[i] == 0);
    CHECK(load_le32(bytes.data() + account + account_header::size) == record);
    CHECK(load_le32(bytes.data() + account + account_header::name) == 23);
    CHECK(load_le32(bytes.data() + account + account_header::int_count) == 1);
    CHECK(load_le32(bytes.data() + account + account_header::real_count) == 0);
    CHECK(load_le32(bytes.data() + account + account_header::text_count) == 0);
    CHECK(load_le32(bytes.data() + account + account_header::blob_count) == 1);
    CHECK(load_le32(bytes.data() + account + account_header::packed) == 0);
    CHECK(load_le32(bytes.data() + account + 0x1c) == 0);
    const uint32_t entries = account + account_header_bytes;
    CHECK(load_le32(bytes.data() + entries) == 25);
    CHECK(load_le32(bytes.data() + entries + 4) == 5);
    CHECK(load_le32(bytes.data() + entries + 8) == 0xffffffffu);
    CHECK(load_le32(bytes.data() + entries + 12) == 3);
    CHECK(load_le32(bytes.data() + entries + 16) == entries + 24);
    CHECK(load_le32(bytes.data() + entries + 20) == 2);
    CHECK(bytes[entries + 24] == 0x11 && bytes[entries + 25] == 0x22);
    const std::string pool(
        reinterpret_cast<const char*>(bytes.data()) + account + record,
        bytes.size() - account - record
    );
    CHECK(pool == std::string("Total Annihilation 3.0\0A\0x\0", 27));
}

void empty_accounts_are_skipped() {
    ScopedBank b;
    ScopedImage image;
    CHECK(!bank_write_image(b.get(), "d", false, &image.image)); // no accounts at all
    bank_open_account(b.get(), "Empty");
    bank_open_account(b.get(), "Full");
    bank_set_int(b.get(), "v", 1);
    CHECK(bank_write_image(b.get(), "d", false, &image.image));
    ScopedBank back;
    BankError error{};
    CHECK(bank_read_image(back.get(), image.image.data, image.image.size, "D", nullptr, &error));
    CHECK(back->accounts->count == 1);
    CHECK(std::strcmp(back->accounts->items[0].name, "Full") == 0);
}

// The id slot of a named blob entry repeats the last double name offset.
void named_blob_carries_scratch_slot() {
    ScopedBank b;
    bank_open_account(b.get(), "Acc");
    bank_set_real(b.get(), "r", 1.5);
    bank_open_blob_name(b.get(), "Blob");
    bank_blob_write(b.get(), "x", 1);
    ScopedImage image;
    CHECK(bank_write_image(b.get(), "d", false, &image.image));
    const auto bytes = image.bytes();
    const uint32_t entries = bank_header_bytes + account_header_bytes;
    const uint32_t real_name = load_le32(bytes.data() + entries);
    CHECK(real_name == 6); // "d\0Acc\0r"
    const uint32_t blob = entries + real_entry_bytes;
    CHECK(load_le32(bytes.data() + blob) == 8); // "Blob"
    CHECK(load_le32(bytes.data() + blob + 4) == real_name);
    double value = 0;
    std::memcpy(&value, bytes.data() + entries + 4, 8);
    CHECK(value == 1.5);
}

void fill_rich(Bank* bank) {
    bank_open_account(bank, "Summary");
    bank_set_int(bank, "Game ID", 0x29a);
    bank_set_text(bank, "Mission", "Core Prime");
    bank_set_real(bank, "Scale", -2.5);
    bank_open_blob_name(bank, "Radar Image");
    std::vector<uint8_t> image(4000);
    for (std::size_t i = 0; i < image.size(); ++i)
        image[i] = static_cast<uint8_t>((i / 13) & 0x7);
    bank_blob_write(bank, image.data(), static_cast<uint32_t>(image.size()));
    bank_open_account(bank, "Units");
    for (int id = 0; id < 5; ++id) {
        bank_open_blob_id(bank, id);
        std::vector<uint8_t> record(0xb8, static_cast<uint8_t>(id));
        bank_blob_write(bank, record.data(), static_cast<uint32_t>(record.size()));
    }
    bank_set_int(bank, "Number of Units", 5);
    bank_set_int(bank, "Version", 0x11);
    bank_open_account(bank, "Camera");
    bank_set_int(bank, "X Position", 640);
    bank_set_int(bank, "Z Position", -32);
}

bool same_banks(const Bank* a, const Bank* b) {
    if (a->accounts->count != b->accounts->count)
        return false;
    for (int i = 0; i < a->accounts->count; ++i) {
        const BankAccount& x = a->accounts->items[i];
        const BankAccount& y = b->accounts->items[i];
        if (std::strcmp(x.name, y.name) != 0 || x.blob_count != y.blob_count)
            return false;
        int fields_x = 0, fields_y = 0;
        for (int f = 0; f < x.field_count; ++f)
            fields_x += x.fields[f].type != BankFieldType::unset;
        for (int f = 0; f < y.field_count; ++f)
            fields_y += y.fields[f].type != BankFieldType::unset;
        if (fields_x != fields_y)
            return false;
        for (int k = 0; k < x.blob_count; ++k) {
            const BankBlob& p = x.blobs[k];
            const BankBlob& q = y.blobs[k];
            if (p.named != q.named || p.size != q.size || std::memcmp(p.data, q.data, p.size) != 0)
                return false;
            if (p.named ? std::strcmp(p.name, q.name) != 0 : p.id != q.id)
                return false;
        }
    }
    return true;
}

void round_trip(bool pack) {
    ScopedBank b;
    fill_rich(b.get());
    ScopedImage first;
    CHECK(bank_write_image(b.get(), savegame_description, pack, &first.image));
    if (pack) {
        // The compressible accounts are LZ77-packed.
        CHECK(load_le32(first.bytes().data() + bank_header_bytes + account_header::packed) == 1);
    }
    ScopedBank back;
    BankError error{};
    CHECK(bank_read_image(
        back.get(), first.image.data, first.image.size, savegame_description, nullptr, &error
    ));
    CHECK(same_banks(b.get(), back.get()));
    bank_open_account(back.get(), "Summary");
    CHECK(bank_get_int(back.get(), "Game ID", 0) == 0x29a);
    CHECK(bank_get_real(back.get(), "Scale", 0) == -2.5);
    CHECK(std::strcmp(bank_get_text(back.get(), "Mission", ""), "Core Prime") == 0);
    bank_open_account(back.get(), "Camera");
    CHECK(bank_get_int(back.get(), "Z Position", 0) == -32);
    ScopedImage second;
    CHECK(bank_write_image(back.get(), savegame_description, pack, &second.image));
    CHECK(first.bytes() == second.bytes());
}

void read_filters_and_errors() {
    ScopedBank b;
    fill_rich(b.get());
    ScopedImage image;
    bank_write_image(b.get(), savegame_description, true, &image.image);
    BankError error{};
    ScopedBank only;
    CHECK(
        bank_read_image(only.get(), image.image.data, image.image.size, nullptr, "summary", &error)
    );
    CHECK(only->accounts->count == 1);
    ScopedBank other;
    CHECK(!bank_read_image(
        other.get(), image.image.data, image.image.size, "Something else", nullptr, &error
    ));
    auto bytes = image.bytes();
    bytes[0] = 'h';
    CHECK(!bank_read_image(
        other.get(), bytes.data(), static_cast<uint32_t>(bytes.size()), nullptr, nullptr, &error
    ));
    bytes = image.bytes();
    for (uint32_t cut = 0; cut < bytes.size(); cut += 97) {
        ScopedBank partial;
        bank_read_image(
            partial.get(), bytes.data(), cut, nullptr, nullptr, &error
        ); // must not crash
    }
    // Corrupt the packed account payload: reported as a decompression error.
    bytes = image.bytes();
    bytes[bank_header_bytes + account_header_bytes + 0x20] ^= 0x5a;
    CHECK(!bank_read_image(
        other.get(), bytes.data(), static_cast<uint32_t>(bytes.size()), nullptr, nullptr, &error
    ));
    CHECK(std::strstr(error.message, "account data failed to unpack") != nullptr);
}

void audit_text() {
    ScopedBank b;
    bank_open_account(b.get(), "Acc");
    bank_set_int(b.get(), "i", -1);
    bank_set_real(b.get(), "d", 0.5);
    bank_set_text(b.get(), "s", "t");
    bank_find_field(b.get(), "u", true);
    bank_open_blob_id(b.get(), 2);
    bank_blob_write(b.get(), "abc", 3);
    bank_open_blob_name(b.get(), "N");
    ScopedImage text;
    CHECK(bank_format_audit(b.get(), &text.image));
    const std::string got(reinterpret_cast<const char*>(text.image.data), text.image.size);
    const std::string expected =
        "Saved-game bank contents\r\n\r\nAccounts in this bank: 1\r\n\r\n"
        "Bank account \"Acc\"\r\n{\r\n"
        "   \"i\" is a whole number: -1 (0xffffffff)\r\n   \"d\" is a real number: 0.500000\r\n"
        "   \"s\" is text: \"t\"\r\n   \"u\" has no value or an unknown type\r\n\r\n"
        "   Data block #2 holds 3 bytes\r\n   Data block \"N\" holds 0 bytes\r\n"
        "}\r\n\r\n(end of listing)\r\n";
    CHECK(got == expected);
}

void file_round_trip() {
    const auto dir = oa::test::make_scratch_directory("oa-persist-test");
    std::filesystem::create_directories(dir);
    const auto path = (dir / "game.sav").string();
    ScopedBank b;
    fill_rich(b.get());
    const FileSink sink = stdio_file_sink();
    CHECK(bank_write_file(b.get(), path.c_str(), savegame_description, true, true, &sink));
    CHECK(std::filesystem::exists(dir / "game.cpa"));
    // A path longer than any of the game's own path fields keeps its audit
    // listing beside it, under the whole name, where the system makes a
    // folder that deep (Windows does once long paths are on).
    auto deep = dir;
    while (deep.native().size() < 300)
        deep /= std::string(60, 'd');
    std::error_code made;
    std::filesystem::create_directories(deep, made);
    if (!made) {
        const auto deep_path = (deep / "deep.sav").string();
        CHECK(bank_write_file(b.get(), deep_path.c_str(), savegame_description, true, true, &sink));
        CHECK(std::filesystem::exists(deep / "deep.cpa"));
    }
    ScopedBank back;
    const FileSource source = stdio_file_source();
    BankError error{};
    CHECK(bank_read_file(back.get(), path.c_str(), savegame_description, nullptr, &source, &error));
    CHECK(same_banks(b.get(), back.get()));
    CHECK(!bank_read_file(
        back.get(), (dir / "missing.sav").string().c_str(), nullptr, nullptr, &source, &error
    ));
    std::filesystem::remove_all(dir);
}

} // namespace

int main() {
    fields();
    blobs();
    image_layout();
    empty_accounts_are_skipped();
    named_blob_carries_scratch_slot();
    round_trip(false);
    round_trip(true);
    read_filters_and_errors();
    audit_text();
    file_round_trip();
    return oa::data::persist::test::finish("persist-hapibank");
}
