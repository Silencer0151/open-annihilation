// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The display options of the in-game console: the graphics word the model
// renderer and fog read, the render arena release, the model light, the
// display gamma and the clock, and the check that drives them.
#include "oa/app/runtime.hpp"
#include "oa/app/match_console.hpp"

#include "oa/formats/fnt.hpp"
#include "oa/present/palette_tables.hpp"
#include "oa/sim/selection.hpp"
#include "oa/ui/console/console.hpp"
#include "oa/ui/console/game_fields.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

namespace oa::app {

namespace console = oa::ui::console;

namespace {

// Session start and the options screens' reapply read the saved Gamma as
// value / 24 + 0.5.
constexpr float kSavedGammaStep = 0.041666668F;
constexpr double kSavedGammaBase = 0.5;

} // namespace

void Runtime::set_display_gamma(float gamma) {
    oa::present::set_palette_gamma(display_.context, gamma, display_.context.device_palette);
    gamma_identity_ = true;
    for (std::size_t channel = 0; channel < gamma_table_.size(); ++channel) {
        gamma_table_[channel] = oa::present::gamma_channel(static_cast<uint8_t>(channel), gamma);
        gamma_identity_ = gamma_identity_ && gamma_table_[channel] == channel;
    }
}

void Runtime::apply_saved_gamma() {
    set_display_gamma(
        static_cast<float>(
            static_cast<double>(static_cast<int32_t>(preferences_.gamma)) *
                static_cast<double>(kSavedGammaStep) +
            kSavedGammaBase
        )
    );
}

void Runtime::apply_gamma_rgb(uint8_t* rgb, std::size_t pixels, std::size_t stride) const {
    if (gamma_identity_)
        return;
    for (std::size_t i = 0; i < pixels; ++i, rgb += stride) {
        rgb[0] = gamma_table_[rgb[0]];
        rgb[1] = gamma_table_[rgb[1]];
        rgb[2] = gamma_table_[rgb[2]];
    }
}

void Runtime::check_options_gamma() {
    const auto require = [](bool ok, const std::string& what) {
        if (!ok)
            throw std::runtime_error("options gamma check: " + what);
    };
    const auto entry_gamma = preferences_.gamma;
    const auto saved_entry = saved_general_number("Gamma");
    const auto shown_gamma = [](uint32_t saved) {
        return static_cast<float>(
            static_cast<double>(static_cast<int32_t>(saved)) *
                static_cast<double>(kSavedGammaStep) +
            kSavedGammaBase
        );
    };
    const auto open_visuals = [&] {
        exercise_click(entry::resource_name(entry::Button::options));
        require(screen_ == Screen::options, "OPTIONS did not open the options screen");
        exercise_click("VISUALS");
        require(screen_ == Screen::visuals, "VISUALS did not open the visuals panel");
    };
    const auto top_gamma = [&] {
        const auto found = std::find_if(
            resources_.layout.gadgets.begin(),
            resources_.layout.gadgets.end(),
            [](const auto& gadget) { return gadget.common.name == "GAMMA"; }
        );
        require(found != resources_.layout.gadgets.end(), "the visuals panel has no GAMMA slider");
        const auto origin = panel_origin();
        const auto x = static_cast<float>(origin.x + found->common.x + found->common.width - 1);
        const auto y = static_cast<float>(origin.y + found->common.y + found->common.height / 2);
        update_pointer(x, y);
        activate();
    };
    // A frontend frame goes out as the composed one through the display
    // gamma's channel table (render()); every byte must be the gamma's.
    const auto presented_through = [&](float gamma) {
        rebuild_surface();
        auto presented = surface_.rgb;
        apply_gamma_rgb(presented.data(), presented.size() / 3U, 3);
        for (std::size_t i = 0; i < surface_.rgb.size(); ++i)
            if (presented[i] != oa::present::gamma_channel(surface_.rgb[i], gamma))
                return false;
        return true;
    };

    constexpr uint32_t kTopGamma = 20;
    open_visuals();
    top_gamma();
    require(
        preferences_.gamma == kTopGamma && display_.context.gamma == shown_gamma(kTopGamma),
        "the top of the GAMMA slider did not store 20 and show gamma 20 / 24 + 0.5"
    );
    require(
        !gamma_identity_ && presented_through(shown_gamma(kTopGamma)),
        "the screen was not shown through the new gamma"
    );
    exercise_click("CANCEL");
    require(screen_ == Screen::single_player, "CANCEL did not return to SINGLE.GUI");
    const auto saved_after_cancel = saved_general_number("Gamma");
    require(
        preferences_.gamma == entry_gamma && display_.context.gamma == shown_gamma(entry_gamma) &&
            (saved_after_cancel == saved_entry || saved_after_cancel == entry_gamma),
        "CANCEL did not restore and show the entry gamma, or saved the slider's"
    );
    require(
        presented_through(shown_gamma(entry_gamma)),
        "the screen was not shown through the entry gamma"
    );

    open_visuals();
    top_gamma();
    exercise_click("PREV");
    require(screen_ == Screen::single_player, "Previous Menu did not return to SINGLE.GUI");
    require(
        preferences_.gamma == kTopGamma && display_.context.gamma == shown_gamma(kTopGamma) &&
            saved_general_number("Gamma") == kTopGamma,
        "Previous Menu did not keep and save gamma 20"
    );
    preferences_.gamma = entry_gamma;
    save_preferences();
    apply_saved_gamma();
    require(saved_general_number("Gamma") == entry_gamma, "the entry gamma was not saved back");
    std::cout << "options gamma check: the GAMMA slider shows gamma " << shown_gamma(kTopGamma)
              << ", CANCEL shows " << shown_gamma(entry_gamma)
              << " again, Previous Menu saves 20\n";
}

void Runtime::check_console_display_commands(const std::function<void(const char*)>& enter_line) {
    namespace flag = console::graphics_flag;
    oa::World& world = match_->state();
    oa::Game& game = world.game;
    const auto require = [](bool ok, const std::string& what) {
        if (!ok)
            throw std::runtime_error("console display check: " + what);
    };
    const auto saved = [&](const char* key) { return saved_general_number(key); };
    const auto bit = [&](uint16_t mask) { return (game.graphics_flags & mask) != 0 ? 1 : 0; };
    // The frame with the message log emptied, so that the echoes stay out of it.
    const auto frame = [&] {
        oa::sim::messages::clear_messages(game);
        render_match_surface();
        return surface_;
    };
    const auto same = [](const renderer::Surface& a, const renderer::Surface& b) {
        return a.width == b.width && a.height == b.height && a.rgb == b.rgb;
    };

    const uint8_t local = game.local_player_index;
    const oa::Unit* commander = nullptr;
    for (const auto& slot : match_->world().slots)
        if (commander == nullptr && slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == local)
            commander = &slot.record;
    const auto solar = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "ARMSOLAR");
    require(
        commander != nullptr && solar != 0 && console_->host.create_unit != nullptr,
        "no local commander or solar collector type"
    );
    constexpr int32_t kBesideCommander = 64 << 16;
    const FixedVec3 beside{
        commander->position.x + kBesideCommander, commander->position.y, commander->position.z
    };
    console_->host.create_unit(this, local, solar, &beside);
    // A sprite feature with a shadow sequence on clear cells above the commander.
    uint16_t shadowed = oa::sim::map_runtime::no_feature_index;
    for (std::size_t index = 0; index < feature_table_.defs.size(); ++index) {
        const auto& def = feature_table_.defs[index];
        if (shadowed == oa::sim::map_runtime::no_feature_index && def.seq_name_shadow != 0 &&
            (def.flags & (OA_FEATURE_FLAG_SPRITE | OA_FEATURE_FLAG_ANIMATING)) ==
                OA_FEATURE_FLAG_SPRITE)
            shadowed = static_cast<uint16_t>(index);
    }
    require(
        shadowed != oa::sim::map_runtime::no_feature_index, "the map has no feature with a shadow"
    );
    const auto& shadowed_def = feature_table_.defs[shadowed];
    const auto& plots = match_->spatial().plots;
    const auto width = static_cast<int32_t>(selected_tnt_->attribute_width);
    const auto clear = [&](int32_t x, int32_t z) {
        for (int32_t row = 0; row < std::max<int16_t>(1, shadowed_def.footprint_z); ++row)
            for (int32_t column = 0; column < std::max<int16_t>(1, shadowed_def.footprint_x);
                 ++column) {
                const auto index = static_cast<std::size_t>(z + row) * width + (x + column);
                if (x < 0 || z < 0 || x + column >= width || index >= plots.size() ||
                    plots[index].feature_word != oa::sim::spatial_state::no_feature)
                    return false;
            }
        return true;
    };
    constexpr uint32_t kCellShift = 20;
    const auto commander_x =
        static_cast<int32_t>(static_cast<uint32_t>(commander->position.x) >> kCellShift);
    const auto commander_z =
        static_cast<int32_t>(static_cast<uint32_t>(commander->position.z) >> kCellShift);
    int32_t shadow_x = -1, shadow_z = -1;
    for (int32_t step = 4; step < 12 && shadow_x < 0; ++step)
        if (clear(commander_x - 1, commander_z - step)) {
            shadow_x = commander_x - 1;
            shadow_z = commander_z - step;
        }
    require(
        shadow_x >= 0 && console_place_feature(shadowed_def.name, shadow_x, shadow_z),
        std::string("cannot place ") + shadowed_def.name + " above the commander"
    );
    const auto camera_x = match_camera_x_;
    const auto camera_z = match_camera_z_;
    const auto base = frame();
    require(cached_model_images() != 0, "the frame cached no model image");

