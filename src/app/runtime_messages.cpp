// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The in-game message log (Game.chat_lines) drawn over the battlefield, and
// the game speed keys that post to it.
#include "oa/app/runtime.hpp"
#include "oa/sim/speed.hpp"
#include "oa/ui/hud/chat_panel.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {
namespace {

namespace messages = oa::sim::messages;

std::optional<oa::formats::fnt::Font>
load_engine_font(oa::AssetStore& assets, const char* name, const char* language) {
    try {
        return oa::formats::fnt::load_named_fnt(assets, name, language != nullptr ? language : "");
    } catch (const std::exception& error) {
        std::cerr << name << " font unavailable: " << error.what() << '\n';
        return std::nullopt;
    }
}

} // namespace

void Runtime::load_common_fonts() {
    const char* language = oa::app::command_line::launch_language(options_.launch);
    message_font_ = load_engine_font(assets_, "COMIX", language);
    small_font_ = load_engine_font(assets_, "smlfont", language);
}

messages::Hooks Runtime::message_hooks() {
    messages::Hooks hooks{};
    hooks.context = this;
    hooks.play_sound = [](void* context, const char* name) {
        static_cast<Runtime*>(context)->play_match_interface_sound(name);
    };
    // Camera centring moves the camera at once.
    hooks.center_camera = [](void* context, int32_t x, int32_t y, int32_t) {
        auto& runtime = *static_cast<Runtime*>(context);
        runtime.match_camera_x_ = x - runtime.visible_map_width() / 2;
        runtime.match_camera_z_ = y - runtime.visible_map_height() / 2;
    };
    hooks.random = [](void* context) -> uint32_t {
        auto& runtime = *static_cast<Runtime*>(context);
        return runtime.match_ ? static_cast<uint32_t>(runtime.match_->lcg_rand()) : 0U;
    };
    if (extension_.message_hooks != nullptr)
        extension_.message_hooks(extension_.context, *this, hooks);
    return hooks;
}

void Runtime::bind_message_log() {
    auto& game = match_->state().game;
    messages::set_log_options(
        game,
        static_cast<int32_t>(preferences_.text_lines),
        static_cast<int32_t>(preferences_.text_scroll),
        messages::filter_session_start,
        preferences_.screen_chat
    );
    ensure_ui_colors();
    std::copy_n(ui_colors_.begin(), sizeof game.ui_colors, game.ui_colors);
    // Elimination lights the killer's kills and the victim's losses on the
    // kills board while F4 holds it out.
    kill_board_ = {};
    match_->kill_board.context = this;
    match_->kill_board.flash = [](void* context, uint8_t killer, uint8_t victim) {
        oa::ui::hud::flash_kill(static_cast<Runtime*>(context)->kill_board_, killer, victim);
    };
    // Once a player's last unit is gone: a multiplayer game
    // announces the player leaving, a skirmish its forces' end, a campaign
    // nothing.
    match_->last_unit = {this, [](void* context, uint8_t player) {
                             auto& runtime = *static_cast<Runtime*>(context);
                             auto& world = runtime.match_->state();
                             const oa::Player* owner = oa::world_player(&world, player);
                             if (owner == nullptr)
                                 return;
                             const auto& extension = runtime.extension_;
                             if (extension.player_gone != nullptr &&
                                 extension.player_gone(extension.context, runtime, world, *owner))
                                 return;
                             if (!runtime.campaign_mission_)
                                 messages::post_elimination(world, *owner, runtime.message_hooks());
                         }};
}

void Runtime::post_match_message(std::string_view text, uint8_t kind, uint16_t value) {
    if (!match_)
        return;
    char line[messages::text_bytes];
    std::snprintf(line, sizeof line, "%.*s", static_cast<int>(text.size()), text.data());
    messages::post_message(
        match_->state(), line, kind, value, messages::sender_none, message_hooks()
    );
}

void Runtime::post_unit_report(uint16_t unit, std::string_view text) {
    const auto* definition = definition_for(unit);
    const std::string name = definition != nullptr && !definition->display_name.empty()
                                 ? definition->display_name
                                 : unit_info_name(unit);
    post_match_message(name + ": " + std::string(text), messages::kind_unit_report, unit);
}

