// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Notices for the frontend entries the game data cannot support, such as
// skirmish in the Total Annihilation demo (1997), and the web link the
// notice's website button opens; and the main menu's notice of a new
// renderer record.
#include "oa/app/game_directory.hpp"
#include "oa/app/runtime.hpp"
#include "render_host.hpp"
#include "render_run.hpp"
#include "web_link_state.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/ui/frontend_state/app_modes.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace oa::app {

namespace {

namespace dialogs = oa::ui::frontend_dialogs;

// Wrap width of the message box shown when the notice cannot be drawn.
constexpr int32_t kMessageWidth = 0x140;

[[nodiscard]] std::string_view text_or_empty(const char* text) {
    return text == nullptr ? std::string_view{} : std::string_view(text);
}

/// What the main menu says of an accelerated-unusable record.
constexpr std::string_view kCardUnusableNotice =
    "The graphics card could not be used, so the processor draws the game. You can try it "
    "again under Graphics in the OA settings.";
/// What it says of a failed-driver record, whatever tier the next driver runs.
constexpr std::string_view kDriverFailedNotice =
    "A graphics driver failed, so the game now uses another one. You can try it again under "
    "Graphics in the OA settings.";
/// Frames in a row the main menu shows before its notice: a start that
/// passes the main menu at its first update shows none.
constexpr uint32_t kNoticeMenuFrames = 2;

/// Tells whether the main menu shows as itself: its own frame, not a screen
/// package's, with no multiplayer signal waiting to leave it, as -n leaves
/// one until the next pass of the frontend, which may be many frames on.
///
/// @param screen the screen shown
/// @param state the frontend's state
/// @param owned a screen package owns the frame
/// @return true when the main menu shows and stays
[[nodiscard]] bool
main_menu_settled(Screen screen, const oa::ui::frontend_state::State& state, bool owned) noexcept {
    return screen == Screen::main_menu && !owned &&
           state.state == oa::ui::frontend_state::state_id::main_menu &&
           state.pending_signal != oa::ui::frontend_state::signal_id::multiplayer;
}

} // namespace

void Runtime::show_missing_content(MissingContent missing) {
    std::string_view text;
    switch (missing) {
    case MissingContent::skirmish_maps:
        text = "This game includes no skirmish maps, so skirmish is not available.";
        break;
    case MissingContent::multiplayer_maps:
        text = "This game includes no multiplayer maps, so multiplayer is not available.";
        break;
    case MissingContent::further_missions:
        text = "Campaign complete. This game includes no further missions.";
        break;
    }
    auto context = screen_context();
    const auto closed = [](void* runtime, dialogs::NoticeChoice choice) {
        auto& owner = *static_cast<Runtime*>(runtime);
        if (choice == dialogs::NoticeChoice::website)
            owner.open_web_link(project_website_address);
        else
            owner.notice_returns_to_main_menu_ = true;
    };
    if (dialogs::open_notice(
            &context, text, web_link_caption(project_website_address), this, closed
        ))
        return;
    show_frontend_message(
        translate_ui(text), kMessageWidth, entry::message_show_ok, entry::message_fit_width
    );
}

void Runtime::run_pending_notice_return() {
    if (!notice_returns_to_main_menu_)
        return;
    notice_returns_to_main_menu_ = false;
    if (screen_ == Screen::main_menu)
        return;
    frontend::reset_to_main_menu(state_, *this);
    set_app_mode(state_, frontend::mode_id::frontend);
    step(frontend::Step::reload_unit_overrides, state_);
    frontend::dispatch(state_, *this, frontend_states_);
}

void Runtime::tell_renderer_records() {
    if (!render_run_ || render_run_->host == nullptr)
        return;
    auto& run = *render_run_;
    if (!main_menu_settled(screen_, state_, frame_owned_by_package())) {
        run.main_menu_frames = 0;
        return;
    }
    if (++run.main_menu_frames < kNoticeMenuFrames || dialogs::dialog_count() != 0 ||
        engine_settings_dialog() != nullptr)
        return;
    auto& records = run.host->records();
    // Most frames have nothing to tell, and cost no more than this.
    if (!renderer_state::has_untold_record(records.records()))
        return;
    std::vector<std::string> passed_over;
    for (const auto& noted : run.notices_noted)
        passed_over.push_back(noted.driver);
    const auto notice = renderer_state::next_notice(records.records(), passed_over);
    if (!notice)
        return;
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver == nullptr)
        driver = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
    const bool unwatched =
        options_.unattended ||
        unattended_environment(text_or_empty(SDL_getenv("CI")), text_or_empty(driver));
    const auto action = renderer_state::notice_action(unwatched, options_.check_renderer_ladder);
    if (action != renderer_state::NoticeAction::show)
        run.notices_noted.push_back(*notice);
    if (action != renderer_state::NoticeAction::note) {
        // Later starts that merely honour the record show nothing.
        std::ignore = renderer_state::mark_told(records.records(), notice->driver);
        std::ignore = records.write_records();
    }
    if (action != renderer_state::NoticeAction::show)
        return;
    ++run.notices_shown;
    const std::string_view text = notice->kind == renderer_state::NoticeKind::failed_driver
                                      ? kDriverFailedNotice
                                      : kCardUnusableNotice;
    auto context = screen_context();
    const auto closed = [](void* runtime, dialogs::NoticeChoice choice) {
        if (choice == dialogs::NoticeChoice::website)
            static_cast<Runtime*>(runtime)->open_web_link(project_website_address);
    };
    if (dialogs::open_notice(
            &context, text, web_link_caption(project_website_address), this, closed
        ))
        return;
    show_frontend_message(
        translate_ui(text), kMessageWidth, entry::message_show_ok, entry::message_fit_width
    );
}

void Runtime::destroy_web_link_state(WebLinkState* state) noexcept {
    delete state;
}

void Runtime::open_web_link(std::string_view address) {
    if (!web_links_ || web_links_->hooks.open == nullptr)
        return;
    const auto& hooks = web_links_->hooks;
    const std::string text(address);
    if (!hooks.open(hooks.context, text.c_str()))
        status_ = "cannot open " + text + ": " + SDL_GetError();
}

void Runtime::choose_web_links() {
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver == nullptr)
        driver = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
    const bool unwatched =
        options_.unattended ||
        unattended_environment(text_or_empty(SDL_getenv("CI")), text_or_empty(driver));
    if (!web_links_)
        web_links_.reset(new WebLinkState());
    web_links_->hooks = unwatched ? recorded_web_links(web_links_->requests) : browser_web_links();
}

} // namespace oa::app
