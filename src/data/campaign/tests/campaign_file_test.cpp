// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/campaign/campaign_file.hpp"

#include "oa/data/campaign/campaign_assets.hpp"
#include "oa/core/game_state.h"
#include "oa/test/game_assets.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

int32_t memory_count(void* context, const char* pattern);

namespace {

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
    std::vector<std::string> messages;
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
        if (path.rfind(prefix, 0) == 0 && path.find('/', prefix.size()) == std::string::npos &&
            path.size() > suffix.size() &&
            path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0)
            visit(visit_context, path.substr(prefix.size()).c_str());
}

// "dir/*.ext" or "dir/*" over the folded paths: the directory part and the
// extension the names must end with (empty for any).
struct PatternParts {
    std::string directory;
    std::string suffix;
};

PatternParts pattern_parts(const char* pattern) {
    const auto folded = fold(pattern);
    const auto slash = folded.find_last_of('/');
    PatternParts parts{slash == std::string::npos ? "" : folded.substr(0, slash + 1), ""};
    const auto spec = slash == std::string::npos ? folded : folded.substr(slash + 1);
    if (spec.rfind("*.", 0) == 0 && spec != "*.*")
        parts.suffix = spec.substr(1);
    return parts;
}

void memory_find(
    void* context,
    const char* pattern,
    void (*visit)(void*, const oa::data::campaign::FindRecord&),
    void* user
) {
    const auto parts = pattern_parts(pattern);
    for (const auto& [path, bytes] : static_cast<MemoryFiles*>(context)->files) {
        if (path.rfind(parts.directory, 0) != 0 ||
            path.find('/', parts.directory.size()) != std::string::npos)
            continue;
        const auto name = path.substr(parts.directory.size());
        if (!parts.suffix.empty() &&
            (name.size() < parts.suffix.size() ||
             name.compare(name.size() - parts.suffix.size(), parts.suffix.size(), parts.suffix) !=
                 0))
            continue;
        visit(user, {0, 0, static_cast<uint32_t>(bytes.size()), name.c_str()});
    }
}

void memory_message(void* context, const char* text) {
    static_cast<MemoryFiles*>(context)->messages.emplace_back(text);
}

oa::data::campaign::CampaignFiles memory_services(MemoryFiles& files) {
    return {
        &files,
        memory_size,
        memory_read,
        memory_list,
        nullptr,
        memory_message,
        nullptr,
        nullptr,
        memory_find
    };
}

const char* kCampaign = R"([HEADER]
{
campaignside=ARM;
}
[MISSION0]
{
missionfile=T01.ota;
missionname=1: First;
}
[MISSION1]
{
missionfile=T02.ota;
}
[MISSION3]
{
missionfile=T03.ota;
missionname=Unreachable;
}
)";

const char* kMission = R"([GlobalHeader]
{
missionname=First;
brief=T01;
narration=T01N;
planet=Lunar;
maxunits=150;
mapping=1;
lineofsight=1;
minwindspeed=100;
maxwindspeed=2500;
gravity=112;
tidalstrength=18.5;
missiondescription=Hold the line;
[Schema 0]
    {
    Type=Hard;
    HumanMetal=500;
    [units]
        {
        [unit0]
            {
            Unitname=ARMCOM;
            XPos=100;
            ZPos=200;
            Player=0;
            }
        }
    }
[Schema 1]
    {
    Type=Easy;
    HumanMetal=1000;
    HumanEnergy=2000;
    ComputerMetal=100;
    ComputerEnergy=150;
    SurfaceMetal=3;
    [units]
        {
        [unit0]
            {
            Unitname=ARMCOM;
            Ident=hero;
            XPos=1000;
            YPos=10;
            ZPos=2000;
            Angle=90;
            Player=1;
            MissionCriticalUnit=1;
            InitialGroup=5;
            Immunity=1;
            }
        [unit1]
            {
            Unitname=CORAK;
            XPos=64;
            ZPos=64;
            Player=2;
            HealthPercentage=50;
            AiIgnore=1;
            }
        }
    [specials]
        {
        [special0]
            {
            specialwhat=StartPos3;
            XPos=300;
            ZPos=400;
            }
        [special1]
            {
            specialwhat=startpos;
            XPos=10;
            ZPos=20;
            }
        [special2]
            {
            specialwhat=CenterMap;
            }
        }
    [features]
        {
        [feature0]
            {
            Featurename=Tree1;
            XPos=4;
            ZPos=5;
            }
        [feature1]
            {
            Featurename=Rock;
            XPos=-1;
            ZPos=5;
            }
        }
    }
}
)";

