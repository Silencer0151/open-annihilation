// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/campaign/directory_list.hpp"

#include "oa/data/defs/files.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace oa::data::campaign {
namespace {

constexpr std::size_t kSizeTagBytes = 16;

struct Listing {
    const DirectoryFind* find;
    char* names;
    std::size_t capacity;
    std::size_t used;
    char* tags;
    std::size_t tag_capacity;
    std::size_t tag_used;
    uint32_t times[kListTimeCapacity];
    int32_t time_count;
    int32_t count;
    bool strip_extensions;
};

// Room for `bytes` more name bytes and the list's final NUL.
bool fits(const Listing& listing, std::size_t bytes) {
    return listing.used + bytes < listing.capacity;
}

void add_tag(Listing& listing, const char* tag) {
    if (listing.tags == nullptr)
        return;
    const std::size_t bytes = std::strlen(tag) + 1;
    if (listing.tag_used + bytes > listing.tag_capacity)
        return;
    std::memcpy(listing.tags + listing.tag_used, tag, bytes);
    listing.tag_used += bytes;
}

void add_directory(void* user, const FindRecord& record) {
    auto& listing = *static_cast<Listing*>(user);
    if (record.name[0] == '.' || record.attributes != kFindDirectory)
        return;
    const std::size_t length = std::strlen(record.name);
    if (!fits(listing, length + 2))
        return;
    listing.names[listing.used] = '\\';
    std::memcpy(listing.names + listing.used + 1, record.name, length + 1);
    listing.used += length + 2;
    if (listing.tags != nullptr &&
        listing.tag_used + kListDirectoryTagBytes <= listing.tag_capacity) {
        std::memcpy(listing.tags + listing.tag_used, kListDirectoryTag, kListDirectoryTagBytes);
        listing.tag_used += kListDirectoryTagBytes;
    }
    ++listing.count;
}

void add_file(void* user, const FindRecord& record) {
    auto& listing = *static_cast<Listing*>(user);
    if (record.name[0] == '.' || record.attributes == kFindDirectory)
        return;
    const std::size_t length = std::strlen(record.name);
    if (!fits(listing, length + 1))
        return;
    char* name = listing.names + listing.used;
    std::memcpy(name, record.name, length + 1);
    if (listing.strip_extensions)
        data::defs::remove_extension(name);
    listing.used += std::strlen(name) + 1;
    if (listing.time_count < kListTimeCapacity)
        listing.times[listing.time_count++] = record.write_time;
    if (listing.tags != nullptr) {
        const uint32_t size = listing.find->file_size != nullptr
                                  ? listing.find->file_size(listing.find->context, record.name)
                                  : 0;
        char tag[kSizeTagBytes];
        std::snprintf(tag, sizeof tag, "%d", static_cast<int>(size));
        add_tag(listing, tag);
    }
    ++listing.count;
}

// The C-locale case-insensitive comparison: A-Z fold to a-z, bytes unsigned.
int compare_nocase(const char* left, const char* right) {
    for (;; ++left, ++right) {
        unsigned char a = static_cast<unsigned char>(*left);
        unsigned char b = static_cast<unsigned char>(*right);
        if (a >= 'A' && a <= 'Z')
            a = static_cast<unsigned char>(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z')
            b = static_cast<unsigned char>(b + ('a' - 'A'));
        if (a != b || a == 0)
            return static_cast<int>(a) - static_cast<int>(b);
    }
}

// Copies the entries in `order` back over `list`.
void write_back(char* list, const char* const* order, int32_t count, char* scratch) {
    std::size_t used = 0;
    for (int32_t index = 0; index < count; ++index) {
        const std::size_t bytes = std::strlen(order[index]) + 1;
        std::memcpy(scratch + used, order[index], bytes);
        used += bytes;
    }
    std::memcpy(list, scratch, used);
}

} // namespace

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
) {
    if (capacity == 0)
        return 0;
    auto* listing = static_cast<Listing*>(std::calloc(1, sizeof(Listing)));
    if (listing == nullptr) {
        names[0] = '\0';
        return 0;
    }
    listing->find = &find;
    listing->names = names;
    listing->capacity = capacity;
    listing->tags = tags;
    listing->tag_capacity = tag_capacity;
    listing->strip_extensions = strip_extensions;
    if (find.find != nullptr) {
        if (directories)
            find.find(find.context, kListDirectoryPattern, add_directory, listing);
        find.find(find.context, pattern, add_file, listing);
    }
    const int32_t count = listing->count;
    if (sort != ListSort::none)
        sort_paired_lists(
            names, nullptr, sort == ListSort::by_name ? nullptr : listing->times, count
        );
    names[listing->used] = '\0';
    std::free(listing);
    return count;
}

void sort_paired_lists(char* names, char* second, uint32_t* keys, int32_t count) {
    if (count <= 0 || count > kSortEntryLimit)
        return;
    auto** first =
        static_cast<const char**>(std::calloc(static_cast<std::size_t>(count), sizeof(char*)));
    auto** paired =
        static_cast<const char**>(std::calloc(static_cast<std::size_t>(count), sizeof(char*)));
    auto* scratch = static_cast<char*>(std::malloc(kSortListBytes));
    if (first == nullptr || paired == nullptr || scratch == nullptr) {
        std::free(first);
        std::free(paired);
        std::free(scratch);
        return;
    }
    int32_t split = -1;
    std::size_t first_bytes = 0;
    std::size_t second_bytes = 0;
    for (int32_t index = 0; index < count; ++index) {
        first[index] = names + first_bytes;
        first_bytes += std::strlen(first[index]) + 1;
        if (second != nullptr) {
            paired[index] = second + second_bytes;
            second_bytes += std::strlen(paired[index]) + 1;
        }
        if (first[index][0] == '\\')
            split = index;
    }
    if (first_bytes <= kSortListBytes && second_bytes <= kSortListBytes) {
        const auto swap_down = [&](int32_t at) {
            const char* name = first[at];
            first[at] = first[at + 1];
            first[at + 1] = name;
            if (second != nullptr) {
                const char* other = paired[at];
                paired[at] = paired[at + 1];
                paired[at + 1] = other;
            }
            if (keys != nullptr) {
                const uint32_t key = keys[at];
                keys[at] = keys[at + 1];
                keys[at + 1] = key;
            }
        };
        if (split != -1) {
            bool swapped = true;
            while (swapped && split >= 1) {
                swapped = false;
                for (int32_t at = 0; at < split; ++at) {
                    const int order = keys != nullptr
                                          ? static_cast<int32_t>(keys[at] - keys[at + 1])
                                          : compare_nocase(first[at], first[at + 1]);
                    if (order > 0) {
                        swap_down(at);
                        swapped = true;
                    }
                }
            }
        }
        bool swapped = true;
        while (swapped && split + 1 < count - 1) {
            swapped = false;
            for (int32_t at = split + 1; at < count - 1; ++at) {
                const int order = keys != nullptr ? static_cast<int32_t>(keys[at + 1] - keys[at])
                                                  : compare_nocase(first[at], first[at + 1]);
                if (order > 0) {
                    swap_down(at);
                    swapped = true;
                }
            }
        }
        write_back(names, first, count, scratch);
        if (second != nullptr)
            write_back(second, paired, count, scratch);
    }
    std::free(first);
    std::free(paired);
    std::free(scratch);
}

} // namespace oa::data::campaign