    // Toggles one bit of the graphics word twice: the first line must flip it,
    // save it (or not) under key, release the arena (or not) and change the
    // frame; the second must restore the frame.
    const auto toggle = [&](const char* line,
                            uint16_t mask,
                            const char* key,
                            bool saves,
                            bool releases,
                            bool changes_frame) {
        const int before = bit(mask);
        const auto saved_before = saved(key);
        enter_line(line);
        require(bit(mask) != before, std::string(line) + " did not flip its graphics bit");
        require(
            saves ? saved(key) == bit(mask) : saved(key) == saved_before,
            std::string(line) + (saves ? " did not save " : " saved ") + key
        );
        if (releases)
            require(
                cached_model_images() == 0, std::string(line) + " kept the cached model images"
            );
        if (changes_frame)
            require(!same(frame(), base), std::string(line) + " did not change the frame");
        enter_line(line);
        require(bit(mask) == before, std::string(line) + " twice did not restore its bit");
        if (changes_frame)
            require(same(frame(), base), std::string(line) + " twice did not restore the frame");
    };
    toggle("+shading", flag::shading, "Shading", true, true, true);
    toggle("+antialias", flag::anti_alias, "Anti-Alias", true, true, true);
    toggle("+shadow", flag::shadow, "Shadows", true, true, true);
    toggle("+tshadow", flag::vehicle_shadow, "VehicleShadows", false, false, true);
    toggle("+fshadow", flag::feature_shadow, "FeatureShadows", false, false, true);
    // FShadow takes the sprite features' shadows away and nothing else: every
    // battlefield pixel it changes is one a feature's shadow frame covers, and
    // the staged feature's shadow is among them.
    {
        auto viewport = live_viewport(
            static_cast<uint32_t>(match_camera_x_), static_cast<uint32_t>(match_camera_z_)
        );
        viewport.destination_x = 0;
        viewport.destination_y = 0;
        const double scale = viewport.scale <= 0.0F ? 1.0 : static_cast<double>(viewport.scale);
        const auto under_shadow = [&](const MatchGafFeatureDraw& feature, int x, int y) {
            if (feature.shadow_anim >= match_gaf_anims_.size() ||
                match_gaf_anims_[feature.shadow_anim].frames.empty())
                return false;
            const auto& anim = match_gaf_anims_[feature.shadow_anim];
            const auto& image =
                anim.frames[std::min<std::size_t>(anim.frame, anim.frames.size() - 1)];
            const auto screen = project_match_point(
                viewport,
                {static_cast<uint32_t>(feature.position.x),
                 static_cast<uint32_t>(feature.position.y),
                 static_cast<uint32_t>(feature.position.z)}
            );
            const int left = screen.x - static_cast<int>(std::lround(image.origin_x * scale));
            const int top = screen.y - static_cast<int>(std::lround(image.origin_y * scale));
            const int w = std::max(1, static_cast<int>(std::lround(image.width * scale)));
            const int h = std::max(1, static_cast<int>(std::lround(image.height * scale)));
            if (x < left || y < top || x >= left + w || y >= top + h)
                return false;
            const auto offset =
                static_cast<std::size_t>(y - top) * image.height / static_cast<std::size_t>(h) *
                    image.width +
                static_cast<std::size_t>(x - left) * image.width / static_cast<std::size_t>(w);
            return offset < image.coverage.size() && image.coverage[offset] != 0;
        };
        const MatchGafFeatureDraw* staged = nullptr;
        for (const auto& feature : match_gaf_features_)
            if (feature.cell_x == shadow_x && feature.cell_z == shadow_z &&
                feature.feature_index == shadowed)
                staged = &feature;
        require(staged != nullptr, "the staged feature was not drawn");
        const auto world_layer = [&] {
            (void)frame();
            return match_world_cpu_;
        };
        auto with = world_layer();
        enter_line("+fshadow");
        auto without = world_layer();
        enter_line("+fshadow");
        if (bit(flag::feature_shadow) == 0)
            std::swap(with, without);
        require(
            with.width == without.width && with.rgb.size() == without.rgb.size(),
            "+fshadow changed the battlefield size"
        );
        bool staged_shadow = false;
        for (int y = 0; y < static_cast<int>(with.height); ++y)
            for (int x = 0; x < static_cast<int>(with.width); ++x) {
                const auto at = (static_cast<std::size_t>(y) * with.width + x) * 3U;
                if (std::equal(
                        with.rgb.begin() + at, with.rgb.begin() + at + 3, without.rgb.begin() + at
                    ))
                    continue;
                bool shadowed_pixel = false;
                for (const auto& feature : match_gaf_features_)
                    shadowed_pixel = shadowed_pixel || under_shadow(feature, x, y);
                require(
                    shadowed_pixel,
                    "+fshadow changed pixel " + std::to_string(x) + "," + std::to_string(y) +
                        " that no feature shadow covers"
                );
                staged_shadow = staged_shadow || under_shadow(*staged, x, y);
            }
        require(staged_shadow, "+fshadow left the staged feature's shadow");
    }
    toggle("+dither", flag::dither, "DitheredFog", true, false, false);
    enter_line("+dither");
    (void)frame();
    require(fog_shading_.dithered == (bit(flag::dither) != 0), "the fog did not follow +dither");
    enter_line("+dither");

