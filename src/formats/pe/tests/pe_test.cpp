// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The PE resource reader over synthetic images built here: resources found by
// type name, id and language in images with the 32-bit and the 64-bit
// optional header, and malformed images, each refused with its error and the
// offset where it was found: truncated headers, section tables and
// directories, loops in the resource directory, and RVAs outside the
// sections. Every truncation of an image and every single-byte corruption of
// its headers and directory is refused or answered with a range inside the
// file.
#include "oa/formats/pe.hpp"
#include "oa/test/pe_image.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

namespace pe = oa::formats::pe;
namespace image = oa::test::pe_image;
using image::get32;
using image::put16;
using image::put32;

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

// The resource the tests look for, and its key.
constexpr uint32_t archive_id = 130;
constexpr uint32_t english = 1033;
constexpr uint32_t german = 1031;
const pe::ResourceKey archive_key{"ADD", archive_id, english};

std::vector<uint8_t> pattern(size_t size, uint8_t seed) {
    std::vector<uint8_t> bytes(size);
    for (size_t index = 0; index < size; ++index)
        bytes[index] = static_cast<uint8_t>(seed + index * 7);
    return bytes;
}

image::Image archive_image(bool wide = false) {
    return image::build({{"ADD", archive_id, english, pattern(300, 1)}}, wide);
}

pe::ResourceLookup
find(const std::vector<uint8_t>& bytes, const pe::ResourceKey& key = archive_key) {
    return pe::find_resource(bytes, key);
}

bool holds(
    const std::vector<uint8_t>& bytes,
    const pe::ResourceLookup& lookup,
    const std::vector<uint8_t>& data
) {
    return lookup.error == pe::Error::none && lookup.range.size == data.size() &&
           lookup.range.offset + lookup.range.size <= bytes.size() &&
           std::equal(
               data.begin(),
               data.end(),
               bytes.begin() + static_cast<std::ptrdiff_t>(lookup.range.offset)
           );
}

// The error and the offset it names.
bool refused(const pe::ResourceLookup& lookup, pe::Error error, uint64_t offset) {
    return lookup.error == error && lookup.error_offset == offset && lookup.range.size == 0;
}

void test_finds_resources() {
    for (const bool wide : {false, true}) {
        const auto built = archive_image(wide);
        const auto lookup = find(built.bytes);
        CHECK(holds(built.bytes, lookup, pattern(300, 1)));
        CHECK(lookup.range.offset == built.data);
    }
    const std::vector<image::Resource> resources{
        {"ADD", archive_id, english, pattern(64, 2)},
        {"ADD", 135, english, pattern(17, 3)},
        {"ADD", 135, german, pattern(18, 4)},
        {"ADD", 136, english, pattern(1, 5)},
        {"BIN", archive_id, english, pattern(33, 6)},
    };
    const auto built = image::build(resources);
    for (const auto& resource : resources) {
        const auto lookup = find(built.bytes, {resource.type_name, resource.id, resource.language});
        CHECK(holds(built.bytes, lookup, resource.data));
    }
    CHECK(holds(built.bytes, find(built.bytes, {"add", archive_id, english}), resources[0].data));
    CHECK(holds(built.bytes, find(built.bytes, {"bin", archive_id, english}), resources[4].data));
    CHECK(find(built.bytes, {"AD", archive_id, english}).error == pe::Error::type_not_found);
    CHECK(find(built.bytes, {"ADDS", archive_id, english}).error == pe::Error::type_not_found);
    CHECK(find(built.bytes, {"TXT", archive_id, english}).error == pe::Error::type_not_found);
    CHECK(find(built.bytes, {"ADD", 131, english}).error == pe::Error::id_not_found);
    CHECK(find(built.bytes, {"ADD", archive_id, german}).error == pe::Error::language_not_found);
    CHECK(
        refused(find(built.bytes, {"ADD", 131, english}), pe::Error::id_not_found, built.type_table)
    );
    CHECK(refused(
        find(built.bytes, {"ADD", archive_id, german}),
        pe::Error::language_not_found,
        built.language_table
    ));
    CHECK(
        refused(find(built.bytes, {"TXT", 1, english}), pe::Error::type_not_found, built.root_table)
    );
}

