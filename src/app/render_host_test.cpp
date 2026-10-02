// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The walk of the render drivers. First through stand-in hooks over made-up
// drivers: SDL's order walked one driver at a time, every refusal logged
// with its reason, the first driver that starts kept and nothing after it
// tried; drivers recorded as failed skipped, but never software; the
// framebuffer hint set just before software once a hardware driver
// refused, as "0" where the window has a framebuffer of its own and
// otherwise as the drivers still trusted, or the first alone where the
// hint takes no list; a hint not taken logged while the walk goes on; a
// second walk from the top, logged once, when the records would leave
// nothing able to present; and under SDL_RENDER_DRIVER SDL's own call
// alone. Then on SDL's dummy video driver: the walk makes the renderer
// SDL's own choice makes, after every earlier driver of SDL's order
// refused, with the hint "0" (read back only: SDL's dummy video driver
// never presents through a texture, so the hint changes nothing there); a
// hint SDL already holds at a higher priority is refused with no reason,
// and the line says so; a named driver that does not exist ends the start
// after one attempt, with the hint unset; software named is made with the
// hint unset. With --case named-missing, the missing driver is named by
// the environment variable itself.
#include "render_host.hpp"

#include "oa/test/check.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using oa::app::CreationAttempt;
using oa::app::CreationHooks;
using oa::app::CreationOutcome;
using oa::app::RendererHost;
using oa::app::render_policy::CreationInputs;

/// The window the dummy cases make a renderer for, pixels.
constexpr int window_width = 64;
constexpr int window_height = 48;

/// What the stand-in hooks did, in order.
struct Stand {
    std::vector<std::string_view> refusing{}; ///< drivers that refuse; others start
    bool refuse_choice{};                     ///< SDL's own choice refuses
    bool refuse_hint{};                       ///< the hint is not taken
    std::vector<std::string> calls{};         ///< "create <driver>", "create *" or "hint <value>"
    std::vector<std::string> log{};           ///< the lines logged
};

/// Takes or refuses the hint as the stand-in says.
///
/// @param context the Stand
/// @param value the hint's value
/// @param[out] error the reason, when refused
/// @return false when the stand-in refuses hints
bool stand_hint(void* context, const std::string& value, std::string& error) {
    auto& stand = *static_cast<Stand*>(context);
    stand.calls.push_back("hint " + value);
    if (!stand.refuse_hint)
        return true;
    error = "An environment variable is taking priority";
    return false;
}

/// Starts or refuses a driver as the stand-in says.
///
/// @param context the Stand
/// @param driver the driver; empty for SDL's own choice
/// @param[out] error the reason, when refused
/// @return true when the driver starts
bool stand_create(void* context, const std::string& driver, std::string& error) {
    auto& stand = *static_cast<Stand*>(context);
    if (driver.empty()) {
        stand.calls.emplace_back("create *");
        if (!stand.refuse_choice)
            return true;
        error = "missing not available";
        return false;
    }
    stand.calls.push_back("create " + driver);
    for (const std::string_view refusing : stand.refusing)
        if (refusing == driver) {
            error = driver + " would not start";
            return false;
        }
    return true;
}

/// Keeps a logged line.
///
/// @param context the Stand
/// @param line the line
void stand_log(void* context, const std::string& line) {
    static_cast<Stand*>(context)->log.push_back(line);
}

/// Walks the drivers through the stand-in.
///
/// @param inputs the walk's inputs
/// @param[in,out] stand the stand-in
/// @return the walk's outcome
CreationOutcome walk(const CreationInputs& inputs, Stand& stand) {
    CreationHooks hooks;
    hooks.context = &stand;
    hooks.set_framebuffer_hint = stand_hint;
    hooks.create = stand_create;
    hooks.log = stand_log;
    return oa::app::walk_render_drivers(inputs, hooks);
}

/// Returns a driver's refusal line as the stand-in refuses it.
///
/// @param driver the driver
/// @return the line
std::string refused(std::string_view driver) {
    return oa::app::refusal_log_line(driver, std::string(driver) + " would not start");
}

