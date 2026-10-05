// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The chooser's layouts, painted in the Game files screen's look
// (folder_chooser.hpp). Each view is the header bar, then a column of three
// blocks: a fixed top (the title, the lines under it, the notice and, in the
// browser, the places and the verdict), a body that scrolls when the column
// does not fit (the rows), and a fixed bottom (the buttons). When even the
// fixed blocks leave the body too little room, the whole column scrolls.
// Every control is at least min_button_points high, and each one's box is a
// target of its own, in the order the focus moves.
#include "oa/ui/folder_chooser.hpp"

#include "oa/data/languages/interface_text.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace oa::ui::folder_chooser {

namespace {

namespace gf = oa::ui::game_files;
using gf::Colour;
using gf::Glyph;
using gf::Item;
using gf::ItemRole;
using gf::TextRole;

/// The main button's text: dark ink on the green fill.
constexpr Colour ink_colour{0x10, 0x12, 0x0d};
/// A plain button's text.
constexpr Colour button_text_colour{0xc9, 0xcd, 0xbf};

/// The sizes of one form, in points.
struct Metrics {
    float header_height{};     ///< the header bar
    float header_pad{};        ///< the header's side padding
    float badge{};             ///< the OA badge's side
    float header_text{};       ///< "OPEN ANNIHILATION  GAME FOLDER"
    float header_gap{};        ///< between the header's pieces
    float version_text{};      ///< the version
    float max_content_width{}; ///< the widest the column grows (0: no limit)
    float side_pad{};          ///< the column's least side padding
    float content_top{};       ///< above the column
    float content_bottom{};    ///< below the column
    float title{};             ///< the title
    float subtitle{};          ///< the lines under it
    float title_gap{};         ///< between the title and the next line
    float block_gap{};         ///< between the blocks of a view
    float banner_text{};       ///< a banner's text
    float banner_pad_x{};      ///< a banner's side padding
    float banner_pad_y{};      ///< a banner's top and bottom padding
    float banner_icon{};       ///< a banner's mark
    float heading{};           ///< a heading over a panel of rows
    float row_pad_x{};         ///< a row's side padding
    float row_pad_y{};         ///< a row's top and bottom padding
    float row_icon{};          ///< a row's mark
    float row_title{};         ///< a row's title
    float row_detail{};        ///< a row's other lines
    float row_gap{};           ///< between a row's columns
    float button_text{};       ///< a button's label
    float button_pad{};        ///< a button's side padding
    float button_min_width{};  ///< the main buttons' least width
    float button_gap{};        ///< between buttons
};

/// The tablet form (a 1280x800 window, the Steam Deck's screen).
constexpr Metrics tablet_metrics{
    .header_height = 56.0f,
    .header_pad = 24.0f,
    .badge = 26.0f,
    .header_text = 15.0f,
    .header_gap = 12.0f,
    .version_text = 12.0f,
    .max_content_width = 820.0f,
    .side_pad = 24.0f,
    .content_top = 24.0f,
    .content_bottom = 18.0f,
    .title = 24.0f,
    .subtitle = 14.0f,
    .title_gap = 8.0f,
    .block_gap = 16.0f,
    .banner_text = 13.5f,
    .banner_pad_x = 14.0f,
    .banner_pad_y = 8.0f,
    .banner_icon = 18.0f,
    .heading = 12.5f,
    .row_pad_x = 14.0f,
    .row_pad_y = 8.0f,
    .row_icon = 26.0f,
    .row_title = 15.0f,
    .row_detail = 12.5f,
    .row_gap = 12.0f,
    .button_text = 12.5f,
    .button_pad = 16.0f,
    .button_min_width = 150.0f,
    .button_gap = 12.0f,
};

/// The compact form (an 852x393-point phone window, or a small window), controls kept at
/// 44 points.
constexpr Metrics compact_metrics{
    .header_height = 48.0f,
    .header_pad = 14.0f,
    .badge = 22.0f,
    .header_text = 12.5f,
    .header_gap = 10.0f,
    .version_text = 11.0f,
    .max_content_width = 0.0f,
    .side_pad = 20.0f,
    .content_top = 10.0f,
    .content_bottom = 6.0f,
    .title = 19.0f,
    .subtitle = 12.0f,
    .title_gap = 3.0f,
    .block_gap = 8.0f,
    .banner_text = 11.0f,
    .banner_pad_x = 8.0f,
    .banner_pad_y = 4.0f,
    .banner_icon = 14.0f,
    .heading = 11.0f,
    .row_pad_x = 10.0f,
    .row_pad_y = 4.0f,
    .row_icon = 22.0f,
    .row_title = 13.0f,
    .row_detail = 10.5f,
    .row_gap = 8.0f,
    .button_text = 11.0f,
    .button_pad = 12.0f,
    .button_min_width = 124.0f,
    .button_gap = 8.0f,
};

/// A window whose shorter side is under this many points takes the compact sizes.
constexpr float compact_short_side_points = 600.0f;
/// The least height, in points, a scrolling body keeps before the whole column scrolls.
constexpr float least_body_points = 96.0f;
/// The space, in points, a label keeps from its button's edges when shortened.
constexpr float label_margin_points = 4.0f;
/// The space, in points, the header's version needs before "GAME FOLDER" shows beside the title.
constexpr float header_word_room_points = 40.0f;
/// The most lines the title takes.
constexpr int most_title_lines = 2;
/// The most lines the text under the title takes.
constexpr int most_subtitle_lines = 4;
/// The most lines a banner takes.
constexpr int most_banner_lines = 8;
/// The most lines a row's verdict takes.
constexpr int most_verdict_lines = 2;
/// The ellipsis that ends a shortened line.
constexpr std::string_view ellipsis = "\xE2\x80\xA6";

/// Returns one of the chooser's words in the installed language.
///
/// @param english the words as the source writes them
/// @return their translation, or the words themselves
std::string tr(std::string_view english) {
    return std::string(oa::data::languages::interface_text(english));
}

/// Returns the place after the UTF-8 character that starts at a place.
///
/// @param text the text
/// @param at the character's first byte
/// @return the place after the character
std::size_t next_character(std::string_view text, std::size_t at) noexcept {
    constexpr unsigned two_byte_mask = 0xe0u;
    constexpr unsigned two_byte_lead = 0xc0u;
    constexpr unsigned three_byte_mask = 0xf0u;
    constexpr unsigned three_byte_lead = 0xe0u;
    constexpr unsigned four_byte_mask = 0xf8u;
    constexpr unsigned four_byte_lead = 0xf0u;
    if (at >= text.size())
        return text.size();
    const auto lead = static_cast<unsigned char>(text[at]);
    std::size_t length = 1;
    if ((lead & two_byte_mask) == two_byte_lead)
        length = 2;
    else if ((lead & three_byte_mask) == three_byte_lead)
        length = 3;
    else if ((lead & four_byte_mask) == four_byte_lead)
        length = 4;
    return std::min(text.size(), at + length);
}

/// A text's look.
struct Style {
    float points{};                 ///< size in points
    bool bold{};                    ///< bold
    Colour colour{gf::text_colour}; ///< colour
    TextRole role{TextRole::body};  ///< its role
};

/// Items and targets laid out in a block, with boxes relative to the block's top.
struct Block {
    std::vector<Item> items{};  ///< in painting order
    std::vector<HitBox> hits{}; ///< the block's targets, in focus order
    int height{};               ///< pixels
};

/// The targets laid out so far: how many, and the focus order.
struct Tally {
    int32_t targets{};                 ///< targets added
    std::vector<Target> focus_order{}; ///< the enabled ones, in order
};

/// What every part of the layout reads: the model, the state, the measure and the form.
struct Context {
    const Model& model;              ///< what to show
    const UiState& state;            ///< what the chooser keeps between frames
    const TextMeasureHooks& measure; ///< the text measure
    const Metrics& metrics;          ///< the form's sizes
    float scale{1.0f};               ///< canvas pixels per point