void test_truncated_headers() {
    const auto built = archive_image();
    CHECK(refused(find({}), pe::Error::truncated_dos_header, 0));
    CHECK(refused(find({'M', 'Z'}), pe::Error::truncated_dos_header, 0));
    {
        auto bytes = built.bytes;
        bytes[0] = 'Z';
        CHECK(refused(find(bytes), pe::Error::missing_dos_signature, 0));
    }
    {
        auto bytes = built.bytes;
        put32(bytes, 0x3c, static_cast<uint32_t>(bytes.size() - 8));
        CHECK(refused(find(bytes), pe::Error::truncated_pe_header, 0x3c));
        put32(bytes, 0x3c, 0xffffffffu);
        CHECK(refused(find(bytes), pe::Error::truncated_pe_header, 0x3c));
    }
    {
        auto bytes = built.bytes;
        bytes[built.pe_header + 1] = 'X';
        CHECK(refused(find(bytes), pe::Error::missing_pe_signature, built.pe_header));
    }
    {
        auto bytes = built.bytes;
        put16(bytes, built.pe_header + 6, pe::limit::sections + 1);
        CHECK(refused(find(bytes), pe::Error::too_many_sections, built.pe_header + 6));
        // The most sections allowed, whose table runs past this small file.
        put16(bytes, built.pe_header + 6, pe::limit::sections);
        CHECK(refused(find(bytes), pe::Error::truncated_section_table, built.section_table));
    }
    {
        auto bytes = built.bytes;
        put16(bytes, built.pe_header + 20, 0xffff);
        CHECK(refused(find(bytes), pe::Error::truncated_optional_header, built.optional_header));
        put16(bytes, built.pe_header + 20, 1);
        CHECK(refused(find(bytes), pe::Error::truncated_optional_header, built.optional_header));
        // Too short for the data directory count.
        put16(bytes, built.pe_header + 20, 90);
        CHECK(refused(find(bytes), pe::Error::truncated_optional_header, built.optional_header));
        // Too short for the resource table's entry.
        put16(bytes, built.pe_header + 20, 100);
        CHECK(refused(find(bytes), pe::Error::truncated_optional_header, built.optional_header));
    }
    {
        auto bytes = built.bytes;
        put16(bytes, built.optional_header, 0x107);
        CHECK(refused(find(bytes), pe::Error::unknown_optional_header, built.optional_header));
    }
    {
        auto bytes = built.bytes;
        put32(bytes, built.optional_header + 92, 2);
        CHECK(refused(find(bytes), pe::Error::no_resource_directory, built.optional_header + 92));
    }
    // The whole image cut short at every length: refused until the data is
    // all there.
    const uint64_t data_end = built.data + 300;
    bool all_refused = true;
    for (size_t size = 0; size < data_end; ++size) {
        const std::vector<uint8_t> prefix(
            built.bytes.begin(), built.bytes.begin() + static_cast<std::ptrdiff_t>(size)
        );
        if (find(prefix).error == pe::Error::none)
            all_refused = false;
    }
    CHECK(all_refused);
    const std::vector<uint8_t> whole(
        built.bytes.begin(), built.bytes.begin() + static_cast<std::ptrdiff_t>(data_end)
    );
    CHECK(holds(whole, find(whole), pattern(300, 1)));
}

void test_resource_directory_bounds() {
    const auto built = archive_image();
    const size_t directory = built.resource_data_directory;
    {
        auto bytes = built.bytes;
        put32(bytes, directory, 0);
        CHECK(refused(find(bytes), pe::Error::no_resource_directory, directory));
        put32(bytes, directory, image::resource_section_rva);
        put32(bytes, directory + 4, 0);
        CHECK(refused(find(bytes), pe::Error::no_resource_directory, directory));
    }
    {
        auto bytes = built.bytes;
        // An RVA no section covers, and one past the end of the address space.
        put32(bytes, directory, 0x9000);
        CHECK(refused(find(bytes), pe::Error::resource_directory_outside_sections, directory));
        put32(bytes, directory, 0xfffffff0u);
        CHECK(refused(find(bytes), pe::Error::resource_directory_outside_sections, directory));
        // Inside the code section, but running past its end.
        put32(bytes, directory, image::code_section_rva + 0x100);
        CHECK(refused(find(bytes), pe::Error::resource_directory_outside_sections, directory));
    }
    {
        auto bytes = built.bytes;
        put32(bytes, directory + 4, 0x10000);
        CHECK(refused(find(bytes), pe::Error::resource_directory_outside_sections, directory));
    }
    {
        // The resource section's bytes placed past the end of the file.
        auto bytes = built.bytes;
        put32(bytes, built.resource_section_header + 20, static_cast<uint32_t>(bytes.size()));
        CHECK(refused(find(bytes), pe::Error::resource_directory_outside_sections, directory));
    }
    {
        auto bytes = built.bytes;
        put16(bytes, built.root_table + 12, 0xffff);
        CHECK(refused(find(bytes), pe::Error::truncated_resource_directory, built.root_table));
    }
    {
        auto bytes = built.bytes;
        put16(bytes, built.language_table + 14, 0xffff);
        CHECK(refused(find(bytes), pe::Error::truncated_resource_directory, built.language_table));
    }
    {
        // The type's name string pointed past the resources, then given a
        // length that runs past them.
        auto bytes = built.bytes;
        put32(bytes, built.root_table + 16, 0x80000000u | 0x7ffffff0u);
        CHECK(refused(find(bytes), pe::Error::truncated_resource_name, built.root_table + 16));
        bytes = built.bytes;
        put16(bytes, built.type_name, 0xffff);
        CHECK(refused(find(bytes), pe::Error::truncated_resource_name, built.type_name));
    }
    {
        // A subdirectory offset past the resources.
        auto bytes = built.bytes;
        put32(bytes, built.root_table + 20, 0x80000000u | 0x7ffffff0u);
        CHECK(refused(find(bytes), pe::Error::truncated_resource_directory, built.root_table + 20));
    }
}

