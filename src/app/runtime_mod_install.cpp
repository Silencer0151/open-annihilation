// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The mod packages opened in the game, installed over the main menu: each
// taken from the inbox once the menu has settled, read, its question asked
// in a prompt in the settings dialog's look, its files unpacked a budget a
// frame with the progress shown, put in place, and what came of it told;
// a change to the mod played waits for the run to end. PLAY NOW switches to
// the mod installed, and the Mods page's ROLL BACK swaps a mod folder with
// the version its .backup keeps.

#include "engine_settings_state.hpp"
#include "mod_install_state.hpp"
#include "user_folder_state.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/app/mod_install.hpp"
#include "oa/app/mod_install/handoff.hpp"
#include "oa/app/mod_install/inbox.hpp"
#include "oa/app/mod_install/prompts.hpp"
#include "oa/app/platform_hooks.hpp"
#include "oa/app/runtime.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/base/threads.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/engine_settings/prompt.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"
#include "oa/ui/frontend_state/app_modes.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace oa::app {

namespace settings = oa::ui::engine_settings;
namespace artless = oa::ui::frontend_renderer;
namespace install = oa::app::mod_install;

namespace {

/// The prompt overlay's z: over the notices' overlay, which never shows with it.
constexpr int16_t kPromptOverlayZ = 102;
/// Frames in a row the main menu shows before a prompt.
constexpr uint32_t kPromptMenuFrames = 2;
/// The package bytes an unpacking step reads.
constexpr uint64_t kUnpackBudget = uint64_t{256} << 10;
/// The most time a frame spends unpacking.
constexpr std::chrono::milliseconds kUnpackFrameTime{15};
/// How often the hand-off folder is looked in, in milliseconds.
constexpr uint64_t kHandoffPollMs = 1000;
/// The slices a wait between renames pumps events in, in milliseconds.
constexpr uint32_t kWaitSliceMs = 50;
/// The sound a prompt's button plays as it closes the prompt.
constexpr std::string_view kCloseSound = "Options";

/// Writes a line to standard error, which the log keeps.
///
/// @param line the line
void log_line(std::string_view line) {
    std::cerr << "open-annihilation: mod install: " << line << '\n';
}

/// Returns a folder's path as the settings keep it: absolute, normal, UTF-8.
///
/// @param folder the folder
/// @return its path
std::string kept_path(const fs::path& folder) {
    std::error_code error;
    const fs::path absolute = fs::absolute(folder, error);
    return path_to_utf8((error ? folder : absolute).lexically_normal());
}

/// Tells whether two kept paths name one folder: compared without case on
/// the systems whose file systems fold it.
///
/// @param left one path
/// @param right the other
/// @return true when they match
bool same_folder(std::string_view left, std::string_view right) {
#if defined(_WIN32) || defined(__APPLE__)
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z')
            a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
#else
    return left == right;
#endif
}

/// Waits between tries of a rename, keeping the window answering: SDL's
/// events are pumped every slice.
///
/// @param milliseconds how long
void wait_pumping(void*, uint32_t milliseconds) {
    while (milliseconds > 0) {
        const uint32_t slice = std::min(milliseconds, kWaitSliceMs);
        SDL_PumpEvents();
        oa::base::threads::sleep_ms(slice);
        milliseconds -= slice;
    }
}

/// Returns the source pixel an input's pointer is over.
///
/// @param input the input
/// @param[out] x the pixel's column
/// @param[out] y the pixel's row
void pointer_pixel(const ScreenInput& input, int32_t& x, int32_t& y) {
    x = static_cast<int32_t>(std::floor(input.x));
    y = static_cast<int32_t>(std::floor(input.y));
}

} // namespace

void Runtime::destroy_mod_install_state(ModInstallState* state) noexcept {
    if (state != nullptr && state->unpacking && state->stage == ModInstallState::Stage::unpacking)
        state->unpacking->cancel();
    delete state;
}

Runtime::ModInstallState& Runtime::mod_install_state() {
    if (!mod_install_state_) {
        mod_install_state_.reset(new ModInstallState{});
        // The folders the start's recovery, or a change that waited, left
        // to delete.
        const auto discards = install::take_discards();
        mod_install_state_->discarder.add(discards);
    }
    return *mod_install_state_;
}

