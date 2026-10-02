// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Find-first/find-next listings of a wildcard path into a NUL-separated
// name list, and the exchange sort of such lists.
#pragma once

#include <cstddef>
#include <cstdint>

namespace oa::data::campaign {

// Find record attribute of a directory; the listings compare it exactly.
inline constexpr uint32_t kFindDirectory = 0x10;
// Write times one listing keeps.
inline constexpr int32_t kListTimeCapacity = 3000;
// Pattern the directory pass finds (the current directory's subdirectories).
inline constexpr const char* kListDirectoryPattern = "*.";
inline constexpr const char* kListDirectoryTag = "<DIR>";
inline constexpr std::size_t kListDirectoryTagBytes = 6;
// The most name bytes and entries the sort takes; a longer list is left unsorted.
inline constexpr std::size_t kSortListBytes = 96000;
inline constexpr int32_t kSortEntryLimit = 3000;

// One entry a find-first/find-next walk reports.
struct FindRecord {
    uint32_t attributes{}; // kFindDirectory for a directory
    uint32_t write_time{};
    uint32_t size{}; // bytes
    const char* name{};
};

// The virtual file system's find walk.
struct DirectoryFind {
    void* context{};
    // Calls visit with each entry of `pattern`, in find order.
    void (*find)(
        void* context,
        const char* pattern,
        void (*visit)(void* user, const FindRecord& record),
        void* user
    ) = nullptr;
    // Size of a file by name, as the size tags read it.
    uint32_t (*file_size)(void* context, const char* name) = nullptr;
};

enum class ListSort : int32_t {
    none = 0,
    by_time = 1, // subdirectories oldest first, files newest first
    by_name = 2, // case-insensitive
};

/// Writes the names a wildcard finds into a NUL-separated list followed by one more NUL.
///
/// First, when asked, each subdirectory of the current directory as "\<name>", then each
/// file, its extension cut when asked. Entries whose names start with '.' are skipped.
///
/// @param find the find walk and file sizes
/// @param pattern wildcard path
/// @param[out] names receives the list
/// @param capacity bytes of `names`; names that no longer fit are dropped
/// @param[out] tags receives each entry's tag: "<DIR>" or the file's decimal size; may be null
/// @param tag_capacity bytes of `tags`; tags past it are dropped
/// @param directories whether to list subdirectories first
/// @param strip_extensions whether to cut file extensions
/// @param sort order of the list
/// @return the number of entries
/// @quirk Only files record a write time, so a time sort with subdirectories pairs names
///        with the wrong times.
int32_t list_directory_entries(
    const DirectoryFind& find,
    const char* pattern,
    char* names,
    std::size_t capacity,
    char* tags,
    std::size_t tag_capacity,
    bool directories,
    bool strip_extensions,
    ListSort sort
);

/// Sorts a NUL-separated name list in place, with a parallel second list and key array.
///
/// The entries up to the last one starting with '\' and those after it are sorted
/// apart, by key when keys are given (ascending before the split, descending after it)
/// and otherwise by case-insensitive name. Equal entries keep their order.
///
/// @param[in,out] names name list
/// @param[in,out] second parallel list moved with the names; may be null
/// @param[in,out] keys sort keys moved with the names; may be null
/// @param count number of entries
/// @quirk Keys compare by their signed 32-bit difference. More than 3000 entries or 96000
///        bytes are left unsorted.
void sort_paired_lists(char* names, char* second, uint32_t* keys, int32_t count);

} // namespace oa::data::campaign