    /// Converts points to whole canvas pixels.
    ///
    /// @param points the length
    /// @return pixels, rounded
    [[nodiscard]] int px(float points) const noexcept {
        return static_cast<int>(std::lround(points * scale));
    }

    /// Converts a text size in points to a pixel size.
    ///
    /// @param points the size
    /// @return pixels, at least 1
    [[nodiscard]] int font(float points) const noexcept { return std::max(1, px(points)); }

    /// Measures a line.
    ///
    /// @param text UTF-8 text
    /// @param size pixel size
    /// @param bold bold
    /// @return its width in pixels
    [[nodiscard]] int width(std::string_view text, int size, bool bold) const {
        constexpr double fallback_em_per_character = 0.55;
        if (text.empty())
            return 0;
        if (measure.width != nullptr)
            return measure.width(measure.context, text, size, bold);
        std::size_t characters = 0;
        for (std::size_t at = 0; at < text.size(); at = next_character(text, at))
            ++characters;
        return static_cast<int>(
            std::ceil(fallback_em_per_character * size * static_cast<double>(characters))
        );
    }

    /// Gives a line's height.
    ///
    /// @param size pixel size
    /// @param bold bold
    /// @return pixels
    [[nodiscard]] int line(int size, bool bold) const {
        constexpr double fallback_line_em = 1.25;
        if (measure.line_height != nullptr)
            return std::max(1, measure.line_height(measure.context, size, bold));
        return static_cast<int>(std::ceil(fallback_line_em * size));
    }

    /// Shortens a line to a width, ending it with an ellipsis.
    ///
    /// @param text the line
    /// @param size pixel size
    /// @param bold bold
    /// @param room the width, pixels
    /// @return the line itself when it fits, else its longest start that fits with the
    ///     ellipsis; empty when not even the ellipsis fits
    [[nodiscard]] std::string fit(std::string_view text, int size, bool bold, int room) const {
        if (width(text, size, bold) <= room)
            return std::string(text);
        if (width(ellipsis, size, bold) > room)
            return {};
        std::vector<std::size_t> ends;
        for (std::size_t at = 0; at < text.size();) {
            at = next_character(text, at);
            ends.push_back(at);
        }
        std::size_t low = 0;
        std::size_t high = ends.size();
        std::string best(ellipsis);
        while (low < high) {
            const std::size_t middle = (low + high + 1) / 2;
            std::string candidate(text.substr(0, ends[middle - 1]));
            while (!candidate.empty() && candidate.back() == ' ')
                candidate.pop_back();
            candidate += ellipsis;
            if (width(candidate, size, bold) <= room) {
                best = std::move(candidate);
                low = middle;
            } else {
                high = middle - 1;
            }
        }
        return best;
    }

