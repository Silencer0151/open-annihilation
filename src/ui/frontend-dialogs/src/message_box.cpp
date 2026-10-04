// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// MSGBOX.GUI: a wrapped message with an optional OK button.
#include "dialog_internal.hpp"
#include "oa/data/defs/layout.hpp"

#include "oa/formats/fnt.hpp"
#include "oa/ui/decoded.hpp"
#include "oa/ui/gui_input/gadget_panel.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace oa::ui::frontend_dialogs {

namespace {

/// Gives a button the quick key its caption takes, as the gadget engine
/// assigns one when a caption is set (ui::gui_input::caption_quick_key): the
/// first letter of the caption no other button's key holds.
///
/// @param[in,out] gadgets the dialog's records
/// @param index the button's record
void assign_caption_quick_key(std::vector<ui::gui_layout::Gadget>& gadgets, std::size_t index) {
    auto* button = std::get_if<ui::gui_layout::ButtonFields>(&gadgets[index].fields);
    if (button == nullptr)
        return;
    const auto rule = ui::gui_input::caption_quick_key(
        static_cast<uint32_t>(gadgets[index].common.attributes), button->stages, button->text
    );
    if (rule == ui::gui_input::CaptionQuickKey::keep)
        return;
    button->quick_key = 0;
    if (rule == ui::gui_input::CaptionQuickKey::none)
        return;
    std::vector<int8_t> taken;
    for (const auto& other : gadgets)
        if (const auto* fields = std::get_if<ui::gui_layout::ButtonFields>(&other.fields))
            taken.push_back(fields->quick_key);
    button->quick_key = static_cast<int8_t>(ui::gui_input::free_quick_key(button->text, taken));
}

constexpr const char* kLayout = "msgbox.gui";
constexpr const char* kLineFont = "COMIX";  // the active FNT font while the frontend runs
constexpr std::size_t kMessageBytes = 0xfe; // bounded copy before the line split
constexpr int16_t kFirstLineY = 0x14;
constexpr int16_t kLineGap = 5;      // added to the font height per line
constexpr int16_t kRowHeight = 0x19; // panel height per line
constexpr int16_t kPanelPadding = 0x28;
constexpr int16_t kFitPadding = 0x14; // added to the longest line
constexpr int16_t kButtonInset = 0xf;
constexpr char kLineDelimiter = '\n';
constexpr std::size_t kButtonRecord = 1; // OK is the only authored record

} // namespace