bool Runtime::mod_install_prompt_shown() const noexcept {
    return mod_install_state_ && mod_install_state_->shown.has_value();
}

artless::Placement Runtime::ModInstallState::prompt_placement(int32_t height) {
    return {(kCanvasWidth - settings::notice_width) / 2, (kCanvasHeight - height) / 2, 1};
}

int Runtime::ModInstallState::prompt_event(ScreenContext* context, void*) {
    auto& runtime = *static_cast<Runtime*>(context->host);
    if (!runtime.mod_install_state_)
        return 0;
    auto& state = *runtime.mod_install_state_;
    const auto& input = *context->input;
    if (state.latched_key != 0 && input.key == state.latched_key) {
        if (input.kind == ScreenInputKind::key_up)
            state.latched_key = 0;
        if (input.kind == ScreenInputKind::key_down || input.kind == ScreenInputKind::key_up)
            return 1;
    }
    if (!state.shown || runtime.screen_ != Screen::main_menu)
        return 0;
    auto& prompt = state.shown->prompt;
    const auto* fonts = runtime.engine_settings_fonts();
    const int32_t height = settings::prompt_height(prompt, fonts);
    const auto placement = prompt_placement(height);
    int32_t x = 0;
    int32_t y = 0;
    pointer_pixel(input, x, y);
    x -= placement.x;
    y -= placement.y;
    settings::PromptAnswer answer{};
    uint32_t key_down = 0;
    switch (input.kind) {
    case ScreenInputKind::pointer_move:
        answer = settings::prompt_pointer_move(prompt, x, y, height);
        break;
    case ScreenInputKind::pointer_down:
        if (input.button == SDL_BUTTON_LEFT)
            answer = runtime.engine_settings_state().finger_pointer
                         ? settings::prompt_finger_down(
                               prompt, x, y, height, EngineSettingsState::finger_reach(runtime, 1.0)
                           )
                         : settings::prompt_pointer_down(prompt, x, y, height);
        break;
    case ScreenInputKind::pointer_up:
        if (input.button == SDL_BUTTON_LEFT)
            answer = settings::prompt_pointer_up(prompt, x, y, height);
        break;
    case ScreenInputKind::key_down:
        if (const auto key = engine_settings_dialog_key(input.key, input.modifiers)) {
            answer = settings::prompt_key(prompt, *key);
            key_down = input.key;
        }
        break;
    default:
        break;
    }
    if (answer.action == settings::PromptAction::answered) {
        state.latched_key = key_down;
        runtime.answer_mod_install_prompt(answer.button);
    }
    // The prompt is modal: nothing under it sees any input while it shows.
    return 1;
}

void Runtime::ModInstallState::prompt_tick(ScreenContext* context, void*) {
    auto& runtime = *static_cast<Runtime*>(context->host);
    if (!runtime.mod_install_state_ || runtime.screen_ == Screen::main_menu)
        return;
    auto& state = *runtime.mod_install_state_;
    // A question set aside by another screen waits again for the main menu.
    if (state.stage == Stage::asking) {
        install::return_mod_file(state.file);
        state.stage = Stage::idle;
        state.shown.reset();
        state.package.reset();
    } else if (state.stage == Stage::telling || state.stage == Stage::idle) {
        state.shown.reset();
        state.stage = Stage::idle;
    }
}

void Runtime::ModInstallState::prompt_draw(ScreenContext* context, void*) {
    auto& runtime = *static_cast<Runtime*>(context->host);
    if (!runtime.mod_install_state_ || !runtime.mod_install_state_->shown ||
        runtime.screen_ != Screen::main_menu || context->surface == nullptr)
        return;
    const auto* fonts = runtime.engine_settings_fonts();
    if (fonts == nullptr)
        return;
    const auto& prompt = runtime.mod_install_state_->shown->prompt;
    auto& frame = *context->surface;
    artless::blend_source_rect(
        frame,
        {0, 0, 1},
        {0, 0, static_cast<int32_t>(frame.width), static_cast<int32_t>(frame.height)},
        settings::backdrop_color,
        settings::menu_backdrop_opacity
    );
    settings::draw_prompt(
        frame,
        prompt_placement(settings::prompt_height(prompt, fonts)),
        prompt,
        *fonts,
        runtime.engine_settings_icon()
    );
}

