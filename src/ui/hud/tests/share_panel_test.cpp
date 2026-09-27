// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"

#include "oa/ui/hud/order_panel.hpp"
#include "oa/ui/hud/share_panel.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace oa;
using namespace oa::ui::hud;

namespace {

struct Match {
    std::unique_ptr<Game> game = std::make_unique<Game>();
    UnitEconomy economy[OA_PLAYER_COUNT]{};
    UnitEconomy* economies[OA_PLAYER_COUNT]{};
    uint8_t setup[OA_PLAYER_COUNT]{};

    Match() {
        for (uint8_t i = 0; i < OA_PLAYER_COUNT; ++i) {
            economies[i] = &economy[i];
            game->players[i].index = kNoPlayer;
        }
        add(0, OA_PLAYER_STATUS_LOCAL, 100);
        add(1, OA_PLAYER_STATUS_COMPUTER, 200);
        add(2, OA_PLAYER_STATUS_MIRRORED, 300);
        game->local_player_index = 0;
    }

    void add(uint8_t index, uint8_t status, uint32_t player_id) {
        auto& player = game->players[index];
        player.in_use = 1;
        player.status = status;
        player.index = index;
        player.player_id = player_id;
        player.unit_count = 3;
        player.units_created = 3;
        player.metal = 500.0F;
        player.energy = 800.0F;
    }

    ShareWorld world(int32_t difficulty = OA_DIFFICULTY_HARD) {
        return {game->players, economies, setup, game->local_player_index, difficulty};
    }
};

struct Sender {
    std::vector<std::string> log;

    static void energy(void* user, uint8_t from, uint8_t to, float amount) {
        static_cast<Sender*>(user)->log.push_back(
            "E" + std::to_string(from) + std::to_string(to) + ":" +
            std::to_string(static_cast<int>(amount))
        );
    }

    static void metal(void* user, uint8_t from, uint8_t to, float amount) {
        static_cast<Sender*>(user)->log.push_back(
            "M" + std::to_string(from) + std::to_string(to) + ":" +
            std::to_string(static_cast<int>(amount))
        );
    }

    static void units(void* user, uint8_t from, uint8_t to) {
        static_cast<Sender*>(user)->log.push_back("U" + std::to_string(from) + std::to_string(to));
    }

    static void map(void* user, uint8_t from, uint8_t to) {
        static_cast<Sender*>(user)->log.push_back("L" + std::to_string(from) + std::to_string(to));
    }

