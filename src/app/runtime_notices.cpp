// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Notices for the frontend entries the game data cannot support, such as
// skirmish in the Total Annihilation demo (1997), and the web link the
// notice's website button opens.
#include "oa/app/game_directory.hpp"
#include "oa/app/runtime.hpp"
#include "web_link_state.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/ui/frontend_state/app_modes.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <string_view>

namespace oa::app {

namespace {

namespace dialogs = oa::ui::frontend_dialogs;

// Wrap width of the message box shown when the notice cannot be drawn.
constexpr int32_t kMessageWidth = 0x140;

[[nodiscard]] std::string_view text_or_empty(const char* text) {
    return text == nullptr ? std::string_view{} : std::string_view(text);
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