    /// Breaks text into lines no wider than a width, at spaces (and after a path's separators
    /// when a word is wider than a line); '\n' starts a new line.
    ///
    /// @param text the text
    /// @param size pixel size
    /// @param bold bold
    /// @param room the width, pixels
    /// @param most_lines the most lines (0: no limit); the last is shortened with an ellipsis
    /// @return the lines
    [[nodiscard]] std::vector<std::string>
    wrap(std::string_view text, int size, bool bold, int room, int most_lines) const {
        std::vector<std::string> lines;
        if (room <= 0 || text.empty())
            return lines;
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t end = std::min(text.find('\n', start), text.size());
            const std::string_view paragraph = text.substr(start, end - start);
            std::string current;
            std::size_t at = 0;
            while (at < paragraph.size()) {
                const std::size_t space = std::min(paragraph.find(' ', at), paragraph.size());
                std::string_view word = paragraph.substr(at, space - at);
                at = space + 1;
                if (word.empty())
                    continue;
                if (!current.empty()) {
                    std::string joined = current + " " + std::string(word);
                    if (width(joined, size, bold) <= room) {
                        current = std::move(joined);
                        continue;
                    }
                    lines.push_back(std::move(current));
                    current.clear();
                }
                while (width(word, size, bold) > room) {
                    const std::size_t piece = breaking_point(word, size, bold, room);
                    lines.emplace_back(word.substr(0, piece));
                    word.remove_prefix(piece);
                }
                current = std::string(word);
            }
            if (!current.empty())
                lines.push_back(std::move(current));
            if (end >= text.size())
                break;
            start = end + 1;
        }
        if (most_lines > 0 && lines.size() > static_cast<std::size_t>(most_lines)) {
            std::string rest = lines[static_cast<std::size_t>(most_lines) - 1];
            for (std::size_t index = static_cast<std::size_t>(most_lines); index < lines.size();
                 ++index)
                rest += " " + lines[index];
            lines.resize(static_cast<std::size_t>(most_lines));
            lines.back() = fit(rest + std::string(ellipsis), size, bold, room);
        }
        return lines;
    }

    /// Finds where to break a word wider than a line: after the last path separator that
    /// fits, else after the last character that fits, at least one character.
    ///
    /// @param word the word
    /// @param size pixel size
    /// @param bold bold
    /// @param room the width, pixels
    /// @return the bytes of the first piece
    [[nodiscard]] std::size_t
    breaking_point(std::string_view word, int size, bool bold, int room) const {
        std::size_t fits = next_character(word, 0);
        std::size_t separator = 0;
        for (std::size_t at = fits; at < word.size();) {
            const std::size_t next = next_character(word, at);
            if (width(word.substr(0, next), size, bold) > room)
                break;
            fits = next;
            if (word[next - 1] == '/' || word[next - 1] == '\\')
                separator = next;
            at = next;
        }
        if (fits < word.size() && separator > 0)
            return separator;
        return fits;
    }
};

/// Adds a text item, wrapped to a column.
///
/// @param context the layout's context
/// @param[in,out] block the block
/// @param x the column's left, pixels
/// @param y the text's top, pixels
/// @param room the column's width, pixels
/// @param text the text
/// @param style its look
/// @param most_lines the most lines (0: no limit)
/// @param role the item's role
/// @return the text's height, pixels; 0 for no text
int add_text(
    const Context& context,
    Block& block,
    int x,
    int y,
    int room,
    std::string_view text,
    const Style& style,
    int most_lines = 0,
    ItemRole role = ItemRole::text
) {
    if (text.empty() || room <= 0)
        return 0;
    const int size = context.font(style.points);
    std::vector<std::string> lines = context.wrap(text, size, style.bold, room, most_lines);
    lines.erase(
        std::remove_if(
            lines.begin(), lines.end(), [](const std::string& line) { return line.empty(); }
        ),
        lines.end()
    );
    if (lines.empty())
        return 0;
    Item item{};
    item.role = role;
    item.text = style.role;
    item.pixel_size = size;
    item.bold = style.bold;
    item.colour = style.colour;
    item.box = {x, y, room, context.line(size, style.bold) * static_cast<int>(lines.size())};
    item.lines = std::move(lines);
    block.items.push_back(std::move(item));
    return block.items.back().box.height;
}

/// Adds one line of text, shortened to fit, centred on a middle line.
///
/// @param context the layout's context
/// @param[in,out] block the block
/// @param x its left, pixels (its right with `from_right`)
/// @param middle the line's middle, pixels
/// @param room the most width, pixels
/// @param text the text
/// @param style its look
/// @param from_right `x` is the line's right edge
/// @param role the item's role
/// @return the width taken, pixels; 0 for none
int add_line(
    const Context& context,
    Block& block,
    int x,
    int middle,
    int room,
    std::string_view text,
    const Style& style,
    bool from_right = false,
    ItemRole role = ItemRole::text
) {
    if (text.empty() || room <= 0)
        return 0;
    const int size = context.font(style.points);
    std::string line = context.fit(text, size, style.bold, room);
    if (line.empty())
        return 0;
    const int line_height = context.line(size, style.bold);
    const int line_width = std::min(room, context.width(line, size, style.bold));
    Item item{};
    item.role = role;
    item.text = style.role;
    item.pixel_size = size;
    item.bold = style.bold;
    item.colour = style.colour;
    item.box = {from_right ? x - line_width : x, middle - line_height / 2, line_width, line_height};
    item.lines = {std::move(line)};
    block.items.push_back(std::move(item));
    return line_width;
}

/// Adds a mark: a glyph in a box.
///
/// @param[in,out] block the block
/// @param box its box, pixels
/// @param glyph the mark
/// @param colour its colour
void add_icon(Block& block, Rect box, Glyph glyph, Colour colour) {
    if (glyph == Glyph::none)
        return;
    Item item{};
    item.role = ItemRole::icon;
    item.box = box;
    item.glyph = glyph;
    item.colour = colour;
    block.items.push_back(std::move(item));
}

