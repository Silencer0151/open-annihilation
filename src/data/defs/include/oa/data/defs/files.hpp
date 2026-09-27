// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Game-data file access for the definition loaders.
#pragma once

#include "oa/formats/tdf.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::data::defs {

inline constexpr std::size_t path_capacity = 256; // bytes of a path buffer, as in 3.1c

// Platform boundary over the merged loose-file + HPI view. Paths use '\\' as
// the game does; implementations may accept either separator.
struct Files {
    void* context;
    // Reads a whole file. On success *data is released with release().
    bool (*read)(
        void* context, const char* path, uint8_t** data, uint32_t* size, bool* from_archive
    );
    void (*release)(void* context, uint8_t* data);
    // Reports true when the path names a readable file.
    bool (*exists)(void* context, const char* path);
    // Calls visit(user, name) for each file name (without directory) in the
    // directory with the extension (no dot), in the order the VFS enumerates them.
    void (*list)(
        void* context,
        const char* directory,
        const char* extension,
        void (*visit)(void* user, const char* name),
        void* user
    );
};

/// Reads a TDF-family file whole through the file boundary and parses it.
///
/// @param files file boundary; null fails
/// @param path file path
/// @param[out] document parsed document; filled only on success
/// @param[out] error parse error details when the text is malformed
/// @return false when the file is missing, empty, larger than formats::tdf::max_input_bytes
///     or fails to parse
[[nodiscard]] bool load_tdf_file(
    const Files* files,
    const char* path,
    formats::tdf::Document* document,
    formats::tdf::ParseError* error
) noexcept;

/// Cuts a path at its last '.', even when that dot is in a directory name.
///
/// @param[in,out] path NUL-terminated path; unchanged when it has no '.'
void remove_extension(char* path) noexcept;

/// Copies a path with its extension replaced.
///
/// An extension is dropped only from the final component (after the last
/// '\\'); "." and the new extension are then appended as far as they fit.
///
/// @param source path to copy
/// @param[out] out destination buffer; always NUL-terminated when capacity > 0
/// @param capacity size of `out` in bytes; 0 writes nothing
/// @param extension new extension, without the dot
void format_with_extension(
    const char* source, char* out, std::size_t capacity, const char* extension
) noexcept;

/// Truncates a path just after its last '\\'.
///
/// @param[in,out] path NUL-terminated path; unchanged when it has no '\\'
void truncate_to_directory(char* path) noexcept;

// The GUI's GAF directory as start-up sets it.
inline constexpr const char* gui_gaf_directory = "anims\\";

/// Reads a panel's own GAF file by name from the GAF directory.
///
/// The directory and name are joined and any extension of the name is
/// replaced by GAF.
///
/// @param files file boundary; null fails
/// @param gaf_directory directory prefix such as gui_gaf_directory, ending in
///     '\\'; null or empty for none
/// @param name file name; null counts as empty
/// @param[out] data file bytes, released with files->release; set only on success
/// @param[out] size file size in bytes; set only on success
/// @return false when the file is missing or empty
[[nodiscard]] bool read_named_gaf(
    const Files* files, const char* gaf_directory, const char* name, uint8_t** data, uint32_t* size
) noexcept;

/// Builds the path of a game-data file, preferring the variant directory.
///
/// Yields "<directory>-<variant>\\<name>.<extension>" when the variant is set
/// and that file exists, else "<directory>\\<name>.<extension>"; any extension
/// the name had is replaced.
///
/// @param files file boundary used for the existence check; null skips the variant
/// @param[out] out destination buffer; set to "" when `name` is empty
/// @param capacity size of `out` in bytes; 0 writes nothing
/// @param directory base directory without a trailing '\\'
/// @param name file name
/// @param extension extension without the dot
/// @param variant game-data variant suffix; null or empty for none
void build_variant_path(
    const Files* files,
    char* out,
    std::size_t capacity,
    const char* directory,
    const char* name,
    const char* extension,
    const char* variant
) noexcept;

} // namespace oa::data::defs
