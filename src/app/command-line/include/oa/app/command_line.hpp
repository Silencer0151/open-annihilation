// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Start-up switches of the 3.1c command line: "-x" or "/x" single-letter
// switches, the "-b" word options and a bare language argument. Letters the
// engine does not handle itself are offered to an optional SwitchHandler.
#pragma once

#include <cstddef>
#include <cstdint>

namespace oa::app::command_line {

// Room in each text field, terminator included; at most 63 characters are
// kept.
inline constexpr size_t kTextBytes = 64;
// Longest command line parsed, terminator included; the parse works on a
// private copy of this size.
inline constexpr size_t kMaximumLineBytes = 4096;

// Game.setup_options bit set when the "-b" token reads "lock".
inline constexpr uint8_t kGameLocked = 0x01;

// Values "-d" stores; start-up folds the inverted low bit into the display flags.
namespace display_option {
inline constexpr int32_t f_suffix = 2; // "-df" / "-dF"
inline constexpr int32_t other = 3;    // "-d" with any other suffix
} // namespace display_option

// Switch letters of the game's command line that the engine leaves to a
// SwitchHandler. One no handler takes stops the parse.
inline constexpr char kReservedSwitches[] = "cehnpty";

// What the switches set. Only the intro skip is reset by a parse; everything
// else keeps earlier values.
struct Switches {
    int32_t skip_intro{};                   // -c -n -y
    uint8_t game_options{};                 // folded into Game.setup_options
    int32_t display_option{};               // -d, a display_option value
    uint32_t forced_disc_drive{};           // -f: disc looked up on H:
    uint32_t l_switch_flag{1};              // -l clears it; it has no effect
    uint8_t playback_suppressed{};          // -s -w: the mixer plays nothing
    uint8_t system_sound{};                 // -w: named sounds use the system player
    char unavailable_switch{};              // the reserved letter, as typed, that stopped the parse
    char language[kTextBytes]{};            // last bare argument
    char registration[kMaximumLineBytes]{}; // text after -r
};

enum class Status : uint8_t {
    run,                  // start the game
    register_application, // -r: register the text in Switches::registration and quit
    line_too_long,        // longer than kMaximumLineBytes - 1; nothing parsed
    argument_too_long,    // a bare argument does not fit Switches::language
    // A reserved letter no handler took; Switches::unavailable_switch names it.
    unavailable_switch,
};

// The text a handler reads after its switch letter: what is attached to the
// letter (possibly empty) and, through next_token, the tokens after it. A
// token taken this way is not parsed as a switch; next_token returns null
// past the last one.
struct SwitchArguments {
    const char* attached{};
    const char* (*next_token)(void* tokens){};
    void* tokens{};
};

// Effects a handler asks the parse to apply for a switch it took.
inline constexpr uint32_t switch_effect_skip_intro = 1;

// Launch switches the engine leaves to an extension. take returns 0 when it
// does not take the letter; reset is called at the start of every parse, where
// skip_intro is reset.
struct SwitchHandler {
    void* context{};
    int (*take)(void* context, char letter, const SwitchArguments* arguments, uint32_t* effects){};
    void (*reset)(void* context){};
};

/// Parses a command line into `switches`, applying each recognised switch in order.
///
/// Tokens are split at blanks and tabs. A token not starting with '-' or '/'
/// is the language argument (the last one wins). Debug-heap switches such as
/// "-memfussy" are skipped whole. The switch letter is case-insensitive: -b,
/// -d, -f, -l, -r, -s and -w are handled here; any other letter is offered to
/// `handler` in lower case, and one it does not take is ignored unless it is
/// in kReservedSwitches. Before the first token skip_intro is reset to 0 and
/// the handler's reset runs; every other field keeps its earlier value.
///
/// @param line Command line text, NUL-terminated.
/// @param[in,out] switches Receives the switch values; a status other than run
///                         leaves the switches of the tokens already read applied.
/// @param handler Takes the letters the engine does not handle; null offers none.
/// @return run when the whole line was read; register_application at "-r", whose
///         text is the rest of the unsplit line; line_too_long (nothing parsed)
///         when the line is kMaximumLineBytes or longer; argument_too_long when a
///         bare argument does not fit Switches::language; unavailable_switch at
///         a reserved letter no handler took.
/// @quirk "-b" compares the whole token, switch character included, with "lock",
///        so no "-b" token ever sets kGameLocked.
Status parse(const char* line, Switches& switches, const SwitchHandler* handler = nullptr) noexcept;

/// Stores the forced disc drive flag that "-f" sets.
///
/// @param[out] switches Receives Switches::forced_disc_drive.
/// @param value Flag value; the parse passes 1.
void set_forced_disc_drive(Switches& switches, uint32_t value) noexcept;

/// Stores the flag word that "-l" clears; the flag has no effect.
///
/// @param[out] switches Receives Switches::l_switch_flag.
/// @param value Flag value; the parse passes 0.
void set_l_switch_flag(Switches& switches, uint32_t value) noexcept;

/// Returns the bare language argument of the last parse.
///
/// @param switches Parsed switches.
/// @return Switches::language, or null when no bare argument was given.
[[nodiscard]]
const char* launch_language(const Switches& switches) noexcept;

} // namespace oa::app::command_line
