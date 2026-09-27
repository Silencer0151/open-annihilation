// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/app/command_line.hpp"

#include <cstring>

namespace oa::app::command_line {
namespace {

// Developer switches, matched by prefix before the switch letter is read and
// skipped whole.
constexpr const char* kDebugHeapSwitches[] = {
    "-memfussy",
    "-memnofussy",
    "-memfrontalign",
    "-gonzo",
    "-memset",
    "-memnoset",
    "-fpufussy",
    "-fpunofussy",
    "-dprinton",
    "-dprintoff",
    "-dprintfile",
    "-memorystatus",
    "-performancestatus",
    "-disableimagehlp",
    "-enableimagehlp",
    "-disableimagehlplines",
    "-enableimagehlplines",
    "-debughelper",
    "-debughelperold",
    "-saveresources",
    "-assertmethod",
};

constexpr char kLockWord[] = "lock";

/// Tests for a token delimiter: blank or tab.
///
/// @param c Character.
/// @return Whether `c` separates tokens.
bool is_delimiter(char c) noexcept {
    return c == ' ' || c == '\t';
}

/// Tests for the C locale's space class: blank, tab, newline, vertical tab, form feed and return.
///
/// @param c Character.
/// @return Whether `c` is a space.
bool is_space(char c) noexcept {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

/// Lowers an ASCII capital letter; other characters are returned unchanged.
///
/// @param c Character.
/// @return The lower-case character.
char fold(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}

/// Compares two strings for equality, ignoring ASCII case.
///
/// @param a First string.
/// @param b Second string.
/// @return Whether the strings are equal.
bool equal_nocase(const char* a, const char* b) noexcept {
    for (; *a != '\0' && fold(*a) == fold(*b); ++a, ++b) {
    }
    return fold(*a) == fold(*b);
}

/// Tests whether a text starts with a prefix, ignoring ASCII case.
///
/// @param text Text to test.
/// @param prefix Prefix.
/// @return Whether `text` starts with `prefix`.
bool starts_with_nocase(const char* text, const char* prefix) noexcept {
    for (; *prefix != '\0'; ++text, ++prefix)
        if (fold(*text) != fold(*prefix))
            return false;
    return true;
}

/// Tests whether a token is one of the debug-heap switches, which are matched by prefix.
///
/// @param token Token, switch character included.
/// @return Whether the token starts with an entry of kDebugHeapSwitches.
bool debug_heap_switch(const char* token) noexcept {
    for (const char* prefix : kDebugHeapSwitches)
        if (starts_with_nocase(token, prefix))
            return true;
    return false;
}

/// Copies text into a text field, truncated to kTextBytes - 1 characters.
///
/// @param[out] destination Field receiving the text.
/// @param source Text to copy.
void copy_text(char (&destination)[kTextBytes], const char* source) noexcept {
    destination[0] = '\0';
    std::strncat(destination, source, kTextBytes - 1);
}

// strtok over the private copy, with blank and tab as the only delimiters.
struct Tokens {
    char* next{};

    /// Returns the next token, terminating it in place.
    ///
    /// @param start Text to start splitting, or null to continue after the previous token.
    /// @return The token, or null past the last one.
    char* take(char* start) noexcept {
        char* at = start != nullptr ? start : next;
        if (at == nullptr)
            return nullptr;
        while (*at != '\0' && is_delimiter(*at))
            ++at;
        if (*at == '\0') {
            next = nullptr;
            return nullptr;
        }
        char* token = at;
        while (*at != '\0' && !is_delimiter(*at))
            ++at;
        if (*at != '\0')
            *at++ = '\0';
        next = at;
        return token;
    }
};

/// Takes the next token for a switch handler (SwitchArguments::next_token).
///
/// @param tokens The parse's Tokens.
/// @return The token, or null past the last one.
const char* take_next_token(void* tokens) noexcept {
    return static_cast<Tokens*>(tokens)->take(nullptr);
}

/// Tests whether a lower-case switch letter is one of kReservedSwitches.
///
/// @param letter Switch letter.
/// @return Whether it is reserved; false for '\0'.
bool reserved(char letter) noexcept {
    return letter != '\0' && std::strchr(kReservedSwitches, letter) != nullptr;
}

} // namespace

Status parse(const char* line, Switches& switches, const SwitchHandler* handler) noexcept {
    const size_t length = std::strlen(line);
    if (length >= kMaximumLineBytes)
        return Status::line_too_long;
    char copy[kMaximumLineBytes];
    std::memcpy(copy, line, length + 1);
    switches.skip_intro = 0;
    if (handler != nullptr && handler->reset != nullptr)
        handler->reset(handler->context);
    Tokens tokens;
    for (char* token = tokens.take(copy); token != nullptr; token = tokens.take(nullptr)) {
        if (token[0] != '-' && token[0] != '/') {
            if (std::strlen(token) >= kTextBytes)
                return Status::argument_too_long;
            copy_text(switches.language, token);
            continue;
        }
        if (debug_heap_switch(token))
            continue;
        switch (fold(token[1])) {
        case 'b':
            // Compares the whole token, switch characters included; of the
            // words compared, only "lock" has an effect.
            if (equal_nocase(token, kLockWord))
                switches.game_options |= kGameLocked;
            break;
        case 'd':
            switches.display_option =
                fold(token[2]) == 'f' ? display_option::f_suffix : display_option::other;
            break;
        case 'f':
            set_forced_disc_drive(switches, 1);
            break;
        case 'l':
            set_l_switch_flag(switches, 0);
            break;
        case 'r': {
            // The rest of the unsplit line, so later tokens belong to it.
            const char* rest = line + (token - copy) + 2;
            while (is_space(*rest))
                ++rest;
            std::memcpy(switches.registration, rest, std::strlen(rest) + 1);
            return Status::register_application;
        }
        case 's':
            switches.playback_suppressed = 1;
            break;
        case 'w':
            // Sounds through the system player also silence the mixer.
            switches.system_sound = 1;
            switches.playback_suppressed = 1;
            break;
        default: {
            const char letter = fold(token[1]);
            if (letter == '\0')
                break;
            if (handler != nullptr && handler->take != nullptr) {
                const SwitchArguments arguments{token + 2, take_next_token, &tokens};
                uint32_t effects = 0;
                if (handler->take(handler->context, letter, &arguments, &effects) != 0) {
                    if ((effects & switch_effect_skip_intro) != 0)
                        switches.skip_intro = 1;
                    break;
                }
            }
            if (reserved(letter)) {
                switches.unavailable_switch = token[1];
                return Status::unavailable_switch;
            }
            break;
        }
        }
    }
    return Status::run;
}

void set_forced_disc_drive(Switches& switches, uint32_t value) noexcept {
    switches.forced_disc_drive = value;
}

void set_l_switch_flag(Switches& switches, uint32_t value) noexcept {
    switches.l_switch_flag = value;
}

const char* launch_language(const Switches& switches) noexcept {
    return switches.language[0] != '\0' ? switches.language : nullptr;
}

} // namespace oa::app::command_line
