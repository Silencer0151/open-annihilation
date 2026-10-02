// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Campaign savegames through the Summary writer, the loader's Summary read
// and the campaign object's mission binding over the installed Arm Campaign.
#include "oa/data/campaign/campaign_file.hpp"
#include "oa/core/world.h"
#include "oa/ui/frontend_state/dispatcher.hpp"
#include "oa/data/persist/hapibank.hpp"
#include "oa/data/persist/save_sections.hpp"
#include "oa/ui/frontend/savegame_dialogs.hpp"
#include "test_support.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace oa::ui::frontend::test {
namespace {

namespace missions = oa::data::campaign;

constexpr const char* kCampaign = "Arm Campaign";
constexpr int32_t kAc01 = 0; // MISSION0
constexpr int32_t kAc02 = 1; // MISSION1: missionfile=AC02.ota
constexpr const char* kAc02Name = "2: CORE KBot Base, Destroy It!";
constexpr const char* kAc03Name = "3: Spider Technology";
constexpr const char* kResults = "WUUUUUUUUUUUUUUUUUUUUUUUU";

struct CampaignObject {
    missions::CampaignFile file{};
    missions::CampaignEnv env{game_campaign_files(), nullptr, 1, 0};

    CampaignObject() {
        missions::campaign_file_init(&file);
        file.kind = missions::SessionKind::campaign;
    }

    ~CampaignObject() { missions::campaign_file_free(&file); }

    CampaignObject(const CampaignObject&) = delete;
    CampaignObject& operator=(const CampaignObject&) = delete;
};

// The Summary hooks a campaign save binds: the campaign object's name and
// mission-list name, the next mission between missions and the rebind to the
// Game block's mission index afterwards.
struct Session {
    CampaignObject* campaign{};
    World* world{};
    int advanced = 0;
    int rebound = 0;
};

data::persist::SummaryHooks campaign_hooks(Session* session) {
    data::persist::SummaryHooks hooks{};
    hooks.context = session;
    hooks.build_date = "Nov 17 1999";
    hooks.build_time = "11:45:48";
    hooks.campaign_name = [](void* c) {
        return missions::campaign_name_if_loaded(&static_cast<Session*>(c)->campaign->file);
    };
    hooks.advance_next_mission = [](void* c) {
        auto* s = static_cast<Session*>(c);
        ++s->advanced;
        (void)missions::campaign_advance_next_mission(&s->campaign->file, &s->campaign->env);
    };
    hooks.mission_name = [](void* c) -> const char* {
        return static_cast<Session*>(c)->campaign->file.mission_name;
    };
    hooks.game_type = [](void*) { return data::persist::game_type_campaign; };
    hooks.bind_mission_info = [](void* c) {
        auto* s = static_cast<Session*>(c);
        ++s->rebound;
        (void)missions::campaign_bind_mission(
            &s->campaign->file, &s->campaign->env, s->world->game.mission_index
        );
    };
    hooks.radar_image = [](void*) -> const data::persist::ImageRows* { return nullptr; };
    hooks.write_stats_panel = [](void*, data::persist::Bank*) {};
    hooks.save_conditions = [](void*, data::persist::Bank*) {};
    return hooks;
}

struct SavedImage {
    std::vector<uint8_t> bytes;
};

data::persist::FileSink memory_sink(SavedImage* image) {
    return {image, [](void* c, const char*, const uint8_t* data, std::size_t size) {
                static_cast<SavedImage*>(c)->bytes.assign(data, data + size);
                return true;
            }};
}

// A save of a mission from a Game block in the given mode; the campaign
// object is on that mission and the local player plays ARM.
SavedImage save_mission(CampaignObject& campaign, int32_t mode, Session& session, int32_t mission) {
    auto world = std::make_unique<World>();
    world->game.mode = mode;
    world->game.difficulty = 2;
    world->game.player_count = 2;
    world->game.mission_index = mission;
    world->game.players[0].info = oa_ref_from_index(0);
    world->player_info[0].side = 0;
    std::memcpy(world->game.mission_results, kResults, std::strlen(kResults) + 1);
    data::persist::SaveHooks save_hooks{};
    save_hooks.resolve = [](void* c, data::persist::SaveRef kind, oa_ref32 ref) -> void* {
        auto* w = static_cast<World*>(c);
        return kind == data::persist::SaveRef::player_info && ref != 0 && ref <= OA_PLAYER_COUNT
                   ? &w->player_info[ref - 1]
                   : nullptr;
    };
    save_hooks.context = world.get();
    sim::world_environment::MeteorState meteor{};
    uint8_t feature_record[data::persist::feature_record_bytes]{};
    data::persist::SaveContext save{
        world.get(), nullptr, nullptr, feature_record, &meteor, &save_hooks
    };
    session = {&campaign, world.get()};
    const auto hooks = campaign_hooks(&session);
    SavedImage image;
    const auto sink = memory_sink(&image);
    OA_CHECK(data::persist::save_write_game(&save, &hooks, "SAVEGAME/ac02.sav", "AC02", 7, &sink));
    return image;
}

SavedImage save_ac02(CampaignObject& campaign, int32_t mode, Session& session) {
    return save_mission(campaign, mode, session, kAc02);
}

struct ReadBank {
    data::persist::Bank bank{};