/// SDL's order in the stand-in cases: three hardware drivers, then software.
constexpr std::string_view driver_alpha = "alpha";
constexpr std::string_view driver_beta = "beta";
constexpr std::string_view driver_gamma = "gamma";
constexpr std::string_view software = oa::app::render_policy::software_driver;
const std::vector<std::string_view> sdl_order{driver_alpha, driver_beta, driver_gamma, software};

/// Returns the inputs of a start over the stand-in's order.
///
/// @param native whether the window has a framebuffer of its own
/// @param list whether the framebuffer hint takes a list
/// @return the inputs
CreationInputs start_inputs(bool native, bool list) {
    CreationInputs inputs;
    inputs.sdl_order = sdl_order;
    inputs.native_window_framebuffer = native;
    inputs.hint_takes_list = list;
    return inputs;
}

/// The log lines name the driver and the reason, after the game's prefix.
void test_log_lines() {
    OA_CHECK(
        oa::app::refusal_log_line("alpha", "no device") ==
        "open-annihilation: graphics: renderer alpha refused: no device"
    );
    OA_CHECK(
        oa::app::framebuffer_hint_log_line("0", "taken elsewhere") ==
        "open-annihilation: graphics: the framebuffer hint \"0\" was not taken: taken elsewhere"
    );
    OA_CHECK(oa::app::second_walk_log_line().starts_with("open-annihilation: graphics: "));
}

/// With every driver working the first of SDL's order is made, as SDL's
/// own choice makes it: one attempt, no hint, nothing logged.
void test_every_driver_works() {
    Stand stand;
    const CreationOutcome outcome = walk(start_inputs(true, true), stand);
    OA_CHECK(outcome.created);
    OA_CHECK(outcome.error.empty());
    OA_CHECK(outcome.attempts.size() == 1);
    OA_CHECK(outcome.attempts.size() == 1 && outcome.attempts[0].driver == driver_alpha);
    OA_CHECK(outcome.attempts.size() == 1 && outcome.attempts[0].created);
    OA_CHECK(stand.calls == std::vector<std::string>{"create alpha"});
    OA_CHECK(stand.log.empty());
}

/// The walk goes down SDL's order one driver at a time, logs each refusal
/// with its reason and stops at the first that starts, with no hint.
void test_walk_in_order() {
    Stand stand;
    stand.refusing = {driver_alpha, driver_beta};
    const CreationOutcome outcome = walk(start_inputs(true, true), stand);
    OA_CHECK(outcome.created);
    OA_CHECK(
        stand.calls == std::vector<std::string>({"create alpha", "create beta", "create gamma"})
    );
    OA_CHECK(stand.log == std::vector<std::string>({refused(driver_alpha), refused(driver_beta)}));
    OA_CHECK(outcome.attempts.size() == 3);
    if (outcome.attempts.size() == 3) {
        OA_CHECK(outcome.attempts[0].driver == driver_alpha && !outcome.attempts[0].created);
        OA_CHECK(outcome.attempts[0].error == "alpha would not start");
        OA_CHECK(outcome.attempts[1].driver == driver_beta && !outcome.attempts[1].created);
        OA_CHECK(outcome.attempts[2].driver == driver_gamma && outcome.attempts[2].created);
        OA_CHECK(outcome.attempts[2].error.empty());
    }
}

/// When every hardware driver refuses, the hint is set just before
/// software: "0" where the window has a framebuffer of its own, else the
/// drivers no record left out, as a list or the first alone.
void test_software_fall_back() {
    Stand native;
    native.refusing = {driver_alpha, driver_beta, driver_gamma};
    const CreationOutcome outcome = walk(start_inputs(true, true), native);
    OA_CHECK(outcome.created);
    OA_CHECK(
        native.calls ==
        std::vector<std::string>(
            {"create alpha", "create beta", "create gamma", "hint 0", "create software"}
        )
    );
    OA_CHECK(
        native.log == std::vector<std::string>(
                          {refused(driver_alpha), refused(driver_beta), refused(driver_gamma)}
                      )
    );
    OA_CHECK(outcome.attempts.size() == 4);
    OA_CHECK(!outcome.attempts.empty() && outcome.attempts.back().driver == software);

    Stand listed;
    listed.refusing = {driver_alpha, driver_beta, driver_gamma};
    OA_CHECK(walk(start_inputs(false, true), listed).created);
    OA_CHECK(listed.calls.size() == 5);
    OA_CHECK(listed.calls.size() == 5 && listed.calls[3] == "hint alpha,beta,gamma");

    Stand single;
    single.refusing = {driver_alpha, driver_beta, driver_gamma};
    OA_CHECK(walk(start_inputs(false, false), single).created);
    OA_CHECK(single.calls.size() == 5 && single.calls[3] == "hint alpha");
}