void synthetic_tests() {
    using namespace oa::data::campaign;
    MemoryFiles memory;
    memory.files["camps/test campaign.tdf"] = kCampaign;
    memory.files["maps/t01.ota"] = kMission;
    memory.files["maps/t01.tnt"] = std::string(1234, 'x');
    memory.files["camps/briefs/t01.txt"] = "Commander, &Rlisten&.";
    memory.files["camps/other.tdf"] = "[HEADER]\n{\ncampaignside=CORE;\n}\n";
    memory.files["camps/shared.tdf"] = "[HEADER]\n{\ncampaignside=ALL;\n}\n";
    const auto services = memory_services(memory);

    static oa::Game game{};
    // The single-player rules already loaded; every mission load replaces them.
    game.session_record[0] = 1;
    game.session_record[3] = 0;
    CampaignEnv env{&services, &game, 0, 0};
    auto* file = new CampaignFile;
    std::memset(file, 0xcd, sizeof(*file));
    campaign_file_construct(file, SessionKind::campaign, &env);
    expect(file->kind == SessionKind::campaign, "constructor stores the session kind");
    // campaign_kind returns the object's session kind: 1 for a campaign.
    expect(static_cast<int32_t>(campaign_kind(file)) == 1, "kind read from the object");
    expect(file->campaign_name[0] == '\0' && file->paths[0][0] == '\0', "nothing loaded");
    expect(file->mission_file_size == 0 && file->units == nullptr, "fields cleared");
    expect(file->tidal_strength == -1.0f, "tidal strength unloaded");
    expect(campaign_planet(file) == file->planet && file->planet[0] == '\0', "no planet yet");
    // campaign_mission_file_size reads mission_file_size: no map bound means no
    // map selected.
    expect(campaign_mission_file_size(file) == 0, "no map file yet");
    expect(campaign_load_file(file, &env, "Test Campaign"), "campaign loads");
    expect(std::strcmp(file->paths[0], "camps/Test Campaign.TDF") == 0, "campaign path");
    expect(campaign_count_missions(file) == 2, "mission count stops at the first gap");
    char names[4][kCampaignNameBytes]{};
    expect(campaign_load_mission_list(file, names, 4) == 2, "mission list count");
    expect(std::strcmp(names[0], "1: First") == 0, "mission name");
    expect(std::strcmp(names[1], "Error -- Unnamed Mission") == 0, "unnamed placeholder");
    expect(file->mission_index == 0, "mission 0 bound");
    expect(std::strcmp(file->mission_name, "1: First") == 0, "mission name stored");
    expect(std::strcmp(file->schema, "Schema 1") == 0, "easy schema selected");
    expect(file->units_per_player == 150 && game.units_per_player == 150, "maxunits");
    // The map load sets Game.session_record = {0, mapping, lineofsight, 1}.
    expect(
        game.session_record[0] == 0 && game.session_record[1] == 1 && game.session_record[2] == 1 &&
            game.session_record[3] == 1,
        "session record from the mission header"
    );
    int32_t record[4] = {-1, -1, -1, -1};
    campaign_session_record(file, record);
    expect(
        record[0] == kCampaignCommanderRule && record[1] == 1 && record[2] == 1 &&
            record[3] == kCampaignSessionEnabled,
        "session record rebuilt from the file"
    );
    expect(
        std::strcmp(file->paths[1], "Maps/T01.TNT") == 0 && file->mission_file_size == 1234,
        "tnt path"
    );
    // The accessors read mission_file_size, mission_name in place and
    // mission_index.
    expect(campaign_mission_file_size(file) == 1234, "map file size");
    expect(
        campaign_mission_name(file) == file->mission_name &&
            std::strcmp(campaign_mission_name(file), "1: First") == 0,
        "bound mission name"
    );
    expect(campaign_mission_index(file) == 0, "bound mission index");
    expect(std::strcmp(file->paths[2], "camps/briefs/T01.TXT") == 0, "briefing path");
    expect(std::strcmp(file->paths[3], "camps/briefs/T01N.WAV") == 0, "narration path");
    expect(std::strcmp(file->paths[7], "ai/Default.txt") == 0, "default ai profile");
    expect(
        campaign_briefing_text(file) != nullptr &&
            std::strcmp(campaign_briefing_text(file), "Commander, &Rlisten&.") == 0,
        "briefing text"
    );
    expect(std::strcmp(file->planet, "Lunar") == 0, "planet");
    expect(std::strcmp(file->description, "Hold the line") == 0, "description falls back");
    expect(file->min_wind == 100 && file->max_wind == 2500 && file->gravity == 112, "header ints");
    expect(file->tidal_strength == 18.5f, "tidal strength");
    expect(file->metal[0] == 1000.0f && file->energy[0] == 2000.0f, "human resources");
    expect(file->metal[1] == 100.0f && file->energy[1] == 150.0f, "computer resources");
    expect(file->surface_metal == 3, "surface metal");
    expect(file->unit_count == 2, "unit count");
    const MissionUnit& hero = file->units[0];
    expect(
        std::strcmp(hero.unit_name, "ARMCOM") == 0 && std::strcmp(hero.ident, "hero") == 0,
        "unit strings"
    );
    expect(hero.initial_mission == nullptr, "absent string is null");
    expect(
        hero.x == (1000 << 16) && hero.y == (10 << 16) && hero.z == (2000 << 16), "unit position"
    );
    expect(hero.angle == static_cast<int16_t>((90 << 16) / 360), "angle conversion");
    expect(hero.health_percent == 100 && hero.player == 1, "unit defaults");
    expect(
        hero.flags == (mission_unit_flag::mission_critical | mission_unit_flag::immunity | 5),
        "unit flags"
    );
    const MissionUnit& enemy = file->units[1];
    expect(
        enemy.player == 2 && enemy.health_percent == 50 &&
            enemy.flags == mission_unit_flag::ai_ignore,
        "second unit"
    );
    expect(file->rule_count == 3, "rule count");
    expect(
        file->rules[0].type == MissionRuleType::start_position && file->rules[0].index == 2 &&
            file->rules[0].x == 300 && file->rules[0].z == 400,
        "numbered start position"
    );
    expect(
        file->rules[1].type == MissionRuleType::start_position && file->rules[1].index == 0,
        "unnumbered start position counts up"
    );
    expect(file->rules[2].type == MissionRuleType::none, "other special");
    expect(
        file->feature_count == 2 && std::strcmp(file->features[0].name, "Tree1") == 0 &&
            file->features[1].name[0] == '\0',
        "features"
    );

    env.difficulty = 2;
    expect(campaign_bind_mission(file, &env, 0), "rebind hard");
    expect(std::strcmp(file->schema, "Schema 0") == 0 && file->metal[0] == 500.0f, "hard schema");
    expect(file->units[0].player == 1, "player 0 becomes 1");
    env.difficulty = 1;
    expect(campaign_bind_mission(file, &env, 0), "medium falls back to easy");
    expect(std::strcmp(file->schema, "Schema 1") == 0, "medium order picks easy first");

    expect(campaign_has_next_mission(file, 1) && !campaign_has_next_mission(file, 2), "has next");
    memory.messages.clear();
    expect(campaign_advance_next_mission(file, &env), "advance to mission 1");
    expect(file->mission_index == 1 && campaign_mission_index(file) == 1, "index advanced");
    expect(!memory.messages.empty(), "missing OTA reported");
    expect(!campaign_advance_next_mission(file, &env) && file->mission_index == 1, "no mission 2");

    expect(campaign_select_mission(file, &env, "1: first"), "select by name");
    expect(file->mission_index == 0, "selected index");

    memory.messages.clear();
    expect(!campaign_load_file(file, &env, "Missing"), "missing campaign");
    expect(
        memory.messages.size() == 1 &&
            memory.messages[0] == "The requested campaign file, camps/Missing.TDF, does not exist.",
        "missing campaign message"
    );
    expect(
        campaign_name_if_loaded(file) == nullptr && campaign_count_missions(file) == 0, "unloaded"
    );

    char list[256];
    auto counted = services;
    counted.count = memory_count;
    expect(campaign_load_names(&counted, "ARM", list, sizeof(list)) == 2, "ARM + ALL campaigns");
    expect(
        std::strcmp(list, "shared") == 0 && std::strcmp(list + 7, "test campaign") == 0, "names"
    );

    campaign_file_free(file);
    delete file;
}

