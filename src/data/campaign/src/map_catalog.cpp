// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Multiplayer map catalogue: the eligible-map list.
#include "oa/data/campaign/map_catalog.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace oa::data::campaign {
namespace {

constexpr char kMapsDirectory[] = "Maps";
constexpr char kOtaExtension[] = "OTA";

// Copies at most capacity - 1 characters of text into out and fills the rest
// of out with zero bytes, so out always ends in one.
void copy_bounded(char* out, std::size_t capacity, const char* text) noexcept {
    std::size_t length = 0;
    while (length + 1 < capacity && text[length] != '\0')
        ++length;
    std::memcpy(out, text, length);
    std::memset(out + length, 0, capacity - length);
}

struct ScanState {
    MapList* list;
    const CampaignFiles* files;
    bool first_only;
    bool stopped;
};

// Appends a name to the packed block, keeping the closing NUL after it.
bool append_name(MapList& list, const char* name) noexcept {
    const auto length = static_cast<int32_t>(std::strlen(name)) + 1;
    auto* grown =
        static_cast<char*>(std::realloc(list.names, static_cast<std::size_t>(list.bytes + length)));
    if (grown == nullptr)
        return false;
    list.names = grown;
    const auto offset = list.bytes - 1;
    std::memcpy(list.names + offset, name, static_cast<std::size_t>(length));
    list.names[offset + length] = '\0';
    list.bytes += length;
    ++list.count;
    return true;
}

// Display name of an eligible map: the translation of its lower-cased stem,
// or the stem as listed when the translation is the stem itself.
void display_name(
    const CampaignFiles& files, const char* file_name, char* out, std::size_t capacity
) {
    char stem[kCampaignNameBytes];
    copy_bounded(stem, sizeof(stem), file_name);
    if (char* dot = std::strrchr(stem, '.'))
        *dot = '\0';
    char lowered[kCampaignNameBytes];
    copy_bounded(lowered, sizeof(lowered), stem);
    for (char* p = lowered; *p != '\0'; ++p)
        *p = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    const char* translated =
        files.translate != nullptr ? files.translate(files.context, lowered) : nullptr;
    if (translated == nullptr || oa::formats::tdf::compare_nocase(lowered, translated) == 0)
        translated = stem;
    copy_bounded(out, capacity, translated);
}

void scan_map_file(void* context, const char* file_name) {
    auto& scan = *static_cast<ScanState*>(context);
    if (scan.stopped)
        return;
    char path[kCampaignPathBytes];
    build_variant_path(scan.files, path, sizeof(path), kMapsDirectory, file_name, kOtaExtension);
    oa::formats::tdf::Document ota{};
    oa::formats::tdf::document_init(&ota);
    if (load_tdf(scan.files, &ota, path) &&
        find_matching_schema(SessionKind::multiplayer, &ota, 0, 0, nullptr, 0)) {
        char name[kCampaignNameBytes];
        display_name(*scan.files, file_name, name, sizeof(name));
        append_name(*scan.list, name);
        if (scan.first_only)
            scan.stopped = true;
    }
    oa::formats::tdf::document_free(&ota);
}

} // namespace

int32_t map_build_multiplayer_list(
    MapList& list,
    const CampaignFiles& files,
    const MapScanHost& host,
    char** out,
    bool first_only,
    bool take
) {
    if (list.names == nullptr) {
        if (host.set_cursor != nullptr)
            host.set_cursor(host.context, kMapScanBusyCursor);
        list.bytes = 1;
        list.names = static_cast<char*>(std::malloc(1));
        if (list.names == nullptr)
            return 0;
        list.names[0] = '\0';
        list.count = 0;
        ScanState scan{&list, &files, first_only, false};
        if (files.list != nullptr)
            files.list(files.context, kMapsDirectory, "ota", scan_map_file, &scan);
        if (host.set_cursor != nullptr)
            host.set_cursor(host.context, kMapScanIdleCursor);
        if (list.complete == 0)
            list.complete = first_only ? 0 : 1;
    }
    if (out != nullptr) {
        if (!take || list.complete != 0) {
            *out = static_cast<char*>(std::malloc(static_cast<std::size_t>(list.bytes)));
            if (*out != nullptr)
                std::memcpy(*out, list.names, static_cast<std::size_t>(list.bytes));
        } else {
            *out = list.names;
            list.names = nullptr;
        }
    }
    return list.count;
}

void map_clear_list_cache(MapList& list, CampaignFile* map_context) noexcept {
    std::free(list.names);
    list = {};
    if (map_context != nullptr)
        campaign_file_free(map_context);
}

} // namespace oa::data::campaign