    // Light a b c: the light is a%, b%, c% and the arena goes.
    enter_line("+light 10 20 30");
    require(
        model_light_ && (*model_light_)[0] == static_cast<float>(10 * 0.01) &&
            (*model_light_)[1] == static_cast<float>(20 * 0.01) &&
            (*model_light_)[2] == static_cast<float>(30 * 0.01),
        "+light 10 20 30 did not set the model light to 0.1, 0.2, 0.3"
    );
    require(cached_model_images() == 0, "+light kept the cached model images");
    require(!same(frame(), base), "+light did not change the shaded building");
    enter_line("+light -80 100 25");
    require(same(frame(), base), "+light -80 100 25 did not restore the default light");

    const auto screen_chat = game.screen_chat;
    enter_line("+screenchat");
    require(
        game.screen_chat == (screen_chat ^ 1U) && saved("screenchat") == (screen_chat ^ 1U),
        "+screenchat did not flip and save the on-screen chat"
    );
    enter_line("+screenchat");
    require(saved("screenchat") == screen_chat, "+screenchat twice did not save it back");

    // Gamma n: the palette at n * 0.1, so 20 doubles every channel up to 255.
    const auto gamma = game.gamma;
    enter_line("+gamma 20");
    require(
        display_.context.gamma == 2.0F && game.gamma == 20 && saved("Gamma") == 20,
        "+gamma 20 did not set gamma 2.0 and save 20"
    );
    const auto bright = frame();
    bool doubled = bright.rgb.size() == base.rgb.size();
    for (std::size_t i = 0; doubled && i < base.rgb.size(); ++i)
        doubled = bright.rgb[i] == std::min(255, base.rgb[i] * 2);
    require(doubled, "+gamma 20 did not show every channel doubled");
    enter_line("+gamma 10");
    require(
        display_.context.gamma == 1.0F && same(frame(), base), "+gamma 10 did not restore the frame"
    );
    game.gamma = gamma;
    console_->host.save_game_options(this);
    apply_saved_gamma();