const char* kSkirmishMap = R"([GlobalHeader]
{
missionname=Two Ways;
[Schema 0]
    {
    Type=Network 1;
    aiprofile=Hover;
    [specials]
        {
        [special0]
            {
            specialwhat=StartPos1;
            }
        [special1]
            {
            specialwhat=StartPos2;
            }
        }
    }
[Schema 1]
    {
    Type=Network 2;
    [specials]
        {
        [special0]
            {
            specialwhat=StartPos1;
            }
        [special1]
            {
            specialwhat=StartPos2;
            }
        [special2]
            {
            specialwhat=StartPos3;
            }
        [special3]
            {
            specialwhat=StartPos4;
            }
        }
    }
}
)";

// A skirmish loads its map's mission info as kind 2. The schema choice
// takes the multiplayer schema whose start positions match the highest occupied
// roster slot, or the largest while none matches, and the map load resolves that
// schema's aiprofile into path slot 7, else ai/Default.txt.
void skirmish_profile_tests() {
    using namespace oa::data::campaign;
    MemoryFiles memory;
    memory.files["maps/twoways.ota"] = kSkirmishMap;
    const auto services = memory_services(memory);
    auto* file = new CampaignFile;
    campaign_file_init(file);
    file->kind = SessionKind::skirmish;
    CampaignEnv env{&services, nullptr, 1, 2};
    expect(campaign_load_mission_info(file, &env, "TwoWays"), "skirmish map loads");
    expect(std::strcmp(file->schema, "Schema 0") == 0, "two players take the two-start schema");
    expect(
        std::strcmp(campaign_path(file, CampaignPath::ai_profile), "ai/Hover.txt") == 0,
        "schema ai profile"
    );
    env.player_count = 3;
    expect(campaign_load_mission_info(file, &env, "TwoWays"), "three-player load");
    expect(std::strcmp(file->schema, "Schema 1") == 0, "no match takes the larger schema");
    expect(
        std::strcmp(campaign_path(file, CampaignPath::ai_profile), "ai/Default.txt") == 0,
        "a schema without aiprofile falls back to Default"
    );
    campaign_file_free(file);
    delete file;
}