void test_directory_shape() {
    const auto built = archive_image();
    const uint64_t root = built.root_table;
    const uint64_t type_target = built.root_table + 16 + 4;
    const uint64_t id_target = built.type_table + 16 + 4;
    const uint64_t language_target = built.language_table + 16 + 4;
    {
        // The type leads back to the root.
        auto bytes = built.bytes;
        put32(bytes, type_target, 0x80000000u);
        CHECK(refused(find(bytes), pe::Error::directory_loop, type_target));
    }
    {
        // The id leads back to the root, then to its own table.
        auto bytes = built.bytes;
        put32(bytes, id_target, 0x80000000u);
        CHECK(refused(find(bytes), pe::Error::directory_loop, id_target));
        put32(bytes, id_target, 0x80000000u | static_cast<uint32_t>(built.type_table - root));
        CHECK(refused(find(bytes), pe::Error::directory_loop, id_target));
    }
    {
        // The language leads to a directory instead of data.
        auto bytes = built.bytes;
        put32(bytes, language_target, 0x80000000u | static_cast<uint32_t>(built.type_table - root));
        CHECK(refused(find(bytes), pe::Error::unexpected_directory, language_target));
    }
    {
        // The type leads to data instead of a directory.
        auto bytes = built.bytes;
        put32(bytes, type_target, static_cast<uint32_t>(built.data_entry - root));
        CHECK(refused(find(bytes), pe::Error::unexpected_data_entry, type_target));
    }
    {
        // An id entry that carries a name instead of an id is not the id.
        auto bytes = built.bytes;
        put32(bytes, built.type_table + 16, 0x80000000u | archive_id);
        CHECK(find(bytes).error == pe::Error::id_not_found);
    }
    {
        // A named entry without the name bit is not a name.
        auto bytes = built.bytes;
        put32(bytes, built.root_table + 16, get32(bytes, built.root_table + 16) & 0x7fffffffu);
        CHECK(find(bytes).error == pe::Error::type_not_found);
    }
    {
        auto bytes = built.bytes;
        put32(bytes, language_target, 0x7ffffff8u);
        CHECK(refused(find(bytes), pe::Error::truncated_data_entry, language_target));
    }
}

void test_data_bounds() {
    const auto built = archive_image();
    {
        auto bytes = built.bytes;
        put32(bytes, built.data_entry, 0x7000);
        CHECK(refused(find(bytes), pe::Error::data_outside_sections, built.data_entry));
        put32(bytes, built.data_entry, 0xffffff00u);
        CHECK(refused(find(bytes), pe::Error::data_outside_sections, built.data_entry));
        // Starting inside the code section, but running past its end.
        put32(bytes, built.data_entry, image::code_section_rva + 0x100);
        CHECK(refused(find(bytes), pe::Error::data_outside_sections, built.data_entry));
    }
    {
        auto bytes = built.bytes;
        put32(bytes, built.data_entry + 4, 0x10000);
        CHECK(refused(find(bytes), pe::Error::data_outside_sections, built.data_entry));
        put32(bytes, built.data_entry + 4, 0xffffffffu);
        CHECK(refused(find(bytes), pe::Error::data_outside_sections, built.data_entry));
    }
    {
        // Data wholly inside the code section is found there.
        auto bytes = built.bytes;
        put32(bytes, built.data_entry, image::code_section_rva + 0x10);
        put32(bytes, built.data_entry + 4, 0x20);
        const auto lookup = find(bytes);
        CHECK(lookup.error == pe::Error::none && lookup.range.size == 0x20);
    }
}

void test_every_byte_corrupted() {
    // Every byte up to the end of the directory set to 0x00, 0x80 and 0xff
    // in turn: the lookup ends, and a range it reports lies in the file.
    const auto built = image::build(
        {{"ADD", archive_id, english, pattern(40, 7)}, {"ADD", 135, german, pattern(40, 8)}}
    );
    bool bounded = true;
    for (size_t offset = 0; offset < built.data; ++offset) {
        for (const uint8_t value : {uint8_t{0x00}, uint8_t{0x80}, uint8_t{0xff}}) {
            auto bytes = built.bytes;
            bytes[offset] = value;
            const auto lookup = find(bytes);
            if (lookup.error == pe::Error::none &&
                (lookup.range.offset > bytes.size() ||
                 lookup.range.size > bytes.size() - lookup.range.offset))
                bounded = false;
            if (lookup.error != pe::Error::none && lookup.error_offset > bytes.size())
                bounded = false;
        }
    }
    CHECK(bounded);
}

void test_descriptions() {
    CHECK(pe::describe(pe::Error::none).empty());
    for (int error = 1; error <= static_cast<int>(pe::Error::data_outside_sections); ++error) {
        const auto text = pe::describe(static_cast<pe::Error>(error));
        CHECK(!text.empty() && text != "an unknown error");
    }
}

} // namespace

int main() {
    test_finds_resources();
    test_truncated_headers();
    test_resource_directory_bounds();
    test_directory_shape();
    test_data_bounds();
    test_every_byte_corrupted();
    test_descriptions();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("pe resource checks passed");
    return 0;
}
