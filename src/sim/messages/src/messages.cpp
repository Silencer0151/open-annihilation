// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/messages.hpp"

#include <cstdio>
#include <cstring>

namespace oa::sim::messages {
namespace {

inline constexpr size_t player_chat_name_bytes = sizeof(Player::second_name);
inline constexpr size_t formatted_bytes = 200;
inline constexpr uint8_t kind_mask = 0x0f;

// Bounded copy of Player.second_name, the name chat lines carry.
void chat_name(const Player& player, char out[player_chat_name_bytes + 1]) {
    std::memcpy(out, player.second_name, player_chat_name_bytes);
    out[player_chat_name_bytes] = '\0';
}

} // namespace

int32_t line_capacity(const Game& game) noexcept {
    return game.text_lines;
}

int32_t text_scroll(const Game& game) noexcept {
    return game.text_scroll;
}

void set_log_options(
    Game& game, int32_t lines, int32_t scroll, int32_t filter, uint32_t screen_chat
) noexcept {
    game.text_lines = lines;
    game.text_scroll = scroll;
    game.message_filter = filter;
    game.screen_chat = screen_chat;
}

MessageLine* message_line(Game& game, uint32_t index) noexcept {
    return index < OA_CHAT_LINE_COUNT ? reinterpret_cast<MessageLine*>(game.chat_lines[index])
                                      : nullptr;
}

const char* translate(const Hooks& hooks, const char* text) noexcept {
    const char* translated =
        hooks.translate != nullptr ? hooks.translate(hooks.context, text) : nullptr;
    return translated != nullptr ? translated : text;
}

void clear_messages(Game& game) noexcept {
    game.chat_head = 0;
    game.chat_tail = 0;
}

bool expire_oldest_message(Game& game) noexcept {
    if (game.chat_head == game.chat_tail)
        return false;
    const MessageLine* oldest = message_line(game, game.chat_tail);
    if (oldest == nullptr)
        return false;
    const uint32_t expires =
        oldest->tick + static_cast<uint32_t>(text_scroll(game) + 1) * ticks_per_second;
    if (expires >= game.tick)
        return false;
    ++game.chat_tail;
    if (game.chat_tail == OA_CHAT_LINE_COUNT)
        game.chat_tail = 0;
    return true;
}

bool track_next_reported_unit(World& world, const Hooks& hooks) {
    Game& game = world.game;
    if (game.chat_head >= OA_CHAT_LINE_COUNT)
        return false;
    for (uint32_t index = game.chat_tail; index != game.chat_head;) {
        MessageLine* line = message_line(game, index);
        if (line == nullptr)
            return false;
        const Unit* unit = line->value != 0 && (line->kind & line_flag_visited) == 0
                               ? world_unit_at(&world, line->value)
                               : nullptr;
        if (unit != nullptr && (unit->flags & OA_UNIT_FLAG_LIVE) != 0) {
            line->kind = static_cast<uint8_t>(line->kind | line_flag_visited | line_flag_highlight);
            if (hooks.center_camera != nullptr)
                hooks.center_camera(
                    hooks.context,
                    static_cast<int16_t>(static_cast<uint32_t>(unit->position.x) >> 16),
                    static_cast<int16_t>(static_cast<uint32_t>(unit->position.z) >> 16),
                    reported_unit_camera_glide
                );
            return true;
        }
        if (++index == OA_CHAT_LINE_COUNT)
            index = 0;
    }
    return false;
}

void cycle_reported_units(World& world, const Hooks& hooks) {
    for (uint32_t index = 0; index < OA_CHAT_LINE_COUNT; ++index)
        if (MessageLine* line = message_line(world.game, index))
            line->kind = static_cast<uint8_t>(line->kind & ~line_flag_highlight);
    if (track_next_reported_unit(world, hooks))
        return;
    for (uint32_t index = 0; index < OA_CHAT_LINE_COUNT; ++index)
        if (MessageLine* line = message_line(world.game, index))
            line->kind = static_cast<uint8_t>(line->kind & ~line_flag_visited);
    (void)track_next_reported_unit(world, hooks);
}

void post_message(
    World& world, const char* text, uint8_t kind, uint16_t value, uint8_t sender, const Hooks& hooks
) {
    Game& game = world.game;
    if (text[0] == '\0')
        return;
    const int32_t capacity = line_capacity(game);
    if (capacity == 0)
        return;
    if ((static_cast<int32_t>(game.chat_head) + 1) % capacity ==
        static_cast<int32_t>(game.chat_tail)) {
        ++game.chat_tail;
        if (game.chat_tail == OA_CHAT_LINE_COUNT)
            game.chat_tail = 0;
    }
    MessageLine* line = message_line(game, game.chat_head);
    if (line == nullptr)
        return;
    std::strncpy(line->text, text, text_bytes);
    line->text[text_bytes - 1] = '\0';
    line->tick = game.tick;
    line->kind = static_cast<uint8_t>((line->kind & ~kind_mask) | (kind & kind_mask));
    line->value = value;
    line->sender = sender;
    ++game.chat_head;
    if (game.chat_head == OA_CHAT_LINE_COUNT)
        game.chat_head = 0;
    if (sender != sender_none && hooks.play_sound != nullptr)
        hooks.play_sound(hooks.context, sound_message_arrived);
    if (hooks.refresh_panel != nullptr)
        hooks.refresh_panel(hooks.context);
}

void post_notice(World& world, const char* text, const Hooks& hooks) {
    while (*text == ' ')
        ++text;
    auto remaining = static_cast<int32_t>(std::strlen(text));
    char chunk[text_bytes];
    for (;;) {
        if (remaining < static_cast<int32_t>(text_bytes)) {
            if (remaining > 0)
                post_message(world, text, kind_notice, 0, sender_none, hooks);
            return;
        }
        int32_t length = wrap_width;
        if (*text != ' ') {
            for (int32_t step = 0; step < wrap_lookback; ++step) {
                if (text[wrap_width - 1 - step] == ' ')
                    break;
                --length;
            }
        }
        std::memcpy(chunk, text, static_cast<size_t>(length));
        chunk[length] = '\0';
        remaining -= length;
        post_message(world, chunk, kind_notice, 0, sender_none, hooks);
        for (text += length; remaining > 0 && *text == ' '; ++text)
            --remaining;
    }
}

void post_elimination(World& world, const Player& player, const Hooks& hooks) {
    const PlayerSetupInfo* info = world_player_info(&world, &player);
    const char* side = info != nullptr && info->side == 0 ? side_name_arm : side_name_core;
    const uint32_t roll = hooks.random != nullptr ? hooks.random(hooks.context) : 0;
    const char* taunt = translate(hooks, elimination_messages[roll % elimination_message_count]);
    char line[formatted_bytes];
    std::snprintf(line, sizeof line, "%s %s", side, taunt);
    post_message(world, line, kind_elimination, 0, player.index, hooks);
}

void post_chat(
    World& world,
    const Player& speaker,
    const char* text,
    uint8_t kind,
    const char* target,
    const Hooks& hooks
) {
    char name[player_chat_name_bytes + 1];
    chat_name(speaker, name);
    char line[formatted_bytes];
    std::snprintf(
        line,
        sizeof line,
        "<%s%s%s> %s",
        name,
        target != nullptr ? "->" : "",
        target != nullptr ? target : "",
        text
    );
    if (world.game.chat_mode != chat_mode_local_only && hooks.share_chat != nullptr)
        hooks.share_chat(hooks.context, line);
    const uint8_t mode = world.game.chat_mode;
    if (hooks.game_kind != nullptr && hooks.game_kind(hooks.context) == game_kind_multiplayer &&
        mode != OA_CHAT_MODE_CHOSEN && mode != OA_CHAT_MODE_ALLIES &&
        mode != chat_mode_local_only && hooks.record_chat != nullptr)
        hooks.record_chat(hooks.context, line);
    post_message(world, line, kind, 0, sender_none, hooks);
}

} // namespace oa::sim::messages