void Runtime::register_mod_install_overlay() {
    // On every screen, so that its tick sets a question aside when another
    // screen replaces the main menu; it takes input and draws there only.
    OverlayDesc prompt{};
    prompt.name = "mod_install_prompt";
    prompt.screen = kScreenAny;
    prompt.z = kPromptOverlayZ;
    prompt.event = ModInstallState::prompt_event;
    prompt.tick = ModInstallState::prompt_tick;
    prompt.draw = ModInstallState::prompt_draw;
    overlay_register(&screens_, &prompt);
}

void Runtime::ModInstallState::show(install::ModPrompt made) {
    shown = std::move(made);
    ++prompts_shown;
}

void Runtime::ModInstallState::release_opened_copy() {
    if (opened_copy.empty())
        return;
    const PlatformHooks& hooks = platform_hooks();
    if (hooks.release_opened_file != nullptr)
        hooks.release_opened_file(hooks.context, path_to_utf8(opened_copy).c_str());
    opened_copy.clear();
}

void Runtime::ModInstallState::finish_package() {
    release_opened_copy();
    package.reset();
    install::finish_mod_file();
}

void Runtime::take_handed_mod_files() {
    const auto folder = install::handoff_folder();
    if (!folder)
        return;
    const auto files = install::take_handed_files(*folder);
    for (const auto& file : files) {
        log_line("handed over by another start: " + path_to_utf8(file));
        install::post_mod_file(file);
    }
    if (!files.empty() && sdl_.window != nullptr) {
        if ((SDL_GetWindowFlags(sdl_.window) & SDL_WINDOW_MINIMIZED) != 0)
            std::ignore = SDL_RestoreWindow(sdl_.window);
        std::ignore = SDL_RaiseWindow(sdl_.window);
    }
}

