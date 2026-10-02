// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The guarded call of the extension tables' hooks (hook_call.hpp): a hook
// of each handling (raised, reported, must not throw) that throws is caught
// at the call, and the error is raised as a std::runtime_error or handed to
// the report; a null hook answers its zero value; a std::exception and
// anything else are both caught; and an error from an extension of the
// combined table (extension_list.hpp) is caught the same way.
#include "oa/app/hook_call.hpp"

#include "oa/app/command_line.hpp"
#include "oa/app/extension_list.hpp"
#include "oa/test/check.hpp"

#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace oa::app;

// A reference the hooks under test take and never use.
template <typename T>
T& opaque() {
    alignas(64) static unsigned char storage[64]{};
    return *reinterpret_cast<T*>(storage);
}

// The reports a test's report received, as "<hook>: <message>".
std::vector<std::string> reports;

void record_report(const char* hook, const char* message) {
    reports.push_back(std::string(hook) + ": " + message);
}

/// Returns the reports so far, and clears them.
///
/// @return the reports, in order
std::vector<std::string> take_reports() {
    return std::exchange(reports, {});
}

/// Returns the message of the std::runtime_error a call raises.
///
/// @param call the call
/// @return the message; "<nothing raised>" when the call returned, and
///         "<other exception>" for an exception of another type
template <typename Call>
std::string raised(Call&& call) {
    try {
        call();
    } catch (const std::runtime_error& error) {
        return error.what();
    } catch (...) {
        return "<other exception>";
    }
    return "<nothing raised>";
}

// A hook of each handling, throwing a std::runtime_error.
void check_options_throws(void*) {
    throw std::runtime_error("options refused");
}

bool take_option_throws(void*, const char*, const OptionValues&, uint32_t& effects) {
    effects = option_effect::skip_intro;
    throw std::runtime_error("bad option value");
}

void draw_match_hud_throws(void*, Runtime&) {
    throw std::runtime_error("no font");
}

bool player_gone_throws(void*, Runtime&, oa::World&, const oa::Player&) {
    throw std::runtime_error("no announcement");
}

bool simulation_step_throws(void*, Runtime&) {
    throw std::runtime_error("step failed");
}

void speed_changed_throws(void*, Runtime&, uint16_t) {
    throw std::runtime_error("speed not shared");
}

void release_runtime_throws(void*, Runtime&) {
    throw std::runtime_error("state not freed");
}

bool replay_step_throws(void*) {
    throw std::runtime_error("tick failed");
}

void replay_status_throws(void*, RecordingStatus& status) {
    status.tick = 7;
    throw std::runtime_error("status unread");
}

int take_switch_throws(void*, char, const command_line::SwitchArguments*, uint32_t*) {
    throw std::runtime_error("bad switch");
}

// A hook that throws what is no std::exception.
void startup_throws_other(void*, Runtime&) {
    throw 7;
}

// Hooks that answer.
bool take_option_takes(void*, const char*, const OptionValues&, uint32_t&) {
    return true;
}

uint32_t state_multiplayer(void*, const Runtime&) {
    return extension_state::multiplayer;
}

void check_raised() {
    Extension table{};
    table.check_options = check_options_throws;
    OA_CHECK(raised([&] {
                 call_hook_or_raise<&Extension::check_options>(table);
             }) == "options refused");
    table.take_option = take_option_throws;
    uint32_t effects = 0;
    const OptionValues values{};
    OA_CHECK(raised([&] {
                 (void)call_hook_or_raise<&Extension::take_option>(table, "--x", values, effects);
             }) == "bad option value");
    table.startup = startup_throws_other;
    OA_CHECK(raised([&] {
                 call_hook_or_raise<&Extension::startup>(table, opaque<Runtime>());
             }) == "an exception that is not a std::exception");
    // A hook that answers gives its answer; a null hook its zero value.
    table.take_option = take_option_takes;
    OA_CHECK(call_hook_or_raise<&Extension::take_option>(table, "--x", values, effects));
    table.state = state_multiplayer;
    OA_CHECK(
        call_hook_or_raise<&Extension::state>(table, opaque<Runtime>()) ==
        extension_state::multiplayer
    );
    const Extension none{};
    OA_CHECK(!call_hook_or_raise<&Extension::take_option>(none, "--x", values, effects));
    OA_CHECK(call_hook_or_raise<&Extension::state>(none, opaque<Runtime>()) == 0);
    OA_CHECK(call_hook_or_raise<&Extension::return_label>(none) == nullptr);
    OA_CHECK(raised([&] {
                 call_hook_or_raise<&Extension::check_options>(none);
             }) == "<nothing raised>");
}

