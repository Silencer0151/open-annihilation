// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// oa-game command-line parsing of the trace stream, seed, drawing threads,
// game directory, data folder, window size, frame rate, hardware
// acceleration, video capture and showcase options (arm-first-mission and
// skirmish-battle), and of the options and switches an extension takes.
#include "oa/app/app.hpp"
#include "oa/app/extension.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/app/command_line.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

const oa::app::Extension kNoExtension{};

oa::app::Options parse(
    const std::vector<const char*>& arguments, const oa::app::Extension& extension = kNoExtension
) {
    std::vector<std::string> storage{"open-annihilation"};
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& argument : storage)
        argv.push_back(argument.data());
    return oa::app::parse_options(static_cast<int>(argv.size()), argv.data(), extension);
}

// The message parse_options rejects the arguments with, or "" when it takes them.
std::string rejection(
    const std::vector<const char*>& arguments, const oa::app::Extension& extension = kNoExtension
) {
    try {
        (void)parse(arguments, extension);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return {};
}

// A test extension: "--extra VALUE" (headless and unattended), "--quiet-extra"
// (no effects) that "--extra" must not follow, and the game's -y switch.
struct TestExtension {
    std::string extra;
    bool quiet = false;
    int switches_taken = 0;
    oa::app::command_line::SwitchHandler switches{};
};

oa::app::Extension test_extension(TestExtension& state) {
    oa::app::Extension table{};
    table.context = &state;
    table.take_option = [](void* context,
                           const char* name,
                           const oa::app::OptionValues& values,
                           uint32_t& effects) {
        auto& self = *static_cast<TestExtension*>(context);
        if (std::string_view(name) == "--extra") {
            self.extra = values.next(values.arguments);
            effects |= oa::app::option_effect::headless_check | oa::app::option_effect::unattended;
            return true;
        }
        if (std::string_view(name) == "--quiet-extra") {
            self.quiet = true;
            effects |= oa::app::option_effect::skip_intro;
            return true;
        }
        return false;
    };
    table.check_options = [](void* context) {
        const auto& self = *static_cast<TestExtension*>(context);
        if (self.quiet && !self.extra.empty())
            throw std::runtime_error("--extra and --quiet-extra are used apart");
    };
    state.switches.context = &state;
    state.switches.take = [](void* context,
                             char letter,
                             const oa::app::command_line::SwitchArguments*,
                             uint32_t* effects) {
        if (letter != 'y')
            return 0;
        ++static_cast<TestExtension*>(context)->switches_taken;
        *effects |= oa::app::command_line::switch_effect_skip_intro;
        return 1;
    };
    table.switch_handler = [](void* context) -> const oa::app::command_line::SwitchHandler* {
        return &static_cast<TestExtension*>(context)->switches;
    };
    table.text = [](void*, oa::app::ExtensionText which) -> const char* {
        return which == oa::app::ExtensionText::register_switch ? "-r is the extension's" : nullptr;
    };
    return table;
}

// --check-renderer-ladder and the failure --render-fault narrows it to.
void renderer_ladder_options() {
    const auto ladder = parse({"--check-renderer-ladder"});
    expect(
        ladder.check_renderer_ladder && ladder.fixed_clock && ladder.unattended &&
            !ladder.render_fault,
        "--check-renderer-ladder is a fixed-clock, unattended run of every case"
    );
    expect(!parse({}).check_renderer_ladder, "no renderer ladder check unasked");
    const std::pair<const char*, oa::app::RenderFaultPoint> points[] = {
        {"create", oa::app::RenderFaultPoint::create},
        {"present", oa::app::RenderFaultPoint::present},
        {"reset", oa::app::RenderFaultPoint::reset},
        {"lost", oa::app::RenderFaultPoint::lost},
        {"stall", oa::app::RenderFaultPoint::stall},
        {"float", oa::app::RenderFaultPoint::float_state},
        {"memory", oa::app::RenderFaultPoint::memory},
        {"card", oa::app::RenderFaultPoint::card},
        {"full-memory", oa::app::RenderFaultPoint::full_memory},
    };
    for (const auto& [name, point] : points) {
        const auto faulted = parse({"--check-renderer-ladder", "--render-fault", name});
        expect(
            faulted.render_fault && faulted.render_fault->point == point &&
                !faulted.render_fault->frame,
            name
        );
        if (point == oa::app::RenderFaultPoint::create)
            continue;
        const std::string at_frame = std::string(name) + "@120";
        const auto framed = parse({"--check-renderer-ladder", "--render-fault", at_frame.c_str()});
        expect(
            framed.render_fault && framed.render_fault->point == point &&
                framed.render_fault->frame == 120U,
            at_frame.c_str()
        );
    }
    const std::string usage =
        "--render-fault takes create, present, reset, lost, stall, float, memory, card or "
        "full-memory, optionally @FRAME";
    for (const char* refused : {"present@0", "present@x", "present@", "reset@-1", "melt", "@5"})
        expect(rejection({"--check-renderer-ladder", "--render-fault", refused}) == usage, refused);
    expect(
        rejection({"--check-renderer-ladder", "--render-fault"}) ==
            "--render-fault requires a value",
        "--render-fault takes a value"
    );
    expect(
        rejection({"--check-renderer-ladder", "--render-fault", "create@5"}) ==
            "--render-fault create acts at start-up and takes no frame",
        "create takes no frame"
    );
    expect(
        rejection({"--render-fault", "present"}) == "--render-fault needs --check-renderer-ladder",
        "--render-fault without the check is refused"
    );
    expect(
        rejection({"--render-script", "film.oascript", "--check-renderer-ladder"}) ==
            "--render-script cannot be used with --check-renderer-ladder",
        "the director does not run the renderer ladder check"
    );
}

} // namespace