    // Clock: the option word bit, saved at once, and the game time drawn two
    // pixels right of the side column and two above the bottom bar.
    const auto clock = console::console_flags(game) & console::console_flag::clock;
    if (clock != 0)
        enter_line("+clock");
    const auto without = frame();
    enter_line("+clock");
    require(
        (console::console_flags(game) & console::console_flag::clock) != 0 && saved("clock") == 1,
        "+clock did not set and save the clock"
    );
    const auto with = frame();
    int x0 = with.width, y0 = with.height, x1 = -1, y1 = -1;
    for (int y = 0; y < static_cast<int>(with.height); ++y)
        for (int x = 0; x < static_cast<int>(with.width); ++x) {
            const auto at = (static_cast<std::size_t>(y) * with.width + x) * 3U;
            if (std::equal(
                    with.rgb.begin() + at, with.rgb.begin() + at + 3, without.rgb.begin() + at
                ))
                continue;
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
        }
    const oa::formats::fnt::Font* font = match_label_font();
    const int scale = hud_text_scale();
    const int text_top = match_layout_.bottom_bar_y() - 2 * scale -
                         static_cast<int>(oa::formats::fnt::line_height(*font)) * scale;
    require(
        x1 >= 0 && x0 >= match_layout_.left + 2 * scale && y0 >= text_top &&
            y1 < match_layout_.bottom_bar_y() - scale && x0 < match_layout_.left + 8 * scale,
        "+clock drew nothing, or not at the side column above the bottom bar"
    );
    // The chat formatter names the speaker by Player.second_name.
    char echo[64];
    std::snprintf(echo, sizeof echo, "<%.30s> +clock", game.players[local].second_name);
    enter_line("+clock");
    require(saved("clock") == 0, "+clock twice did not save the clock off");
    if (const auto lines = match_message_lines(); lines.empty() || lines.back() != echo)
        throw std::runtime_error(
            std::string("console display check: +clock was not echoed as ") + echo
        );
    if (clock != 0)
        enter_line("+clock");

