// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/campaign/map_catalog.hpp"

#include "oa/data/campaign/campaign_assets.hpp"
#include "oa/test/game_assets.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace oa::data::campaign;

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

std::string fold(std::string path) {
    for (auto& c : path)
        c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return path;
}

struct MemoryFiles {
    std::map<std::string, std::string> files;
    std::map<std::string, std::string> translations;
    // Names the listing reports again after the files, as a later archive
    // holding the same map reports it.
    std::vector<std::string> listed_again;
    int reads = 0;
};

int32_t memory_size(void* context, const char* path) {
    auto* files = static_cast<MemoryFiles*>(context);
    const auto found = files->files.find(fold(path));
    return found == files->files.end() ? -1 : static_cast<int32_t>(found->second.size());
}

int32_t memory_read(void* context, const char* path, char* buffer, uint32_t capacity) {
    auto* files = static_cast<MemoryFiles*>(context);
    const auto found = files->files.find(fold(path));
    if (found == files->files.end())
        return -1;
    ++files->reads;
    const auto count = std::min<std::size_t>(capacity, found->second.size());
    std::memcpy(buffer, found->second.data(), count);
    return static_cast<int32_t>(count);
}

void memory_list(
    void* context,
    const char* directory,
    const char* extension,
    void (*visit)(void*, const char*),
    void* visit_context
) {
    auto* files = static_cast<MemoryFiles*>(context);
    const auto prefix = fold(directory) + "/";
    const auto suffix = "." + fold(extension);
    for (const auto& [path, _] : files->files)
        if (path.rfind(prefix, 0) == 0 && path.size() > suffix.size() &&
            path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
            // Listed names keep the capitalisation the tests expect to see.
            std::string name = path.substr(prefix.size());
            name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
            visit(visit_context, name.c_str());
        }
    for (const auto& name : files->listed_again)
        visit(visit_context, name.c_str());
}

const char* memory_translate(void* context, const char* text) {
    auto* files = static_cast<MemoryFiles*>(context);
    const auto found = files->translations.find(text);
    return found == files->translations.end() ? nullptr : found->second.c_str();
}

CampaignFiles services(MemoryFiles& files) {
    CampaignFiles services{};
    services.context = &files;
    services.size = memory_size;
    services.read = memory_read;
    services.list = memory_list;
    services.translate = memory_translate;
    return services;
}

std::string ota(const char* schema_types) {
    std::string text = "[GlobalHeader]\n{\nmissionname=Test;\n";
    int index = 0;
    for (const char* type = schema_types; *type != '\0'; type += std::strlen(type) + 1)
        text += "[Schema " + std::to_string(index++) + "]\n{\nType=" + type +
                ";\n[specials]\n{\n[special0]\n{\nspecialwhat=StartPos1;\nXPos=16;\nZPos=16;\n}\n}"
                "\n}\n";
    return text + "}\n";
}

struct Cursor {
    std::vector<uint32_t> animations;
};

MapScanHost scan_host(Cursor& cursor) {
    return {&cursor, [](void* context, uint32_t animation) {
                static_cast<Cursor*>(context)->animations.push_back(animation);
            }};
}

std::vector<std::string> unpack(const char* names, int32_t count) {
    std::vector<std::string> out;
    for (int32_t i = 0; i < count; ++i) {
        out.emplace_back(names);
        names += out.back().size() + 1;
    }
    return out;
}

