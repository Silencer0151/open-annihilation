// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Per-user configuration values, each named by an application key and a
// value name and stored as a string, binary or dword value. A PrefBackend
// holds them; pref_file_backend maps them onto the user preferences file.

#include "oa/platform/preferences.hpp"

#include <cstdint>

namespace oa::ui::services {

// Value types; the numbers are stored in the preferences file.
enum class PrefType : uint32_t { string = 1, binary = 3, dword = 4 };

enum class PrefStatus : uint32_t {
    ok,
    more_data, // stored value longer than the buffer; *size holds its length, data untouched
    missing,
    failed,
};

struct PrefBackend {
    void* context{};
    PrefStatus (*query)(
        void* context, const char* application, const char* name, uint8_t* data, uint32_t* size
    ){};
    PrefStatus (*store)(
        void* context,
        const char* application,
        const char* name,
        PrefType type,
        const uint8_t* data,
        uint32_t size
    ){};
};

/// Reads or writes one configuration value.
///
/// @param backend value store
/// @param application application key, e.g. the game's name
/// @param name value name
/// @param[in,out] data value bytes: filled on a read, stored on a write
/// @param[in,out] size on a read the buffer capacity on entry and the stored
///        length on return; on a write the number of bytes stored
/// @param type value type recorded on a write
/// @param read true to read, false to write
/// @return whether the value was read or stored
/// @quirk A read also succeeds when the value was too long for the buffer, in
///        which case nothing is copied: callers that pass a 4-byte buffer for a
///        longer value keep their previous contents.
bool pref_access(
    const PrefBackend* backend,
    const char* application,
    const char* name,
    uint8_t* data,
    uint32_t* size,
    PrefType type,
    bool read
) noexcept;
/// Reads a value's raw bytes.
///
/// @param backend value store
/// @param application application key
/// @param name value name
/// @param[out] data buffer for the bytes; untouched when the value is too long
/// @param[in,out] size buffer capacity on entry, stored length on return
/// @return whether the value exists (see pref_access for the too-long case)
bool pref_read(
    const PrefBackend* backend,
    const char* application,
    const char* name,
    uint8_t* data,
    uint32_t* size
) noexcept;
/// Reads a 4-byte little-endian value.
///
/// @param backend value store
/// @param application application key
/// @param name value name
/// @param[in,out] value the stored value; keeps its previous contents when the
///        value is missing or longer than 4 bytes
/// @return whether the value exists
bool pref_read_dword(
    const PrefBackend* backend, const char* application, const char* name, uint32_t* value
) noexcept;
/// Stores raw bytes as a binary value.
///
/// @param backend value store
/// @param application application key
/// @param name value name
/// @param data bytes to store
/// @param size number of bytes
/// @return whether the value was stored
bool pref_write_binary(
    const PrefBackend* backend,
    const char* application,
    const char* name,
    const uint8_t* data,
    uint32_t size
) noexcept;
/// Stores text, with its terminating NUL, as a string value.
///
/// @param backend value store
/// @param application application key
/// @param name value name
/// @param text NUL-terminated text
/// @return whether the value was stored
bool pref_write_string(
    const PrefBackend* backend, const char* application, const char* name, const char* text
) noexcept;
/// Stores a 4-byte little-endian dword value.
///
/// @param backend value store
/// @param application application key
/// @param name value name
/// @param value value to store
/// @return whether the value was stored
bool pref_write_dword(
    const PrefBackend* backend, const char* application, const char* name, uint32_t value
) noexcept;

/// Creates a backend over a preferences value map.
///
/// Keys are "<application>\\<name>", ASCII-lowercased because value names
/// are case-insensitive; values are "<type>:<hex bytes>". The map is modified
/// in memory only; persist it with oa::platform::preferences::save.
///
/// @param values map the backend reads and writes; must outlive the backend
/// @return the backend
[[nodiscard]] PrefBackend pref_file_backend(platform::preferences::Values* values) noexcept;

} // namespace oa::ui::services
