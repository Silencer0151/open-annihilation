// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/speed.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace oa::sim::speed {

void format_message(
    char (&out)[message_bytes], int32_t speed, const messages::Hooks& hooks
) noexcept {
    const int32_t offset = speed - normal;
    if (offset == 0) {
        std::snprintf(out, sizeof out, "%s", messages::translate(hooks, text_normal));
        return;
    }
    std::snprintf(
        out,
        sizeof out,
        "%s  %c%d\n",
        messages::translate(hooks, text_prefix),
        offset > 0 ? '+' : ' ',
        offset
    );
}

namespace {

/// Skips spaces.
///
/// @param text a position in a line
/// @return the first character that is not a space
const char* skip_spaces(const char* text) noexcept {
    while (*text == ' ')
        ++text;
    return text;
}

/// Reads one word as a whole number that may carry a '-'.
///
/// @param[in,out] text the position of the word; moves past it
/// @return the number, or 0 when the word is not one
int32_t read_number(const char*& text) noexcept {
    text = skip_spaces(text);
    const char* word = text;
    while (*text != '\0' && *text != ' ')
        ++text;
    const char* at = word;
    const bool negative = at < text && *at == '-';
    if (negative)
        ++at;
    if (at == text)
        return 0;
    int32_t value = 0;
    for (; at < text; ++at) {
        if (*at < '0' || *at > '9')
            return 0;
        if (value < 100000)
            value = value * 10 + (*at - '0');
    }
    return negative ? -value : value;
}

/// Tells whether a line starts with a command word, matched without case,
/// that ends the line or is followed by a space.
///
/// @param text the line after the '.'
/// @param word the command word, in lower case
/// @return true when it does
bool starts_with_word(const char* text, const char* word) noexcept {
    const std::size_t length = std::strlen(word);
    for (std::size_t at = 0; at < length; ++at)
        if (std::tolower(static_cast<unsigned char>(text[at])) != word[at])
            return false;
    return text[length] == '\0' || text[length] == ' ';
}

} // namespace

LockLine read_lock_line(const char* text) noexcept {
    LockLine out{};
    if (text == nullptr)
        return out;
    text = skip_spaces(text);
    if (*text != '.')
        return out;
    ++text;
    if (starts_with_word(text, "syncoff")) {
        out.request = LockRequest::unlock;
        return out;
    }
    if (!starts_with_word(text, "syncon"))
        return out;
    text += std::strlen("syncon");
    out.request = LockRequest::lock;
    out.low = read_number(text);
    out.high = read_number(text);
    return out;
}

uint16_t set_speed(World& world, int32_t speed, const messages::Hooks& hooks, Range range) {
    Game& game = world.game;
    if (speed > range.fastest)
        speed = range.fastest;
    if (speed < range.slowest)
        speed = range.slowest;
    if (speed != game.requested_speed) {
        char line[message_bytes];
        format_message(line, speed, hooks);
        messages::post_message(world, line, messages::kind_status, 0, messages::sender_none, hooks);
    }
    game.requested_speed = static_cast<uint16_t>(speed);
    game.current_speed = static_cast<uint16_t>(speed);
    return game.requested_speed;
}

uint16_t raise_speed(World& world, const messages::Hooks& hooks, Range range) {
    if (world.game.requested_speed < range.fastest)
        set_speed(world, world.game.requested_speed + 1, hooks, range);
    return world.game.requested_speed;
}

uint16_t lower_speed(World& world, const messages::Hooks& hooks, Range range) {
    if (world.game.requested_speed > range.slowest)
        set_speed(world, world.game.requested_speed - 1, hooks, range);
    return world.game.requested_speed;
}

} // namespace oa::sim::speed