    explicit ReadBank(const SavedImage& image) {
        data::persist::bank_init(&bank);
        data::persist::BankError error{};
        OA_CHECK(
            data::persist::bank_read_image(
                &bank,
                image.bytes.data(),
                static_cast<uint32_t>(image.bytes.size()),
                data::persist::savegame_description,
                nullptr,
                &error
            )
        );
        data::persist::bank_open_account(&bank, data::persist::save_key::summary);
    }

    ~ReadBank() { data::persist::bank_destroy(&bank); }

    ReadBank(const ReadBank&) = delete;
    ReadBank& operator=(const ReadBank&) = delete;
};

// Binds a campaign save as loading one does: a fresh campaign object of the
// saved name, bound by the saved mission-list name.
int32_t bind_saved_mission(
    const LoadSummary& summary, const missions::CampaignFiles* files = game_campaign_files()
) {
    CampaignObject loaded;
    loaded.env.files = files;
    if (!summary.has_campaign ||
        !missions::campaign_load_file(&loaded.file, &loaded.env, summary.campaign.data()) ||
        !missions::campaign_select_mission(&loaded.file, &loaded.env, summary.mission.data()))
        return -1;
    return loaded.file.mission_index;
}

bool arm_campaign_present() {
    const oa::AssetStore* assets = game_assets();
    const bool present = assets != nullptr && assets->file_size("camps/arm campaign.tdf") != 0;
    OA_CHECK(present);
    return present;
}

} // namespace

OA_GAME_DATA_TEST(campaign_save_in_match_names_the_mission_list_entry) {
    if (!arm_campaign_present())
        return;
    CampaignObject campaign;
    OA_CHECK(missions::campaign_load_file(&campaign.file, &campaign.env, kCampaign));
    OA_CHECK(missions::campaign_bind_mission(&campaign.file, &campaign.env, kAc02));
    Session session;
    const auto image = save_ac02(campaign, data::persist::game_mode_in_match, session);
    OA_CHECK(session.advanced == 0 && session.rebound == 0);
    ReadBank read(image);
    data::persist::Bank* bank = &read.bank;
    OA_CHECK(
        data::persist::bank_get_int(bank, data::persist::save_key::game_type, 0) ==
        data::persist::game_type_campaign
    );
    OA_CHECK(
        std::string(data::persist::bank_get_text(bank, data::persist::save_key::campaign, "")) ==
        kCampaign
    );
    OA_CHECK(
        std::string(data::persist::bank_get_text(bank, data::persist::save_key::mission, "")) ==
        kAc02Name
    );
    OA_CHECK(
        std::string(data::persist::bank_get_text(bank, data::persist::save_key::map, "")) ==
        kAc02Name
    );
    OA_CHECK(!data::persist::bank_has_field(bank, data::persist::save_key::between_missions));
    OA_CHECK(!data::persist::bank_has_field(bank, data::persist::save_key::commander_death));

    LoadSummary summary;
    OA_CHECK(savegame_read_load_summary(savegame_persist_reader(nullptr), bank, summary));
    OA_CHECK(summary.game_type == data::persist::game_type_campaign && !summary.between_missions);
    OA_CHECK(summary.side == 0 && summary.difficulty == 2);
    OA_CHECK(std::string(summary.start_pattern.data()) == kResults);
    OA_CHECK(bind_saved_mission(summary) == kAc02);
}

OA_GAME_DATA_TEST(campaign_save_between_missions_names_the_next_mission) {
    if (!arm_campaign_present())
        return;
    CampaignObject campaign;
    OA_CHECK(missions::campaign_load_file(&campaign.file, &campaign.env, kCampaign));
    OA_CHECK(missions::campaign_bind_mission(&campaign.file, &campaign.env, kAc02));
    Session session;
    const auto image = save_ac02(campaign, 0, session);
    OA_CHECK(session.advanced == 1 && session.rebound == 1);
    // The Summary names AC03; the campaign object is back on AC02.
    OA_CHECK(campaign.file.mission_index == kAc02);
    OA_CHECK(std::string(campaign.file.mission_name) == kAc02Name);
    ReadBank read(image);
    data::persist::Bank* bank = &read.bank;
    OA_CHECK(
        std::string(data::persist::bank_get_text(bank, data::persist::save_key::mission, "")) ==
        kAc03Name
    );
    OA_CHECK(data::persist::bank_get_int(bank, data::persist::save_key::between_missions, 0) == 1);
    OA_CHECK(bank->accounts->count == 1); // the Summary alone

    LoadSummary summary;
    OA_CHECK(savegame_read_load_summary(savegame_persist_reader(nullptr), bank, summary));
    OA_CHECK(summary.between_missions);
    OA_CHECK(bind_saved_mission(summary) == kAc02 + 1);
}