int main() {
    renderer_ladder_options();
    const auto plain = parse({"--headless-check"});
    expect(plain.trace_digest.empty() && plain.trace_units.empty(), "no trace without the flag");
    expect(!plain.seed, "no fixed seed without the flag");

    const auto traced =
        parse({"--trace-digest", "run.trace", "--trace-units", "run.units", "--seed", "1234567"});
    expect(traced.trace_digest == "run.trace", "--trace-digest takes its file");
    expect(traced.trace_units == "run.units", "--trace-units takes its file");
    expect(traced.seed && *traced.seed == 1234567u, "--seed takes its value");

    // The frame rate the loop keeps, and the headless run drawn frame by frame.
    expect(
        plain.max_frames_per_second == oa::app::kDefaultMaxFramesPerSecond,
        "120 frames a second unless --max-fps says otherwise"
    );
    expect(parse({"--max-fps", "60"}).max_frames_per_second == 60, "--max-fps takes its rate");
    expect(!plain.max_frames_per_second_given, "no --max-fps, none given");
    expect(
        parse({"--max-fps", "120"}).max_frames_per_second_given,
        "--max-fps at the default rate is given all the same"
    );
    expect(parse({"--max-fps", "0"}).max_frames_per_second == 0, "--max-fps 0 is no limit");

    // Hardware acceleration's flags decide the setting for the run: a
    // level named, Full for the bare flag, Off for --no-hardware-acceleration.
    using oa::ui::engine_settings::HardwareAcceleration;
    expect(!plain.hardware_acceleration, "no acceleration flag, none given");
    expect(
        parse({"--hardware-acceleration"}).hardware_acceleration == HardwareAcceleration::full,
        "--hardware-acceleration asks for Full"
    );
    expect(
        parse({"--hardware-acceleration=full"}).hardware_acceleration == HardwareAcceleration::full,
        "--hardware-acceleration=full asks for Full"
    );
    expect(
        parse({"--hardware-acceleration=basic"}).hardware_acceleration ==
            HardwareAcceleration::basic,
        "--hardware-acceleration=basic asks for Basic"
    );
    expect(
        parse({"--hardware-acceleration=off"}).hardware_acceleration == HardwareAcceleration::off,
        "--hardware-acceleration=off turns it off"
    );
    expect(
        parse({"--no-hardware-acceleration"}).hardware_acceleration == HardwareAcceleration::off,
        "--no-hardware-acceleration turns it off"
    );
    expect(
        rejection({"--hardware-acceleration=high"}) ==
                "--hardware-acceleration takes off, basic or full" &&
            rejection({"--hardware-acceleration="}) ==
                "--hardware-acceleration takes off, basic or full" &&
            rejection({"--hardware-acceleration=Basic"}) ==
                "--hardware-acceleration takes off, basic or full",
        "a level that is not off, basic or full is refused"
    );
    expect(
        rejection({"--hardware-acceleration", "--no-hardware-acceleration"}) ==
                "--hardware-acceleration and --no-hardware-acceleration cannot be used together" &&
            rejection({"--no-hardware-acceleration", "--skip-intro", "--hardware-acceleration"}) ==
                "--hardware-acceleration and --no-hardware-acceleration cannot be used together",
        "both acceleration flags are refused, in either order"
    );
    expect(
        rejection({"--hardware-acceleration=basic", "--hardware-acceleration=full"}) ==
                "--hardware-acceleration=basic and --hardware-acceleration=full cannot be used "
                "together" &&
            rejection({"--hardware-acceleration=basic", "--hardware-acceleration"}) ==
                "--hardware-acceleration and --hardware-acceleration=basic cannot be used "
                "together" &&
            rejection({"--no-hardware-acceleration", "--hardware-acceleration=basic"}) ==
                "--hardware-acceleration=basic and --no-hardware-acceleration cannot be used "
                "together",
        "flags that name different levels are refused"
    );
    expect(
        parse({"--hardware-acceleration", "--hardware-acceleration"}).hardware_acceleration ==
                HardwareAcceleration::full &&
            parse({"--hardware-acceleration=off", "--no-hardware-acceleration"})
                    .hardware_acceleration == HardwareAcceleration::off &&
            parse({"--hardware-acceleration", "--hardware-acceleration=full"})
                    .hardware_acceleration == HardwareAcceleration::full,
        "flags that name one level are taken"
    );
    expect(
        parse({"--headless-check", "--no-hardware-acceleration"}).hardware_acceleration ==
                HardwareAcceleration::off &&
            parse({"--headless-check", "--hardware-acceleration"}).headless_check,
        "a headless run takes either flag"
    );
    expect(
        oa::app::hardware_acceleration_asked(plain, HardwareAcceleration::basic) ==
                HardwareAcceleration::basic &&
            oa::app::hardware_acceleration_asked(plain, HardwareAcceleration::off) ==
                HardwareAcceleration::off,
        "without a flag the setting decides"
    );
    expect(
        oa::app::hardware_acceleration_asked(
            parse({"--hardware-acceleration"}), HardwareAcceleration::off
        ) == HardwareAcceleration::full &&
            oa::app::hardware_acceleration_asked(
                parse({"--hardware-acceleration=basic"}), HardwareAcceleration::off
            ) == HardwareAcceleration::basic &&
            oa::app::hardware_acceleration_asked(
                parse({"--no-hardware-acceleration"}), HardwareAcceleration::full
            ) == HardwareAcceleration::off,
        "a flag decides over the setting"
    );
    expect(!plain.force_capable, "no --force-capable, none given");
    expect(
        parse({"--check-engine-settings", "--force-capable"}).force_capable,
        "--force-capable goes with --check-engine-settings"
    );
    expect(
        parse({"--check-kill-board", "--force-capable"}).force_capable,
        "--force-capable goes with --check-kill-board"
    );
    expect(
        rejection({"--force-capable"}) ==
                "--force-capable is accepted only with --check-render-tiers, "
                "--check-engine-settings and --check-kill-board" &&
            rejection({"--force-capable", "--check-navigation"}) ==
                "--force-capable is accepted only with --check-render-tiers, "
                "--check-engine-settings and --check-kill-board",
        "--force-capable is refused without a check that takes it"
    );
    expect(
        rejection({"--max-fps", "1001"}) ==
            "--max-fps expects 0 for no limit, or frames a second from 30 through 1000",
        "--max-fps refuses a rate above 1000"
    );
    expect(
        rejection({"--max-fps", "29"}) ==
            "--max-fps expects 0 for no limit, or frames a second from 30 through 1000",
        "--max-fps refuses a rate at which a frame may run two ticks"
    );
    expect(oa::app::kLowestMaxFramesPerSecond == 30, "the lowest rate is the tick rate");
    expect(
        parse({"--max-fps", "30"}).max_frames_per_second == oa::app::kLowestMaxFramesPerSecond,
        "--max-fps takes its lowest rate, a frame a tick"
    );
    const auto framed = parse(
        {"--headless-check",
         "--match-ticks",
         "60",
         "--frame-rate",
         "120",
         "--frame-log",
         "run.frames",
         "--scroll-camera",
         "--march",
         "--follow",
         "--frame-clock",
         "4294965296"}
    );
    expect(framed.frame_rate && *framed.frame_rate == 120, "--frame-rate takes its rate");
    expect(
        framed.frame_log == "run.frames" && framed.scroll_camera && framed.march && framed.follow &&
            framed.frame_clock_ms == 4294965296U,
        "--frame-log, --scroll-camera, --march, --follow and --frame-clock go with --frame-rate"
    );
    expect(
        !plain.frame_rate && plain.frame_log.empty() && !plain.scroll_camera && !plain.march &&
            !plain.follow && !plain.frame_clock_ms,
        "no frame-by-frame run without the flags"
    );
    expect(
        rejection(
            {"--headless-check", "--match-ticks", "60", "--frame-rate", "30", "--frame-clock", "-1"}
        ) == "--frame-clock expects milliseconds from 0 through 1000000000000",
        "--frame-clock refuses a negative time"
    );
    expect(
        rejection(
            {"--headless-check",
             "--match-ticks",
             "60",
             "--frame-rate",
             "30",
             "--frame-clock",
             "1000000000001"}
        ) == "--frame-clock expects milliseconds from 0 through 1000000000000",
        "--frame-clock refuses a time past its latest"
    );
    expect(
        rejection({"--headless-check", "--match-ticks", "60", "--frame-rate", "0"}) ==
            "--frame-rate expects frames a second from 1 through 1000",
        "--frame-rate refuses 0"
    );
    expect(
        rejection({"--headless-check", "--frame-rate", "60"}) ==
            "--frame-rate draws a headless skirmish of --match-ticks ticks",
        "--frame-rate needs --match-ticks"
    );
    expect(
        parse({"--headless-check", "--match-ticks", "60", "--combat", "4", "--busy-combat"})
                .busy_combat &&
            !plain.busy_combat,
        "--busy-combat goes with --combat"
    );
    expect(
        rejection({"--headless-check", "--match-ticks", "60", "--busy-combat"}) ==
            "--busy-combat needs --combat",
        "--busy-combat needs --combat"
    );
    expect(
        parse({"--headless-check", "--match-ticks", "60", "--stage", "s.stage"}).stage_file ==
                oa::app::fs::path("s.stage") &&
            plain.stage_file.empty(),
        "--stage names the file a headless skirmish stages"
    );
    expect(
        rejection({"--headless-check", "--stage", "s.stage"}) ==
            "--stage stages a headless skirmish of --match-ticks ticks",
        "--stage needs --match-ticks"
    );
    expect(
        rejection(
            {"--headless-check", "--match-ticks", "60", "--load", "a.sav", "--stage", "s.stage"}
        ) == "--stage stages a headless skirmish of --match-ticks ticks",
        "--stage does not stage a loaded game"
    );
    expect(
        rejection({"--headless-check", "--match-ticks", "60", "--march"}) ==
            "--frame-log, --scroll-camera, --march, --follow and --frame-clock need --frame-rate",
        "--march needs --frame-rate"
    );
    expect(
        rejection({"--headless-check", "--match-ticks", "60", "--follow"}) ==
            "--frame-log, --scroll-camera, --march, --follow and --frame-clock need --frame-rate",
        "--follow needs --frame-rate"
    );
    expect(
        rejection({"--headless-check", "--match-ticks", "60", "--frame-clock", "0"}) ==
            "--frame-log, --scroll-camera, --march, --follow and --frame-clock need --frame-rate",
        "--frame-clock needs --frame-rate"
    );

    // Without an extension the engine knows none of an extension's options.
    for (const char* flag : {"--extra", "--quiet-extra", "--not-an-option"})
        expect(rejection({flag}) == std::string("unknown option: ") + flag, flag);
    expect(
        parse({"--check-multiplayer-menu"}).check_multiplayer_menu,
        "the multiplayer menu check is still an option"
    );
    // Nor the game's reserved switches: the first one stops the start.
    for (const char* flag : {"-n1", "-hHost", "-y", "-t60", "-e5", "-p12", "-c"})
        expect(
            rejection({"-s", flag}) == std::string("-") + flag[1] + " is not handled by this build",
            flag
        );
    expect(parse({"-s", "-w"}).launch.system_sound == 1, "the other game switches still apply");
    expect(
        rejection({"-rkey"}) ==
            "-r registers the game for multiplayer, which this game does not need",
        "-r without an extension"
    );

    // An extension's options take their values and imply engine options.
    {
        TestExtension state;
        const auto table = test_extension(state);
        const auto extra = parse({"--extra", "value", "--seed", "3"}, table);
        expect(
            state.extra == "value" && extra.seed == 3u, "the extension takes its option's value"
        );
        expect(
            extra.headless_check && extra.fixed_clock && extra.unattended,
            "an extension option implies --headless-check and an unattended run"
        );
        expect(
            rejection({"--extra"}, table) == "--extra requires a value", "its value is required"
        );
    }
    {
        TestExtension state;
        const auto table = test_extension(state);
        const auto quiet = parse({"--quiet-extra"}, table);
        expect(
            state.quiet && quiet.skip_intro && !quiet.headless_check && !quiet.unattended,
            "an extension option implies --skip-intro alone"
        );
        expect(
            rejection({"--unknown"}, table) == "unknown option: --unknown",
            "options neither knows are refused"
        );
    }
    {
        TestExtension state;
        const auto table = test_extension(state);
        expect(
            rejection({"--quiet-extra", "--extra", "x"}, table) ==
                "--extra and --quiet-extra are used apart",
            "the extension checks its options after the engine's"
        );
    }
    {
        // The switches it takes run and skip the intro; the ones it does not
        // are still refused, and -r gives its reason.
        TestExtension state;
        const auto table = test_extension(state);
        const auto switched = parse({"-y"}, table);
        expect(
            state.switches_taken == 1 && switched.launch.skip_intro != 0,
            "the extension takes -y and asks for the intro skip"
        );
        expect(
            rejection({"-t60"}, table) == "-t is not handled by this build",
            "a reserved switch the extension leaves is refused"
        );
        expect(rejection({"-rkey"}, table) == "-r is the extension's", "the extension words -r");
    }

    expect(parse({"--draw-threads", "1"}).draw_threads == 1u, "--draw-threads takes one thread");
    expect(parse({"--draw-threads", "32"}).draw_threads == 32u, "--draw-threads takes 32 threads");
    expect(
        rejection({"--draw-threads", "0"}).find("--draw-threads") == 0,
        "no drawing threads at all is refused"
    );
    expect(
        rejection({"--draw-threads", "33"}).find("--draw-threads") == 0,
        "more drawing threads than the pool runs is refused"
    );
    expect(
        rejection({"--draw-threads", "4x"}).find("--draw-threads") == 0,
        "trailing text after a thread count is refused"
    );
    expect(parse({"--seed", "0"}).seed == 0u, "a zero seed is a seed");
    expect(parse({"--seed", "4294967295"}).seed == 4294967295u, "the widest seed fits");
    expect(
        rejection({"--seed", "4294967296"}).find("--seed") == 0, "a seed past 32 bits is refused"
    );
    expect(rejection({"--seed", "-1"}).find("--seed") == 0, "a negative seed is refused");
    expect(
        rejection({"--seed", "12x"}).find("--seed") == 0, "trailing text after a seed is refused"
    );
    expect(
        rejection({"--trace-digest"}) == "--trace-digest requires a value",
        "the stream needs a file"
    );
    expect(
        rejection({"--trace-digest", ""}) == "--trace-digest requires a value",
        "an empty file name is refused"
    );
    expect(
        rejection({"--trace-units", "run.units"}) == "--trace-units needs --trace-digest",
        "a unit dump needs the stream"
    );

    expect(
        parse({}).game_dir.empty() && !parse({}).choose_game_dir,
        "no game directory until one is passed"
    );
    expect(
        parse({"--game-dir", "Jeux vid\xc3\xa9o"}).game_dir ==
            oa::app::path_from_utf8("Jeux vid\xc3\xa9o"),
        "--game-dir takes a UTF-8 path"
    );
    expect(parse({"--choose-game-dir"}).choose_game_dir, "--choose-game-dir asks for the folder");
    expect(
        parse({}).mod_file.empty() && !parse({}).print_profile &&
            !parse({}).accept_unimplemented_hacks,
        "no mod profile until one is passed"
    );
    expect(
        parse({"--mod", "mods/x/oamod.yaml"}).mod_file ==
            oa::app::path_from_utf8("mods/x/oamod.yaml"),
        "--mod takes a profile file"
    );
    expect(
        parse({"--mod", "a.oamod", "--print-profile", "--accept-unimplemented-hacks"})
            .print_profile,
        "--print-profile prints the --mod profile"
    );
    expect(
        parse({"--game-dir", "ta", "--print-profile"}).print_profile,
        "--print-profile prints the game folder's profile"
    );
    expect(
        rejection({"--print-profile"}) ==
            "--print-profile needs --mod FILE, --mod-dir PATH or --game-dir PATH",
        "--print-profile needs a profile to print"
    );
    expect(
        rejection({"--accept-unimplemented-hacks"}) ==
            "--accept-unimplemented-hacks needs a mod: --mod, --mod-dir, --game-dir or "
            "--print-profile",
        "--accept-unimplemented-hacks needs a profile"
    );
    expect(rejection({"--mod"}) == "--mod requires a value", "--mod needs a file");
    expect(
        parse({"--mod-dir", "mods/x"}).mod_dir == oa::app::path_from_utf8("mods/x"),
        "--mod-dir takes a mod folder"
    );
    expect(parse({"--base-game"}).base_game, "--base-game plays without the remembered mod");
    expect(
        rejection({"--base-game", "--mod-dir", "mods/x"}) ==
            "--base-game and --mod-dir cannot be used together",
        "--base-game and --mod-dir exclude each other"
    );
    const char* data_folder = "Donn\xc3\xa9"
                              "es";
    expect(
        !parse({}).data_dir &&
            parse({"--data-dir", data_folder}).data_dir == oa::app::path_from_utf8(data_folder),
        "--data-dir takes a UTF-8 path; without it the platform's data folder is used"
    );
    expect(rejection({"--data-dir"}) == "--data-dir requires a value", "--data-dir needs a folder");
    expect(
        rejection({"--choose-game-dir", "--game-dir", "ta"}) ==
            "--choose-game-dir and --game-dir cannot be used together",
        "--choose-game-dir does not take a folder"
    );
    expect(
        !parse({}).unattended && !parse({"--skip-intro", "--mute"}).unattended,
        "an interactive start may ask for the folder"
    );
#ifdef _WIN32
    expect(
        parse({}).start_full_screen && parse({"--skip-intro", "--mute"}).start_full_screen,
        "a player's run on Windows starts full screen"
    );
    expect(
        !parse({"-d"}).start_full_screen && !parse({"-dx"}).start_full_screen,
        "-d keeps a player's run in a window"
    );
    expect(
        !parse({"--headless-check"}).start_full_screen &&
            !parse({"--frames", "10"}).start_full_screen,
        "an unattended run keeps a window"
    );
#else
    expect(
        !parse({}).start_full_screen && !parse({"-d"}).start_full_screen,
        "only a run on Windows starts full screen"
    );
#endif
    std::vector<std::vector<const char*>> scripted_runs{
        {"--headless-check"},
        {"--check-navigation"},
        {"--check-multiplayer-menu"},
        {"--check-load-save"},
        {"--check-frontend-controls"},
        {"--check-scroll-bars"},
        {"--check-engine-settings"},
        {"--check-renderer-ladder"},
        {"--check-briefing-narration"},
        {"--check-render-tiers"},
        {"--benchmark", "60"},
        {"--frames", "120"},
        {"--snapshot", "frame.ppm"},
    };
    for (auto scripted : scripted_runs) {
        expect(parse(scripted).unattended, scripted.front());
        scripted.push_back("--choose-game-dir");
        expect(rejection(scripted).find("--choose-game-dir opens a dialog") == 0, scripted.front());
    }

    // The render tiers check draws in a window on the fixed clock, with
    // nobody there; --force-capable goes with it alone.
    const auto tiers = parse({"--check-render-tiers"});
    expect(
        tiers.check_render_tiers && tiers.fixed_clock && tiers.unattended &&
            !tiers.headless_check && !tiers.force_capable,
        "--check-render-tiers is a windowed, fixed-clock and unattended run"
    );
    expect(
        parse({"--check-render-tiers", "--force-capable"}).force_capable,
        "--force-capable takes the renderer as capable for the render tiers check"
    );
    const auto tiers_mission = parse(
        {"--check-render-tiers", "--force-capable", "--campaign", "Arm Campaign", "--mission", "0"}
    );
    expect(
        tiers_mission.check_render_tiers && !tiers_mission.headless_check &&
            tiers_mission.campaign == "Arm Campaign" && tiers_mission.campaign_mission == 0U,
        "the render tiers check takes a campaign mission for data with no skirmish map"
    );
    expect(
        !parse({"--check-match-layers"}).check_render_tiers && !parse({}).force_capable,
        "no render tiers check and no forced renderer unasked"
    );
    expect(
        rejection({"--check-render-tiers", "--headless-check"}) ==
            "--check-render-tiers draws in a window and cannot be used with --headless-check",
        "the render tiers check is not headless"
    );
    expect(
        rejection({"--force-capable"}) ==
            "--force-capable is accepted only with --check-render-tiers, "
            "--check-engine-settings and --check-kill-board",
        "--force-capable alone is refused"
    );
    expect(
        rejection({"--check-match-layers", "--force-capable"}) ==
            "--force-capable is accepted only with --check-render-tiers, "
            "--check-engine-settings and --check-kill-board",
        "--force-capable with another check is refused"
    );
    // --native-density opens the render tiers check's window at the
    // display's own density, and goes with that check alone.
    const auto density = parse(
        {"--check-render-tiers", "--hardware-acceleration", "--force-capable", "--native-density"}
    );
    expect(
        density.native_density && density.check_render_tiers && density.unattended,
        "--native-density goes with the render tiers check"
    );
    expect(
        !tiers.native_density && !parse({}).native_density &&
            !parse({"--hardware-acceleration"}).native_density,
        "no native density unasked"
    );
    expect(
        rejection({"--native-density"}) ==
            "--native-density is accepted only with --check-render-tiers",
        "--native-density alone is refused"
    );
    for (const auto& other : std::vector<std::vector<const char*>>{
             {"--check-engine-settings", "--native-density"},
             {"--check-renderer-ladder", "--native-density"},
             {"--hardware-acceleration", "--native-density"},
             {"--headless-check", "--native-density"},
         })
        expect(
            rejection(other) == "--native-density is accepted only with --check-render-tiers",
            "--native-density with another run is refused"
        );

    // The director view check runs headless on the fixed clock, past the
    // intro, with nobody there.
    const auto director_view = parse({"--check-director-view"});
    expect(
        director_view.check_director_view && director_view.headless_check &&
            director_view.skip_intro && director_view.fixed_clock && director_view.unattended,
        "--check-director-view is a headless, fixed-clock and unattended run"
    );
    expect(!parse({"--headless-check"}).check_director_view, "no director view check unasked");
    expect(
        rejection({"--check-director-view", "--capture-video", "view.mp4"}) ==
            "--capture-video captures the game or a --showcase, not a check or benchmark",
        "the director view check is not captured"
    );

    // The director render check runs headless on the fixed clock too.
    const auto director_render = parse({"--check-director-render"});
    expect(
        director_render.check_director_render && director_render.headless_check &&
            director_render.skip_intro && director_render.fixed_clock && director_render.unattended,
        "--check-director-render is a headless, fixed-clock and unattended run"
    );
    // So does the check of frames drawn between ticks.
    const auto interpolation = parse({"--check-interpolation"});
    expect(
        interpolation.check_interpolation && interpolation.headless_check &&
            interpolation.skip_intro && interpolation.fixed_clock && interpolation.unattended,
        "--check-interpolation is a headless, fixed-clock and unattended run"
    );
    // So does the check of the units of players this machine does not simulate.
    const auto playout = parse({"--check-unit-playout"});
    expect(
        playout.check_unit_playout && playout.headless_check && playout.skip_intro &&
            playout.fixed_clock && playout.unattended,
        "--check-unit-playout is a headless, fixed-clock and unattended run"
    );

    // --generate-script and --render-script run headless on the fixed clock
    // and seed, with nobody there.
    const auto generate = parse({"--generate-script", "game.rec", "--output", "game.oamovie"});
    expect(
        generate.generate_script == oa::app::path_from_utf8("game.rec") &&
            generate.director_output == oa::app::path_from_utf8("game.oamovie") &&
            generate.headless_check && generate.skip_intro && generate.fixed_clock &&
            generate.unattended && !generate.seed && generate.render_script.empty(),
        "--generate-script takes its recording and output and runs headless"
    );
    const auto generate_sized =
        parse({"--generate-script", "game.rec", "--resolution", "3840x2160"});
    expect(
        generate_sized.match_width == 3840 && generate_sized.match_height == 2160 &&
            generate_sized.window_resolution,
        "--resolution sets the size a generated script plans for"
    );
    const auto render = parse({"--render-script", "game.oascript", "--chunks", "2-5"});
    expect(
        render.render_script == oa::app::path_from_utf8("game.oascript") &&
            render.director_chunks == std::pair<uint32_t, uint32_t>{2, 5} &&
            render.director_output.empty() && render.headless_check && render.fixed_clock &&
            render.unattended,
        "--render-script takes its script and chunks and runs headless"
    );
    expect(
        parse({"--render-script", "game.oamovie", "--chunks", "7", "--output", "out"})
                .director_chunks == std::pair<uint32_t, uint32_t>{7, 7},
        "--chunks takes one chunk"
    );
    expect(!parse({}).director_chunks, "every chunk unless --chunks says otherwise");
    expect(
        parse({"--render-script", "game.oascript", "--stills", "0,120,7199"}).director_stills ==
            std::vector<uint64_t>{0, 120, 7199},
        "--stills takes the frames to write as pictures"
    );
    expect(parse({}).director_stills.empty(), "no stills unless --stills asks");
    for (const auto* stills : {"5,5", "9,3", "1,,2", "a", "-1", "1,", ",1", "18446744073709551616"})
        expect(
            rejection({"--render-script", "game.oascript", "--stills", stills}) ==
                "--stills expects frame numbers from 0, in increasing order, separated by commas",
            stills
        );
    expect(
        rejection({"--stills", "1"}) == "--stills needs --render-script" &&
            rejection({"--generate-script", "game.rec", "--stills", "1"}) ==
                "--stills needs --render-script",
        "--stills belongs to a render"
    );
    expect(
        parse({"--render-script", "game.oascript", "--mute", "--preferences-file", "p.conf"}).mute,
        "a render can be silent"
    );
    for (const auto* chunks : {"3-2", "-1", "a-b", "1-", "4294967296", "1-2-3", ""})
        expect(
            rejection({"--render-script", "game.oascript", "--chunks", chunks}).find("--chunks") ==
                0,
            chunks
        );
    expect(
        rejection({"--generate-script", "game.rec", "--render-script", "game.oascript"}) ==
            "--generate-script and --render-script cannot be used together",
        "one director run at a time"
    );
    expect(
        rejection({"--output", "out"}) == "--output needs --generate-script or --render-script",
        "--output belongs to a director run"
    );
    expect(
        rejection({"--chunks", "1"}) == "--chunks needs --render-script" &&
            rejection({"--generate-script", "game.rec", "--chunks", "1"}) ==
                "--chunks needs --render-script",
        "--chunks belongs to a render"
    );
    expect(
        rejection({"--render-script", "game.oascript", "--resolution", "640x360"}) ==
            "--render-script takes the frame size from the script, not from --resolution",
        "a render's size is the script's"
    );
    const std::vector<std::vector<const char*>> refused_with_scripts{
        {"--seed", "5"},
        {"--capture-video", "game.mp4"},
        {"--showcase", "arm-first-mission"},
        {"--benchmark", "60"},
        {"--frames", "60"},
        {"--snapshot", "frame.ppm"},
        {"--match-ticks", "60"},
        {"--campaign", "Arm Campaign", "--mission", "0"},
        {"--load", "game.sav"},
        {"--save-after", "30"},
        {"--camera", "10,20"},
        {"--zoom", "2"},
        {"--combat", "4"},
        {"--busy-combat"},
        {"--check-navigation"},
        {"--check-match-layers"},
        {"--check-render-tiers"},
        {"--check-director-view"},
        {"--check-director-render"},
        {"--check-interpolation"},
        {"--check-unit-playout"},
    };
    for (const auto* run : {"--generate-script", "--render-script"})
        for (const auto& other : refused_with_scripts) {
            std::vector<const char*> arguments{run, "game.file"};
            arguments.insert(arguments.end(), other.begin(), other.end());
            expect(
                rejection(arguments) == std::string(run) + " cannot be used with " + other.front(),
                other.front()
            );
        }

    // --resolution sizes the window of a run that opens one.
    expect(!parse({}).window_resolution, "the window keeps its default size");
    const auto sized = parse({"--resolution", "1280x1024"});
    expect(
        sized.window_resolution && sized.match_width == 1280 && sized.match_height == 1024,
        "--resolution sizes the window"
    );
    expect(
        rejection({"--resolution", "0x480"}) == "--resolution expects a width and height above 0",
        "a window has a size"
    );

    // A capture and a showcase run in the game's window; a showcase is
    // unattended and seeded the same way each time.
    const auto captured = parse({"--capture-video", "showcase.mp4"});
    expect(
        captured.capture_video == oa::app::path_from_utf8("showcase.mp4") && !captured.unattended,
        "--capture-video takes its file and leaves the game to the player"
    );
    const auto showcase = parse({"--showcase", "arm-first-mission"});
    expect(
        showcase.showcase == oa::app::Showcase::arm_first_mission && showcase.unattended &&
            showcase.seed == oa::app::kFixedRandomSeed && !showcase.fixed_clock,
        "--showcase plays the first Arm mission, unattended, on the real clock"
    );
    expect(
        parse({"--showcase", "arm-first-mission", "--seed", "9"}).seed == 9u,
        "--seed chooses a showcase's seed"
    );
    const auto battle = parse({"--showcase", "skirmish-battle", "--combat", "40"});
    expect(
        battle.showcase == oa::app::Showcase::skirmish_battle && battle.unattended &&
            battle.combat_units == 40 && !battle.fixed_clock,
        "--showcase skirmish-battle plays a battle of --combat's armies on the real clock"
    );
    expect(
        rejection({"--showcase", "skirmish"}) ==
            "--showcase knows two showcases: arm-first-mission and skirmish-battle",
        "an unknown showcase is refused"
    );
    expect(
        rejection({"--capture-video", "check.mp4", "--headless-check"}) ==
            "--capture-video captures the game or a --showcase, not a check or benchmark",
        "a headless run cannot be captured"
    );
    expect(
        rejection({"--showcase", "arm-first-mission", "--check-navigation"}) ==
            "--showcase plays in a window; it is not a check or benchmark",
        "a showcase is not a check"
    );
    return failures == 0 ? 0 : 1;
}