/// A hint that is not taken is logged and the walk goes on to software.
void test_hint_not_taken() {
    Stand stand;
    stand.refusing = {driver_alpha, driver_beta, driver_gamma};
    stand.refuse_hint = true;
    const CreationOutcome outcome = walk(start_inputs(true, true), stand);
    OA_CHECK(outcome.created);
    OA_CHECK(stand.calls.size() == 5 && stand.calls.back() == "create software");
    OA_CHECK(
        stand.log ==
        std::vector<std::string>(
            {refused(driver_alpha),
             refused(driver_beta),
             refused(driver_gamma),
             oa::app::framebuffer_hint_log_line("0", "An environment variable is taking priority")}
        )
    );
}

/// When software refuses too, nothing is made: every refusal is logged,
/// and the error is the game's usual one with the last reason.
void test_nothing_starts() {
    Stand stand;
    stand.refusing = {driver_alpha, driver_beta, driver_gamma, software};
    const CreationOutcome outcome = walk(start_inputs(true, true), stand);
    OA_CHECK(!outcome.created);
    OA_CHECK(outcome.attempts.size() == 4);
    OA_CHECK(stand.log.size() == 4 && stand.log.back() == refused(software));
    OA_CHECK(outcome.error == "SDL_CreateRenderer: software would not start");

    CreationInputs none;
    Stand nothing;
    const CreationOutcome empty = walk(none, nothing);
    OA_CHECK(!empty.created);
    OA_CHECK(empty.attempts.empty());
    OA_CHECK(empty.error == "SDL_CreateRenderer: no render driver is available");
}

/// A driver recorded as failed is never tried; software is never skipped.
void test_skipping() {
    const std::vector<std::string_view> recorded{driver_alpha};
    CreationInputs inputs = start_inputs(true, true);
    inputs.failed_drivers = recorded;
    Stand stand;
    OA_CHECK(walk(inputs, stand).created);
    OA_CHECK(stand.calls == std::vector<std::string>{"create beta"});

    // Skipping a driver counts as a miss: the hint is set before software.
    const std::vector<std::string_view> all_but_software{
        driver_alpha, driver_beta, driver_gamma, software
    };
    inputs.failed_drivers = all_but_software;
    Stand last;
    OA_CHECK(walk(inputs, last).created);
    OA_CHECK(last.calls == std::vector<std::string>({"hint 0", "create software"}));
    OA_CHECK(last.log.empty());
}

/// The records are advice: where they would leave nothing able to present,
/// the walk starts again from the top with them ignored, and logs that once.
void test_advice_rule() {
    // Every hardware driver recorded, and no framebuffer of the window's own:
    // software would have nothing to present through.
    const std::vector<std::string_view> every_hardware{driver_alpha, driver_beta, driver_gamma};
    CreationInputs inputs = start_inputs(false, true);
    inputs.failed_drivers = every_hardware;
    Stand stand;
    const CreationOutcome outcome = walk(inputs, stand);
    OA_CHECK(outcome.created);
    OA_CHECK(stand.calls == std::vector<std::string>{"create alpha"});
    OA_CHECK(stand.log == std::vector<std::string>{oa::app::second_walk_log_line()});

    // One driver recorded and software refused: the second walk tries the
    // recorded driver too, and the line is logged once.
    const std::vector<std::string_view> one{driver_alpha};
    inputs = start_inputs(true, true);
    inputs.failed_drivers = one;
    Stand again;
    again.refusing = {driver_beta, driver_gamma, software};
    const CreationOutcome second = walk(inputs, again);
    OA_CHECK(second.created);
    OA_CHECK(
        again.calls ==
        std::vector<std::string>(
            {"create beta", "create gamma", "hint 0", "create software", "create alpha"}
        )
    );
    OA_CHECK(
        again.log == std::vector<std::string>(
                         {refused(driver_beta),
                          refused(driver_gamma),
                          refused(software),
                          oa::app::second_walk_log_line()}
                     )
    );
    OA_CHECK(!second.attempts.empty() && second.attempts.back().driver == driver_alpha);
}