std::vector<std::string> Runtime::match_message_lines() {
    std::vector<std::string> lines;
    if (!match_)
        return lines;
    auto& game = match_->state().game;
    for (uint32_t index = game.chat_tail; index != game.chat_head;
         index = (index + 1U) % OA_CHAT_LINE_COUNT) {
        const auto* line = messages::message_line(game, index);
        if (line == nullptr)
            break;
        lines.emplace_back(line->text, strnlen(line->text, sizeof line->text));
    }
    return lines;
}

const oa::formats::fnt::Font& Runtime::message_font() {
    if (!message_font_)
        message_font_ = match_small_font_ ? *match_small_font_ : oa::formats::fnt::Font{};
    return *message_font_;
}

void Runtime::draw_match_message_log() {
    if (!match_)
        return;

    struct Paint {
        Runtime* runtime{};
        const oa::formats::fnt::Font* font{};
        std::array<uint8_t, 3> color{};
        oa::ui::display_layout::Point corner{};
        int scale{};
    };

    Paint paint{
        this,
        &message_font(),
        {255, 255, 255},
        hud_canvas(oa::ui::hud::kMessageLogLeft, oa::ui::hud::kMessageLogTop),
        hud_text_scale()
    };
    oa::ui::hud::MessageLogSink sink{};
    sink.user = &paint;
    sink.font_height = [](void* user) {
        return static_cast<int32_t>(
            oa::formats::fnt::line_height(*static_cast<Paint*>(user)->font)
        );
    };
    sink.set_color = [](void* user, uint8_t color) {
        auto& target = *static_cast<Paint*>(user);
        target.color = target.runtime->palette_rgb(color);
    };
    sink.text = [](void* user, const char* text, int32_t x, int32_t y) {
        auto& target = *static_cast<Paint*>(user);
        target.runtime->paint_text(
            *target.font,
            target.corner.x + (x - oa::ui::hud::kMessageLogLeft) * target.scale,
            target.corner.y + (y - oa::ui::hud::kMessageLogTop) * target.scale,
            text,
            target.color,
            target.scale
        );
    };
    oa::ui::hud::draw_message_log(match_->state(), sink);
}

CanvasRect Runtime::message_log_rect(std::size_t lines) {
    const auto corner = oa::ui::display_layout::source_to_canvas(
        match_layout_, oa::ui::hud::kMessageLogLeft, oa::ui::hud::kMessageLogTop
    );
    const int right = match_layout_.left + match_layout_.battlefield_width();
    const int bottom = match_layout_.top + match_layout_.battlefield_height();
    const auto height = static_cast<std::size_t>(oa::formats::fnt::line_height(message_font())) *
                        static_cast<std::size_t>(hud_text_scale()) * lines;
    return {
        corner.x,
        corner.y,
        right - corner.x,
        static_cast<int>(std::min(height, static_cast<std::size_t>(std::max(0, bottom - corner.y))))
    };
}

