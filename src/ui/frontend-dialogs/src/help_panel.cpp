// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// HELP.GUI: three pages of keyboard commands read from gamedata/help.tdf.
#include "dialog_internal.hpp"

#include "oa/formats/tdf.hpp"

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

namespace oa::ui::frontend_dialogs {

namespace {

constexpr const char* kLayout = "guis/help.gui";
constexpr const char* kBackdrop = "bitmaps/dhelp.pcx";
constexpr const char* kTextFile = "gamedata/help.tdf";
constexpr const char* kSection = "Help";
constexpr const char* kLineKey = "Line%d";
constexpr const char* kPageButton = "Page";
constexpr int32_t kLinesPerPage = 0x11;
constexpr int16_t kFirstLineY = 0x32;
constexpr int16_t kLineStep = 0x12;
constexpr int16_t kKeyColumnX = 0x28;
constexpr int16_t kKeyColumnWidth = 0x4e;
constexpr int16_t kTextColumnX = 0x7d;
constexpr int16_t kTextColumnWidth = 300;
constexpr std::size_t kLineBytes = 0x80;
constexpr std::size_t kKeyBytes = 0x10;
constexpr char kColumnSeparator = '|';

/// Adds one column label, drawn left-aligned.
///
/// @param[in,out] dialog The help panel; the label is appended to its records.
/// @param text Label text, translated before it is stored.
/// @param x Left edge relative to the panel, in pixels.
/// @param y Top edge relative to the panel, in pixels.
/// @param width Label width in pixels.
void add_column(Dialog& dialog, const char* text, int16_t x, int16_t y, int16_t width) {
    const auto added = dialog_add_label(dialog, dialog_translate(text), x, y, width, kLabelCentred);
    if (added != kNoGadget)
        dialog.resources.layout.gadgets[static_cast<std::size_t>(added)].common.attributes =
            kLabelLeftAligned;
}

/// Replaces the labels added after the loaded records with the Line entries of one page.
///
/// Each gamedata/help.tdf "Line<n>" entry splits at '|' into a key column
/// and a description column, 18 pixels per row.
///
/// @param ctx Screen context supplying the assets; null leaves the page empty.
/// @param[in,out] dialog The help panel; labels past its loaded records are replaced.
/// @param page Page number, from 0.
/// @param per_page Lines per page.
/// @quirk A help text that does not load leaves the page empty, as in 3.1c.
void help_fill_lines(app::ScreenContext* ctx, Dialog& dialog, int32_t page, int32_t per_page) {
    auto& gadgets = dialog.resources.layout.gadgets;
    const auto loaded = static_cast<std::size_t>(dialog.help_loaded_count) + 1U;
    if (gadgets.size() > loaded) {
        gadgets.resize(loaded);
        dialog.stages.resize(loaded);
    }
    if (ctx == nullptr || ctx->assets == nullptr)
        return;
    std::vector<uint8_t> bytes;
    try {
        bytes = ctx->assets->read(kTextFile).bytes;
    } catch (const std::exception&) {
        return;
    }
    formats::tdf::Document document;
    formats::tdf::document_init(&document);
    formats::tdf::ParseError error{};
    if (formats::tdf::parse_text(
            &document,
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<uint32_t>(bytes.size()),
            false,
            &error
        )) {
        const formats::tdf::Block* section = formats::tdf::find_child(document.root, kSection);
        int16_t y = kFirstLineY;
        const int32_t first = page * per_page;
        for (int32_t index = first; section != nullptr && index < first + per_page; ++index) {
            char key[kKeyBytes];
            std::snprintf(key, sizeof key, kLineKey, index);
            // The zero-filled buffer ends the description when a line holds
            // only the separator.
            char line[kLineBytes] = {};
            if (!formats::tdf::get_string(section, key, line, sizeof line, ""))
                continue;
            const char* description = nullptr;
            if (line[0] == kColumnSeparator) {
                line[0] = ' ';
                line[1] = '\0';
                description = &line[2];
            } else {
                char* separator = line + 1;
                while (*separator != '\0' && *separator != kColumnSeparator)
                    ++separator;
                description = *separator != '\0' ? separator + 1 : separator;
                *separator = '\0';
            }
            add_column(dialog, line, kKeyColumnX, y, kKeyColumnWidth);
            add_column(dialog, description, kTextColumnX, y, kTextColumnWidth);
            y = static_cast<int16_t>(y + kLineStep);
        }
    }
    formats::tdf::document_free(&document);
}

} // namespace

bool open_help(app::ScreenContext* ctx) {
    auto* dialog = dialog_push(
        ctx,
        DialogKind::help,
        kLayout,
        kBackdrop,
        panel_flag::beside_hud | panel_flag::shade_below | panel_flag::modal_backdrop |
            panel_flag::first_draw
    );
    if (dialog == nullptr)
        return false;
    dialog->help_loaded_count = static_cast<int32_t>(dialog->resources.layout.gadgets.size()) - 1;
    dialog->help_page = 0;
    help_fill_lines(ctx, *dialog, dialog->help_page, kLinesPerPage);
    dialog_place(*dialog, dialog_screen_width(ctx), dialog_screen_height(ctx));
    return true;
}

void help_click(app::ScreenContext* ctx, Dialog& dialog, std::string_view control) {
    if (control == kDefaultButton) {
        dialog_play_sound(kOptionsSound);
        dialog_close(dialog);
        return;
    }
    if (control != kPageButton)
        return;
    const auto page_button = dialog_find(dialog, kPageButton);
    if (page_button == kNoGadget)
        return;
    dialog_play_sound(kOptionsSound);
    dialog.help_page = dialog.stages[static_cast<std::size_t>(page_button)];
    help_fill_lines(ctx, dialog, dialog.help_page, kLinesPerPage);
}

} // namespace oa::ui::frontend_dialogs