/// Under SDL_RENDER_DRIVER the start is SDL's own call, once, with no hint
/// and no line of its own; a refusal ends the start with SDL's reason.
void test_environment_start() {
    CreationInputs inputs = start_inputs(true, true);
    inputs.render_driver_named = true;
    Stand works;
    const CreationOutcome made = walk(inputs, works);
    OA_CHECK(made.created);
    OA_CHECK(works.calls == std::vector<std::string>{"create *"});
    OA_CHECK(made.attempts.size() == 1 && made.attempts[0].driver.empty());

    Stand refuses;
    refuses.refuse_choice = true;
    const CreationOutcome failed = walk(inputs, refuses);
    OA_CHECK(!failed.created);
    OA_CHECK(refuses.calls == std::vector<std::string>{"create *"});
    OA_CHECK(refuses.log.empty());
    OA_CHECK(failed.attempts.size() == 1);
    OA_CHECK(failed.error == "SDL_CreateRenderer: missing not available");
}

/// Returns the framebuffer hint SDL holds.
///
/// @return its value; empty when unset
std::string framebuffer_hint() {
    const char* value = SDL_GetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION);
    return value != nullptr ? value : "";
}

/// Starts SDL's video and opens a window, for one case.
///
/// @return the window; null, with the failure counted, when SDL failed
SDL_Window* open_window() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        OA_CHECK(false);
        return nullptr;
    }
    SDL_Window* window = SDL_CreateWindow("render host test", window_width, window_height, 0);
    OA_CHECK(window != nullptr);
    return window;
}

/// Closes a case's window and SDL, which forgets the hints the case set.
///
/// @param window the window; null is allowed
void close_window(SDL_Window* window) {
    if (window != nullptr)
        SDL_DestroyWindow(window);
    SDL_Quit();
}

/// On the dummy video driver, with no driver named, the walk tries SDL's
/// drivers in SDL's order and ends where SDL's own choice ends, with every
/// earlier driver refused and the hint "0" set before software.
void test_dummy_walk() {
    SDL_Window* window = open_window();
    if (window == nullptr) {
        close_window(window);
        return;
    }
    RendererHost host;
    try {
        host.create(window);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "create: %s\n", error.what());
    }
    OA_CHECK(host.renderer() != nullptr);
    const auto attempts = host.attempts();
    OA_CHECK(!attempts.empty());
    OA_CHECK(static_cast<int>(attempts.size()) <= SDL_GetNumRenderDrivers());
    for (std::size_t index = 0; index < attempts.size(); ++index) {
        const char* name = SDL_GetRenderDriver(static_cast<int>(index));
        OA_CHECK(name != nullptr && attempts[index].driver == name);
        const bool last = index + 1 == attempts.size();
        OA_CHECK(attempts[index].created == last);
        OA_CHECK(attempts[index].error.empty() == last);
    }
    OA_CHECK(host.facts().renderer == oa::app::render_policy::software_driver);
    OA_CHECK(framebuffer_hint() == (attempts.size() > 1 ? "0" : ""));
    if (host.renderer() != nullptr) {
        // SDL's own choice for another window of the same video driver.
        SDL_Window* other = SDL_CreateWindow("render host test", window_width, window_height, 0);
        SDL_Renderer* chosen = other != nullptr ? SDL_CreateRenderer(other, nullptr) : nullptr;
        OA_CHECK(chosen != nullptr);
        if (chosen != nullptr) {
            const char* name = SDL_GetRendererName(chosen);
            OA_CHECK(name != nullptr && host.facts().renderer == name);
            SDL_DestroyRenderer(chosen);
        }
        if (other != nullptr)
            SDL_DestroyWindow(other);
    }
    host.destroy();
    OA_CHECK(host.renderer() == nullptr);
    close_window(window);
}