void Runtime::check_game_speed_messages() {
    if (screen_ != Screen::match || !match_)
        throw std::runtime_error("speed check needs a running match");
    bool running = true;
    const auto key = [&](SDL_Keycode code, SDL_Scancode scancode, int times) {
        for (int press = 0; press < times; ++press) {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = code;
            event.key.scancode = scancode;
            handle_sdl_event(event, running);
        }
    };
    constexpr uint32_t kFrameMs = 25;
    constexpr uint32_t kFramesPerSecond = 1000 / kFrameMs;
    uint32_t clock_ms = 1000;
    const auto ticks_in_one_second = [&] {
        match_timing_.previous_clock =
            oa::base::game_loop::scaled_clock(clock_ms, match_clock_scale());
        match_timing_.remainder = 0.0F;
        const auto before = match_timing_.tick;
        for (uint32_t frame = 0; frame < kFramesPerSecond; ++frame) {
            clock_ms += kFrameMs;
            advance_match_clock(clock_ms);
        }
        return static_cast<int32_t>(match_timing_.tick - before);
    };
    const auto expect_speed = [&](int32_t speed, const char* step) {
        if (match_timing_.requested_rate != speed || match_timing_.actual_rate != speed)
            throw std::runtime_error(
                std::string("speed check: ") + step + " left the rate unchanged"
            );
        char expected[oa::sim::speed::message_bytes];
        oa::sim::speed::format_message(expected, speed, message_hooks());
        const auto lines = match_message_lines();
        if (lines.empty() || lines.back() != expected)
            throw std::runtime_error(std::string("speed check: ") + step + " posted no speed line");
        const auto ticks = ticks_in_one_second();
        const auto wanted = speed * 3;
        if (std::abs(ticks - wanted) > 1)
            throw std::runtime_error(
                std::string("speed check: ") + step + " ran " + std::to_string(ticks) +
                " ticks in a second, not " + std::to_string(wanted)
            );
    };
    // From the preferences' speed to normal, then three steps up and six down.
    const int32_t base = match_timing_.requested_rate;
    constexpr int32_t normal = oa::sim::speed::normal;
    key(SDLK_MINUS, SDL_SCANCODE_MINUS, std::max(0, base - normal));
    key(SDLK_EQUALS, SDL_SCANCODE_EQUALS, std::max(0, normal - base));
    if (match_timing_.requested_rate != normal)
        throw std::runtime_error("speed check: the keys did not reach normal speed");
    const auto normal_ticks = ticks_in_one_second();
    if (std::abs(normal_ticks - normal * 3) > 1)
        throw std::runtime_error(
            "speed check: normal speed ran " + std::to_string(normal_ticks) + " ticks in a second"
        );
    constexpr int32_t faster = normal + 3;
    key(SDLK_EQUALS, SDL_SCANCODE_EQUALS, 3);
    expect_speed(faster, "'+'");
    constexpr int32_t slower = faster - 6;
    key(SDLK_MINUS, SDL_SCANCODE_MINUS, 6);
    expect_speed(slower, "'-'");
    key(SDLK_EQUALS, SDL_SCANCODE_EQUALS, base - slower);
    if (match_timing_.requested_rate != base)
        throw std::runtime_error("speed check: '+' did not return to the starting speed");

    uint16_t commander = 0;
    for (const auto& slot : match_->world().slots)
        if (commander == 0 && slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == match_local_player_)
            commander = slot.unit_index;
    if (commander == 0)
        throw std::runtime_error("speed check needs the local commander");
    offline_services_.command_sound(
        match_->world().slots[commander],
        static_cast<uint32_t>(oa::audio::game_audio::UnitAnnouncementCategory::under_attack)
    );
    const auto* definition = definition_for(commander);
    const std::string report =
        (definition != nullptr ? definition->display_name : std::string()) + ": Under Attack";
    bool reported = false;
    for (std::size_t pump = 0;
         pump < oa::audio::game_audio::AnnouncementQueue::capacity && !reported;
         ++pump) {
        present_unit_announcements();
        const auto lines = match_message_lines();
        reported = !lines.empty() && lines.back() == report;
    }
    if (!reported)
        throw std::runtime_error("speed check: \"" + report + "\" did not reach the message log");

    // The lines the log shows, found by the log's walk without painting,
    // must each change their rows of the battlefield in the composed frame
    // between a render with them and one after the log is cleared.
    std::size_t shown = 0;
    oa::ui::hud::MessageLogSink counter{};
    counter.user = &shown;
    counter.text = [](void* user, const char*, int32_t, int32_t) {
        ++*static_cast<std::size_t*>(user);
    };
    oa::ui::hud::draw_message_log(match_->state(), counter);
    const auto log = message_log_rect(shown);
    const auto log_pixels = [&] {
        renderer::Surface frame;
        render_match_surface();
        compose_match_frame(frame);
        return copy_rect(frame, log);
    };
    const auto with_log = log_pixels();
    oa::sim::messages::clear_messages(match_->state().game);
    const auto changed = changed_pixels(with_log, log_pixels());
    if (shown == 0 || changed < kTextMinPixels * shown)
        throw std::runtime_error(
            "speed check: the message log's " + std::to_string(shown) + " lines changed " +
            std::to_string(changed) + " pixels of the battlefield"
        );
    std::cout << "speed check: '+' and '-' ran " << normal * 3 << ", " << faster * 3 << " and "
              << slower * 3 << " ticks a second and posted their speed lines; \"" << report
              << "\" reached the log, whose " << shown << " lines changed " << changed
              << " pixels of the battlefield\n";
}

} // namespace oa::app
