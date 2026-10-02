// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/app/netgame/launch_switches.hpp"
#include "oa/base/text.hpp"

#include <cstring>

namespace oa::app::netgame::launch {
namespace {

namespace cl = oa::app::command_line;

/// Tests for the C locale's space class: blank, tab, newline, vertical tab, form feed and return.
///
/// @param c Character.
/// @return Whether `c` is a space.
bool is_space(char c) noexcept {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

/// Reads a decimal integer: leading space, an optional sign and decimal digits.
///
/// @param text Text to read; reading stops at the first other character.
/// @return The value, wrapping at 32 bits; 0 without digits.
int32_t decode_int(const char* text) noexcept {
    while (is_space(*text))
        ++text;
    const char sign = *text;
    if (sign == '-' || sign == '+')
        ++text;
    uint32_t value = 0;
    for (; *text >= '0' && *text <= '9'; ++text)
        value = value * 10U + static_cast<uint32_t>(*text - '0');
    return static_cast<int32_t>(sign == '-' ? 0U - value : value);
}

/// Copies text into a text field, truncated to kTextBytes - 1 characters.
///
/// @param[out] destination Field receiving the text.
/// @param source Text to copy.
void copy_text(char (&destination)[kTextBytes], const char* source) noexcept {
    oa::base::text::copy_terminated(destination, source);
}

/// Appends text to a text field, stopping at the end of the field.
///
/// At most 63 characters are appended, and the text stops at the end of the
/// field, however many appends came before.
///
/// @param[in,out] destination Field the text is appended to.
/// @param source Text to append.
void append_text(char (&destination)[kTextBytes], const char* source) noexcept {
    oa::base::text::append_terminated(destination, source);
}

/// Returns a switch's argument: the text after the two switch characters, or the next token when none follows.
///
/// @param arguments The switch's arguments.
/// @return The argument, or null when no token follows.
const char* argument(const cl::SwitchArguments* arguments) noexcept {
    return *arguments->attached != '\0' ? arguments->attached
                                        : arguments->next_token(arguments->tokens);
}

/// Parses one network switch (SwitchHandler::take); see launch_switch_handler.
///
/// @param context The LaunchSwitches.
/// @param letter Lower-case switch letter.
/// @param arguments Text after the letter and the following tokens.
/// @param[out] effects Gains switch_effect_skip_intro for -n.
/// @return 1 when the letter is one of -e -h -n -p -t, else 0.
int take_switch(
    void* context, char letter, const cl::SwitchArguments* arguments, uint32_t* effects
) noexcept {
    LaunchSwitches& switches = *static_cast<LaunchSwitches*>(context);
    switch (letter) {
    case 'e': {
        const char* value = argument(arguments);
        if (value != nullptr && *value != '\0') {
            int32_t percent = *value == '-' ? 0 : decode_int(value);
            if (percent < 0 || percent > kMaximumSendErrorPercent)
                percent = 0;
            switches.send_error_percent = percent;
        }
        return 1;
    }
    case 'h': {
        const char* name = arguments->attached;
        if (*name == '\0') {
            name = arguments->next_token(arguments->tokens);
            if (name == nullptr || *name == '\0' || *name == '-')
                return 1;
        }
        set_host_game(switches, 1, name);
        return 1;
    }
    case 'n': {
        const char* value = argument(arguments);
        if (value != nullptr && *value != '\0' && *value != '-') {
            const char* colon = std::strchr(value, ':');
            const int32_t type = decode_int(value);
            if (type == kAddressedConnectionType && colon != nullptr)
                set_connection_address(switches, colon + 1);
            set_connection_type(switches, type);
        }
        *effects |= cl::switch_effect_skip_intro;
        return 1;
    }
    case 'p': {
        const char* value = argument(arguments);
        if (value != nullptr && *value != '\0') {
            switches.send_pacing = *value == '-' ? kPacingFromDash : decode_int(value);
            switches.send_pacing_set = 1;
        }
        return 1;
    }
    case 't': {
        const char* value = argument(arguments);
        if (value != nullptr && *value != '\0') {
            int32_t seconds = *value == '-' ? 0 : decode_int(value);
            if (seconds < kDefaultNetTimeoutSeconds || seconds > kMaximumNetTimeoutSeconds)
                seconds = kDefaultNetTimeoutSeconds;
            switches.net_timeout_seconds = seconds;
        }
        return 1;
    }
    default:
        return 0;
    }
}

/// Resets the net timeout and send-error percentage before a parse (SwitchHandler::reset).
///
/// @param context The LaunchSwitches.
void reset_switches(void* context) noexcept {
    LaunchSwitches& switches = *static_cast<LaunchSwitches*>(context);
    switches.net_timeout_seconds = kDefaultNetTimeoutSeconds;
    switches.send_error_percent = 0;
}

} // namespace

cl::SwitchHandler launch_switch_handler(LaunchSwitches* switches) noexcept {
    return cl::SwitchHandler{switches, take_switch, reset_switches};
}

void set_host_game(LaunchSwitches& switches, uint32_t hosting, const char* name) noexcept {
    switches.block.hosting = hosting != 0 ? 1U : 0U;
    if (name != nullptr) {
        copy_text(switches.block.session_name, name);
        append_text(switches.host_game_name_copy, name);
    }
}

void set_connection_address(LaunchSwitches& switches, const char* address) noexcept {
    copy_text(switches.block.address, address);
    append_text(switches.connection_address_copy, address);
}

void set_connection_type(LaunchSwitches& switches, int32_t type) noexcept {
    if (type >= kFirstConnectionType && type <= kLastConnectionType) {
        switches.block.connection_type = type;
        switches.connection_type_copy = type;
    }
}

void enable_netsetup(LaunchSwitches& switches) noexcept {
    switches.netsetup = 1;
}

} // namespace oa::app::netgame::launch
