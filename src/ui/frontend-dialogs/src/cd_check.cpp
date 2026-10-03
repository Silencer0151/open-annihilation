// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// CDCHECK.GUI: the "insert the Play CD" prompt shown before a campaign
// outcome screen when the disc is missing.
#include "dialog_internal.hpp"
#include "oa/data/defs/layout.hpp"

#include <string>

namespace oa::ui::frontend_dialogs {

namespace {

constexpr const char* kLayout = "cdcheck.gui";

} // namespace

bool open_cd_check(app::ScreenContext* ctx) {
    auto* dialog = dialog_push(
        ctx,
        DialogKind::cd_check,
        oa::data::defs::gui_path(kLayout).c_str(),
        nullptr,
        panel_flag::centre | panel_flag::first_draw
    );
    if (dialog == nullptr)
        return false;
    dialog_place(*dialog, dialog_screen_width(ctx), dialog_screen_height(ctx));
    return true;
}

// The OK handler lives with the campaign screens; the host runs it and
// reports whether the prompt may go.
void cd_check_dialog_click(app::ScreenContext*, Dialog& dialog, std::string_view control) {
    const auto& host = dialog_stack().host;
    if (host.cd_check_click == nullptr)
        return;
    const std::string name(control);
    if (host.cd_check_click(host.context, name.c_str()) != 0)
        dialog_close(dialog);
}

} // namespace oa::ui::frontend_dialogs
