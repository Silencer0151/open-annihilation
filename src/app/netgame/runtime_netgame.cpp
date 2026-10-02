// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Network play's map lookup by mission name and the network load's player
// bars in the app.
#include "oa/app/runtime.hpp"
#include "network_play.hpp"
#include "oa/app/check_host.hpp"
#include "net_state.hpp"

#include "oa/ui/frontend_renderer/gadget_draw.hpp"
#include "oa/netgame/match/net_match.hpp"
#include "oa/formats/ota.hpp"
#include "oa/present/raster.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace oa::app {

namespace {

// Offsets into the guipal-derived colour table the loading screen draws in.
constexpr std::size_t kLoadChipDoneColor = 10;
constexpr std::size_t kLoadPlayerBarColor = 4;

} // namespace

bool NetworkPlay::select_map_named(std::string_view name) {
    if (name.empty())
        return false;
    if (runtime_.select_map(name) != 0)
        return true;
    const auto same = [](std::string_view left, std::string_view right) {
        return left.size() == right.size() &&
               std::equal(
                   left.begin(), left.end(), right.begin(), [](unsigned char a, unsigned char b) {
                       return std::tolower(a) == std::tolower(b);
                   }
               );
    };
    const CheckHost host = check_host(runtime_);
    for (const auto& path : host.assets(host.context)->list_effective("maps", ".ota")) {
        const auto bytes = runtime_.read(path);
        if (!bytes)
            continue;
        const auto parsed = oa::formats::ota::parse(
            std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size())
        );
        if (!parsed.ok() || !same(parsed.metadata->mission_name, name))
            continue;
        return runtime_.select_map(fs::path(path).stem().string()) != 0;
    }
    return false;
}

void NetworkPlay::draw_loading_players(oa::Surface& target, const oa::present::GafSprites* font) {
    namespace nm = oa::netgame::match;
    const auto* session = loading_net_match();
    if (session == nullptr)
        return;
    nm::LoadingScreenStatus status{};
    nm::net_match_loading_screen_status(session->net.get(), &status);
    for (int32_t i = 0; i < status.bar_count; ++i) {
        const auto& bar = status.bars[i];
        oa::Rect32 area{bar.left, nm::loading_bar_top, bar.right, nm::loading_bar_bottom};
        oa::present::fill_clipped_rect(&target, area, runtime_.ui_colors_[kLoadPlayerBarColor]);
        area.x2 = bar.filled;
        oa::present::fill_clipped_rect(&target, area, runtime_.ui_colors_[kLoadChipDoneColor]);
        renderer::draw_gadget_text(
            &target, font, bar.player->name, bar.left, nm::loading_bar_top, bar.right - bar.left, 0
        );
    }
    renderer::draw_gadget_text(
        &target,
        font,
        status.text,
        nm::loading_status_x,
        nm::loading_status_y,
        renderer::gadget_text_unbounded,
        0
    );
}

} // namespace oa::app
