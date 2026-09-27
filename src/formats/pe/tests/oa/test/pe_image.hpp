// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Builds small synthetic PE images that carry resources, for the tests of the
// resource reader and of the code that unpacks a resource from a program
// file. The images hold headers, a code section of zeros and a resource
// section; nothing in them can run.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace oa::test::pe_image {

/// One resource to place in an image.
struct Resource {
    std::string type_name; ///< ASCII
    uint32_t id{};
    uint32_t language{};
    std::vector<uint8_t> data;
};

/// A built image and the file offsets of the structures tests corrupt.
struct Image {
    std::vector<uint8_t> bytes;
    size_t pe_header{};               ///< the PE signature
    size_t optional_header{};         ///< the optional header's magic
    size_t resource_data_directory{}; ///< the resource table's RVA and size
    size_t section_table{};
    size_t resource_section_header{}; ///< the resource section's header in the table
    size_t root_table{};              ///< the resource directory's root table
    // The path of the first resource: its type's table, its id's table,
    // its data entry, its name string and its data.
    size_t type_table{};
    size_t language_table{};
    size_t data_entry{};
    size_t type_name{};
    size_t data{};
};

/// Where the resource section starts in memory.
inline constexpr uint32_t resource_section_rva = 0x2000;
/// Where the code section starts in memory.
inline constexpr uint32_t code_section_rva = 0x1000;
/// Bytes the code section holds.
inline constexpr uint32_t code_section_size = 0x200;
/// File alignment of the section bytes.
inline constexpr size_t file_alignment = 0x200;