void Runtime::tell_mod_installs() {
    auto& state = mod_install_state();
    const fs::path mods = user_folder_ / std::string(user_mods_folder_name);
    // An unpacking runs on under its prompt, a budget a frame, whatever
    // shows: the prompt is modal on the main menu, the only screen it
    // starts on.
    if (state.stage == ModInstallState::Stage::unpacking && state.unpacking) {
        const bool watched =
            !UserFolderState::unwatched(options_.unattended) || !state.check_shows_prompts;
        if (state.placing) {
            state.placing = false;
            install::ChangeOptions options{};
            options.expected = state.expected;
            options.hooks.wait = wait_pumping;
            const install::ChangeResult result =
                install::commit_change(mods, state.target, state.change, options);
            state.discarder.add(result.discards);
            state.discarder.add(state.unpacking->discards());
            state.unpacking.reset();
            state.stage = ModInstallState::Stage::telling;
            const fs::path folder = mods / path_from_utf8(state.target);
            if (!result.changed) {
                install::Problem problem{};
                problem.refusal = result.refusal;
                problem.detail = result.detail;
                log_line(state.package->file_name + ": not put in place: " + result.detail);
                state.show(install::refused_prompt(state.package->file_name, problem, true));
            } else {
                log_line(state.package->file_name + ": installed in " + path_to_utf8(folder));
                state.told_folder = folder;
                state.play_folder = folder;
                const bool locked = !options_.mod_dir.empty() || options_.base_game;
                state.show(
                    state.change == install::Change::install
                        ? install::installed_prompt(state.incoming, folder, !locked)
                        : install::updated_prompt(
                              state.change, state.incoming, state.expected, folder, result, !locked
                          )
                );
                list_offered_mods();
            }
            state.finish_package();
            return;
        }
        const auto started = std::chrono::steady_clock::now();
        install::Problem problem{};
        auto step = oa::formats::zip::StreamStep::more;
        while (step == oa::formats::zip::StreamStep::more) {
            step = state.unpacking->step(kUnpackBudget, problem);
            if (watched && std::chrono::steady_clock::now() - started >= kUnpackFrameTime)
                break;
        }
        if (step == oa::formats::zip::StreamStep::failed) {
            state.discarder.add(state.unpacking->discards());
            state.unpacking.reset();
            log_line(state.package->file_name + ": " + install::refusal_text(problem));
            state.stage = ModInstallState::Stage::telling;
            state.show(install::refused_prompt(state.package->file_name, problem, false));
            state.finish_package();
            return;
        }
        if (step == oa::formats::zip::StreamStep::more) {
            auto made = install::installing_prompt(
                state.incoming, state.unpacking->done_bytes(), state.unpacking->total_bytes(), false
            );
            made.prompt.hovered = state.shown ? state.shown->prompt.hovered : settings::no_control;
            made.prompt.pressed = state.shown ? state.shown->prompt.pressed : settings::no_control;
            state.shown = std::move(made);
            return;
        }
        // Unpacked whole. The mod played is changed once the run ends and
        // its archives are closed; any other is put in place on the next
        // frame, once the prompt says so.
        if (!state.target_played) {
            state.placing = true;
            state.shown = install::installing_prompt(
                state.incoming, state.unpacking->total_bytes(), state.unpacking->total_bytes(), true
            );
            return;
        }
        const auto& game_folder =
            options_.game_folders.empty() ? options_.game_dir : options_.game_folders.back();
        const auto check = check_picked_mod_folder(
            state.unpacking->staging(),
            game_folder,
            ModChoice{{}, {}, options_.accept_unimplemented_hacks, &preference_values_}
        );
        if (!check.refusal.empty()) {
            for (const auto& line : check.errors)
                log_line(state.package->file_name + ": " + line);
            state.unpacking->cancel();
            state.discarder.add(state.unpacking->discards());
            state.unpacking.reset();
            install::Problem refused{};
            refused.refusal = install::Refusal::profile_errors;
            refused.lines = check.errors;
            state.stage = ModInstallState::Stage::telling;
            state.show(install::refused_prompt(state.package->file_name, refused, false));
            state.finish_package();
            return;
        }
        install::PendingChange pending{};
        pending.mods = mods;
        pending.target = state.target;
        pending.change = state.change;
        pending.incoming = state.incoming;
        pending.replaced = state.expected;
        pending.file_name = state.package->file_name;
        pending.hold = state.unpacking->take_hold();
        install::set_pending_change(std::move(pending));
        log_line(state.package->file_name + ": put in place as the run ends");
        state.unpacking.reset();
        state.shown.reset();
        state.stage = ModInstallState::Stage::idle;
        state.finish_package();
        request_soft_restart();
        return;
    }
    namespace frontend_state = oa::ui::frontend_state;
    const bool settled = screen_ == Screen::main_menu && !frame_owned_by_package() &&
                         state_.state == frontend_state::state_id::main_menu &&
                         state_.pending_signal != frontend_state::signal_id::multiplayer &&
                         !saves_notice_shown() && oa::ui::frontend_dialogs::dialog_count() == 0 &&
                         engine_settings_dialog() == nullptr;
    if (!settled) {
        state.settled_frames = 0;
        return;
    }
    if (++state.settled_frames < kPromptMenuFrames || engine_settings_fonts() == nullptr)
        return;
    // A run nobody watches leaves the packages waiting for one someone does.
    if (UserFolderState::unwatched(options_.unattended) && !state.check_shows_prompts)
        return;
    if (state.discarder.busy())
        std::ignore = state.discarder.step();
    if (state.stage != ModInstallState::Stage::idle || state.shown)
        return;
    // What a change that waited for the last run did.
    if (auto outcome = install::take_change_outcome()) {
        const fs::path folder = outcome->change.mods / path_from_utf8(outcome->change.target);
        state.told_folder = folder;
        state.stage = ModInstallState::Stage::telling;
        if (!outcome->result.changed) {
            install::Problem problem{};
            problem.refusal = outcome->result.refusal;
            problem.detail = outcome->result.detail;
            state.show(
                install::refused_prompt(
                    outcome->change.file_name.empty() ? outcome->change.target
                                                      : outcome->change.file_name,
                    problem,
                    true
                )
            );
        } else {
            state.show(
                install::updated_prompt(
                    outcome->change.change,
                    outcome->change.incoming,
                    outcome->change.replaced,
                    folder,
                    outcome->result,
                    false
                )
            );
        }
        return;
    }
    const uint64_t now = SDL_GetTicks();
    if (now >= state.next_handoff_ms) {
        state.next_handoff_ms = now + kHandoffPollMs;
        take_handed_mod_files();
    }
    const auto file = install::take_mod_file();
    if (!file)
        return;
    state.file = *file;
    state.told_folder.clear();
    state.play_folder.clear();
    log_line("opening " + path_to_utf8(*file));
    // The platform may bring a file it opened into the game's own storage first.
    fs::path readable = *file;
    if (const PlatformHooks& hooks = platform_hooks(); hooks.take_opened_file != nullptr) {
        std::string copy;
        std::string why;
        if (!hooks.take_opened_file(hooks.context, path_to_utf8(*file).c_str(), &copy, &why)) {
            install::Problem problem{};
            problem.refusal = install::Refusal::unreadable;
            problem.detail = why;
            log_line(path_to_utf8(*file) + ": " + why);
            state.stage = ModInstallState::Stage::telling;
            state.show(install::refused_prompt(path_to_utf8(file->filename()), problem, false));
            install::finish_mod_file();
            return;
        }
        if (!copy.empty() && copy != path_to_utf8(*file)) {
            readable = path_from_utf8(copy);
            state.opened_copy = readable;
        }
    }
    install::PackageOptions options{};
    options.accept_unimplemented_hacks = options_.accept_unimplemented_hacks;
    options.preferences = &preference_values_;
    options.game_folder =
        options_.game_folders.empty() ? options_.game_dir : options_.game_folders.back();
    auto opened = install::open_package(readable, options);
    if (!opened.package) {
        const std::string name = path_to_utf8(file->filename());
        log_line(
            name + ": " + install::refusal_text(opened.problem) +
            (opened.problem.detail.empty() ? "" : " (" + opened.problem.detail + ")")
        );
        for (const auto& line : opened.problem.lines)
            log_line(name + ": " + line);
        state.stage = ModInstallState::Stage::telling;
        state.show(install::refused_prompt(name, opened.problem, false));
        state.finish_package();
        return;
    }
    state.package = std::move(opened.package);
    // The texts name the file the player opened, not the platform's copy.
    state.package->file_name = path_to_utf8(file->filename());
    for (const auto& warning : state.package->warnings)
        log_line(state.package->file_name + ": " + warning);
    if (state.package->backup_left_out)
        log_line(state.package->file_name + ": its own .backup folder is left out");
    state.incoming = install::incoming_of(*state.package->profile);
    state.plan = install::plan_install(state.incoming, install::mods_folder_hooks(mods));
    const auto played = [this](const fs::path& folder) {
        return options_.game_folders.size() > 1 &&
               same_folder(kept_path(folder), kept_path(options_.game_folders.front()));
    };
    switch (state.plan.kind) {
    case install::PlanKind::install:
        state.target = state.plan.target;
        state.change = install::Change::install;
        state.expected = {};
        state.target_played = false;
        answer_mod_install_prompt(-1);
        return;
    case install::PlanKind::refuse: {
        install::Problem problem{};
        problem.refusal = install::Refusal::no_free_folder;
        state.stage = ModInstallState::Stage::telling;
        state.show(install::refused_prompt(state.package->file_name, problem, false));
        state.finish_package();
        return;
    }
    default:
        state.stage = ModInstallState::Stage::asking;
        state.show(
            install::question_prompt(
                state.plan,
                state.incoming,
                state.package->file_name,
                mods,
                !state.plan.target.empty() && played(mods / path_from_utf8(state.plan.target))
            )
        );
        return;
    }
}