void check_reported() {
    Extension table{};
    table.draw_match_hud = draw_match_hud_throws;
    call_hook_or_report<&Extension::draw_match_hud>(table, record_report, opaque<Runtime>());
    OA_CHECK(take_reports() == std::vector<std::string>{"draw_match_hud: no font"});
    // A hook that answers, and threw, answers as a null hook: the engine
    // posts its own elimination message.
    table.player_gone = player_gone_throws;
    OA_CHECK(!call_hook_or_report<&Extension::player_gone>(
        table, record_report, opaque<Runtime>(), opaque<oa::World>(), opaque<oa::Player>()
    ));
    OA_CHECK(take_reports() == std::vector<std::string>{"player_gone: no announcement"});
    // The guarded call alone tells the caller the hook threw, as the match
    // clock needs for simulation_step to drop the frame's remaining steps.
    table.simulation_step = simulation_step_throws;
    HookError error;
    OA_CHECK(!call_hook<&Extension::simulation_step>(table, error, opaque<Runtime>()));
    OA_CHECK(error.caught && error.message == "step failed");
    // A later call that does not throw clears the error.
    table.simulation_step = nullptr;
    OA_CHECK(!call_hook<&Extension::simulation_step>(table, error, opaque<Runtime>()));
    OA_CHECK(!error.caught && error.message.empty());
    // A report that throws does not stop the call.
    call_hook_or_report<&Extension::draw_match_hud>(
        table,
        [](const char*, const char*) { throw std::runtime_error("no report"); },
        opaque<Runtime>()
    );
    OA_CHECK(take_reports().empty());
}

void check_must_not_throw() {
    Extension table{};
    table.speed_changed = speed_changed_throws;
    call_hook_or_report<&Extension::speed_changed>(
        table, record_report, opaque<Runtime>(), uint16_t{12}
    );
    OA_CHECK(take_reports() == std::vector<std::string>{"speed_changed: speed not shared"});
    table.release_runtime = release_runtime_throws;
    call_hook_or_report<&Extension::release_runtime>(table, record_report, opaque<Runtime>());
    OA_CHECK(take_reports() == std::vector<std::string>{"release_runtime: state not freed"});
    ReplayHooks replay{};
    replay.step = replay_step_throws;
    replay.status = replay_status_throws;
    OA_CHECK(!call_hook_or_report<&ReplayHooks::step>(replay, record_report));
    RecordingStatus status{};
    call_hook_or_report<&ReplayHooks::status>(replay, record_report, status);
    OA_CHECK(
        take_reports() == (std::vector<std::string>{"step: tick failed", "status: status unread"})
    );
    // A null close is not called.
    call_hook_or_report<&ReplayHooks::close>(replay, record_report);
    OA_CHECK(take_reports().empty());
}

void check_switch_handler() {
    command_line::SwitchHandler handler{};
    handler.take = take_switch_throws;
    HookError error;
    uint32_t effects = 0;
    OA_CHECK(
        call_hook<&command_line::SwitchHandler::take>(handler, error, 'x', nullptr, &effects) == 0
    );
    OA_CHECK(error.caught && error.message == "bad switch");
}

// The combined table: the first extension's hook throws, so the second's is
// not called for that call, and the guarded call of the combined hook
// catches the error.
int second_hud_calls = 0;

void init_throwing(Extension* table) {
    table->draw_match_hud = draw_match_hud_throws;
    table->check_options = check_options_throws;
}

void init_counting(Extension* table) {
    table->draw_match_hud = [](void*, Runtime&) { ++second_hud_calls; };
}

void check_combined_table() {
    const RegisteredExtension extensions[] = {
        {"throwing", init_throwing}, {"counting", init_counting}
    };
    const ExtensionList list{std::span<const RegisteredExtension>(extensions)};
    const Extension& combined = list.combined();
    call_hook_or_report<&Extension::draw_match_hud>(combined, record_report, opaque<Runtime>());
    OA_CHECK(take_reports() == std::vector<std::string>{"draw_match_hud: no font"});
    OA_CHECK(second_hud_calls == 0);
    OA_CHECK(raised([&] {
                 call_hook_or_raise<&Extension::check_options>(combined);
             }) == "options refused");
}

// The guarded calls never let an exception out.
static_assert(noexcept(call_hook<&Extension::frame>(
    std::declval<const Extension&>(),
    std::declval<HookError&>(),
    std::declval<Runtime&>(),
    FrameStage::pump
)));
static_assert(noexcept(call_hook_or_report<&Extension::app_mode_set>(
    std::declval<const Extension&>(), record_report, std::declval<Runtime&>(), 1
)));

} // namespace

int main() {
    check_raised();
    check_reported();
    check_must_not_throw();
    check_switch_handler();
    check_combined_table();
    return oa::test::check_exit_status();
}