// Every campaign the installed game provides, read through its store as the
// game reads it.
void corpus_tests(const oa::AssetStore& assets) {
    using namespace oa::data::campaign;
    const CampaignFiles services = campaign_asset_files(assets);
    std::vector<std::string> campaigns;
    for (const auto& path : assets.list_effective("camps", ".tdf")) {
        const auto name = std::filesystem::path(path).stem().string();
        campaigns.push_back(name);
    }
    std::sort(campaigns.begin(), campaigns.end());
    auto* file = new CampaignFile;
    campaign_file_init(file);
    file->kind = SessionKind::campaign;
    int missions = 0;
    int loaded = 0;
    int units = 0;
    std::map<std::string, int32_t> first_limits;
    for (const auto& name : campaigns) {
        CampaignEnv env{&services, nullptr, 0, 0};
        if (!campaign_load_file(file, &env, name.c_str())) {
            std::cerr << "FAIL: campaign " << name << " did not load\n";
            ++failures;
            continue;
        }
        const auto count = campaign_count_missions(file);
        for (int32_t index = 0; index < count; ++index) {
            for (int32_t difficulty = 0; difficulty < 3; ++difficulty) {
                env.difficulty = difficulty;
                ++missions;
                if (campaign_bind_mission(file, &env, index)) {
                    ++loaded;
                    units += file->unit_count;
                    // Every story campaign mission maps the world and plays with line of sight.
                    if (name != "example" && name.rfind("battle tactics", 0) != 0) {
                        int32_t record[4];
                        campaign_session_record(file, record);
                        if (record[0] != 0 || record[1] != 1 || record[2] != 1 || record[3] != 1) {
                            std::cerr << "FAIL: " << name << " mission " << index
                                      << " session record\n";
                            ++failures;
                        }
                    }
                    if (difficulty == 0 && index == 0)
                        first_limits[fold(name)] = file->units_per_player;
                } else if (name != "example") {
                    std::cerr << "FAIL: " << name << " mission " << index << " difficulty "
                              << difficulty << '\n';
                    ++failures;
                }
            }
        }
    }
    // GlobalHeader maxunits, 200 when absent.
    expect(first_limits["arm campaign"] == 200, "AC01 keeps the default unit limit");
    expect(first_limits["krogoth encounter"] == 400, "Krogoth Encounter allows 400 units");
    // CC21 lists its schemas Hard, Medium, Easy (SurfaceMetal 3, 4, 5); the
    // difficulty picks the schema, not its position.
    constexpr int32_t kCc21 = 20;
    CampaignEnv easy{&services, nullptr, 0, 0};
    if (campaign_load_file(file, &easy, "core campaign") &&
        campaign_bind_mission(file, &easy, kCc21))
        expect(
            std::strcmp(file->schema, "Schema 2") == 0 && file->surface_metal == 5,
            "CC21 easy schema"
        );
    else
        expect(false, "CC21 loads");
    CampaignEnv hard{&services, nullptr, 2, 0};
    expect(
        campaign_bind_mission(file, &hard, kCc21) && std::strcmp(file->schema, "Schema 0") == 0 &&
            file->surface_metal == 3,
        "CC21 hard schema"
    );
    // The map load parses the placement tables for every kind, so a skirmish
    // map's multiplayer schema brings its [features]: Aqua Verdigris's DragonsTeeth rows.
    auto* skirmish = new CampaignFile;
    campaign_file_init(skirmish);
    skirmish->kind = SessionKind::skirmish;
    CampaignEnv roster{&services, nullptr, 1, 2};
    expect(
        campaign_load_mission_info(skirmish, &roster, "Aqua Verdigris") &&
            std::strcmp(skirmish->schema, "Schema 0") == 0,
        "Aqua Verdigris multiplayer schema"
    );
    expect(
        skirmish->feature_count == 345 && skirmish->features != nullptr &&
            std::strcmp(skirmish->features[0].name, "DragonsTeeth") == 0 &&
            skirmish->features[0].x == 28 && skirmish->features[0].z == 146 &&
            skirmish->features[344].x == 246 && skirmish->features[344].z == 70,
        "Aqua Verdigris schema features"
    );
    campaign_file_free(skirmish);
    delete skirmish;
    char list[4096];
    const auto arm = campaign_load_names(&services, "ARM", list, sizeof(list));
    const auto core = campaign_load_names(&services, "CORE", list, sizeof(list));
    expect(arm >= 5 && core >= 4, "side campaign lists");
    expect(loaded > 100, "the installed missions loaded");
    std::cout << "campaign corpus: " << campaigns.size() << " campaigns, " << loaded << "/"
              << missions << " mission loads, " << units << " placed units, ARM " << arm << " CORE "
              << core << '\n';
    campaign_file_free(file);
    delete file;
}

} // namespace