void Runtime::answer_mod_install_prompt(int32_t button) {
    auto& state = mod_install_state();
    const fs::path mods = user_folder_ / std::string(user_mods_folder_name);
    install::Answer answer = install::Answer::ok;
    if (button >= 0) {
        if (!state.shown || static_cast<std::size_t>(button) >= state.shown->answers.size())
            return;
        answer = state.shown->answers[static_cast<std::size_t>(button)];
    }
    const auto start_unpacking = [&] {
        const fs::path folder = mods / path_from_utf8(state.target);
        state.target_played =
            options_.game_folders.size() > 1 &&
            same_folder(kept_path(folder), kept_path(options_.game_folders.front()));
        state.unpacking = std::make_unique<install::Unpacking>();
        install::Problem problem{};
        if (!state.unpacking->start(*state.package, mods, state.target, state.expected, problem)) {
            state.discarder.add(state.unpacking->discards());
            state.unpacking.reset();
            log_line(
                state.package->file_name + ": " + install::refusal_text(problem) +
                (problem.detail.empty() ? "" : " (" + problem.detail + ")")
            );
            state.stage = ModInstallState::Stage::telling;
            state.show(install::refused_prompt(state.package->file_name, problem, false));
            state.finish_package();
            return;
        }
        log_line(
            state.package->file_name + ": unpacking into " +
            path_to_utf8(state.unpacking->staging())
        );
        state.stage = ModInstallState::Stage::unpacking;
        state.placing = false;
        state.show(
            install::installing_prompt(state.incoming, 0, state.unpacking->total_bytes(), false)
        );
    };
    switch (state.stage) {
    case ModInstallState::Stage::idle:
        // A plan that asks nothing.
        if (button < 0 && state.package)
            start_unpacking();
        return;
    case ModInstallState::Stage::asking:
        switch (answer) {
        case install::Answer::replace:
            state.target = state.plan.target;
            state.change = install::Change::replace;
            state.expected = state.plan.installed;
            start_unpacking();
            return;
        case install::Answer::reinstall:
            state.target = state.plan.target;
            state.change = install::Change::reinstall;
            state.expected = state.plan.installed;
            start_unpacking();
            return;
        case install::Answer::alongside:
            state.target = state.plan.alongside;
            state.change = install::Change::install;
            state.expected = {};
            start_unpacking();
            return;
        default:
            log_line(state.package ? state.package->file_name + ": cancelled" : "cancelled");
            play_ui_sound(kCloseSound, 0);
            state.shown.reset();
            state.stage = ModInstallState::Stage::idle;
            state.finish_package();
            return;
        }
    case ModInstallState::Stage::unpacking:
        // CANCEL stops the unpacking; nothing is changed.
        if (answer != install::Answer::cancel || state.placing || !state.unpacking)
            return;
        state.unpacking->cancel();
        state.discarder.add(state.unpacking->discards());
        state.unpacking.reset();
        log_line(state.package ? state.package->file_name + ": unpacking cancelled" : "cancelled");
        play_ui_sound(kCloseSound, 0);
        state.shown.reset();
        state.stage = ModInstallState::Stage::idle;
        state.finish_package();
        return;
    case ModInstallState::Stage::telling:
        switch (answer) {
        case install::Answer::open_folder: {
            const FolderOpening opening = open_player_folder(state.told_folder);
            if (state.shown)
                state.shown->prompt.failure = opening.opened ? std::string() : opening.reason;
            return;
        }
        case install::Answer::play_now: {
            std::string refusal;
            if (!switch_to_mod_folder(state.play_folder, refusal)) {
                if (state.shown)
                    state.shown->prompt.failure =
                        std::string(oa::data::languages::interface_text(refusal));
                return;
            }
            state.shown.reset();
            state.stage = ModInstallState::Stage::idle;
            return;
        }
        default:
            play_ui_sound(kCloseSound, 0);
            state.shown.reset();
            state.stage = ModInstallState::Stage::idle;
            return;
        }
    }
}