/// On the dummy video driver, with the framebuffer hint already held at
/// SDL's override priority, SDL refuses the walk's hint without a reason:
/// the line logged says no reason was given rather than repeating the last
/// driver's refusal, and the walk still ends on software.
void test_dummy_hint_held() {
    SDL_Window* window = open_window();
    if (window == nullptr) {
        close_window(window);
        return;
    }
    OA_CHECK(SDL_SetHintWithPriority(SDL_HINT_FRAMEBUFFER_ACCELERATION, "1", SDL_HINT_OVERRIDE));
    RendererHost host;
    std::ostringstream logged;
    std::streambuf* const standard_output = std::cout.rdbuf(logged.rdbuf());
    try {
        host.create(window);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "create: %s\n", error.what());
    }
    std::cout.rdbuf(standard_output);
    OA_CHECK(host.renderer() != nullptr);
    OA_CHECK(host.facts().renderer == oa::app::render_policy::software_driver);
    OA_CHECK(framebuffer_hint() == "1");
    const auto attempts = host.attempts();
    std::vector<std::string> lines;
    std::istringstream reading(logged.str());
    for (std::string line; std::getline(reading, line);)
        lines.push_back(line);
    const std::string not_taken =
        oa::app::framebuffer_hint_log_line("0", oa::app::unexplained_refusal);
    std::size_t not_taken_count = 0;
    for (const std::string& line : lines)
        not_taken_count += line == not_taken ? 1U : 0U;
    OA_CHECK(not_taken_count == (attempts.size() > 1 ? 1U : 0U));
    host.destroy();
    close_window(window);
}

/// A driver that does not exist, named: SDL's own call fails once, the
/// start throws the game's usual error, and no hint is set.
///
/// @param by_hint name it through SDL's hint, as the variable would; false
///     when the environment variable itself names it
void test_named_missing(bool by_hint) {
    SDL_Window* window = open_window();
    if (window == nullptr) {
        close_window(window);
        return;
    }
    if (by_hint)
        OA_CHECK(SDL_SetHint(SDL_HINT_RENDER_DRIVER, "missing"));
    RendererHost host;
    bool thrown = false;
    try {
        host.create(window);
    } catch (const std::runtime_error& error) {
        thrown = std::string_view(error.what()).starts_with(oa::app::renderer_creation_error);
    }
    OA_CHECK(thrown);
    OA_CHECK(host.renderer() == nullptr);
    OA_CHECK(host.attempts().size() == 1);
    OA_CHECK(host.attempts().size() == 1 && host.attempts()[0].driver.empty());
    OA_CHECK(host.attempts().size() == 1 && !host.attempts()[0].created);
    OA_CHECK(framebuffer_hint().empty());
    close_window(window);
}

/// Software named: SDL's own call makes it at once, with no hint.
void test_named_software() {
    SDL_Window* window = open_window();
    if (window == nullptr) {
        close_window(window);
        return;
    }
    OA_CHECK(SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software"));
    RendererHost host;
    try {
        host.create(window);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "create: %s\n", error.what());
    }
    OA_CHECK(host.renderer() != nullptr);
    OA_CHECK(host.facts().renderer == oa::app::render_policy::software_driver);
    OA_CHECK(host.attempts().size() == 1 && host.attempts()[0].created);
    OA_CHECK(framebuffer_hint().empty());
    host.destroy();
    close_window(window);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--case" &&
        std::string_view(argv[2]) == "named-missing") {
        test_named_missing(false);
        return oa::test::check_exit_status();
    }
    test_log_lines();
    test_every_driver_works();
    test_walk_in_order();
    test_software_fall_back();
    test_hint_not_taken();
    test_nothing_starts();
    test_skipping();
    test_advice_rule();
    test_environment_start();
    test_dummy_walk();
    test_dummy_hint_held();
    test_named_missing(true);
    test_named_software();
    return oa::test::check_exit_status();
}
