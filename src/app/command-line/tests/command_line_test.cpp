// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/app/command_line.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace cl = oa::app::command_line;

namespace {
int failures = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "command-line test failed: %s\n", message);
        ++failures;
    }
}

cl::Switches parsed(const char* line, cl::Status expected = cl::Status::run) {
    cl::Switches switches;
    require(cl::parse(line, switches) == expected, line);
    return switches;
}

void defaults_and_resets() {
    const auto none = parsed("");
    require(none.skip_intro == 0 && none.l_switch_flag == 1, "untouched flags");
    cl::Switches kept;
    kept.skip_intro = 1;
    kept.playback_suppressed = 1;
    require(cl::parse("english", kept) == cl::Status::run, "bare language");
    require(kept.skip_intro == 0, "the intro skip is reset before the scan");
    require(kept.playback_suppressed == 1, "other fields keep their values");
    require(std::strcmp(kept.language, "english") == 0, "language stored");
    // launch_language hands out the language field only when it holds text.
    require(cl::launch_language(kept) == kept.language, "language handed out");
    require(cl::launch_language(cl::Switches{}) == nullptr, "no language is null");
}

// Without a handler every reserved switch stops the parse and is named; the
// intro skip keeps its reset value and earlier switches still apply.
void reserved_switches_unavailable() {
    for (const char* line :
         {"-n1:10.0.0.5",
          "-n2",
          "-n",
          "-hHost",
          "-h Name",
          "-c conline.cfg",
          "-c",
          "-y",
          "-Y",
          "-t120",
          "-T 300",
          "/e100",
          "-p12"}) {
        cl::Switches switches;
        require(cl::parse(line, switches) == cl::Status::unavailable_switch, line);
        require(switches.unavailable_switch == line[1], "the unavailable switch is named as typed");
        require(switches.skip_intro == 0, "an unavailable switch sets nothing");
    }
    cl::Switches switches;
    require(cl::parse("-s -y -w", switches) == cl::Status::unavailable_switch, "-s -y -w");
    require(
        switches.playback_suppressed == 1 && switches.system_sound == 0,
        "switches before the unavailable one applied, later ones not"
    );
    require(parsed("-x -z -a").skip_intro == 0, "unreserved unknown letters are ignored");
}

struct Offered {
    std::string letters;
    std::string attached;
    std::string argument;
    int resets = 0;
    int calls = 0;
};

// Takes x (reading one more token), q (asking for the intro skip), and y
// only when it is attached to "es"; declines everything else.
int take_probe(
    void* context, char letter, const cl::SwitchArguments* arguments, uint32_t* effects
) {
    auto& offered = *static_cast<Offered*>(context);
    ++offered.calls;
    offered.letters += letter;
    offered.attached += std::string(arguments->attached) + "|";
    switch (letter) {
    case 'x': {
        const char* next = arguments->next_token(arguments->tokens);
        offered.argument = next != nullptr ? next : "(none)";
        return 1;
    }
    case 'q':
        *effects |= cl::switch_effect_skip_intro;
        return 1;
    case 'y':
        return std::strcmp(arguments->attached, "es") == 0 ? 1 : 0;
    default:
        return 0;
    }
}

void reset_probe(void* context) {
    ++static_cast<Offered*>(context)->resets;
}