bool Runtime::switch_to_mod_folder(const fs::path& folder, std::string& refusal) {
    auto& state = engine_settings_state();
    settings::EngineSettings chosen = state.current;
    chosen.mod_folder = kept_path(folder);
    const auto& game_folder =
        options_.game_folders.empty() ? options_.game_dir : options_.game_folders.back();
    auto check = check_picked_mod_folder(
        folder,
        game_folder,
        ModChoice{{}, {}, options_.accept_unimplemented_hacks, &preference_values_}
    );
    if (check.refusal.empty() && check.without_profile)
        if (auto problem =
                side_data_problem_over(folder, game_folder, data_word_for(chosen.language));
            !problem.empty()) {
            check.refusal = "That folder cannot be played; the log says why.";
            check.errors.push_back(std::move(problem));
        }
    for (const auto& line : check.errors)
        std::cerr << "open-annihilation: the mod chosen: " << line << '\n';
    if (!check.refusal.empty()) {
        refusal = check.refusal;
        return false;
    }
    const settings::EngineSettings opened = state.current;
    apply_engine_settings(chosen);
    if (const auto failure = save_engine_settings(opened, chosen, false)) {
        EngineSettingsState::report_failed_save(*this, *failure);
        refusal = *failure;
        return false;
    }
    request_soft_restart();
    return true;
}