    // BigBrother: the countdown starts at one, so the next unit sweep selects
    // the viewpoint player's first selectable unit and the camera follows it;
    // 90 ticks later the next one.
    clear_local_selection();
    enter_line("+bigbrother");
    require(
        (game.periodic_flags & console::kPeriodicFlagObserver) != 0 && game.periodic_countdown == 1,
        "+bigbrother did not start the observer camera"
    );
    const auto run_tick = [&] {
        ++match_timing_.tick;
        match_->simulation().tick = match_timing_.tick;
        match_->tick();
        follow_match_camera_unit();
    };
    run_tick();
    const auto first = static_cast<uint16_t>(oa::oa_unit_slot_from_ref(game.follow_unit));
    require(
        first != 0 && match_tracking_ && tracked_match_unit_ == first &&
            (world.units[first].flags & OA_UNIT_FLAG_SELECTED) != 0 &&
            game.periodic_countdown == 90,
        "the observer camera did not select and follow a unit"
    );
    for (int tick = 0; tick < 90; ++tick)
        run_tick();
    const auto second = static_cast<uint16_t>(oa::oa_unit_slot_from_ref(game.follow_unit));
    require(
        second != 0 && second != first && tracked_match_unit_ == second &&
            (world.units[first].flags & OA_UNIT_FLAG_SELECTED) == 0,
        "the observer camera did not move on after 90 ticks"
    );
    enter_line("+bigbrother");
    follow_match_camera_unit();
    require(
        (game.periodic_flags & console::kPeriodicFlagObserver) == 0 && game.follow_unit == 0 &&
            !match_tracking_,
        "+bigbrother twice did not stop the observer camera"
    );
    clear_local_selection();
    match_camera_x_ = camera_x;
    match_camera_z_ = camera_z;
    (void)console_burn_feature(shadow_x, shadow_z, true);
    std::cout << "console display check: +shading, +antialias and +shadow flip, save and redraw "
                 "the models, +tshadow and +fshadow flip unsaved and drop the unit and feature "
                 "shadows, +dither shades the fog, +light "
                 "relights the buildings, +screenchat saves, +gamma 20 doubles every channel, "
                 "+clock saves and draws the game time, +bigbrother follows the units\n";
}

} // namespace oa::app
