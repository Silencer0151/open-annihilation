// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Finds where a resource's data lies in a Windows program file, a Portable
// Executable (PE) image: the DOS header, the PE and optional headers, the
// section table and the resource directory. It only reads bytes: nothing in
// the image is run, mapped or relocated, and every offset and size it follows
// is checked against the bytes it was given.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace oa::formats::pe {

namespace limit {
/// Most sections an image may declare.
inline constexpr uint16_t sections = 96;
} // namespace limit

/// What stopped a resource lookup.
enum class Error : uint8_t {
    none,
    truncated_dos_header,                ///< shorter than the DOS header
    missing_dos_signature,               ///< does not start with "MZ"
    truncated_pe_header,                 ///< the PE signature and file header lie past the end
    missing_pe_signature,                ///< no "PE\0\0" where the DOS header points
    too_many_sections,                   ///< more than limit::sections sections
    truncated_optional_header,           ///< the optional header lies past the end
    unknown_optional_header,             ///< neither the 32-bit nor the 64-bit optional header
    truncated_section_table,             ///< the section table lies past the end
    no_resource_directory,               ///< the image declares no resources
    resource_directory_outside_sections, ///< the resources lie in no section's file bytes
    truncated_resource_directory,        ///< a directory table lies past the resources
    truncated_resource_name,             ///< a name string lies past the resources
    directory_loop,                      ///< a subdirectory leads back to one on the path
    type_not_found,                      ///< no resource of the type
    id_not_found,                        ///< the type holds no resource of the id
    language_not_found,                  ///< the resource has no data in the language
    unexpected_data_entry,               ///< data where a subdirectory belongs
    unexpected_directory,                ///< a subdirectory where data belongs
    truncated_data_entry,                ///< a data entry lies past the resources
    data_outside_sections,               ///< the data lies in no section's file bytes
};

/// Names one resource: its type by name, its id and its language.
struct ResourceKey {
    /// ASCII; letters match without regard to case.
    std::string_view type_name;
    uint32_t id{};
    /// A Windows language identifier, such as 1033 for English (United States).
    uint32_t language{};
};

/// A range of bytes in the image file.
struct FileRange {
    uint64_t offset{};
    uint64_t size{};
};

/// The outcome of a resource lookup.
struct ResourceLookup {
    /// Where the resource's data lies; empty unless the lookup succeeded.
    FileRange range;
    Error error{};
    /// File offset of the field or structure that stopped the lookup.
    uint64_t error_offset{};
};

/// Finds where a resource's data lies in a PE image.
///
/// Reads the DOS header, the PE signature, the file header, the 32-bit or
/// 64-bit optional header and its resource data directory, and the section
/// table, then walks the resource directory from its root through the type,
/// the id and the language to the data entry. Every offset and size is
/// checked against `image` before it is read, a subdirectory that leads back
/// to one already on the path is refused, and the data must lie within one
/// section's bytes in the file.
///
/// @param image the whole file
/// @param key the resource to find
/// @return the data's range in `image`, or the error and the file offset
///     where it was found
[[nodiscard]] ResourceLookup
find_resource(std::span<const uint8_t> image, const ResourceKey& key) noexcept;

/// Describes a lookup error in words.
///
/// @param error the error
/// @return a lower-case phrase such as "the file does not start with the DOS
///     signature"; empty for Error::none
[[nodiscard]] std::string_view describe(Error error) noexcept;

} // namespace oa::formats::pe