bool Runtime::roll_back_mod_folder(settings::Dialog& dialog) {
    auto& state = engine_settings_state();
    const fs::path folder = path_from_utf8(dialog.roll_back_folder);
    const fs::path mods = folder.parent_path();
    const std::string target = path_to_utf8(folder.filename());
    const install::InstalledMod installed = install::read_installed_mod(folder);
    const auto backup = install::read_backup(folder);
    const std::string title = installed.name.empty() ? target : installed.name;
    // A refusal is told in a prompt over the dialog, whose text wraps, and
    // the dialog stays open under it.
    const auto refuse = [&](install::ModPrompt made, std::string_view why) {
        log_line(path_to_utf8(folder) + ": not rolled back: " + std::string(why));
        auto& install_state = mod_install_state();
        if (!install_state.shown && install_state.stage == ModInstallState::Stage::idle) {
            install_state.told_folder.clear();
            install_state.play_folder.clear();
            install_state.stage = ModInstallState::Stage::telling;
            install_state.show(std::move(made));
        }
        std::ignore = settings::set_folder_notice(dialog, {});
        return false;
    };
    if (!backup || backup->kind != install::FolderKind::mod)
        return refuse(
            install::roll_back_failed_prompt(title), "it keeps no version that can be read"
        );
    // The kept version must still be playable: an engine since may refuse it.
    const auto& game_folder =
        options_.game_folders.empty() ? options_.game_dir : options_.game_folders.back();
    const fs::path kept = entry_without_case(folder, install::backup_folder_name)
                              .value_or(folder / std::string(install::backup_folder_name));
    const auto check = check_picked_mod_folder(
        kept,
        game_folder,
        ModChoice{{}, {}, options_.accept_unimplemented_hacks, &preference_values_}
    );
    for (const auto& line : check.errors)
        log_line(path_to_utf8(kept) + ": " + line);
    if (!check.refusal.empty())
        return refuse(
            install::roll_back_refused_prompt(
                title,
                install::version_label(
                    backup->version, backup->revision, backup->version == installed.version
                ),
                oa::data::languages::interface_text(check.refusal)
            ),
            "the version it keeps cannot be played"
        );
    const bool played = options_.game_folders.size() > 1 &&
                        same_folder(kept_path(folder), kept_path(options_.game_folders.front()));
    install::PendingChange pending{};
    pending.mods = mods;
    pending.target = target;
    pending.change = install::Change::roll_back;
    pending.incoming = {backup->id, backup->name, backup->version, backup->revision};
    pending.replaced = installed;
    if (played) {
        // As SWITCH does: the dialog's settings are kept and saved, it
        // closes, and the run ends for the folder to change.
        take_renderer_retry(dialog);
        keep_renderer_records();
        apply_engine_settings(dialog.chosen);
        const auto failure = save_engine_settings(dialog.opened, dialog.chosen, dialog.restored);
        state.last_page = dialog.page;
        state.last_developer_list = dialog.developer;
        state.dialog.reset();
        if (failure) {
            EngineSettingsState::report_failed_save(*this, *failure);
            return true;
        }
        install::set_pending_change(std::move(pending));
        log_line(path_to_utf8(folder) + ": rolled back as the run ends");
        request_soft_restart();
        return true;
    }
    install::ChangeOptions options{};
    options.expected = installed;
    options.hooks.wait = wait_pumping;
    const install::ChangeResult result =
        install::commit_change(mods, target, install::Change::roll_back, options);
    mod_install_state().discarder.add(result.discards);
    if (!result.changed)
        return refuse(install::roll_back_failed_prompt(title), result.detail);
    log_line(path_to_utf8(folder) + ": rolled back to " + backup->version);
    list_offered_mods();
    dialog.mod_names = state.mod_names;
    dialog.mod_folders = state.mod_folders;
    dialog.mod_details = state.mod_details;
    std::ignore = settings::set_folder_notice(dialog, {});
    return false;
}

} // namespace oa::app