std::string counted_pattern;

int32_t memory_count(void* context, const char* pattern) {
    counted_pattern = pattern;
    int32_t count = 0;
    for (const auto& [path, _] : static_cast<MemoryFiles*>(context)->files)
        if (path.rfind("camps/", 0) == 0 && path.find('/', 6) == std::string::npos)
            ++count;
    return count;
}

// The campaign count counts "camps\*.TDF" through the variant path, whose language
// variant needs the wildcard name itself to open, which it never does.
void camps_count_tests() {
    MemoryFiles files;
    files.files["camps/arm campaign.tdf"] = "";
    files.files["camps/core campaign.tdf"] = "";
    files.files["camps/briefs/arm01.txt"] = "";
    files.files["camps-english/arm campaign.tdf"] = "";
    auto services = memory_services(files);
    expect(
        oa::data::campaign::campaign_count_files(&services) == 0, "no counting service counts none"
    );
    services.count = memory_count;
    services.language = "english";
    expect(
        oa::data::campaign::campaign_count_files(&services) == 2 &&
            counted_pattern == "camps/*.TDF",
        "campaign files counted in the plain directory"
    );
}

// Directory listing: "\name" subdirectories first when asked, then files with
// their extensions cut; a time sort keeps subdirectories oldest first and
// files newest first, a name sort ignores case.
struct FindEntries {
    std::vector<oa::data::campaign::FindRecord> directories;
    std::vector<oa::data::campaign::FindRecord> files;
};