bool open_message_box(
    app::ScreenContext* ctx,
    std::string_view text,
    int32_t width,
    int32_t show_ok,
    int32_t fit_width
) {
    auto* dialog = dialog_push(
        ctx,
        DialogKind::message_box,
        oa::data::defs::gui_path(kLayout).c_str(),
        nullptr,
        panel_flag::shade_below
    );
    if (dialog == nullptr)
        return false;
    auto& gadgets = dialog->resources.layout.gadgets;
    int32_t font_height = 0;
    try {
        font_height = ui::decoded::require(
                          formats::fnt::load_named_fnt(*ctx->assets, kLineFont, ""), kLineFont
        )
                          .nominal_height;
    } catch (const std::exception&) {
        dialog_close(*dialog);
        return false;
    }
    if (gadgets.size() <= kButtonRecord) {
        dialog_close(*dialog);
        return false;
    }
    auto wrapped = dialog_wrap(*dialog, dialog_translate(text), width);
    if (wrapped.size() > kMessageBytes)
        wrapped.resize(kMessageBytes);
    const auto line_step = static_cast<int16_t>(kLineGap + font_height);
    const auto first_label = gadgets.size();
    int16_t y = kFirstLineY;
    int32_t lines = 0;
    std::size_t at = 0;
    while (at < wrapped.size()) {
        while (at < wrapped.size() && wrapped[at] == kLineDelimiter)
            ++at;
        if (at >= wrapped.size())
            break;
        auto end = wrapped.find(kLineDelimiter, at);
        if (end == std::string::npos)
            end = wrapped.size();
        const std::string_view line(wrapped.data() + at, end - at);
        dialog_add_label(*dialog, line, 0, y, kLabelWidthToRootEdge, kLabelCentred);
        y = static_cast<int16_t>(y + line_step);
        ++lines;
        at = end;
    }
    auto& root = gadgets.front().common;
    auto panel_width = static_cast<int16_t>(width);
    if (fit_width != 0) {
        int32_t widest = 0;
        for (std::size_t index = first_label; index < gadgets.size(); ++index)
            if (const auto* label =
                    std::get_if<ui::gui_layout::LabelFields>(&gadgets[index].fields))
                widest = std::max(widest, dialog_text_width(*dialog, label->text));
        panel_width = static_cast<int16_t>(widest + kFitPadding);
    }
    root.width = panel_width;
    root.height = static_cast<int16_t>(
        gadgets[kButtonRecord].common.height + lines * kRowHeight + kPanelPadding
    );
    // Centred on the screen; the placement resolves the marker.
    dialog->authored_x = kCentreMarker;
    dialog->authored_y = kCentreMarker;
    for (auto& gadget : gadgets) {
        if (gadget.common.type != ui::gui_layout::GadgetType::label)
            continue;
        gadget.common.width = root.width;
        gadget.common.attributes = kLabelCentred;
    }
    auto& button = gadgets[kButtonRecord].common;
    if (show_ok == 0) {
        button.active = 0;
    } else {
        button.y = static_cast<int16_t>(root.height - button.height - kButtonInset);
        button.x = static_cast<int16_t>(root.width - button.width - kButtonInset);
        if (auto* panel = std::get_if<ui::gui_layout::PanelFields>(&gadgets.front().fields)) {
            panel->carriage_return_default = kDefaultButton;
            panel->escape_default = kDefaultButton;
        }
    }
    dialog_place(*dialog, dialog_screen_width(ctx), dialog_screen_height(ctx));
    return true;
}

bool open_continue_watching(app::ScreenContext* ctx, void* context, void (*chosen)(void*, bool)) {
    auto* dialog = dialog_push(
        ctx,
        DialogKind::continue_watching,
        oa::data::defs::gui_path("yesorno.gui").c_str(),
        nullptr,
        panel_flag::centre | panel_flag::shade_below
    );
    if (dialog == nullptr)
        return false;
    dialog->choice_context = context;
    dialog->choice_callback = chosen;
    auto& gadgets = dialog->resources.layout.gadgets;
    for (const auto& [name, caption] :
         {std::pair<std::string_view, std::string_view>{"CHOICE1", "Yes"}, {"CHOICE2", "No"}}) {
        const auto index = dialog_find(*dialog, name);
        if (index == kNoGadget)
            continue;
        auto& gadget = gadgets[static_cast<std::size_t>(index)];
        if (auto* button = std::get_if<ui::gui_layout::ButtonFields>(&gadget.fields)) {
            button->text = dialog_translate(caption);
            assign_caption_quick_key(gadgets, static_cast<std::size_t>(index));
        }
    }
    for (auto& gadget : gadgets)
        if (gadget.common.name == "TITLE")
            if (auto* label = std::get_if<ui::gui_layout::LabelFields>(&gadget.fields))
                label->text = dialog_translate("You're out!  Continue Watching?");
    auto* panel = std::get_if<ui::gui_layout::PanelFields>(&dialog_root(*dialog).fields);
    if (panel != nullptr) {
        panel->carriage_return_default = "CHOICE1";
        panel->escape_default = "CHOICE2";
    }
    return true;
}

void continue_watching_click(Dialog& dialog, std::string_view control) {
    dialog_play_sound("BigButton");
    if (control != "CHOICE1" && control != "CHOICE2")
        return;
    const auto callback = dialog.choice_callback;
    void* context = dialog.choice_context;
    const bool keep = control == "CHOICE1";
    dialog_close(dialog);
    if (callback != nullptr)
        callback(context, keep);
}

void message_box_click(app::ScreenContext*, Dialog& dialog, std::string_view) {
    dialog_close(dialog);
}

} // namespace oa::ui::frontend_dialogs