// A campaign whose next mission's files are missing: saved between missions
// after AC01, the save loads AC02's information to name it, which shows the
// mission loader's message, and binds AC01 again; the save is written as
// ever. Starting AC02 from the end-of-mission panel or from the save shows
// the same message and does not bind it.
OA_GAME_DATA_TEST(campaign_save_between_missions_with_the_next_mission_missing) {
    if (!arm_campaign_present())
        return;
    MissingMissionFiles missing("ac02");
    CampaignObject campaign;
    campaign.env.files = &missing.files;
    OA_CHECK(missions::campaign_load_file(&campaign.file, &campaign.env, kCampaign));
    OA_CHECK(missions::campaign_bind_mission(&campaign.file, &campaign.env, kAc01));
    const std::string ac01_name = campaign.file.mission_name;
    OA_CHECK(missing.messages.empty());

    Session session;
    const auto image = save_mission(campaign, 0, session, kAc01);
    OA_CHECK(session.advanced == 1 && session.rebound == 1);
    const std::string message =
        "Hey, joker!  There is no mission defintion for this mission: AC02.ota";
    OA_CHECK(missing.messages.size() == 1 && missing.messages[0] == message);
    OA_CHECK(campaign.file.mission_index == kAc01);
    OA_CHECK(campaign.file.mission_name == ac01_name);
    ReadBank read(image);
    data::persist::Bank* bank = &read.bank;
    OA_CHECK(
        std::string(data::persist::bank_get_text(bank, data::persist::save_key::mission, "")) ==
        kAc02Name
    );
    OA_CHECK(data::persist::bank_get_int(bank, data::persist::save_key::between_missions, 0) == 1);

    // Start on the end-of-mission panel binds the Missions row.
    OA_CHECK(!missions::campaign_bind_mission(&campaign.file, &campaign.env, kAc02));
    OA_CHECK(missing.messages.size() == 2 && missing.messages[1] == message);

    // Loading the save binds the mission it names.
    LoadSummary summary;
    OA_CHECK(savegame_read_load_summary(savegame_persist_reader(nullptr), bank, summary));
    OA_CHECK(summary.between_missions);
    OA_CHECK(bind_saved_mission(summary, &missing.files) == -1);
    OA_CHECK(missing.messages.size() == 3 && missing.messages[2] == message);
    OA_CHECK(bind_saved_mission(summary) == kAc02);
}

// The loader copies "Thumbs" with a 25-byte bound and replaces a record that
// is not 25 long by the skirmish start's: 25 'U' bytes and a terminating zero.
OA_TEST(load_summary_thumbs_rule) {
    const auto read = [](const char* thumbs) {
        data::persist::Bank bank{};
        data::persist::bank_init(&bank);
        OA_CHECK(data::persist::bank_reset(&bank));
        data::persist::bank_open_account(&bank, data::persist::save_key::summary);
        data::persist::bank_set_text(&bank, data::persist::save_key::mission, kAc02Name);
        if (thumbs != nullptr)
            data::persist::bank_set_text(&bank, data::persist::save_key::thumbs, thumbs);
        LoadSummary summary;
        OA_CHECK(savegame_read_load_summary(savegame_persist_reader(nullptr), &bank, summary));
        data::persist::bank_destroy(&bank);
        return summary.start_pattern;
    };
    const std::string reset(25, static_cast<char>(0x55));
    OA_CHECK(std::string(read(kResults).data()) == kResults);
    const auto shorter = read("WWL");
    OA_CHECK(std::string(shorter.data()) == reset && shorter[25] == '\0');
    OA_CHECK(std::string(read("").data()) == reset);
    OA_CHECK(std::string(read(nullptr).data()) == reset);
    // Longer records keep their first 25 results.
    OA_CHECK(std::string(read("LLLLLLLLLLLLLLLLLLLLLLLLLWW").data()) == std::string(25, 'L'));
}

OA_GAME_DATA_TEST(campaign_save_unknown_mission_does_not_bind) {
    if (!arm_campaign_present())
        return;
    LoadSummary summary;
    summary.has_campaign = true;
    std::snprintf(summary.campaign.data(), summary.campaign.size(), "%s", kCampaign);
    std::snprintf(summary.mission.data(), summary.mission.size(), "%s", "AC02.ota");
    OA_CHECK(bind_saved_mission(summary) == -1);
}

} // namespace oa::ui::frontend::test
