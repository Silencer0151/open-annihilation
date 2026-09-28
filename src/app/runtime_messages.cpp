// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The in-game message log (Game.chat_lines) drawn over the battlefield, and
// the game speed keys that post to it.
#include "oa/app/runtime.hpp"
#include "oa/present/model/mesh_raster.hpp"
#include "oa/sim/speed.hpp"
#include "oa/ui/console/console.hpp"
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

void Runtime::post_match_message(
    std::string_view text, uint8_t kind, uint16_t value, uint8_t sender
) {
    if (!match_)
        return;
    char line[messages::text_bytes];
    std::snprintf(line, sizeof line, "%.*s", static_cast<int>(text.size()), text.data());
    messages::post_message(match_->state(), line, kind, value, sender, message_hooks());
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
    // The whole frame of the sender's colour logo is stretched over the
    // square, which covers x0..x1-1 and y0..y1-1.
    sink.logo =
        [](void* user, const oa::Player& player, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
            auto& target = *static_cast<Paint*>(user);
            const auto frame = target.runtime->player_logo_frame(player);
            if (!frame)
                return;
            const int32_t square[4] = {x0, y0, x1, y1};
            const auto blit = oa::ui::hud::player_logo_blit(square, frame->width, frame->height, 0);
            struct Logo {
                oa::Sprite texture{};
                oa::present::PolygonVertex quad[4]{};
                oa::present::model::TexturePoint uv[4]{};
            } logo{};
            logo.texture.width = frame->width;
            logo.texture.height = frame->height;
            logo.texture.key = frame->transparency_index;
            logo.texture.encoding = OA_SPRITE_RAW;
            logo.texture.data = const_cast<uint8_t*>(frame->pixels.data());
            for (int corner = 0; corner < 4; ++corner) {
                logo.quad[corner] = {blit.dest[2 * corner] - x0, blit.dest[2 * corner + 1] - y0};
                logo.uv[corner] = {blit.source[2 * corner], blit.source[2 * corner + 1]};
            }
            const int width = x1 - x0 + 1;
            target.runtime->overlay_patch(
                {target.corner.x + (x0 - oa::ui::hud::kMessageLogLeft) * target.scale,
                 target.corner.y + (y0 - oa::ui::hud::kMessageLogTop) * target.scale},
                width,
                y1 - y0 + 1,
                width,
                [](void* context, oa::Surface& surface) {
                    const auto& logo = *static_cast<const Logo*>(context);
                    oa::present::model::texture_quad(&surface, &logo.texture, logo.quad, logo.uv);
                },
                &logo
            );
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
    // Only a unit off screen says it is under attack; with no unit listed on
    // screen the commander's notice reaches the log.
    match_->state().game.hot_unit_count = 0;
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

    // A chat line another player sent, posted through the console's host with
    // its sender, is stored as such and starts with the logo of the sender's
    // colour over the square the log's walk sets aside for it; its text
    // starts past the logo. The same line from no player starts its text at
    // the log's left edge, inside that square.
    const auto* log_console = match_console();
    const auto* host = log_console != nullptr ? log_console->host : nullptr;
    if (host == nullptr || host->post_message == nullptr)
        throw std::runtime_error("speed check: the console's host posts no lines");
    auto& game = match_->state().game;
    const uint8_t sender = match_local_player_;
    const auto logo = player_logo_frame(game.players[sender]);
    if (!logo)
        throw std::runtime_error("speed check: the sender has no colour logo");

    struct LogWalk {
        int32_t line_height{};
        bool logo{};
        int32_t square[4]{}; // x0, y0, x1, y1: the logo covers x0..x1-1, y0..y1-1
        int32_t text_x{};
        int32_t text_y{};
    };

    const auto walk_log = [&] {
        LogWalk walk{static_cast<int32_t>(oa::formats::fnt::line_height(message_font()))};
        oa::ui::hud::MessageLogSink walker{};
        walker.user = &walk;
        walker.font_height = [](void* user) { return static_cast<LogWalk*>(user)->line_height; };
        walker.logo =
            [](void* user, const oa::Player&, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
                auto& walk = *static_cast<LogWalk*>(user);
                walk.logo = true;
                walk.square[0] = x0;
                walk.square[1] = y0;
                walk.square[2] = x1;
                walk.square[3] = y1;
            };
        walker.text = [](void* user, const char*, int32_t x, int32_t y) {
            auto& walk = *static_cast<LogWalk*>(user);
            walk.text_x = x;
            walk.text_y = y;
        };
        oa::ui::hud::draw_message_log(match_->state(), walker);
        return walk;
    };
    const auto newest_line = [&] {
        return messages::message_line(
            game, (game.chat_head + OA_CHAT_LINE_COUNT - 1U) % OA_CHAT_LINE_COUNT
        );
    };
    const auto frame_now = [&] {
        renderer::Surface frame;
        render_match_surface();
        compose_match_frame(frame);
        return frame;
    };
    // The canvas pixels of the log's screen pixel (x, y) are a scale x scale
    // block from the log's corner.
    const int scale = hud_text_scale();
    const auto log_corner = oa::ui::display_layout::source_to_canvas(
        match_layout_, oa::ui::hud::kMessageLogLeft, oa::ui::hud::kMessageLogTop
    );
    const auto rgb_at = [](const renderer::Surface& frame, int x, int y) {
        std::array<uint8_t, 3> rgb{};
        if (x < 0 || y < 0 || x >= static_cast<int>(frame.width) ||
            y >= static_cast<int>(frame.height))
            return rgb;
        const auto* pixel =
            frame.rgb.data() +
            (static_cast<std::size_t>(y) * frame.width + static_cast<std::size_t>(x)) * 3U;
        std::copy(pixel, pixel + 3, rgb.begin());
        return rgb;
    };
    // Visits the canvas pixels of the log's screen rectangle x0..x1-1, y0..y1-1.
    const auto each_pixel = [&](int32_t x0, int32_t y0, int32_t x1, int32_t y1, const auto& visit) {
        for (int y = log_corner.y + (y0 - oa::ui::hud::kMessageLogTop) * scale;
             y < log_corner.y + (y1 - oa::ui::hud::kMessageLogTop) * scale;
             ++y)
            for (int x = log_corner.x + (x0 - oa::ui::hud::kMessageLogLeft) * scale;
                 x < log_corner.x + (x1 - oa::ui::hud::kMessageLogLeft) * scale;
                 ++x)
                visit(x, y);
    };
    const auto changed_in = [&](const renderer::Surface& before,
                                const renderer::Surface& after,
                                int32_t x0,
                                int32_t y0,
                                int32_t x1,
                                int32_t y1) {
        std::size_t count = 0;
        each_pixel(x0, y0, x1, y1, [&](int x, int y) {
            count += rgb_at(before, x, y) != rgb_at(after, x, y) ? 1 : 0;
        });
        return count;
    };
    constexpr const char* kChatText = "<sender> hello";
    const auto battlefield_right =
        oa::ui::hud::kMessageLogLeft +
        (match_layout_.left + match_layout_.battlefield_width() - log_corner.x) / scale;

    const auto empty = frame_now();
    host->post_message(host->context, kChatText, messages::kind_player_chat, sender);
    const auto* chat = newest_line();
    if (chat == nullptr || chat->sender != sender ||
        (chat->kind & messages::kind_mask) != messages::kind_player_chat)
        throw std::runtime_error(
            "speed check: the console's host did not store the line as the sender's chat"
        );
    const auto marked = walk_log();
    if (!marked.logo || marked.text_x <= marked.square[2])
        throw std::runtime_error("speed check: the log set no logo before the sender's line");
    const auto marked_frame = frame_now();
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    write_ppm(report_directory / "native-message-log-sender.ppm", marked_frame);
    // The square shows the whole frame of the sender's colour logo, from
    // its corner (0, 0) to (width, height), stretched over it.
    const auto& square = marked.square;
    const int32_t size = square[2] - square[0];
    if (size <= 0 || square[3] - square[1] != size)
        throw std::runtime_error("speed check: the log's logo square is not square");
    auto stretched = oa::present::create_surface(size + 1, size + 1);
    oa::Sprite texture{};
    texture.width = logo->width;
    texture.height = logo->height;
    texture.key = logo->transparency_index;
    texture.encoding = OA_SPRITE_RAW;
    texture.data = const_cast<uint8_t*>(logo->pixels.data());
    const oa::present::PolygonVertex quad[4] = {{0, 0}, {size, 0}, {size, size}, {0, size}};
    const int32_t frame_width = logo->width;
    const int32_t frame_height = logo->height;
    const oa::present::model::TexturePoint whole_frame[4] = {
        {0, 0}, {frame_width, 0}, {frame_width, frame_height}, {0, frame_height}
    };
    oa::present::model::texture_quad(&stretched.surface, &texture, quad, whole_frame);
    std::size_t unlike = 0;
    each_pixel(square[0], square[1], square[2], square[3], [&](int x, int y) {
        const auto column = (x - log_corner.x) / scale - (square[0] - oa::ui::hud::kMessageLogLeft);
        const auto row = (y - log_corner.y) / scale - (square[1] - oa::ui::hud::kMessageLogTop);
        const auto index = stretched.pixels[static_cast<std::size_t>(row * (size + 1) + column)];
        unlike += rgb_at(marked_frame, x, y) != palette_rgb(index) ? 1 : 0;
    });
    if (unlike != 0)
        throw std::runtime_error(
            "speed check: the sender's line does not start with the logo of its colour (" +
            std::to_string(unlike) + " of " + std::to_string(size * size * scale * scale) +
            " pixels of its square differ)"
        );
    const auto text_bottom = marked.text_y + marked.line_height;
    const auto gap_changed =
        changed_in(empty, marked_frame, square[2], marked.text_y, marked.text_x, text_bottom);
    const auto text_changed = changed_in(
        empty, marked_frame, marked.text_x, marked.text_y, battlefield_right, text_bottom
    );
    if (gap_changed != 0 || text_changed < kTextMinPixels)
        throw std::runtime_error(
            "speed check: the sender's line does not start its text past the logo (" +
            std::to_string(gap_changed) + " pixels changed between them, " +
            std::to_string(text_changed) + " past them)"
        );

    messages::clear_messages(game);
    host->post_message(
        host->context, kChatText, oa::ui::hud::kMessageKindChat, oa::ui::console::kMessageNoSender
    );
    const auto* echo = newest_line();
    if (echo == nullptr || echo->sender != messages::sender_none)
        throw std::runtime_error("speed check: a line from no player was stored with a sender");
    const auto plain = walk_log();
    if (plain.logo || plain.text_x != oa::ui::hud::kMessageLogLeft)
        throw std::runtime_error(
            "speed check: the line from no player does not start at the log's edge"
        );
    const auto plain_changed =
        changed_in(empty, frame_now(), square[0], square[1], square[2], square[3]);
    if (plain_changed == 0)
        throw std::runtime_error(
            "speed check: the line from no player left the logo's square as it was"
        );
    messages::clear_messages(game);

    std::cout << "speed check: '+' and '-' ran " << normal * 3 << ", " << faster * 3 << " and "
              << slower * 3 << " ticks a second and posted their speed lines; \"" << report
              << "\" reached the log, whose " << shown << " lines changed " << changed
              << " pixels of the battlefield; the sender's line starts with its " << size << "x"
              << size << " colour logo, its text at x " << marked.text_x << "\n";
}

} // namespace oa::app
