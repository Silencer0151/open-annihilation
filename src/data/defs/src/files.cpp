// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/defs/files.hpp"
#include "oa/base/text.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace oa::data::defs {
namespace {

constexpr const char* gaf_extension = "GAF";

void append(char* out, std::size_t capacity, const char* text) noexcept {
    oa::base::text::append_terminated(std::span(out, capacity), text);
}

} // namespace

bool load_tdf_file(
    const Files* files,
    const char* path,
    formats::tdf::Document* document,
    formats::tdf::ParseError* error
) noexcept {
    uint8_t* data = nullptr;
    uint32_t size = 0;
    bool from_archive = false;
    if (files == nullptr || !files->read(files->context, path, &data, &size, &from_archive))
        return false;
    bool loaded = false;
    if (size != 0 && size <= formats::tdf::max_input_bytes)
        loaded = formats::tdf::parse_text(
            document, reinterpret_cast<const char*>(data), size, from_archive, error
        );
    files->release(files->context, data);
    return loaded;
}

void remove_extension(char* path) noexcept {
    for (std::size_t index = std::strlen(path); index-- > 0;) {
        if (path[index] == '.') {
            path[index] = '\0';
            return;
        }
    }
}

void format_with_extension(
    const char* source, char* out, std::size_t capacity, const char* extension
) noexcept {
    if (capacity == 0)
        return;
    oa::base::text::copy_padded(out, source, capacity - 1);
    out[capacity - 1] = '\0';
    for (std::size_t index = std::strlen(out); index-- > 0;) {
        if (out[index] == '\\')
            break;
        if (out[index] == '.') {
            out[index] = '\0';
            break;
        }
    }
    append(out, capacity, ".");
    append(out, capacity, extension);
}

void truncate_to_directory(char* path) noexcept {
    for (std::size_t index = std::strlen(path); index-- > 0;) {
        if (path[index] == '\\') {
            path[index + 1] = '\0';
            return;
        }
    }
}

bool read_named_gaf(
    const Files* files, const char* gaf_directory, const char* name, uint8_t** data, uint32_t* size
) noexcept {
    char joined[path_capacity] = {};
    if (gaf_directory != nullptr && gaf_directory[0] != '\0')
        append(joined, sizeof joined, gaf_directory);
    append(joined, sizeof joined, name != nullptr ? name : "");
    char path[path_capacity];
    format_with_extension(joined, path, sizeof path, gaf_extension);
    if (files == nullptr || files->read == nullptr)
        return false;
    uint8_t* bytes = nullptr;
    uint32_t length = 0;
    bool from_archive = false;
    if (!files->read(files->context, path, &bytes, &length, &from_archive))
        return false;
    if (length == 0) {
        files->release(files->context, bytes);
        return false;
    }
    *data = bytes;
    *size = length;
    return true;
}

void build_variant_path(
    const Files* files,
    char* out,
    std::size_t capacity,
    const char* directory,
    const char* name,
    const char* extension,
    const char* variant
) noexcept {
    if (capacity == 0)
        return;
    out[0] = '\0';
    if (name[0] == '\0')
        return;
    if (variant != nullptr && variant[0] != '\0') {
        std::snprintf(out, capacity, "%s-%s\\%s", directory, variant, name);
        remove_extension(out);
        append(out, capacity, ".");
        append(out, capacity, extension);
        if (files != nullptr && files->exists(files->context, out))
            return;
    }
    std::snprintf(out, capacity, "%s\\%s", directory, name);
    remove_extension(out);
    append(out, capacity, ".");
    append(out, capacity, extension);
}

} // namespace oa::data::defs
