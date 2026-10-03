// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// DEMOMSG.GUI: a notice filling the screen over its own bitmap, with OK and a
// website button.
#include "dialog_internal.hpp"
#include "oa/data/defs/layout.hpp"

#include <string>
#include <variant>

namespace oa::ui::frontend_dialogs {

namespace {

constexpr const char* kLayout = "demomsg.gui";
constexpr const char* kBackdrop = "bitmaps/demotextbg.pcx";
constexpr const char* kWebsiteButton = "GotoWebsite";
constexpr const char* kButtonSound = "BigButton";
// The text column: lines start 50 pixels from the left, from 105 pixels down,
// 15 pixels apart, wrapped to 540 pixels.
constexpr int16_t kTextX = 50;
constexpr int16_t kFirstLineY = 105;
constexpr int16_t kLineStep = 15;
constexpr int32_t kTextWidth = 540;
constexpr char kLineDelimiter = '\n';

} // namespace

bool open_notice(
    app::ScreenContext* ctx,
    std::string_view text,
    std::string_view website_caption,
    void* context,
    void (*closed)(void* context, NoticeChoice choice)
) {
    auto* dialog = dialog_push(
        ctx,
        DialogKind::notice,
        oa::data::defs::gui_path(kLayout).c_str(),
        kBackdrop,
        panel_flag::centre | panel_flag::modal_backdrop | panel_flag::first_draw
    );
    if (dialog == nullptr)
        return false;
    dialog->choice_context = context;
    dialog->notice_closed = closed;
    if (const auto website = dialog_find(*dialog, kWebsiteButton);
        website != kNoGadget && !website_caption.empty())
        if (auto* button = std::get_if<ui::gui_layout::ButtonFields>(
                &dialog->resources.layout.gadgets[static_cast<std::size_t>(website)].fields
            ))
            button->text = std::string(website_caption);
    const auto wrapped = dialog_wrap(*dialog, dialog_translate(text), kTextWidth);
    int16_t y = kFirstLineY;
    std::size_t at = 0;
    while (at <= wrapped.size()) {
        auto end = wrapped.find(kLineDelimiter, at);
        if (end == std::string::npos)
            end = wrapped.size();
        const std::string_view line(wrapped.data() + at, end - at);
        if (!line.empty()) {
            const auto added =
                dialog_add_label(*dialog, line, kTextX, y, kTextWidth, kLabelCentred);
            if (added != kNoGadget)
                dialog->resources.layout.gadgets[static_cast<std::size_t>(added)]
                    .common.attributes = kLabelLeftAligned;
        }
        y = static_cast<int16_t>(y + kLineStep);
        at = end + 1;
    }
    dialog_place(*dialog, dialog_screen_width(ctx), dialog_screen_height(ctx));
    return true;
}

void notice_click(Dialog& dialog, std::string_view control) {
    NoticeChoice choice = NoticeChoice::ok;
    if (control == kWebsiteButton)
        choice = NoticeChoice::website;
    else if (control != kDefaultButton)
        return;
    dialog_play_sound(kButtonSound);
    const auto closed = dialog.notice_closed;
    void* context = dialog.choice_context;
    dialog_close(dialog);
    if (closed != nullptr)
        closed(context, choice);
}

} // namespace oa::ui::frontend_dialogs