/// Adds a container (card, banner, divider) with no text.
///
/// @param[in,out] block the block
/// @param role its role
/// @param box its box, pixels
/// @param colour its colour (a banner's rule)
void add_box(Block& block, ItemRole role, Rect box, Colour colour = gf::line_colour) {
    Item item{};
    item.role = role;
    item.box = box;
    item.colour = colour;
    block.items.push_back(std::move(item));
}

/// Adds a control's item and its target: the item is marked focused or pressed from the
/// chooser's state, by the target's place in the order they are laid out.
///
/// @param context the layout's context
/// @param[in,out] tally the targets so far
/// @param[in,out] block the block
/// @param item the control's item, its box set
/// @param target what a press on it reaches
/// @param enabled a press reaches it
void add_control(
    const Context& context, Tally& tally, Block& block, Item item, Target target, bool enabled
) {
    const int32_t target_index = tally.targets++;
    item.enabled = enabled;
    item.pressed = enabled && context.state.pressed == target_index;
    if (enabled) {
        item.focused = context.state.focus_shown &&
                       context.state.focus == static_cast<int32_t>(tally.focus_order.size());
        tally.focus_order.push_back(target);
    }
    block.hits.push_back(HitBox{item.box, target, enabled, {}});
    block.items.push_back(std::move(item));
}

/// One button: its label, its look and what it reaches.
struct Button {
    std::string label{};             ///< upper-case label
    Target target{};                 ///< what a press reaches
    ItemRole role{ItemRole::button}; ///< main or plain
    Glyph glyph{Glyph::none};        ///< the mark beside the label
    bool enabled{true};              ///< a press reaches it
};

/// Gives the width a button takes: its label, its glyph and its padding.
///
/// @param context the layout's context
/// @param button the button
/// @return pixels
int button_width(const Context& context, const Button& button) {
    const Metrics& metrics = context.metrics;
    const int size = context.font(metrics.button_text);
    const int glyph =
        button.glyph == Glyph::none
            ? 0
            : size +
                  static_cast<int>(std::lround(static_cast<float>(size) * gf::button_glyph_gap_em));
    const int natural =
        context.width(button.label, size, true) + glyph + 2 * context.px(metrics.button_pad);
    const float least = button.role == ItemRole::button_main ? metrics.button_min_width : 0.0f;
    return std::max(natural, context.px(least));
}

/// Adds a button.
///
/// @param context the layout's context
/// @param[in,out] tally the targets so far
/// @param[in,out] block the block
/// @param box its box, pixels
/// @param button the button
void add_button(
    const Context& context, Tally& tally, Block& block, Rect box, const Button& button
) {
    Item item{};
    item.role = button.role;
    item.box = box;
    item.text = TextRole::button;
    item.pixel_size = context.font(context.metrics.button_text);
    item.bold = true;
    item.colour = button.role == ItemRole::button_main ? ink_colour : button_text_colour;
    item.glyph = button.glyph;
    item.on = button.role == ItemRole::button_main;
    const int glyph =
        button.glyph == Glyph::none
            ? 0
            : item.pixel_size + static_cast<int>(std::lround(
                                    static_cast<float>(item.pixel_size) * gf::button_glyph_gap_em
                                ));
    const int room = box.width - glyph - 2 * context.px(label_margin_points);
    std::string label = context.fit(button.label, item.pixel_size, true, room);
    if (!label.empty())
        item.lines = {std::move(label)};
    add_control(context, tally, block, std::move(item), button.target, button.enabled);
}

/// Adds a row of buttons: the left ones from the left, the right ones from the right, or,
/// when they do not fit on one line, every button from the left, wrapping.
///
/// @param context the layout's context
/// @param[in,out] tally the targets so far
/// @param[in,out] block the block
/// @param x the column's left, pixels
/// @param y the row's top, pixels
/// @param room the column's width, pixels
/// @param left the buttons at the left, left to right
/// @param right the buttons at the right, left to right
/// @return the height taken, pixels
int add_button_row(
    const Context& context,
    Tally& tally,
    Block& block,
    int x,
    int y,
    int room,
    const std::vector<Button>& left,
    const std::vector<Button>& right
) {
    const int height = context.px(gf::min_button_points);
    const int gap = context.px(context.metrics.button_gap);
    std::vector<int> widths;
    int total = 0;
    for (const auto* group : {&left, &right})
        for (const Button& button : *group) {
            widths.push_back(std::min(room, button_width(context, button)));
            total += widths.back() + (widths.size() > 1 ? gap : 0);
        }
    if (widths.empty())
        return 0;
    if (!left.empty() && !right.empty())
        total += gap * 2;
    if (total <= room) {
        int at = x;
        for (std::size_t index = 0; index < left.size(); ++index) {
            add_button(context, tally, block, {at, y, widths[index], height}, left[index]);
            at += widths[index] + gap;
        }
        int right_width = 0;
        for (std::size_t index = 0; index < right.size(); ++index)
            right_width += widths[left.size() + index] + (index > 0 ? gap : 0);
        at = x + room - right_width;
        for (std::size_t index = 0; index < right.size(); ++index) {
            const int width = widths[left.size() + index];
            add_button(context, tally, block, {at, y, width, height}, right[index]);
            at += width + gap;
        }
        return height;
    }
    int at = x;
    int top = y;
    std::size_t index = 0;
    for (const auto* group : {&left, &right})
        for (const Button& button : *group) {
            const int width = widths[index++];
            if (at > x && at + width > x + room) {
                at = x;
                top += height + gap;
            }
            add_button(context, tally, block, {at, top, width, height}, button);
            at += width + gap;
        }
    return top + height - y;
}