void switch_handler() {
    Offered offered;
    const cl::SwitchHandler handler{&offered, take_probe, reset_probe};
    cl::Switches switches;
    require(
        cl::parse("-s -X value -Qz -yes -dF -z", switches, &handler) == cl::Status::run,
        "taken and unreserved letters run"
    );
    require(offered.resets == 1, "one reset per parse");
    require(offered.letters == "xqyz", "the engine's own letters are not offered");
    require(offered.attached == "|z|es||", "the attached text follows the letter");
    require(
        offered.argument == "value" && switches.language[0] == '\0',
        "a token the handler takes is not a bare argument"
    );
    require(switches.skip_intro == 1, "the intro skip effect applies");
    require(
        switches.playback_suppressed == 1 &&
            switches.display_option == cl::display_option::f_suffix,
        "the engine still applies its own switches"
    );

    offered = Offered{};
    require(cl::parse("-x", switches, &handler) == cl::Status::run, "-x at the end");
    require(
        offered.argument == "(none)" && switches.skip_intro == 0,
        "no token after the last; the intro skip is reset"
    );

    offered = Offered{};
    require(
        cl::parse("-s -yno -w", switches, &handler) == cl::Status::unavailable_switch,
        "a reserved letter the handler declines"
    );
    require(switches.unavailable_switch == 'y' && offered.letters == "y", "declined -y named");

    offered = Offered{};
    require(
        cl::parse("english -saveresources - -r rest -x", switches, &handler) ==
            cl::Status::register_application,
        "-r with a handler"
    );
    require(
        offered.calls == 0,
        "bare arguments, debug words, a lone dash and text after -r are not offered"
    );

    offered = Offered{};
    const std::string too_long(cl::kMaximumLineBytes, 'a');
    require(
        cl::parse(too_long.c_str(), switches, &handler) == cl::Status::line_too_long, "long line"
    );
    require(offered.resets == 0, "a line too long is not parsed");

    const cl::SwitchHandler no_take{&offered, nullptr, reset_probe};
    offered = Offered{};
    require(
        cl::parse("-t60", switches, &no_take) == cl::Status::unavailable_switch,
        "a handler without take takes nothing"
    );
    require(offered.resets == 1, "its reset still runs");
}

void flags() {
    const auto set = parsed("-s -w -f -l -dF");
    require(set.playback_suppressed == 1 && set.system_sound == 1, "sound switches");
    const auto quiet = parsed("-S");
    require(quiet.playback_suppressed == 1 && quiet.system_sound == 0, "-s silences the mixer");
    const auto windows = parsed("-W");
    require(
        windows.playback_suppressed == 1 && windows.system_sound == 1, "-w sets both sound flags"
    );
    require(set.forced_disc_drive == 1 && set.l_switch_flag == 0, "disc and unread flag");
    require(set.display_option == cl::display_option::f_suffix, "-dF");
    require(parsed("-dx").display_option == cl::display_option::other, "-d with another suffix");
}

void words_and_debug_switches() {
    require(parsed("-b lock").game_options == 0, "the -b token itself is compared, not lock");
    require(parsed("-block").game_options == 0, "-block is not lock");
    const auto debug = parsed("-saveresources -dprinton -fpufussy -performancestatus");
    require(
        debug.playback_suppressed == 0 && debug.display_option == 0 && debug.forced_disc_drive == 0,
        "debug heap switches are skipped whole"
    );
    require(
        parsed("/saveresources").playback_suppressed == 1,
        "the debug prefixes are only the dash forms"
    );
}

void registration_and_limits() {
    cl::Switches switches;
    require(
        cl::parse("-s -r   C:\\TA\\Game.exe -t60", switches) == cl::Status::register_application,
        "-r stops the scan"
    );
    require(
        std::strcmp(switches.registration, "C:\\TA\\Game.exe -t60") == 0,
        "registration takes the rest of the line"
    );
    require(
        switches.playback_suppressed == 1 && switches.unavailable_switch == '\0',
        "earlier switches applied, later ones not"
    );
    std::string too_long(cl::kMaximumLineBytes, 'a');
    parsed(too_long.c_str(), cl::Status::line_too_long);
    std::string long_language(cl::kTextBytes, 'l');
    parsed(long_language.c_str(), cl::Status::argument_too_long);
}
} // namespace

int main() {
    defaults_and_resets();
    reserved_switches_unavailable();
    switch_handler();
    flags();
    words_and_debug_switches();
    registration_and_limits();
    if (failures != 0)
        return 1;
    std::puts("command line passed");
    return 0;
}