    ShareHost host() { return {this, energy, metal, units, map}; }
};

void test_participation() {
    Match match;
    auto& player = match.game->players[2];
    CHECK(player_participating(player));
    player.unit_count = 0;
    CHECK(!player_participating(player)); // lost every unit
    player.units_created = 0;
    CHECK(player_participating(player)); // never had any
    player.status = OA_PLAYER_STATUS_FREE;
    CHECK(!player_participating(player));
}

void test_transfers() {
    Match match;
    auto world = match.world();
    Sender sender;
    transfer_metal(world, 0, 2, 120.0F, true, sender.host());
    CHECK(match.game->players[0].metal == 380.0F);
    CHECK(match.economy[0].metal.requested == 120.0F);
    CHECK(match.economy[2].metal.produced == 120.0F);
    CHECK(sender.log.back() == "M02:120");

    // The local giver is capped to its store.
    transfer_energy(world, 0, 2, 5000.0F, true, sender.host());
    CHECK(match.game->players[0].energy == 0.0F);
    CHECK(match.economy[2].energy.produced == 800.0F);
    CHECK(sender.log.back() == "E02:800");

    // An empty store moves nothing and sends nothing.
    const auto sent = sender.log.size();
    transfer_energy(world, 0, 2, 10.0F, true, sender.host());
    CHECK(sender.log.size() == sent);

    // A transfer from another player's game credits without debiting or echoing.
    transfer_metal(world, 2, 0, 50.0F, false, sender.host());
    CHECK(match.game->players[2].metal == 500.0F);
    CHECK(match.economy[0].metal.produced == 50.0F);
    CHECK(sender.log.size() == sent);

    // Computer recipients take a handicapped share.
    auto half = match.world(0);
    transfer_metal(half, 0, 1, 100.0F, true, sender.host());
    CHECK(match.economy[1].metal.produced == 50.0F);
    auto most = match.world(1);
    transfer_metal(most, 0, 1, 100.0F, true, sender.host());
    CHECK(std::fabs(match.economy[1].metal.produced - 120.0F) < 1e-4F);

    transfer_metal(world, 0, kNoPlayer, 10.0F, true, sender.host());
    CHECK(match.game->players[0].metal == 180.0F);
}

void test_panel_flow() {
    Match match;
    match.setup[1] = kSetupWatcher; // excluded recipient
    match.add(3, OA_PLAYER_STATUS_MIRRORED, 400);
    match.game->players[3].unit_count = 0; // defeated
    auto world = match.world();
    SharePanel panel{};
    uint16_t frame = 0;
    CHECK(open_share_panel(world, panel, frame));
    CHECK((frame & kFrameSharePanelOpen) != 0);
    CHECK(panel.recipient_count == 1 && panel.recipients[0] == 2 && panel.player_ids[0] == 300);
    CHECK(panel.metal_max == 500 && panel.energy_max == 800);

    const auto metal = slider_amount(40, 101, panel.metal_max);
    CHECK(metal == 200);
    CHECK(slider_amount(7, 1, 500) == 0);
    CHECK(slider_amount(1, 3, 7) == 3);
    char text[16];
    format_share_amount(text, sizeof text, metal);
    CHECK(std::strcmp(text, "200") == 0);

    Sender sender;
    std::vector<std::string> sounds;
    HudEvents events{
        &sounds,
        [](void* user, const char* name) {
            static_cast<std::vector<std::string>*>(user)->emplace_back(name);
        },
        nullptr
    };
    CHECK(
        share_panel_click(
            world, panel, "SHARUNIT", 0, 0, 0, false, false, frame, events, sender.host()
        ) == ShareClick::toggled
    );
    CHECK(sounds.back() == "Options");
    CHECK(
        share_panel_click(
            world, panel, "OK", 0, metal, 100, true, true, frame, events, sender.host()
        ) == ShareClick::none
    );
    CHECK(match.game->players[0].metal == 300.0F && match.game->players[0].energy == 700.0F);
    CHECK(sender.log.size() == 4 && sender.log[0] == "M02:200" && sender.log[1] == "E02:100");
    CHECK(sender.log[2] == "U02" && sender.log[3] == "L02");

    // No selection: nothing moves.
    CHECK(
        share_panel_click(
            world, panel, "OK", -1, 10, 10, false, false, frame, events, sender.host()
        ) == ShareClick::none
    );
    CHECK(sender.log.size() == 4);
    // A recipient that left since the panel opened is ignored.
    match.game->players[2].status = OA_PLAYER_STATUS_FREE;
    CHECK(
        share_panel_click(
            world, panel, "OK", 0, 10, 10, false, false, frame, events, sender.host()
        ) == ShareClick::none
    );
    CHECK(sender.log.size() == 4);

    CHECK(
        share_panel_click(
            world, panel, "CANCEL", 0, 0, 0, false, false, frame, events, sender.host()
        ) == ShareClick::none
    );
    CHECK(sounds.back() == "Previous");
    CHECK(
        share_panel_click(
            world, panel, "PLYRLIST", 0, 0, 0, false, false, frame, events, sender.host()
        ) == ShareClick::clear_selection
    );
    CHECK(
        share_panel_click(
            world, panel, nullptr, -1, 0, 0, false, false, frame, events, sender.host()
        ) == ShareClick::closed
    );
    CHECK((frame & kFrameSharePanelOpen) == 0);

    // A local player flagged out of sharing cannot open the panel.
    match.setup[0] = kSetupWatcher;
    CHECK(!open_share_panel(world, panel, frame));
}

} // namespace

int main() {
    test_participation();
    test_transfers();
    test_panel_flow();
    return 0;
}