/// Adds a banner: its mark and its text.
///
/// @param context the layout's context
/// @param[in,out] block the block
/// @param x the column's left, pixels
/// @param y the banner's top, pixels
/// @param room the column's width, pixels
/// @param text what it says
/// @param glyph the mark at its left
/// @param colour its rule and mark
/// @return the height taken, pixels
int add_banner(
    const Context& context,
    Block& block,
    int x,
    int y,
    int room,
    std::string_view text,
    Glyph glyph,
    Colour colour
) {
    const Metrics& metrics = context.metrics;
    const int pad_x = context.px(metrics.banner_pad_x);
    const int pad_y = context.px(metrics.banner_pad_y);
    const int icon = context.px(metrics.banner_icon);
    const int gap = context.px(metrics.row_gap);
    const int text_x = x + pad_x + icon + gap;
    Block inner;
    const int text_height = add_text(
        context,
        inner,
        text_x,
        0,
        x + room - pad_x - text_x,
        text,
        {metrics.banner_text, false, gf::text_colour, TextRole::lead},
        most_banner_lines
    );
    const int height = pad_y + std::max(text_height, icon) + pad_y;
    add_box(block, ItemRole::banner, {x, y, room, height}, colour);
    add_icon(block, {x + pad_x, y + (height - icon) / 2, icon, icon}, glyph, colour);
    for (Item& item : inner.items) {
        item.box.y += y + (height - text_height) / 2;
        block.items.push_back(std::move(item));
    }
    return height;
}

/// One line of a row under its title.
struct RowLine {
    std::string text{};            ///< the words
    Colour colour{gf::dim_colour}; ///< their colour
    int most_lines{1};             ///< the most lines they wrap to
};

/// One row: a mark, a title, the lines under it and words at its right.
struct Row {
    Glyph glyph{Glyph::none};             ///< the mark at its left
    Colour glyph_colour{gf::dim_colour};  ///< the mark's colour
    std::string title{};                  ///< its title
    Colour title_colour{gf::text_colour}; ///< the title's colour
    bool title_bold{true};                ///< the title is bold
    std::vector<RowLine> lines{};         ///< the lines under the title
    std::string right{};                  ///< words at its right; empty for none
    Colour right_colour{gf::dim_colour};  ///< their colour
    Target target{};                      ///< what a press reaches
    bool enabled{true};                   ///< a press reaches it
};

/// Adds rows in a panel, with dividers between them; each row is a control at least
/// min_button_points high.
///
/// @param context the layout's context
/// @param[in,out] tally the targets so far
/// @param[in,out] block the block
/// @param x the panel's left, pixels
/// @param y its top, pixels
/// @param room its width, pixels
/// @param rows the rows
/// @return the height taken, pixels
int add_rows(
    const Context& context,
    Tally& tally,
    Block& block,
    int x,
    int y,
    int room,
    const std::vector<Row>& rows
) {
    if (rows.empty())
        return 0;
    const Metrics& metrics = context.metrics;
    const int border = std::max(1, context.px(1.0f));
    const int pad_x = context.px(metrics.row_pad_x);
    const int pad_y = context.px(metrics.row_pad_y);
    const int icon = context.px(metrics.row_icon);
    const int gap = context.px(metrics.row_gap);
    const int least = context.px(gf::min_button_points);
    const int panel_index = static_cast<int>(block.items.size());
    add_box(block, ItemRole::card, {x, y, room, 0});
    int at = y + border;
    const int inner_x = x + border;
    const int inner_room = room - 2 * border;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const Row& row = rows[index];
        if (index > 0) {
            add_box(block, ItemRole::divider, {inner_x, at, inner_room, border});
            at += border;
        }
        // The texts are laid out first, to size the row.
        Block texts;
        int right_edge = inner_x + inner_room - pad_x;
        const Style right_style{metrics.row_detail, false, row.right_colour, TextRole::row_detail};
        const int text_x = inner_x + pad_x + icon + gap;
        if (!row.right.empty()) {
            const int size = context.font(right_style.points);
            const int wanted = context.width(row.right, size, false);
            const int width = std::min(wanted, (right_edge - text_x) / 3);
            if (width > 0)
                right_edge -= width + gap;
        }
        const int text_room = std::max(0, right_edge - text_x);
        const Style title_style{
            metrics.row_title, row.title_bold, row.title_colour, TextRole::row_title
        };
        const int title_size = context.font(title_style.points);
        const int title_height = context.line(title_size, row.title_bold);
        int text_height = 0;
        if (add_line(context, texts, text_x, title_height / 2, text_room, row.title, title_style) >
            0)
            text_height = title_height;
        for (const RowLine& line : row.lines)
            text_height += add_text(
                context,
                texts,
                text_x,
                text_height,
                text_room,
                line.text,
                {metrics.row_detail, false, line.colour, TextRole::row_detail},
                line.most_lines
            );
        const int height = std::max(least, text_height + 2 * pad_y);
        Item item{};
        item.role = ItemRole::row;
        item.box = {inner_x, at, inner_room, height};
        item.colour = gf::panel_colour;
        add_control(context, tally, block, std::move(item), row.target, row.enabled);
        const int middle = at + height / 2;
        add_icon(
            block, {inner_x + pad_x, middle - icon / 2, icon, icon}, row.glyph, row.glyph_colour
        );
        const int text_top = at + (height - text_height) / 2;
        for (Item& text : texts.items) {
            text.box.y += text_top;
            block.items.push_back(std::move(text));
        }
        if (!row.right.empty())
            add_line(
                context,
                block,
                inner_x + inner_room - pad_x,
                middle,
                (inner_x + inner_room - pad_x - text_x) / 3,
                row.right,
                right_style,
                true
            );
        at += height;
    }
    at += border;
    block.items[static_cast<std::size_t>(panel_index)].box.height = at - y;
    return at - y;
}