void list_tests() {
    MemoryFiles memory;
    memory.files["maps/alpha.ota"] = ota("Network 1\0");
    memory.files["maps/beta.ota"] = ota("Easy\0Hard\0");
    memory.files["maps/gamma.ota"] = ota("Easy\0Network 3\0");
    memory.files["maps/delta.ota"] = "not a tdf {";
    memory.translations["alpha"] = "ALPHA";
    memory.translations["gamma"] = "Gamma Prime";
    const auto files = services(memory);

    MapList list{};
    Cursor cursor;
    char* names = nullptr;
    const auto count =
        map_build_multiplayer_list(list, files, scan_host(cursor), &names, false, false);
    expect(count == 2, "two maps carry a multiplayer schema");
    expect(
        names != nullptr &&
            unpack(names, count) == std::vector<std::string>{"Alpha", "Gamma Prime"},
        "names are the stem or its translation"
    );
    expect(list.bytes == 19 && names[18] == '\0', "packed block closes with a NUL");
    expect(list.complete == 1, "full scan marks the list complete");
    expect(cursor.animations == std::vector<uint32_t>{20, 19}, "busy cursor around the scan");
    std::free(names);

    // Later calls copy the cached list without scanning again.
    const int reads = memory.reads;
    names = nullptr;
    expect(
        map_build_multiplayer_list(list, files, scan_host(cursor), &names, false, true) == 2,
        "cached count"
    );
    expect(
        memory.reads == reads && list.names != nullptr && names != list.names,
        "complete list is copied even when taken"
    );
    std::free(names);
    expect(
        map_build_multiplayer_list(list, files, scan_host(cursor), nullptr, false, false) == 2,
        "count only"
    );

    CampaignFile* context = new CampaignFile;
    campaign_file_init(context);
    map_clear_list_cache(list, context);
    delete context;
    expect(
        list.names == nullptr && list.count == 0 && list.bytes == 0 && list.complete == 0,
        "cache cleared"
    );

    // A first-only scan stops at the first eligible map and, when taken,
    // hands its block over so the next call scans again.
    Cursor quiet;
    names = nullptr;
    expect(
        map_build_multiplayer_list(list, files, scan_host(quiet), &names, true, true) == 1,
        "first-only scan"
    );
    expect(
        quiet.animations == std::vector<uint32_t>{20, 19}, "first-only scan shows the busy cursor"
    );
    expect(list.names == nullptr && list.complete == 0, "partial list handed over");
    expect(names != nullptr && std::string(names) == "Alpha", "first eligible map");
    std::free(names);
    expect(
        map_build_multiplayer_list(list, files, scan_host(quiet), nullptr, false, false) == 2,
        "rescan after hand-over"
    );
    map_clear_list_cache(list, nullptr);

    // A map that a second archive holds too is listed again, under its own
    // capitalisation; it is still one map.
    memory.listed_again = {"Alpha.ota", "GAMMA.OTA"};
    names = nullptr;
    const auto again = map_build_multiplayer_list(list, files, MapScanHost{}, &names, false, false);
    expect(
        again == 2 && names != nullptr &&
            unpack(names, again) == std::vector<std::string>{"Alpha", "Gamma Prime"},
        "a map listed by two archives is listed once"
    );
    std::free(names);
    map_clear_list_cache(list, nullptr);
}

// The installed game's maps, read through its store as the game reads them.
void corpus_tests(const oa::AssetStore& assets) {
    const CampaignFiles files = campaign_asset_files(assets);
    MapList list{};
    char* names = nullptr;
    const auto count = map_build_multiplayer_list(list, files, MapScanHost{}, &names, false, false);
    expect(count > 0, "installed maps offer multiplayer schemas");
    const auto listed = unpack(names, count);
    expect(
        std::find(listed.begin(), listed.end(), "coast to coast") != listed.end() ||
            std::find(listed.begin(), listed.end(), "Coast To Coast") != listed.end(),
        "Coast To Coast is listed"
    );
    // Comet Catcher comes in both ccmaps.ccx and Cometctr.ufo where an
    // installation has both; it is listed once either way.
    auto sorted = listed;
    std::sort(sorted.begin(), sorted.end());
    expect(
        std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end(), "every map is listed once"
    );
    std::free(names);
    map_clear_list_cache(list, nullptr);
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv))
        corpus_tests(oa::test::require_game_assets("the installed map catalogue"));
    else
        list_tests();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "map catalogue tests passed\n";
    return 0;
}