/// Writes a little-endian 16-bit value.
///
/// @param[in,out] bytes the buffer, at least `offset` + 2 bytes
/// @param offset where to write
/// @param value the value
inline void put16(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

/// Writes a little-endian 32-bit value.
///
/// @param[in,out] bytes the buffer, at least `offset` + 4 bytes
/// @param offset where to write
/// @param value the value
inline void put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (size_t index = 0; index < 4; ++index)
        bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

/// Reads a little-endian 32-bit value.
///
/// @param bytes the buffer
/// @param offset where to read
/// @return the value
[[nodiscard]] inline uint32_t get32(const std::vector<uint8_t>& bytes, size_t offset) {
    uint32_t value = 0;
    for (size_t index = 0; index < 4; ++index)
        value |= static_cast<uint32_t>(bytes[offset + index]) << (index * 8);
    return value;
}

/// Rounds a size up to a multiple of `alignment`.
///
/// @param size the size
/// @param alignment a power of two
/// @return the rounded size
[[nodiscard]] inline size_t align_up(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

/// Builds an image carrying `resources`.
///
/// The DOS header points to the PE header at 0x80; the optional header is
/// the 32-bit one, or the 64-bit one when `wide` is set, with sixteen data
/// directories. The resource directory holds the root table, then each
/// type's table followed by its ids' tables, then the data entries, the type
/// names and the data, each resource's data 4-byte aligned.
///
/// @param resources what to carry; resources of one type share its table
/// @param wide true for the 64-bit optional header
/// @return the image and where its parts lie
[[nodiscard]] inline Image build(const std::vector<Resource>& resources, bool wide = false) {
    // type name -> id -> language -> index into `resources`
    std::map<std::string, std::map<uint32_t, std::map<uint32_t, size_t>>> tree;
    for (size_t index = 0; index < resources.size(); ++index)
        tree[resources[index].type_name][resources[index].id][resources[index].language] = index;

    constexpr size_t table_size = 16;
    constexpr size_t entry_size = 8;
    constexpr size_t data_entry_size = 16;
    // Offsets within the resource section.
    std::vector<uint8_t> section(table_size + tree.size() * entry_size);
    std::vector<size_t> data_entries(resources.size());
    std::vector<size_t> language_tables(resources.size());
    std::vector<size_t> type_tables(resources.size());

    struct PendingName {
        size_t entry{};
        std::string name;
    };

    std::vector<PendingName> names;
    put16(section, 12, static_cast<uint32_t>(tree.size()));
    size_t type_index = 0;
    for (const auto& [type_name, ids] : tree) {
        const size_t type_table = section.size();
        const size_t root_entry = table_size + type_index++ * entry_size;
        names.push_back({root_entry, type_name});
        put32(section, root_entry + 4, 0x80000000u | static_cast<uint32_t>(type_table));
        section.resize(type_table + table_size + ids.size() * entry_size);
        put16(section, type_table + 14, static_cast<uint32_t>(ids.size()));
        size_t id_index = 0;
        for (const auto& [id, languages] : ids) {
            const size_t language_table = section.size();
            const size_t id_entry = type_table + table_size + id_index++ * entry_size;
            put32(section, id_entry, id);
            put32(section, id_entry + 4, 0x80000000u | static_cast<uint32_t>(language_table));
            section.resize(language_table + table_size + languages.size() * entry_size);
            put16(section, language_table + 14, static_cast<uint32_t>(languages.size()));
            size_t language_index = 0;
            for (const auto& [language, resource] : languages) {
                const size_t language_entry =
                    language_table + table_size + language_index++ * entry_size;
                put32(section, language_entry, language);
                type_tables[resource] = type_table;
                language_tables[resource] = language_table;
                // The data entry's offset is filled in below.
                data_entries[resource] = language_entry;
            }
        }
    }
    for (size_t resource = 0; resource < resources.size(); ++resource) {
        const size_t entry = section.size();
        section.resize(entry + data_entry_size);
        put32(section, data_entries[resource] + 4, static_cast<uint32_t>(entry));
        data_entries[resource] = entry;
    }
    std::vector<size_t> name_strings;
    for (const auto& pending : names) {
        const size_t string = section.size();
        name_strings.push_back(string);
        section.resize(string + 2 + pending.name.size() * 2);
        put16(section, string, static_cast<uint32_t>(pending.name.size()));
        for (size_t index = 0; index < pending.name.size(); ++index)
            put16(section, string + 2 + index * 2, static_cast<unsigned char>(pending.name[index]));
        put32(section, pending.entry, 0x80000000u | static_cast<uint32_t>(string));
    }
    std::vector<size_t> data_offsets(resources.size());
    for (size_t resource = 0; resource < resources.size(); ++resource) {
        const size_t data = align_up(section.size(), 4);
        section.resize(data + resources[resource].data.size());
        std::copy(
            resources[resource].data.begin(),
            resources[resource].data.end(),
            section.begin() + static_cast<std::ptrdiff_t>(data)
        );
        data_offsets[resource] = data;
        put32(section, data_entries[resource], resource_section_rva + static_cast<uint32_t>(data));
        put32(
            section,
            data_entries[resource] + 4,
            static_cast<uint32_t>(resources[resource].data.size())
        );
    }

    Image image;
    constexpr size_t pe_header = 0x80;
    constexpr size_t section_count = 2;
    constexpr size_t section_header_size = 40;
    const size_t optional_size = wide ? 240 : 224;
    const size_t optional_header = pe_header + 4 + 20;
    const size_t section_table = optional_header + optional_size;
    const size_t headers =
        align_up(section_table + section_count * section_header_size, file_alignment);
    const size_t code_offset = headers;
    const size_t resource_offset = code_offset + code_section_size;
    const size_t resource_raw_size = align_up(section.size(), file_alignment);
    image.bytes.assign(resource_offset + resource_raw_size, 0);
    auto& bytes = image.bytes;
    // DOS header: "MZ", and the PE header's offset at 0x3c.
    bytes[0] = 'M';
    bytes[1] = 'Z';
    put32(bytes, 0x3c, pe_header);
    // PE signature and file header: machine, section count, optional
    // header size and characteristics.
    bytes[pe_header] = 'P';
    bytes[pe_header + 1] = 'E';
    put16(bytes, pe_header + 4, wide ? 0x8664 : 0x14c);
    put16(bytes, pe_header + 6, section_count);
    put16(bytes, pe_header + 20, static_cast<uint32_t>(optional_size));
    put16(bytes, pe_header + 22, 0x102);
    // Optional header: magic, the data directory count and the resource
    // table's entry.
    put16(bytes, optional_header, wide ? 0x20b : 0x10b);
    const size_t directory_count = optional_header + (wide ? 108 : 92);
    put32(bytes, directory_count, 16);
    const size_t resource_directory = directory_count + 4 + 2 * 8;
    put32(bytes, resource_directory, resource_section_rva);
    put32(bytes, resource_directory + 4, static_cast<uint32_t>(section.size()));
    // Section headers: name, virtual size, virtual address, size in the
    // file and file offset.
    const auto section_header = [&](size_t index,
                                    const char* name,
                                    uint32_t virtual_size,
                                    uint32_t rva,
                                    size_t raw_size,
                                    size_t raw_offset) {
        const size_t header = section_table + index * section_header_size;
        for (size_t letter = 0; name[letter] != '\0'; ++letter)
            bytes[header + letter] = static_cast<uint8_t>(name[letter]);
        put32(bytes, header + 8, virtual_size);
        put32(bytes, header + 12, rva);
        put32(bytes, header + 16, static_cast<uint32_t>(raw_size));
        put32(bytes, header + 20, static_cast<uint32_t>(raw_offset));
        return header;
    };
    (void)section_header(
        0, ".text", code_section_size, code_section_rva, code_section_size, code_offset
    );
    image.resource_section_header = section_header(
        1,
        ".rsrc",
        static_cast<uint32_t>(section.size()),
        resource_section_rva,
        resource_raw_size,
        resource_offset
    );
    std::copy(
        section.begin(), section.end(), bytes.begin() + static_cast<std::ptrdiff_t>(resource_offset)
    );

    image.pe_header = pe_header;
    image.optional_header = optional_header;
    image.resource_data_directory = resource_directory;
    image.section_table = section_table;
    image.root_table = resource_offset;
    if (!resources.empty()) {
        image.type_table = resource_offset + type_tables[0];
        image.language_table = resource_offset + language_tables[0];
        image.data_entry = resource_offset + data_entries[0];
        image.data = resource_offset + data_offsets[0];
        const auto first_type =
            static_cast<size_t>(std::distance(tree.begin(), tree.find(resources[0].type_name)));
        image.type_name = resource_offset + name_strings[first_type];
    }
    return image;
}

} // namespace oa::test::pe_image