/// Adds the header bar: the OA badge, "OPEN ANNIHILATION  GAME FOLDER" and the version.
///
/// @param context the layout's context
/// @param[in,out] block the block (canvas coordinates)
/// @param safe the safe area, pixels
/// @return the header's bottom, pixels
int add_header(const Context& context, Block& block, Rect safe) {
    const Metrics& metrics = context.metrics;
    const Rect bar{safe.x, safe.y, safe.width, context.px(metrics.header_height)};
    add_box(block, ItemRole::header_bar, bar);
    const int pad = context.px(metrics.header_pad);
    const int gap = context.px(metrics.header_gap);
    const int middle = bar.y + bar.height / 2;
    const int badge = context.px(metrics.badge);
    Item badge_item{};
    badge_item.role = ItemRole::badge;
    badge_item.box = {bar.x + pad, middle - badge / 2, badge, badge};
    badge_item.colour = gf::green_colour;
    badge_item.glyph = Glyph::oa;
    block.items.push_back(badge_item);
    int right = bar.x + bar.width - pad;
    const int text_x = bar.x + pad + badge + gap;
    const int version = add_line(
        context,
        block,
        right,
        middle,
        std::max(0, right - text_x),
        context.model.version,
        {metrics.version_text, false, gf::dim_colour, TextRole::small},
        true,
        ItemRole::version
    );
    if (version > 0)
        right -= version + gap;
    const Style header{metrics.header_text, true, gf::text_colour, TextRole::header};
    const int title = add_line(
        context,
        block,
        text_x,
        middle,
        right - text_x,
        "OPEN ANNIHILATION",
        header,
        false,
        ItemRole::header_text
    );
    const int after = text_x + title + gap;
    if (title > 0 && right - after > context.px(header_word_room_points))
        add_line(
            context,
            block,
            after,
            middle,
            right - after,
            tr("GAME FOLDER"),
            {metrics.header_text, false, gf::dim_colour, TextRole::header},
            false,
            ItemRole::header_text
        );
    return bar.y + bar.height;
}

/// A view's blocks.
struct Column {
    Block top{};    ///< fixed above the rows
    Block body{};   ///< the rows, which scroll
    Block bottom{}; ///< fixed below: the buttons
};

/// Adds the title and the lines under it to a block.
///
/// @param context the layout's context
/// @param[in,out] block the block
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @param title the title
/// @param lines the lines under it, each a paragraph; empty ones are left out
/// @return the height taken, with the gap after it
int add_title(
    const Context& context,
    Block& block,
    int x,
    int room,
    std::string_view title,
    const std::vector<std::string>& lines
) {
    const Metrics& metrics = context.metrics;
    int y = add_text(
        context,
        block,
        x,
        0,
        room,
        title,
        {metrics.title, true, gf::text_colour, TextRole::title},
        most_title_lines,
        ItemRole::title
    );
    y += context.px(metrics.title_gap);
    for (const std::string& line : lines) {
        const int height = add_text(
            context,
            block,
            x,
            y,
            room,
            line,
            {metrics.subtitle, false, gf::dim_colour, TextRole::subtitle},
            most_subtitle_lines
        );
        if (height > 0)
            y += height + context.px(metrics.title_gap);
    }
    return y - context.px(metrics.title_gap) + context.px(metrics.block_gap);
}

/// Returns the words that start a row's title, its first letter raised: "Your Steam library".
///
/// @param words the words
/// @return the title
std::string raised_first(std::string words) {
    if (!words.empty() && words[0] >= 'a' && words[0] <= 'z')
        words[0] = static_cast<char>(words[0] - 'a' + 'A');
    return words;
}