void directory_list_tests() {
    using namespace oa::data::campaign;
    FindEntries entries;
    entries.directories = {
        {kFindDirectory, 0, 0, "."},
        {kFindDirectory, 0, 0, "Zeta"},
        {0, 0, 0, "loose.txt"},
        {kFindDirectory, 0, 0, "alpha"}
    };
    entries.files = {
        {0, 30, 10, "b.SAV"},
        {0, 10, 20, "C.sav"},
        {kFindDirectory, 5, 0, "sub"},
        {0, 20, 30, "a.sav"},
        {0, 40, 0, ".hidden"}
    };
    DirectoryFind find{
        &entries,
        [](
            void* context, const char* pattern, void (*visit)(void*, const FindRecord&), void* user
        ) {
            auto& found = *static_cast<FindEntries*>(context);
            const auto& records =
                std::string(pattern) == kListDirectoryPattern ? found.directories : found.files;
            for (const auto& record : records)
                visit(user, record);
        },
        [](void*, const char* name) { return static_cast<uint32_t>(std::strlen(name) * 100); }
    };
    char names[256];
    char tags[256];
    auto count = list_directory_entries(
        find, "SAVEGAME\\*.SAV", names, sizeof names, tags, sizeof tags, true, true, ListSort::none
    );
    expect(count == 5, "two subdirectories and three files");
    expect(
        std::strcmp(names, "\\Zeta") == 0 && std::strcmp(names + 6, "\\alpha") == 0 &&
            std::strcmp(names + 13, "b") == 0 && std::strcmp(names + 15, "C") == 0 &&
            std::strcmp(names + 17, "a") == 0 && names[19] == '\0',
        "listed in find order, extensions cut"
    );
    expect(std::strcmp(tags, "<DIR>") == 0 && std::strcmp(tags + 12, "500") == 0, "tags");

    count = list_directory_entries(
        find, "SAVEGAME\\*.SAV", names, sizeof names, nullptr, 0, false, false, ListSort::by_time
    );
    expect(
        count == 3 && std::strcmp(names, "b.SAV") == 0 && std::strcmp(names + 6, "a.sav") == 0 &&
            std::strcmp(names + 12, "C.sav") == 0,
        "files newest first"
    );
    count = list_directory_entries(
        find, "SAVEGAME\\*.SAV", names, sizeof names, nullptr, 0, true, true, ListSort::by_name
    );
    expect(
        count == 5 && std::strcmp(names, "\\alpha") == 0 && std::strcmp(names + 7, "\\Zeta") == 0 &&
            std::strcmp(names + 13, "a") == 0 && std::strcmp(names + 15, "b") == 0 &&
            std::strcmp(names + 17, "C") == 0,
        "subdirectories and files sorted apart, ignoring case"
    );

    char list[] = "d\0b\0c\0a";
    char second[] = "4\0002\0003\0001";
    uint32_t keys[] = {1, 3, 2, 4};
    sort_paired_lists(list, second, keys, 4);
    expect(
        std::strcmp(list, "a") == 0 && std::strcmp(list + 2, "b") == 0 &&
            std::strcmp(list + 6, "d") == 0 && keys[0] == 4 && keys[3] == 1 &&
            std::strcmp(second, "1") == 0,
        "a key sort after no subdirectory is descending and moves the paired entries"
    );
}

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv)) {
        corpus_tests(oa::test::require_game_assets("the installed campaign corpus"));
        if (failures != 0) {
            std::cerr << failures << " failure(s)\n";
            return 1;
        }
        std::cout << "campaign corpus tests passed\n";
        return 0;
    }
    directory_list_tests();
    camps_count_tests();
    synthetic_tests();
    skirmish_profile_tests();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "campaign file tests passed\n";
    return 0;
}
