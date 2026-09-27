// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The PE headers, the section table and the resource directory walk.
#include "oa/formats/pe.hpp"

#include <algorithm>
#include <array>
#include <optional>

namespace oa::formats::pe {
namespace {

// The DOS header: "MZ" at +0 and, at +0x3c, the file offset of the PE
// signature.
constexpr uint64_t dos_header_size = 64;
constexpr uint16_t dos_signature = 0x5a4d;
constexpr uint64_t pe_header_offset_field = 0x3c;

// The PE signature "PE\0\0", then the file header: the section count at +2
// and the optional header's size at +16.
constexpr uint32_t pe_signature = 0x00004550;
constexpr uint64_t pe_signature_size = 4;
constexpr uint64_t file_header_size = 20;
constexpr uint64_t section_count_field = 2;
constexpr uint64_t optional_header_size_field = 16;

// The optional header starts with its magic, which tells the 32-bit layout
// (PE32) from the 64-bit one (PE32+); the count of data directories and the
// directories themselves follow the fields each layout holds.
constexpr uint64_t optional_magic_size = 2;
constexpr uint16_t pe32_magic = 0x10b;
constexpr uint16_t pe32_plus_magic = 0x20b;
constexpr uint64_t pe32_directory_count_field = 92;
constexpr uint64_t pe32_plus_directory_count_field = 108;
constexpr uint64_t directory_count_size = 4;
constexpr uint64_t pe32_data_directory_table = 96;
constexpr uint64_t pe32_plus_data_directory_table = 112;
// A data directory: the RVA at +0 and the size at +4. The resource table is
// the third.
constexpr uint64_t data_directory_size = 8;
constexpr uint32_t resource_directory_index = 2;

// A section header: virtual size at +8, virtual address at +12, size of the
// bytes in the file at +16 and their file offset at +20.
constexpr uint64_t section_header_size = 40;
constexpr uint64_t section_virtual_size_field = 8;
constexpr uint64_t section_virtual_address_field = 12;
constexpr uint64_t section_raw_size_field = 16;
constexpr uint64_t section_raw_offset_field = 20;

// A resource directory table: the named entry count at +12 and the id entry
// count at +14, then the named entries and the id entries, 8 bytes each: the
// name or id at +0 and the target at +4. Offsets inside the resource
// directory count from its root.
constexpr uint64_t directory_table_size = 16;
constexpr uint64_t named_count_field = 12;
constexpr uint64_t id_count_field = 14;
constexpr uint64_t directory_entry_size = 8;
constexpr uint64_t entry_target_field = 4;
// Set in an entry's name: the rest is the offset of a name string (a 16-bit
// length and that many UTF-16 code units).
constexpr uint32_t name_is_string = 0x80000000;
// Set in an entry's target: the rest is the offset of a subdirectory, not of
// a data entry.
constexpr uint32_t target_is_directory = 0x80000000;
constexpr uint64_t name_length_size = 2;
constexpr uint64_t name_unit_size = 2;
// A data entry: the data's RVA at +0 and its size at +4.
constexpr uint64_t data_entry_size = 16;
constexpr uint64_t data_size_field = 4;
// Type, id and language.
constexpr size_t resource_levels = 3;
// Code units below this are ASCII.
constexpr uint16_t ascii_limit = 0x80;

// Where one section's bytes lie in the file.
struct Section {
    uint32_t virtual_address{};
    // Bytes the file holds for it: its size in the file, or its virtual size
    // when that is smaller and not zero.
    uint32_t file_size{};
    uint32_t file_offset{};
};

[[nodiscard]] uint16_t read_le16(std::span<const uint8_t> bytes, uint64_t offset) noexcept {
    return static_cast<uint16_t>(bytes[offset] | bytes[offset + 1] << 8);
}

[[nodiscard]] uint32_t read_le32(std::span<const uint8_t> bytes, uint64_t offset) noexcept {
    return static_cast<uint32_t>(bytes[offset]) | static_cast<uint32_t>(bytes[offset + 1]) << 8 |
           static_cast<uint32_t>(bytes[offset + 2]) << 16 |
           static_cast<uint32_t>(bytes[offset + 3]) << 24;
}

// Tests whether `size` bytes from `offset` lie within `total` bytes.
[[nodiscard]] bool fits(uint64_t total, uint64_t offset, uint64_t size) noexcept {
    return offset <= total && size <= total - offset;
}

// The file offset of `size` bytes at `rva`, when they lie within one
// section's bytes in the file and within the image.
[[nodiscard]] std::optional<uint64_t> file_offset_of(
    std::span<const Section> sections, uint64_t image_size, uint32_t rva, uint64_t size
) noexcept {
    for (const auto& section : sections) {
        if (rva < section.virtual_address)
            continue;
        const uint64_t into = rva - section.virtual_address;
        if (into >= section.file_size || size > section.file_size - into)
            continue;
        const uint64_t offset = uint64_t{section.file_offset} + into;
        if (fits(image_size, offset, size))
            return offset;
    }
    return std::nullopt;
}

// Tests whether a name string holds `name`, ASCII letters in either case.
[[nodiscard]] bool name_matches(
    std::span<const uint8_t> resources, uint64_t string, uint16_t length, std::string_view name
) noexcept {
    if (length != name.size())
        return false;
    const auto lower = [](uint16_t unit) {
        return unit >= 'A' && unit <= 'Z' ? static_cast<uint16_t>(unit - 'A' + 'a') : unit;
    };
    for (size_t index = 0; index < name.size(); ++index) {
        const uint16_t unit =
            read_le16(resources, string + name_length_size + index * name_unit_size);
        const auto wanted = static_cast<uint16_t>(static_cast<unsigned char>(name[index]));
        if (unit >= ascii_limit || lower(unit) != lower(wanted))
            return false;
    }
    return true;
}

} // namespace

ResourceLookup find_resource(std::span<const uint8_t> image, const ResourceKey& key) noexcept {
    ResourceLookup result;
    const auto fail = [&result](Error error, uint64_t offset) {
        result.error = error;
        result.error_offset = offset;
        return result;
    };
    const uint64_t size = image.size();
    if (size < dos_header_size)
        return fail(Error::truncated_dos_header, 0);
    if (read_le16(image, 0) != dos_signature)
        return fail(Error::missing_dos_signature, 0);
    const uint64_t pe_header = read_le32(image, pe_header_offset_field);
    if (!fits(size, pe_header, pe_signature_size + file_header_size))
        return fail(Error::truncated_pe_header, pe_header_offset_field);
    if (read_le32(image, pe_header) != pe_signature)
        return fail(Error::missing_pe_signature, pe_header);
    const uint64_t file_header = pe_header + pe_signature_size;
    const uint16_t section_count = read_le16(image, file_header + section_count_field);
    if (section_count > limit::sections)
        return fail(Error::too_many_sections, file_header + section_count_field);
    const uint64_t optional_size = read_le16(image, file_header + optional_header_size_field);
    const uint64_t optional_header = file_header + file_header_size;
    if (optional_size < optional_magic_size || !fits(size, optional_header, optional_size))
        return fail(Error::truncated_optional_header, optional_header);
    const uint16_t magic = read_le16(image, optional_header);
    uint64_t directory_count_field = 0;
    uint64_t data_directories = 0;
    if (magic == pe32_magic) {
        directory_count_field = pe32_directory_count_field;
        data_directories = pe32_data_directory_table;
    } else if (magic == pe32_plus_magic) {
        directory_count_field = pe32_plus_directory_count_field;
        data_directories = pe32_plus_data_directory_table;
    } else {
        return fail(Error::unknown_optional_header, optional_header);
    }
    if (!fits(optional_size, directory_count_field, directory_count_size))
        return fail(Error::truncated_optional_header, optional_header);
    if (read_le32(image, optional_header + directory_count_field) <= resource_directory_index)
        return fail(Error::no_resource_directory, optional_header + directory_count_field);
    const uint64_t resource_entry =
        data_directories + resource_directory_index * data_directory_size;
    if (!fits(optional_size, resource_entry, data_directory_size))
        return fail(Error::truncated_optional_header, optional_header);
    const uint32_t resource_rva = read_le32(image, optional_header + resource_entry);
    const uint32_t resource_size = read_le32(image, optional_header + resource_entry + 4);

    const uint64_t section_table = optional_header + optional_size;
    if (!fits(size, section_table, section_count * section_header_size))
        return fail(Error::truncated_section_table, section_table);
    std::array<Section, limit::sections> section_storage{};
    for (uint16_t index = 0; index < section_count; ++index) {
        const uint64_t header = section_table + index * section_header_size;
        const uint32_t virtual_size = read_le32(image, header + section_virtual_size_field);
        const uint32_t raw_size = read_le32(image, header + section_raw_size_field);
        auto& section = section_storage[index];
        section.virtual_address = read_le32(image, header + section_virtual_address_field);
        section.file_size = virtual_size == 0 ? raw_size : std::min(virtual_size, raw_size);
        section.file_offset = read_le32(image, header + section_raw_offset_field);
    }
    const std::span<const Section> sections(section_storage.data(), section_count);

    if (resource_rva == 0 || resource_size == 0)
        return fail(Error::no_resource_directory, optional_header + resource_entry);
    const auto resource_offset = file_offset_of(sections, size, resource_rva, resource_size);
    if (!resource_offset)
        return fail(Error::resource_directory_outside_sections, optional_header + resource_entry);
    const auto resources = image.subspan(*resource_offset, resource_size);

    // The root, then the type's, then the id's directory: the tables on the
    // path, so that a subdirectory leading back to one of them is refused.
    std::array<uint64_t, resource_levels> path{};
    uint64_t table = 0;
    // The file offset of the field that leads to `table`.
    uint64_t table_pointer = optional_header + resource_entry;
    for (size_t level = 0; level < resource_levels; ++level) {
        path[level] = table;
        if (!fits(resource_size, table, directory_table_size))
            return fail(Error::truncated_resource_directory, table_pointer);
        const uint64_t named = read_le16(resources, table + named_count_field);
        const uint64_t ids = read_le16(resources, table + id_count_field);
        const uint64_t entries = table + directory_table_size;
        if (!fits(resource_size, entries, (named + ids) * directory_entry_size))
            return fail(Error::truncated_resource_directory, *resource_offset + table);
        // The type is found among the named entries, the id and the
        // language among the id entries.
        const uint64_t first = level == 0 ? 0 : named;
        const uint64_t last = level == 0 ? named : named + ids;
        std::optional<uint64_t> found;
        for (uint64_t index = first; index < last && !found; ++index) {
            const uint64_t entry = entries + index * directory_entry_size;
            const uint32_t name = read_le32(resources, entry);
            if (level == 0) {
                if ((name & name_is_string) == 0)
                    continue;
                const uint64_t string = name & ~name_is_string;
                if (!fits(resource_size, string, name_length_size))
                    return fail(Error::truncated_resource_name, *resource_offset + entry);
                const uint16_t length = read_le16(resources, string);
                if (!fits(resource_size, string + name_length_size, length * name_unit_size))
                    return fail(Error::truncated_resource_name, *resource_offset + string);
                if (name_matches(resources, string, length, key.type_name))
                    found = entry;
            } else if (
                (name & name_is_string) == 0 && name == (level == 1 ? key.id : key.language)
            ) {
                found = entry;
            }
        }
        if (!found) {
            constexpr std::array<Error, resource_levels> missing{
                Error::type_not_found, Error::id_not_found, Error::language_not_found
            };
            return fail(missing[level], *resource_offset + table);
        }
        const uint64_t target_field = *found + entry_target_field;
        const uint32_t target = read_le32(resources, target_field);
        const uint64_t next = target & ~target_is_directory;
        if (level + 1 < resource_levels) {
            if ((target & target_is_directory) == 0)
                return fail(Error::unexpected_data_entry, *resource_offset + target_field);
            if (std::find(path.begin(), path.begin() + level + 1, next) != path.begin() + level + 1)
                return fail(Error::directory_loop, *resource_offset + target_field);
            table = next;
            table_pointer = *resource_offset + target_field;
            continue;
        }
        if ((target & target_is_directory) != 0)
            return fail(Error::unexpected_directory, *resource_offset + target_field);
        if (!fits(resource_size, next, data_entry_size))
            return fail(Error::truncated_data_entry, *resource_offset + target_field);
        const uint32_t data_rva = read_le32(resources, next);
        const uint32_t data_size = read_le32(resources, next + data_size_field);
        const auto data_offset = file_offset_of(sections, size, data_rva, data_size);
        if (!data_offset)
            return fail(Error::data_outside_sections, *resource_offset + next);
        result.range = {*data_offset, data_size};
    }
    return result;
}

std::string_view describe(Error error) noexcept {
    switch (error) {
    case Error::none:
        return {};
    case Error::truncated_dos_header:
        return "the file is shorter than a DOS header";
    case Error::missing_dos_signature:
        return "the file does not start with the DOS signature";
    case Error::truncated_pe_header:
        return "the PE header lies past the end of the file";
    case Error::missing_pe_signature:
        return "the PE signature is missing";
    case Error::too_many_sections:
        return "the file declares too many sections";
    case Error::truncated_optional_header:
        return "the optional header lies past the end of the file";
    case Error::unknown_optional_header:
        return "the optional header is of an unknown kind";
    case Error::truncated_section_table:
        return "the section table lies past the end of the file";
    case Error::no_resource_directory:
        return "the file holds no resources";
    case Error::resource_directory_outside_sections:
        return "the resources lie outside the file's sections";
    case Error::truncated_resource_directory:
        return "a resource directory lies past the end of the resources";
    case Error::truncated_resource_name:
        return "a resource name lies past the end of the resources";
    case Error::directory_loop:
        return "a resource directory leads back to itself";
    case Error::type_not_found:
        return "the resource type is missing";
    case Error::id_not_found:
        return "the resource id is missing";
    case Error::language_not_found:
        return "the resource language is missing";
    case Error::unexpected_data_entry:
        return "resource data stands where a directory belongs";
    case Error::unexpected_directory:
        return "a resource directory stands where data belongs";
    case Error::truncated_data_entry:
        return "a resource data entry lies past the end of the resources";
    case Error::data_outside_sections:
        return "the resource data lies outside the file's sections";
    }
    return "an unknown error";
}

} // namespace oa::formats::pe