/// Lays out the list: the found folders, the other ways to a folder, and Quit.
///
/// @param context the layout's context
/// @param[in,out] tally the targets so far
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @return the column
Column list_column(const Context& context, Tally& tally, int x, int room) {
    const Model& model = context.model;
    const Metrics& metrics = context.metrics;
    Column column;
    std::string lead;
    if (model.installs.size() > 1)
        lead =
            tr("Total Annihilation is on this computer in more than one place. Choose the one "
               "to play.");
    else if (model.installs.size() == 1)
        lead =
            tr("Total Annihilation was found on this computer. Play it, or choose another "
               "folder.");
    else
        lead =
            tr("Choose the folder that holds totala1.hpi, or the folder that holds the "
               "installer of the Total Annihilation demo (1997).");
    int y = add_title(
        context, column.top, x, room, tr("Choose your Total Annihilation folder"), {lead}
    );
    if (!model.notice.empty())
        y += add_banner(
                 context, column.top, x, y, room, model.notice, Glyph::warning, gf::amber_colour
             ) +
             context.px(metrics.block_gap);
    column.top.height = y;

    int at = 0;
    if (!model.installs.empty()) {
        std::vector<Row> rows;
        for (std::size_t index = 0; index < model.installs.size(); ++index) {
            const InstallRow& install = model.installs[index];
            Row row;
            row.glyph = install.usable ? Glyph::check : Glyph::cross;
            row.glyph_colour = install.usable ? gf::green_colour : gf::red_colour;
            row.title = raised_first(install.source);
            row.title_colour = install.usable ? gf::text_colour : gf::dim_colour;
            row.lines.push_back({install.folder, gf::dim_colour, 1});
            row.lines.push_back(
                {install.verdict,
                 install.usable ? gf::green_colour : gf::red_colour,
                 most_verdict_lines}
            );
            row.target = {TargetKind::install, static_cast<uint16_t>(index)};
            row.enabled = install.usable;
            rows.push_back(std::move(row));
        }
        at += add_rows(context, tally, column.body, x, at, room, rows) +
              context.px(metrics.block_gap);
        at += add_text(
                  context,
                  column.body,
                  x,
                  at,
                  room,
                  tr("Or find the folder yourself:"),
                  {metrics.heading, true, gf::dim_colour, TextRole::small}
              ) +
              context.px(metrics.title_gap);
    }
    std::vector<Row> ways;
    Row browse;
    browse.glyph = Glyph::folder;
    browse.glyph_colour = gf::green_colour;
    browse.title = tr("Browse\xE2\x80\xA6");
    browse.lines.push_back(
        {tr("Look through this computer's folders, drives and SD cards."), gf::dim_colour, 2}
    );
    browse.target = {TargetKind::browse, 0};
    ways.push_back(std::move(browse));
    Row demo;
    demo.glyph = Glyph::file;
    demo.glyph_colour = gf::green_colour;
    demo.title = tr("The Total Annihilation demo (1997)");
    demo.lines.push_back(
        {tr("Choose the folder its installer is in, usually Downloads."), gf::dim_colour, 2}
    );
    demo.target = {TargetKind::demo, 0};
    ways.push_back(std::move(demo));
    if (model.offers_dialog) {
        Row dialog;
        dialog.glyph = Glyph::folder;
        dialog.glyph_colour = gf::dim_colour;
        dialog.title = tr("Use the desktop's folder dialog");
        dialog.lines.push_back(
            {tr("Choose the folder in the system's own window."), gf::dim_colour, 2}
        );
        dialog.target = {TargetKind::dialog, 0};
        ways.push_back(std::move(dialog));
    }
    at += add_rows(context, tally, column.body, x, at, room, ways);
    column.body.height = at;

    Button quit;
    quit.label = tr("QUIT");
    quit.target = {TargetKind::quit, 0};
    column.bottom.height = add_button_row(context, tally, column.bottom, x, 0, room, {}, {quit});
    return column;
}

/// Returns the name a browser's title shows for a folder: its last component, or the whole
/// path for a drive's root.
///
/// @param folder the folder, UTF-8
/// @return the name
std::string folder_title(std::string_view folder) {
    std::string_view trimmed = folder;
    while (trimmed.size() > 1 && (trimmed.back() == '/' || trimmed.back() == '\\'))
        trimmed.remove_suffix(1);
    const std::size_t separator = trimmed.find_last_of("/\\");
    if (separator == std::string_view::npos || separator + 1 >= trimmed.size())
        return std::string(folder);
    const std::string_view name = trimmed.substr(separator + 1);
    // A drive's root ("C:\") keeps its separator.
    if (name.size() == 2 && name[1] == ':')
        return std::string(folder);
    return std::string(name);
}

/// Lays out the browser: the folder, its places, its verdict, its folders, Back and "Play this
/// folder".
///
/// @param context the layout's context
/// @param[in,out] tally the targets so far
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @return the column
Column browse_column(const Context& context, Tally& tally, int x, int room) {
    const Model& model = context.model;
    const Metrics& metrics = context.metrics;
    Column column;
    int y = add_title(
        context, column.top, x, room, folder_title(model.folder), {model.folder, model.hint}
    );
    if (!model.notice.empty())
        y += add_banner(
                 context, column.top, x, y, room, model.notice, Glyph::warning, gf::amber_colour
             ) +
             context.px(metrics.block_gap);
    std::vector<Button> places;
    for (std::size_t index = 0; index < model.place_labels.size(); ++index) {
        Button place;
        place.label = model.place_labels[index];
        place.target = {TargetKind::place, static_cast<uint16_t>(index)};
        places.push_back(std::move(place));
    }
    Button parent;
    parent.label = tr("PARENT FOLDER");
    parent.glyph = Glyph::folder;
    parent.target = {TargetKind::parent, 0};
    parent.enabled = model.has_parent;
    places.push_back(std::move(parent));
    y += add_button_row(context, tally, column.top, x, y, room, places, {}) +
         context.px(metrics.block_gap);
    if (!model.folder_verdict.empty()) {
        // The sentence is translated whole, with the verdict in its place.
        std::string text = model.folder_verdict;
        if (model.folder_usable) {
            text = tr("This folder can be played: {folder}");
            if (const auto at = text.find("{folder}"); at != std::string::npos)
                text.replace(at, std::string_view("{folder}").size(), model.folder_verdict);
        }
        y += add_banner(
                 context,
                 column.top,
                 x,
                 y,
                 room,
                 text,
                 model.folder_usable ? Glyph::check : Glyph::info,
                 model.folder_usable ? gf::green_colour : gf::dim_colour
             ) +
             context.px(metrics.block_gap);
    }
    column.top.height = y;

    if (model.entries.empty()) {
        column.body.height = add_text(
            context,
            column.body,
            x,
            0,
            room,
            tr("No folders here."),
            {metrics.subtitle, false, gf::dim_colour, TextRole::body}
        );
    } else {
        std::vector<Row> rows;
        rows.reserve(model.entries.size());
        for (std::size_t index = 0; index < model.entries.size(); ++index) {
            const FolderEntry& entry = model.entries[index];
            Row row;
            row.glyph = entry.game_folder ? Glyph::check : Glyph::folder;
            row.glyph_colour = entry.game_folder ? gf::green_colour : gf::dim_colour;
            row.title = entry.name;
            row.title_bold = entry.game_folder;
            if (entry.game_folder) {
                row.right = tr("Total Annihilation");
                row.right_colour = gf::green_colour;
            }
            row.target = {TargetKind::entry, static_cast<uint16_t>(index)};
            rows.push_back(std::move(row));
        }
        column.body.height = add_rows(context, tally, column.body, x, 0, room, rows);
    }

    Button back;
    back.label = tr("BACK");
    back.target = {TargetKind::back, 0};
    Button choose;
    choose.label = tr("PLAY THIS FOLDER");
    choose.role = ItemRole::button_main;
    choose.glyph = Glyph::play;
    choose.target = {TargetKind::choose, 0};
    choose.enabled = model.folder_usable;
    column.bottom.height =
        add_button_row(context, tally, column.bottom, x, 0, room, {back}, {choose});
    return column;
}

/// Moves a block's items and targets down by a distance into the layout, clipped.
///
/// @param[in,out] layout the layout
/// @param[in,out] block the block, emptied
/// @param dy pixels to move them down
/// @param clip the scrolling region they lie in; empty for none
void put(Layout& layout, Block& block, int dy, Rect clip) {
    for (Item& item : block.items) {
        item.box.y += dy;
        item.clip = clip;
        layout.paint.items.push_back(std::move(item));
    }
    for (HitBox& hit : block.hits) {
        hit.box.y += dy;
        hit.clip = clip;
        layout.targets.push_back(hit);
    }
    block.items.clear();
    block.hits.clear();
}

} // namespace

Layout lay_out(
    const Model& model,
    const UiState& state,
    const Viewport& viewport,
    const TextMeasureHooks& measure
) {
    Layout layout;
    layout.paint.device = gf::device_class(viewport);
    const float scale = viewport.px_per_point > 0.0f ? viewport.px_per_point : 1.0f;
    layout.paint.px_per_point = scale;
    const Rect safe{
        viewport.safe.left,
        viewport.safe.top,
        viewport.width - viewport.safe.left - viewport.safe.right,
        viewport.height - viewport.safe.top - viewport.safe.bottom,
    };
    if (safe.width <= 0 || safe.height <= 0)
        return layout;
    const bool compact = layout.paint.device == gf::DeviceClass::phone ||
                         static_cast<float>(std::min(viewport.width, viewport.height)) / scale <
                             compact_short_side_points;
    const Context context{model, state, measure, compact ? compact_metrics : tablet_metrics, scale};
    const Metrics& metrics = context.metrics;

    Block header;
    const int header_bottom = add_header(context, header, safe);
    put(layout, header, 0, {});

    int side = context.px(metrics.side_pad);
    if (metrics.max_content_width > 0.0f)
        side = std::max(side, (safe.width - context.px(metrics.max_content_width)) / 2);
    side = std::min(side, safe.width / 4);
    const int x = safe.x + side;
    const int room = safe.width - 2 * side;
    Tally tally;
    Column column = model.view == View::browse ? browse_column(context, tally, x, room)
                                               : list_column(context, tally, x, room);
    layout.focus_order = std::move(tally.focus_order);

    const int area_top = header_bottom + context.px(metrics.content_top);
    const int area_bottom = safe.y + safe.height - context.px(metrics.content_bottom);
    const Rect area{safe.x, area_top, safe.width, std::max(1, area_bottom - area_top)};
    const int between =
        column.body.height > 0 && column.bottom.height > 0 ? context.px(metrics.block_gap) : 0;
    const int natural = column.top.height + column.body.height + between + column.bottom.height;
    const int wanted = std::max(0, context.px(static_cast<float>(state.scroll_points)));
    int scroll_max = 0;
    if (natural <= area.height) {
        put(layout, column.top, area.y, {});
        put(layout, column.body, area.y + column.top.height, {});
        put(layout, column.bottom, area.y + column.top.height + column.body.height + between, {});
    } else {
        const int body_room = area.height - column.top.height - column.bottom.height - between;
        if (column.body.height > 0 && body_room >= context.px(least_body_points)) {
            // The rows scroll between the fixed top and bottom.
            const Rect rows{area.x, area.y + column.top.height, area.width, body_room};
            scroll_max = column.body.height - body_room;
            const int scroll = std::min(wanted, scroll_max);
            put(layout, column.top, area.y, {});
            put(layout, column.body, rows.y - scroll, rows);
            put(layout, column.bottom, rows.y + body_room + between, {});
            layout.paint.rows = rows;
        } else {
            // Too little room for the rows alone: the whole column scrolls.
            scroll_max = natural - area.height;
            const int scroll = std::min(wanted, scroll_max);
            put(layout, column.top, area.y - scroll, area);
            put(layout, column.body, area.y + column.top.height - scroll, area);
            put(layout,
                column.bottom,
                area.y + column.top.height + column.body.height + between - scroll,
                area);
            layout.paint.rows = area;
        }
    }
    layout.scroll_max_points =
        scroll_max > 0 ? static_cast<int32_t>(std::ceil(static_cast<float>(scroll_max) / scale))
                       : 0;
    layout.paint.scroll_max_points = layout.scroll_max_points;
    return layout;
}

} // namespace oa::ui::folder_chooser
