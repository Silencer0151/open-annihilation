// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Game files screen's layouts (game_files.hpp): each step in points for
// the tablet and phone forms inside the safe area, its banner and sheet, the
// scrolling rows, and the hit test.
//
// A step is laid out as the header bar, then a column of three blocks (a
// fixed top, a body that scrolls when the column does not fit, and a fixed
// bottom), then on the first run a footer held at the foot of the safe area.
// When even the fixed blocks leave too little room for the body, the whole
// column scrolls. A sheet is laid out the same way inside its panel, over a
// backdrop that makes everything under it inert.
#include "game_files_internal.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace oa::ui::game_files {

namespace {

using detail::Action;
using detail::BannerContent;
using detail::fill;
using detail::tr;

/// The main button's text: dark ink on the green fill.
constexpr Colour ink_colour{0x10, 0x12, 0x0d};
/// A plain button's text.
constexpr Colour button_text_colour{0xc9, 0xcd, 0xbf};

/// The sizes of one form, in points.
struct Metrics {
    float header_height{};      ///< the header bar
    float header_pad{};         ///< the header's side padding
    float badge{};              ///< the OA badge's side
    float header_text{};        ///< "OPEN ANNIHILATION  GAME FILES"
    float header_gap{};         ///< between the header's pieces
    float version_text{};       ///< the version
    float header_button_text{}; ///< OA · Aa
    float header_button_mark{}; ///< OA · Aa's icon
    float max_content_width{};  ///< the widest the column grows (0: no limit)
    float side_pad{};           ///< the column's least side padding
    float content_top{};        ///< above the column
    float content_bottom{};     ///< below the column
    float title{};              ///< a step's title
    float subtitle{};           ///< the line under it
    float lead{};               ///< S1's lead
    float title_gap{};          ///< between the title and the next line
    float block_gap{};          ///< between blocks of a step
    float banner_text{};        ///< a banner's text
    float banner_pad_x{};       ///< a banner's side padding
    float banner_pad_y{};       ///< a banner's top and bottom padding
    float banner_icon{};        ///< a banner's mark
    float card_pad{};           ///< a card's padding
    float card_gap{};           ///< between cards
    float card_icon{};          ///< a card's framed icon
    float card_title{};         ///< a card's title
    float card_text{};          ///< a card's text
    float card_min_height{};    ///< a card's least height
    float card_min_width{};     ///< narrower cards become rows
    float option_row_height{};  ///< a phone row of S1
    float option_icon{};        ///< its framed icon
    float option_title{};       ///< its title
    float option_text{};        ///< its one line
    float option_gap{};         ///< between the rows
    float footer_text{};        ///< S1's footer
    float footer_pad{};         ///< above and below the footer's line
    float button_text{};        ///< a button's label
    float button_pad{};         ///< a button's side padding
    float button_min_width{};   ///< the main buttons' least width
    float button_gap{};         ///< between buttons
    float row_pad{};            ///< a part row's side padding
    float row_mark{};           ///< its mark
    float row_title{};          ///< its name
    float row_detail{};         ///< its note
    float row_hint{};           ///< its amber hint
    float row_size{};           ///< its size
    float row_size_min{};       ///< the size column's least width
    float row_height{};         ///< a row with no control
    float row_gap{};            ///< between a row's columns
    float switch_width{};       ///< an OFF/ON switch
    float switch_text{};        ///< its labels
    float small_button_text{};  ///< WHY, SHOW, REMOVE
    float small_button_pad{};   ///< their side padding
    float totals_text{};        ///< S3's totals and check lines
    float note_text{};          ///< hints under a step
    float bar_height{};         ///< a progress bar
    float panel_pad{};          ///< a panel's padding (S2, S4, S5)
    float panel_text{};         ///< a panel's main line
    float sheet_width{};        ///< a sheet's width
    float sheet_pad{};          ///< its padding
    float sheet_title{};        ///< its title
    float sheet_text{};         ///< its text
    float sheet_gap{};          ///< between its pieces
    float problem_icon{};       ///< S6's mark
    float problem_title{};      ///< S6's title
    float problem_text{};       ///< S6's text
};

/// The tablet form (a 1194x834-point window).
constexpr Metrics tablet_metrics{
    .header_height = 56.0f,
    .header_pad = 24.0f,
    .badge = 26.0f,
    .header_text = 15.0f,
    .header_gap = 12.0f,
    .version_text = 12.0f,
    .header_button_text = 11.0f,
    .header_button_mark = 20.0f,
    .max_content_width = 1030.0f,
    .side_pad = 24.0f,
    .content_top = 30.0f,
    .content_bottom = 18.0f,
    .title = 28.0f,
    .subtitle = 14.0f,
    .lead = 15.0f,
    .title_gap = 8.0f,
    .block_gap = 18.0f,
    .banner_text = 13.5f,
    .banner_pad_x = 14.0f,
    .banner_pad_y = 6.0f,
    .banner_icon = 18.0f,
    .card_pad = 18.0f,
    .card_gap = 18.0f,
    .card_icon = 44.0f,
    .card_title = 16.0f,
    .card_text = 13.0f,
    .card_min_height = 226.0f,
    .card_min_width = 220.0f,
    .option_row_height = 60.0f,
    .option_icon = 40.0f,
    .option_title = 15.0f,
    .option_text = 12.5f,
    .option_gap = 8.0f,
    .footer_text = 12.5f,
    .footer_pad = 14.0f,
    .button_text = 12.5f,
    .button_pad = 16.0f,
    .button_min_width = 150.0f,
    .button_gap = 12.0f,
    .row_pad = 14.0f,
    .row_mark = 24.0f,
    .row_title = 14.5f,
    .row_detail = 12.5f,
    .row_hint = 11.5f,
    .row_size = 13.0f,
    .row_size_min = 62.0f,
    .row_height = 46.0f,
    .row_gap = 10.0f,
    .switch_width = 84.0f,
    .switch_text = 10.5f,
    .small_button_text = 11.0f,
    .small_button_pad = 10.0f,
    .totals_text = 14.0f,
    .note_text = 12.5f,
    .bar_height = 8.0f,
    .panel_pad = 18.0f,
    .panel_text = 15.0f,
    .sheet_width = 460.0f,
    .sheet_pad = 20.0f,
    .sheet_title = 18.0f,
    .sheet_text = 12.5f,
    .sheet_gap = 10.0f,
    .problem_icon = 44.0f,
    .problem_title = 24.0f,
    .problem_text = 14.0f,
};

/// The phone form (an 852x393-point window), buttons kept at 44 pt.
constexpr Metrics phone_metrics{
    .header_height = 48.0f,
    .header_pad = 14.0f,
    .badge = 22.0f,
    .header_text = 12.5f,
    .header_gap = 10.0f,
    .version_text = 11.0f,
    .header_button_text = 10.5f,
    .header_button_mark = 18.0f,
    .max_content_width = 0.0f,
    .side_pad = 20.0f,
    .content_top = 10.0f,
    .content_bottom = 6.0f,
    .title = 19.0f,
    .subtitle = 12.0f,
    .lead = 12.0f,
    .title_gap = 3.0f,
    .block_gap = 8.0f,
    .banner_text = 11.0f,
    .banner_pad_x = 8.0f,
    .banner_pad_y = 4.0f,
    .banner_icon = 14.0f,
    .card_pad = 14.0f,
    .card_gap = 12.0f,
    .card_icon = 40.0f,
    .card_title = 14.0f,
    .card_text = 11.5f,
    .card_min_height = 0.0f,
    .card_min_width = 100000.0f,
    .option_row_height = 52.0f,
    .option_icon = 36.0f,
    .option_title = 13.5f,
    .option_text = 11.0f,
    .option_gap = 6.0f,
    .footer_text = 10.5f,
    .footer_pad = 6.0f,
    .button_text = 11.0f,
    .button_pad = 12.0f,
    .button_min_width = 124.0f,
    .button_gap = 8.0f,
    .row_pad = 10.0f,
    .row_mark = 20.0f,
    .row_title = 12.5f,
    .row_detail = 10.5f,
    .row_hint = 10.0f,
    .row_size = 11.0f,
    .row_size_min = 48.0f,
    .row_height = 34.0f,
    .row_gap = 8.0f,
    .switch_width = 72.0f,
    .switch_text = 9.5f,
    .small_button_text = 10.0f,
    .small_button_pad = 8.0f,
    .totals_text = 11.5f,
    .note_text = 10.5f,
    .bar_height = 7.0f,
    .panel_pad = 12.0f,
    .panel_text = 13.0f,
    .sheet_width = 420.0f,
    .sheet_pad = 16.0f,
    .sheet_title = 16.0f,
    .sheet_text = 11.5f,
    .sheet_gap = 8.0f,
    .problem_icon = 40.0f,
    .problem_title = 19.0f,
    .problem_text = 12.5f,
};

/// A window whose shorter side is under this many points takes the compact sizes.
constexpr float compact_short_side_points = 600.0f;
/// The least height, in points, a scrolling body keeps before the whole column scrolls.
constexpr float least_body_points = 96.0f;
/// The margin, in points, a sheet keeps from the safe area's edges.
constexpr float sheet_margin_points = 12.0f;
/// The ellipsis that ends a shortened line.
constexpr std::string_view ellipsis = "\xE2\x80\xA6";

/// A text's look.
struct Style {
    float points{};                ///< size in points
    bool bold{};                   ///< bold
    Colour colour{text_colour};    ///< colour
    TextRole role{TextRole::body}; ///< its role
};

/// How a text's box sits.
enum class Fit : uint8_t {
    column,      ///< as wide as the column it is given, left-aligned
    tight_left,  ///< as wide as its widest line, at the left of the column
    tight_right, ///< as wide as its widest line, at the right of the column
};

/// Returns the byte length of the UTF-8 character that starts at a place.
///
/// @param text the text
/// @param at the character's first byte
/// @return the place after the character
std::size_t next_character(std::string_view text, std::size_t at) noexcept {
    if (at >= text.size())
        return text.size();
    const auto lead = static_cast<unsigned char>(text[at]);
    std::size_t length = 1;
    if ((lead & 0xe0u) == 0xc0u)
        length = 2;
    else if ((lead & 0xf0u) == 0xe0u)
        length = 3;
    else if ((lead & 0xf8u) == 0xf0u)
        length = 4;
    return std::min(text.size(), at + length);
}

/// What every part of the layout reads: the model, the measure and the form.
struct Context {
    const Model& model;              ///< what to show
    const TextMeasureHooks& measure; ///< the text measure
    const Metrics& metrics;          ///< the form's sizes
    float scale{1.0f};               ///< canvas pixels per point
    bool phone{};                    ///< the compact sizes: the phone form, or a small window

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
        if (text.empty())
            return 0;
        if (measure.width != nullptr)
            return measure.width(measure.context, text, size, bold);
        std::size_t characters = 0;
        for (std::size_t at = 0; at < text.size(); at = next_character(text, at))
            ++characters;
        return static_cast<int>(std::ceil(0.55 * size * static_cast<double>(characters)));
    }

    /// Gives a line's height.
    ///
    /// @param size pixel size
    /// @param bold bold
    /// @return pixels
    [[nodiscard]] int line(int size, bool bold) const {
        if (measure.line_height != nullptr)
            return std::max(1, measure.line_height(measure.context, size, bold));
        return static_cast<int>(std::ceil(1.25 * size));
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

    /// Takes the longest start of a word that fits a width, at least one character.
    ///
    /// @param word the word
    /// @param size pixel size
    /// @param bold bold
    /// @param room the width, pixels
    /// @return the bytes of the start
    [[nodiscard]] std::size_t
    fitting_start(std::string_view word, int size, bool bold, int room) const {
        std::size_t fits = next_character(word, 0);
        for (std::size_t at = fits; at < word.size();) {
            const std::size_t next = next_character(word, at);
            if (width(word.substr(0, next), size, bold) > room)
                break;
            fits = next;
            at = next;
        }
        return fits;
    }

    /// Breaks text into lines no wider than a width, at spaces; '\n' starts a new line; a
    /// word wider than the width is broken between characters.
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
                    const std::string joined = current + " " + std::string(word);
                    if (width(joined, size, bold) <= room) {
                        current = joined;
                        continue;
                    }
                    lines.push_back(current);
                    current.clear();
                }
                while (width(word, size, bold) > room) {
                    const std::size_t piece = fitting_start(word, size, bold, room);
                    lines.emplace_back(word.substr(0, piece));
                    word.remove_prefix(piece);
                }
                current = std::string(word);
            }
            if (!current.empty())
                lines.push_back(current);
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
            // The last line keeps what fits and ends with the ellipsis.
            std::string cut = fit(rest + std::string(ellipsis), size, bold, room);
            lines.back() = cut;
        }
        return lines;
    }
};

/// Items laid out in a column, with boxes relative to the block's top.
struct Block {
    std::vector<Item> items{}; ///< in painting order
    int height{};              ///< pixels
};

/// Adds a text item, wrapped to a column.
///
/// @param context the layout's context
/// @param[in,out] block the block
/// @param role the item's role
/// @param x the column's left, pixels
/// @param y the text's top, pixels
/// @param room the column's width, pixels
/// @param text the text
/// @param style its look
/// @param fit how its box sits in the column
/// @param most_lines the most lines (0: no limit)
/// @return the text's height, pixels; 0 for no text
int add_text(
    const Context& context,
    Block& block,
    ItemRole role,
    int x,
    int y,
    int room,
    std::string_view text,
    const Style& style,
    Fit fit = Fit::column,
    int most_lines = 0
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
    const int line_height = context.line(size, style.bold);
    Item item{};
    item.role = role;
    item.text = style.role;
    item.pixel_size = size;
    item.bold = style.bold;
    item.colour = style.colour;
    int widest = 0;
    for (const std::string& line : lines)
        widest = std::max(widest, context.width(line, size, style.bold));
    const int box_width = fit == Fit::column ? room : std::min(room, widest);
    const int box_x = fit == Fit::tight_right ? x + room - box_width : x;
    item.box = {box_x, y, box_width, line_height * static_cast<int>(lines.size())};
    item.lines = std::move(lines);
    block.items.push_back(std::move(item));
    return block.items.back().box.height;
}

/// Adds one line of text, shortened to fit, centred on a row's middle.
///
/// @param context the layout's context
/// @param[in,out] block the block
/// @param x its left, pixels (its right with tight_right)
/// @param middle the row's middle, pixels
/// @param room the most width, pixels
/// @param text the text
/// @param style its look
/// @param fit tight_left or tight_right
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
    Fit fit = Fit::tight_left,
    ItemRole role = ItemRole::text
) {
    if (text.empty() || room <= 0)
        return 0;
    const int size = context.font(style.points);
    const std::string line = context.fit(text, size, style.bold, room);
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
    item.box = {
        fit == Fit::tight_right ? x - line_width : x,
        middle - line_height / 2,
        line_width,
        line_height
    };
    item.lines = {line};
    block.items.push_back(std::move(item));
    return line_width;
}

/// Returns the side of a button's mark: its own size when it has one, else the label's.
///
/// @param context the layout's context
/// @param action the button
/// @param label_size the label's pixel size
/// @return the side, pixels
int glyph_side(const Context& context, const Action& action, int label_size) {
    return action.glyph_points > 0.0f ? context.px(action.glyph_points) : label_size;
}

/// Gives the width a button takes: its label, its glyph and its padding.
///
/// @param context the layout's context
/// @param action the button
/// @param text_points the label's size
/// @param pad_points the side padding
/// @param least_points the least width
/// @return pixels
int button_width(
    const Context& context,
    const Action& action,
    float text_points,
    float pad_points,
    float least_points
) {
    const int size = context.font(text_points);
    const int glyph =
        action.glyph == Glyph::none
            ? 0
            : glyph_side(context, action, size) +
                  static_cast<int>(std::lround(static_cast<float>(size) * button_glyph_gap_em));
    const int natural =
        context.width(action.label, size, true) + glyph + 2 * context.px(pad_points);
    return std::max(natural, context.px(least_points));
}

/// Adds a button.
///
/// @param context the layout's context
/// @param[in,out] block the block
/// @param box its box, pixels
/// @param action the button
/// @param text_points the label's size
void add_button(
    const Context& context, Block& block, Rect box, const Action& action, float text_points
) {
    Item item{};
    item.role = action.role;
    item.box = box;
    item.text = TextRole::button;
    item.pixel_size = context.font(text_points);
    item.bold = true;
    item.colour = action.role == ItemRole::button_main     ? ink_colour
                  : action.role == ItemRole::button_danger ? red_colour
                                                           : button_text_colour;
    item.glyph = action.glyph;
    if (action.glyph != Glyph::none && action.glyph_points > 0.0f)
        item.glyph_size = glyph_side(context, action, item.pixel_size);
    item.control = action.control;
    item.enabled = action.enabled;
    item.on = action.role == ItemRole::button_main;
    const int glyph =
        action.glyph == Glyph::none
            ? 0
            : glyph_side(context, action, item.pixel_size) +
                  static_cast<int>(
                      std::lround(static_cast<float>(item.pixel_size) * button_glyph_gap_em)
                  );
    const int room = box.width - glyph - 2 * context.px(4.0f);
    std::string label = context.fit(action.label, item.pixel_size, true, room);
    if (!label.empty())
        item.lines = {std::move(label)};
    block.items.push_back(std::move(item));
}

/// Adds a mark: a glyph in a box, framed or not.
///
/// @param[in,out] block the block
/// @param box its box, pixels
/// @param glyph the mark
/// @param colour its colour
/// @param framed outline the box too
void add_icon(Block& block, Rect box, Glyph glyph, Colour colour, bool framed) {
    if (glyph == Glyph::none && !framed)
        return;
    Item item{};
    item.role = ItemRole::icon;
    item.box = box;
    item.glyph = glyph;
    item.colour = colour;
    item.on = framed;
    block.items.push_back(std::move(item));
}

/// Adds a container (card, row, banner, sheet) with no text.
///
/// @param[in,out] block the block
/// @param role its role
/// @param box its box, pixels
/// @param colour its colour (a banner's rule)
void add_box(Block& block, ItemRole role, Rect box, Colour colour = line_colour) {
    Item item{};
    item.role = role;
    item.box = box;
    item.colour = colour;
    block.items.push_back(std::move(item));
}

/// Moves a block's items down by a distance and appends them to another block.
///
/// @param[in,out] to the block that receives them
/// @param[in,out] from the block whose items move
/// @param dy pixels to move them down
void append_items(Block& to, Block& from, int dy) {
    for (Item& item : from.items) {
        item.box.y += dy;
        to.items.push_back(std::move(item));
    }
    from.items.clear();
}

/// Adds a row of buttons: the left ones from the left, the right ones from the right, or,
/// when they do not fit on one line, every button from the left, wrapping.
///
/// @param context the layout's context
/// @param[in,out] block the block
/// @param x the column's left, pixels
/// @param y the row's top, pixels
/// @param room the column's width, pixels
/// @param left the buttons at the left, left to right
/// @param right the buttons at the right, left to right
/// @return the height taken, pixels
int add_button_row(
    const Context& context,
    Block& block,
    int x,
    int y,
    int room,
    const std::vector<Action>& left,
    const std::vector<Action>& right
) {
    const Metrics& metrics = context.metrics;
    const int height = context.px(min_button_points);
    const int gap = context.px(metrics.button_gap);
    std::vector<int> widths;
    int total = 0;
    const auto measure = [&](const Action& action) {
        const float least = action.role == ItemRole::button_main ? metrics.button_min_width : 0.0f;
        const int width = std::min(
            room, button_width(context, action, metrics.button_text, metrics.button_pad, least)
        );
        widths.push_back(width);
        total += width + (widths.size() > 1 ? gap : 0);
    };
    for (const Action& action : left)
        measure(action);
    for (const Action& action : right)
        measure(action);
    if (widths.empty())
        return 0;
    if (!left.empty() && !right.empty())
        total += gap * 2; // the space between the two groups
    if (total <= room) {
        int at = x;
        for (std::size_t index = 0; index < left.size(); ++index) {
            add_button(
                context, block, {at, y, widths[index], height}, left[index], metrics.button_text
            );
            at += widths[index] + gap;
        }
        int right_width = 0;
        for (std::size_t index = 0; index < right.size(); ++index)
            right_width += widths[left.size() + index] + (index > 0 ? gap : 0);
        at = x + room - right_width;
        for (std::size_t index = 0; index < right.size(); ++index) {
            const int width = widths[left.size() + index];
            add_button(context, block, {at, y, width, height}, right[index], metrics.button_text);
            at += width + gap;
        }
        return height;
    }
    int at = x;
    int top = y;
    std::size_t index = 0;
    const auto place = [&](const Action& action) {
        const int width = widths[index++];
        if (at > x && at + width > x + room) {
            at = x;
            top += height + gap;
        }
        add_button(context, block, {at, top, width, height}, action, metrics.button_text);
        at += width + gap;
    };
    for (const Action& action : left)
        place(action);
    for (const Action& action : right)
        place(action);
    return top + height - y;
}

/// Adds a banner: its mark, its text and its buttons at the right (below the text when the
/// banner is narrow).
///
/// @param context the layout's context
/// @param[in,out] block the block
/// @param x the column's left, pixels
/// @param y the banner's top, pixels
/// @param room the column's width, pixels
/// @param banner what it shows
/// @return the height taken, pixels
int add_banner(
    const Context& context, Block& block, int x, int y, int room, const BannerContent& banner
) {
    const Metrics& metrics = context.metrics;
    const int pad_x = context.px(metrics.banner_pad_x);
    const int pad_y = context.px(metrics.banner_pad_y);
    const int icon = context.px(metrics.banner_icon);
    const int gap = context.px(metrics.button_gap);
    const int button_height = context.px(min_button_points);
    const Style style{metrics.banner_text, false, text_colour, TextRole::lead};
    std::vector<int> widths;
    int buttons = 0;
    for (const Action& action : banner.actions) {
        const float least = action.role == ItemRole::button_main ? metrics.button_min_width : 0.0f;
        widths.push_back(
            button_width(context, action, metrics.button_text, metrics.button_pad, least)
        );
        buttons += widths.back() + (widths.size() > 1 ? gap : 0);
    }
    const int text_x = x + pad_x + icon + gap;
    const int inner_right = x + room - pad_x;
    int text_room = inner_right - text_x - (buttons > 0 ? buttons + gap : 0);
    const bool below = buttons > 0 && text_room < room * 2 / 5;
    if (below)
        text_room = inner_right - text_x;
    Block inner;
    const int text_height =
        add_text(context, inner, ItemRole::text, text_x, 0, text_room, banner.text, style);
    int height = 0;
    if (below) {
        // A narrow banner puts its buttons under the text, wrapping them as a button row does.
        Block row;
        const int row_top = pad_y + std::max(text_height, icon) + gap;
        const int row_height =
            add_button_row(context, row, x + pad_x, row_top, room - 2 * pad_x, {}, banner.actions);
        height = row_top + row_height + pad_y;
        add_box(block, ItemRole::banner, {x, y, room, height}, banner.colour);
        add_icon(block, {x + pad_x, y + pad_y, icon, icon}, banner.glyph, banner.colour, false);
        append_items(block, inner, y + pad_y);
        append_items(block, row, y);
        return height;
    }
    height = pad_y + std::max({text_height, icon, buttons > 0 ? button_height : 0}) + pad_y;
    add_box(block, ItemRole::banner, {x, y, room, height}, banner.colour);
    add_icon(
        block, {x + pad_x, y + (height - icon) / 2, icon, icon}, banner.glyph, banner.colour, false
    );
    append_items(block, inner, y + (height - text_height) / 2);
    int at = inner_right - buttons;
    const int button_top = y + (height - button_height) / 2;
    for (std::size_t index = 0; index < banner.actions.size(); ++index) {
        add_button(
            context,
            block,
            {at, button_top, widths[index], button_height},
            banner.actions[index],
            metrics.button_text
        );
        at += widths[index] + gap;
    }
    return height;
}

/// Tells whether a banner belongs on a step.
///
/// @param step the step
/// @param banner the banner
/// @param pending a replacement waits (S8)
/// @return true when the step shows it
bool banner_on_step(Step step, Banner banner, bool pending) noexcept {
    switch (step) {
    case Step::first_run:
        return banner == Banner::continue_copy || banner == Banner::folder_refused ||
               banner == Banner::nothing_added;
    case Step::copying:
    case Step::checking:
    case Step::ready_to_play:
        return banner == Banner::copy_went_on || banner == Banner::copy_resumed;
    case Step::manage:
        return pending || banner == Banner::added || banner == Banner::next_start ||
               banner == Banner::continue_copy || banner == Banner::nothing_added;
    case Step::looking:
    case Step::nested_offer:
    case Step::already_there:
    case Step::ready_to_copy:
    case Step::problem:
        return false;
    }
    return false;
}

/// Adds the step's banner, when it has one, and the gap after it.
///
/// @param context the layout's context
/// @param[in,out] block the block
/// @param x the column's left, pixels
/// @param y where it goes, pixels
/// @param room the column's width, pixels
/// @return the height taken with the gap, pixels
int add_step_banner(const Context& context, Block& block, int x, int y, int room) {
    const Model& model = context.model;
    if (!banner_on_step(model.step, model.banner, model.pending_replacement))
        return 0;
    BannerContent banner = detail::banner_content(model);
    if (!banner.shown)
        return 0;
    // In the compact form the continue banner leaves CHOOSE FOLDER to the row just below it.
    if (context.phone && model.step == Step::first_run && model.offers_pick_folder)
        banner.actions.erase(
            std::remove_if(
                banner.actions.begin(),
                banner.actions.end(),
                [](const Action& action) {
                    return action.control.kind == ControlKind::choose_folder;
                }
            ),
            banner.actions.end()
        );
    return add_banner(context, block, x, y, room, banner) + context.px(context.metrics.block_gap);
}

/// Adds a step's title and the line under it.
///
/// @param context the layout's context
/// @param[in,out] block the block
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @param title the title
/// @param subtitle the line under it; empty for none
/// @return the height taken, pixels, with the gap after
int add_title(
    const Context& context,
    Block& block,
    int x,
    int room,
    std::string_view title,
    std::string_view subtitle
) {
    const Metrics& metrics = context.metrics;
    int y = block.height;
    y += add_text(
        context,
        block,
        ItemRole::title,
        x,
        y,
        room,
        title,
        {metrics.title, true, text_colour, TextRole::title},
        Fit::column,
        2
    );
    if (!subtitle.empty()) {
        y += context.px(metrics.title_gap / 2.0f);
        y += add_text(
            context,
            block,
            ItemRole::text,
            x,
            y,
            room,
            subtitle,
            {metrics.subtitle, false, dim_colour, TextRole::subtitle},
            Fit::column,
            2
        );
    }
    y += context.px(metrics.block_gap);
    return y - block.height;
}

/// One way in on S1.
struct Option {
    Glyph glyph{};            ///< its icon
    std::string title{};      ///< its title
    std::string text{};       ///< the card's text
    std::string short_text{}; ///< the row's one line
    Action action{};          ///< its button
};

/// Returns S1's ways in that the platform offers, in order.
///
/// @param model the model
/// @return the options
std::vector<Option> first_run_options(const Model& model) {
    std::vector<Option> options;
    if (model.offers_pick_folder) {
        Option option{};
        option.glyph = Glyph::folder;
        option.title = tr("Choose the game folder");
        option.text = fill(
            "Pick the folder that holds totala1.hpi: {places}. It is checked, then copied in.",
            {{"places", platform_word(model, detail::word_picker_places)}}
        );
        option.short_text = fill(
            "The folder that holds totala1.hpi: {places}.",
            {{"places", platform_word(model, detail::word_picker_places_short)}}
        );
        option.action.label = tr("CHOOSE FOLDER");
        option.action.control = {ControlKind::choose_folder};
        option.action.command = Command::pick_game_folder;
        option.action.role = ItemRole::button_main;
        options.push_back(std::move(option));
    }
    if (model.offers_copy_yourself) {
        Option option{};
        option.glyph = Glyph::device;
        option.title = tr("Copy it yourself");
        option.text = fill(
            "{steps} Name it Total Annihilation.",
            {{"steps", platform_word(model, detail::word_copy_yourself_steps)}}
        );
        option.short_text = fill(
            "{steps}, named Total Annihilation.",
            {{"steps", platform_word(model, detail::word_copy_yourself_short)}}
        );
        option.action.label = tr("I HAVE COPIED IT");
        option.action.control = {ControlKind::i_have_copied};
        option.action.command = Command::check_copied;
        option.action.role = options.empty() ? ItemRole::button_main : ItemRole::button;
        options.push_back(std::move(option));
    }
    if (model.offers_pick_folder) {
        Option option{};
        option.glyph = Glyph::disk;
        option.title = tr("Play the 1997 demo");
        option.text = fill(
            "Choose the installer of the Total Annihilation demo (1997). The game recognises it, "
            "never runs it, and unpacks its {size} of game data.",
            {{"size", std::string(detail::demo_data_size_text)}}
        );
        option.short_text = fill(
            "The demo's installer: recognised, never run, {size} unpacked.",
            {{"size", std::string(detail::demo_data_size_text)}}
        );
        option.action.label = tr("CHOOSE INSTALLER");
        option.action.control = {ControlKind::choose_installer};
        option.action.command = Command::pick_installer;
        options.push_back(std::move(option));
    }
    return options;
}

/// Adds S1's ways in as cards in a row.
///
/// @param context the layout's context
/// @param[in,out] block the block
/// @param x the column's left, pixels
/// @param y the cards' top, pixels
/// @param room the column's width, pixels
/// @param options the ways in
/// @return the height taken, pixels
int add_cards(
    const Context& context, Block& block, int x, int y, int room, const std::vector<Option>& options
) {
    const Metrics& metrics = context.metrics;
    const int count = static_cast<int>(options.size());
    const int gap = context.px(metrics.card_gap);
    const int card_width = (room - gap * (count - 1)) / count;
    const int pad = context.px(metrics.card_pad);
    const int icon = context.px(metrics.card_icon);
    const int inner = card_width - 2 * pad;
    const int button_height = context.px(min_button_points);
    const int inner_gap = context.px(10.0f);
    std::vector<Block> texts(options.size());
    int height = context.px(metrics.card_min_height);
    for (std::size_t index = 0; index < options.size(); ++index) {
        Block& text = texts[index];
        const int card_x = x + static_cast<int>(index) * (card_width + gap);
        int at = pad + icon + inner_gap;
        at += add_text(
            context,
            text,
            ItemRole::title,
            card_x + pad,
            at,
            inner,
            options[index].title,
            {metrics.card_title, true, text_colour, TextRole::row_title},
            Fit::column,
            2
        );
        at += inner_gap;
        at += add_text(
            context,
            text,
            ItemRole::text,
            card_x + pad,
            at,
            inner,
            options[index].text,
            {metrics.card_text, false, dim_colour, TextRole::body}
        );
        height = std::max(height, at + inner_gap + button_height + pad);
    }
    for (std::size_t index = 0; index < options.size(); ++index) {
        const int card_x = x + static_cast<int>(index) * (card_width + gap);
        add_box(block, ItemRole::card, {card_x, y, card_width, height});
        add_icon(
            block, {card_x + pad, y + pad, icon, icon}, options[index].glyph, green_colour, true
        );
        for (Item& item : texts[index].items) {
            item.box.y += y;
            block.items.push_back(std::move(item));
        }
        add_button(
            context,
            block,
            {card_x + pad, y + height - pad - button_height, inner, button_height},
            options[index].action,
            metrics.button_text
        );
    }
    return height;
}

/// Adds S1's ways in as rows: icon, title, one line and the button at the right.
///
/// @param context the layout's context
/// @param[in,out] block the block
/// @param x the column's left, pixels
/// @param y the first row's top, pixels
/// @param room the column's width, pixels
/// @param options the ways in
/// @return the height taken, pixels
int add_option_rows(
    const Context& context, Block& block, int x, int y, int room, const std::vector<Option>& options
) {
    const Metrics& metrics = context.metrics;
    const int row_height =
        std::max(context.px(metrics.option_row_height), context.px(min_button_points + 8.0f));
    const int pad = context.px(10.0f);
    const int icon = context.px(metrics.option_icon);
    const int gap = context.px(12.0f);
    const int button_height = context.px(min_button_points);
    int widest = 0;
    for (const Option& option : options)
        widest = std::max(
            widest,
            button_width(
                context,
                option.action,
                metrics.button_text,
                metrics.button_pad,
                metrics.button_min_width
            )
        );
    widest = std::min(widest, room / 2);
    int at = y;
    for (const Option& option : options) {
        add_box(block, ItemRole::card, {x, at, room, row_height});
        add_icon(
            block,
            {x + pad, at + (row_height - icon) / 2, icon, icon},
            option.glyph,
            green_colour,
            true
        );
        const int text_x = x + pad + icon + gap;
        const int text_room = x + room - pad - widest - gap - text_x;
        const Style title{metrics.option_title, true, text_colour, TextRole::row_title};
        const Style line{metrics.option_text, false, dim_colour, TextRole::row_detail};
        const int title_height = context.line(context.font(title.points), true);
        const int line_height = context.line(context.font(line.points), false);
        const int spacing = context.px(2.0f);
        const int text_top = at + (row_height - title_height - spacing - line_height) / 2;
        add_line(
            context, block, text_x, text_top + title_height / 2, text_room, option.title, title
        );
        add_line(
            context,
            block,
            text_x,
            text_top + title_height + spacing + line_height / 2,
            text_room,
            option.short_text,
            line
        );
        add_button(
            context,
            block,
            {x + room - pad - widest, at + (row_height - button_height) / 2, widest, button_height},
            option.action,
            metrics.button_text
        );
        at += row_height + context.px(metrics.option_gap);
    }
    return at - y - context.px(metrics.option_gap);
}

/// The blocks of one step's column.
struct Column {
    Block top{};    ///< fixed at the top
    Block body{};   ///< scrolls when the column does not fit
    Block bottom{}; ///< fixed at the bottom
    Block footer{}; ///< held at the foot of the safe area (S1)
};

/// Returns a row's mark and its colour.
///
/// @param row the row
/// @return the glyph and colour
std::pair<Glyph, Colour> row_mark(const PartRow& row) {
    switch (row.mark) {
    case Mark::none:
    case Mark::waiting:
        return {Glyph::none, dim_colour};
    case Mark::found:
    case Mark::done:
        return {Glyph::check, green_colour};
    case Mark::missing:
        if (row.kind == PartKind::update_31c)
            return {Glyph::warning, amber_colour};
        return {Glyph::dash, dim_colour};
    case Mark::refused:
        return {Glyph::cross, red_colour};
    case Mark::left_out:
        return {Glyph::dash, dim_colour};
    case Mark::copying:
        return {Glyph::clock, amber_colour};
    case Mark::warning:
        return {Glyph::warning, amber_colour};
    }
    return {Glyph::none, dim_colour};
}

/// Adds a panel: a card around content laid out inside it.
///
/// @param[in,out] block the block
/// @param box the panel's box, pixels
/// @param[in,out] inner the content, already placed in the block's coordinates
void add_panel(Block& block, Rect box, Block& inner) {
    add_box(block, ItemRole::card, box);
    append_items(block, inner, 0);
}

/// Adds one part's row.
///
/// @param context the layout's context
/// @param[in,out] block the block
/// @param x the row's left, pixels
/// @param y its top, pixels
/// @param room its width, pixels
/// @param row the part
/// @param index its place in Model::parts; -1 for a row the UI added
/// @param switch_column the rows keep a column for switches
/// @return the row's height, pixels
int add_part_row(
    const Context& context,
    Block& block,
    int x,
    int y,
    int room,
    const PartRow& row,
    int index,
    bool switch_column
) {
    const Model& model = context.model;
    const Metrics& metrics = context.metrics;
    const bool choosing = model.step == Step::ready_to_copy;
    const bool managing = model.step == Step::manage;
    const bool missing = detail::part_missing(row);
    const bool has_switch = choosing && row.has_switch && !missing && index >= 0;
    std::optional<Action> button;
    if (choosing && row.kind == PartKind::mod && detail::part_refused(row) && index >= 0) {
        button = Action{};
        button->label = tr("WHY");
        button->control = {ControlKind::part_why, static_cast<uint16_t>(index)};
    } else if (choosing && row.kind == PartKind::left_out) {
        button = Action{};
        button->label = tr("SHOW");
        button->control = {ControlKind::show_left_out};
    } else if (
        managing && index >= 0 && row.kind != PartKind::game_archives &&
        (row.removable || row.kind == PartKind::demo_data)
    ) {
        button = Action{};
        button->label = tr("REMOVE");
        button->control = {ControlKind::manage_remove, static_cast<uint16_t>(index)};
        button->role = ItemRole::button_danger;
        button->enabled = !model.pending_replacement;
    }
    const int control_height = context.px(min_button_points);
    const int height =
        has_switch || button
            ? std::max(context.px(metrics.row_height), context.px(min_button_points + 4.0f))
            : context.px(metrics.row_height);
    add_box(block, ItemRole::row, {x, y, room, height}, panel_colour);
    const int middle = y + height / 2;
    const int pad = context.px(metrics.row_pad);
    const int gap = context.px(metrics.row_gap);
    int right = x + room - pad;
    if (switch_column) {
        const int width = context.px(metrics.switch_width);
        if (has_switch) {
            Item item{};
            item.role = ItemRole::switch_off_on;
            item.box = {right - width, middle - control_height / 2, width, control_height};
            item.lines = {tr("OFF"), tr("ON")};
            item.text = TextRole::button;
            item.pixel_size = context.font(metrics.switch_text);
            item.bold = true;
            item.colour = text_colour;
            item.control = {ControlKind::part_switch, static_cast<uint16_t>(index)};
            item.on = row.on;
            block.items.push_back(std::move(item));
        }
        right -= width + gap;
    }
    const std::string size = detail::part_size(row);
    const Style size_style{metrics.row_size, false, button_text_colour, TextRole::row_detail};
    const int size_room = std::max(
        context.px(metrics.row_size_min), context.width(size, context.font(metrics.row_size), false)
    );
    add_line(context, block, right, middle, size_room, size, size_style, Fit::tight_right);
    right -= size_room + gap;
    if (button) {
        const int width = std::min(
            button_width(
                context, *button, metrics.small_button_text, metrics.small_button_pad, 0.0f
            ),
            room / 3
        );
        add_button(
            context,
            block,
            {right - width, middle - control_height / 2, width, control_height},
            *button,
            metrics.small_button_text
        );
        right -= width + gap;
    }
    const auto [glyph, glyph_colour] = row_mark(row);
    const int mark = context.px(metrics.row_mark);
    add_icon(block, {x + pad, middle - mark / 2, mark, mark}, glyph, glyph_colour, false);
    const int text_x = x + pad + mark + gap;
    Colour title_colour = text_colour;
    if (row.mark == Mark::done)
        title_colour = green_colour;
    else if (row.mark == Mark::copying)
        title_colour = amber_colour;
    else if (
        row.mark == Mark::waiting || row.mark == Mark::left_out ||
        (missing && row.kind != PartKind::update_31c)
    )
        title_colour = dim_colour;
    const int taken = add_line(
        context,
        block,
        text_x,
        middle,
        right - text_x,
        detail::part_name(row, managing),
        {metrics.row_title, true, title_colour, TextRole::row_title}
    );
    int at = text_x + taken + gap;
    const std::string note = detail::part_detail(model, row);
    const Colour note_colour =
        row.kind == PartKind::update_31c && missing ? amber_colour : dim_colour;
    const Style note_style{metrics.row_detail, false, note_colour, TextRole::row_detail};
    const Style hint_style{metrics.row_hint, false, amber_colour, TextRole::mark_text};
    const int least = context.px(24.0f);
    if (taken > 0 && !note.empty() && right - at >= least)
        at += add_line(context, block, at, middle, right - at, note, note_style) + gap;
    if (taken > 0 && right - at >= least) {
        const int hint_size = context.font(hint_style.points);
        std::string hint = detail::part_hint(model, row, false);
        if (!hint.empty() && context.width(hint, hint_size, false) > right - at)
            hint = detail::part_hint(model, row, true);
        if (!hint.empty() && context.width(hint, hint_size, false) <= right - at)
            add_line(context, block, at, middle, right - at, hint, hint_style);
    }
    return height;
}

/// A row to show and its place in Model::parts (-1: added by the UI).
using ShownRow = std::pair<PartRow, int>;

/// Returns the rows a step shows: the model's parts, and on S3 and S4 the Left out row (when
/// something is left out) or the demo's row, when the model has none.
///
/// @param model the model
/// @return the rows
std::vector<ShownRow> shown_rows(const Model& model) {
    std::vector<ShownRow> rows;
    bool has_left_out = false;
    bool has_demo = false;
    for (std::size_t index = 0; index < model.parts.size(); ++index) {
        rows.emplace_back(model.parts[index], static_cast<int>(index));
        has_left_out = has_left_out || model.parts[index].kind == PartKind::left_out;
        has_demo = has_demo || model.parts[index].kind == PartKind::demo;
    }
    if (model.step != Step::ready_to_copy && model.step != Step::copying)
        return rows;
    if (model.demo) {
        if (!has_demo) {
            PartRow row{};
            row.kind = PartKind::demo;
            row.bytes = model.copy_bytes;
            row.mark = Mark::found;
            rows.emplace_back(row, -1);
        }
    } else if (!has_left_out && !model.left_out.empty()) {
        PartRow row{};
        row.kind = PartKind::left_out;
        row.bytes = model.left_out_bytes;
        row.count = static_cast<uint32_t>(model.left_out.size());
        row.mark = Mark::left_out;
        rows.emplace_back(row, -1);
    }
    return rows;
}

/// Adds the parts' rows in a panel, with dividers between them.
///
/// @param context the layout's context
/// @param[in,out] block the block
/// @param x the panel's left, pixels
/// @param y its top, pixels
/// @param room its width, pixels
/// @return the height taken, pixels
int add_rows_panel(const Context& context, Block& block, int x, int y, int room) {
    const std::vector<ShownRow> rows = shown_rows(context.model);
    if (rows.empty())
        return 0;
    bool switch_column = false;
    for (const auto& [row, index] : rows)
        switch_column =
            switch_column || (context.model.step == Step::ready_to_copy && row.has_switch &&
                              !detail::part_missing(row) && index >= 0);
    const int border = std::max(1, context.px(1.0f));
    Block inner;
    int at = y + border;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (index > 0) {
            add_box(inner, ItemRole::divider, {x + border, at, room - 2 * border, border});
            at += border;
        }
        at += add_part_row(
            context,
            inner,
            x + border,
            at,
            room - 2 * border,
            rows[index].first,
            rows[index].second,
            switch_column
        );
    }
    at += border;
    add_panel(block, {x, y, room, at - y}, inner);
    return at - y;
}

/// Adds the header bar: the OA badge (the Open Annihilation icon), "OPEN ANNIHILATION  GAME
/// FILES", the version and, except in the management state, the OA · Aa button (the icon and
/// "Aa").
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
    badge_item.colour = green_colour;
    badge_item.glyph = Glyph::oa;
    block.items.push_back(badge_item);
    int right = bar.x + bar.width - pad;
    const int text_x = bar.x + pad + badge + gap;
    if (!context.model.management) {
        Action language{};
        language.label = tr("Aa");
        language.glyph = Glyph::oa;
        language.glyph_points = metrics.header_button_mark;
        language.control = {ControlKind::language};
        language.command = Command::open_language;
        const int height = context.px(min_button_points);
        const int width = std::min(
            button_width(context, language, metrics.header_button_text, 12.0f, 0.0f),
            std::max(0, right - text_x)
        );
        add_button(
            context,
            block,
            {right - width, middle - height / 2, width, height},
            language,
            metrics.header_button_text
        );
        right -= width + gap;
    }
    const int version = add_line(
        context,
        block,
        right,
        middle,
        std::max(0, right - text_x),
        context.model.version,
        {metrics.version_text, false, dim_colour, TextRole::small},
        Fit::tight_right,
        ItemRole::version
    );
    if (version > 0)
        right -= version + gap;
    const Style header{metrics.header_text, true, text_colour, TextRole::header};
    const int title = add_line(
        context,
        block,
        text_x,
        middle,
        right - text_x,
        "OPEN ANNIHILATION",
        header,
        Fit::tight_left,
        ItemRole::header_text
    );
    const int after = text_x + title + gap;
    if (title > 0 && right - after > context.px(40.0f))
        add_line(
            context,
            block,
            after,
            middle,
            right - after,
            tr("GAME FILES"),
            {metrics.header_text, false, dim_colour, TextRole::header},
            Fit::tight_left,
            ItemRole::header_text
        );
    return bar.y + bar.height;
}

/// Lays out S1: the title, the lead, the banner, the ways in and the footer.
///
/// @param context the layout's context
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @return the column
Column first_run_column(const Context& context, int x, int room) {
    const Model& model = context.model;
    const Metrics& metrics = context.metrics;
    const std::string device = platform_word(model, detail::word_device);
    Column column;
    Block& top = column.top;
    int y = add_text(
        context,
        top,
        ItemRole::title,
        x,
        0,
        room,
        tr("Add your Total Annihilation files"),
        {metrics.title, true, text_colour, TextRole::title},
        Fit::column,
        2
    );
    y += context.px(metrics.title_gap);
    const Style lead{metrics.lead, false, dim_colour, TextRole::lead};
    if (context.phone)
        y += add_text(
            context,
            top,
            ItemRole::text,
            x,
            y,
            room,
            fill(
                "Copied onto this {device} once from your own copy. Nothing is downloaded.",
                {{"device", device}}
            ),
            lead,
            Fit::column,
            1
        );
    else
        y += add_text(
            context,
            top,
            ItemRole::text,
            x,
            y,
            std::min(room, context.px(900.0f)),
            fill(
                "Open Annihilation plays the game from your own copy, as it does on a computer. It "
                "comes with no game data and downloads nothing: the files are copied onto this "
                "{device} once and stay here.",
                {{"device", device}}
            ),
            lead
        );
    y += context.px(metrics.block_gap);
    y += add_step_banner(context, top, x, y, room);
    top.height = y;

    const std::vector<Option> options = first_run_options(model);
    Block& body = column.body;
    if (!options.empty()) {
        const int count = static_cast<int>(options.size());
        const int card_width = (room - context.px(metrics.card_gap) * (count - 1)) / count;
        const bool cards = !context.phone && card_width >= context.px(metrics.card_min_width);
        body.height = cards ? add_cards(context, body, x, 0, room, options)
                            : add_option_rows(context, body, x, 0, room, options);
    }
    if (!context.phone && model.offers_pick_folder) {
        const int at = body.height + context.px(metrics.block_gap);
        const int height = add_text(
            context,
            body,
            ItemRole::text,
            x,
            at,
            std::min(room, context.px(900.0f)),
            tr("You need the folder of an installed copy of Total Annihilation, the one that holds "
               "totala1.hpi: about 1.1 GB with both expansions and the music. Installers of the "
               "full game are Windows programs and cannot be used here; install the game on a "
               "computer and bring its folder."),
            {metrics.note_text, false, dim_colour, TextRole::small}
        );
        if (height > 0)
            body.height = at + height;
    }

    Block& footer = column.footer;
    const Style small{metrics.footer_text, false, dim_colour, TextRole::small};
    const int pad = context.px(metrics.footer_pad);
    const int line_height = context.line(context.font(small.points), false);
    const int middle = pad + line_height / 2;
    int at = x;
    if (model.free_known)
        at += add_line(
                  context,
                  footer,
                  at,
                  middle,
                  room,
                  fill(
                      "{size} free on this {device}.",
                      {{"size", size_text(model.free_bytes)}, {"device", device}}
                  ),
                  small
              ) +
              context.px(20.0f);
    const std::string hint =
        context.phone
            ? tr("OA settings › Game files manages them later.")
            : tr("OA settings › Game files adds an expansion or removes the files later.");
    if (x + room - at > context.px(60.0f))
        add_line(context, footer, at, middle, x + room - at, hint, small);
    footer.height = pad * 2 + line_height;
    return column;
}

/// Lays out S2 while the source is listed: the count growing, CANCEL and a busy bar.
///
/// @param context the layout's context
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @return the column
Column looking_column(const Context& context, int x, int room) {
    const Model& model = context.model;
    const Metrics& metrics = context.metrics;
    Column column;
    column.top.height = add_title(
        context,
        column.top,
        x,
        room,
        model.demo ? tr("Looking at the file") : tr("Looking at the folder"),
        model.location
    );
    Block inner;
    const int pad = context.px(metrics.panel_pad);
    const int inner_x = x + pad;
    const int inner_room = room - 2 * pad;
    Action cancel{};
    cancel.label = tr("CANCEL");
    cancel.control = {ControlKind::cancel};
    cancel.command = Command::cancel_scan;
    const int cancel_width = std::min(
        button_width(context, cancel, metrics.button_text, metrics.button_pad, 0.0f), inner_room / 2
    );
    const int button_height = context.px(min_button_points);
    add_button(
        context,
        inner,
        {inner_x + inner_room - cancel_width, pad, cancel_width, button_height},
        cancel,
        metrics.button_text
    );
    std::string count;
    if (model.listed_files == 0 && model.listed_bytes == 0)
        count = fill(
            "Looking at {location}…",
            {{"location", model.location.empty() ? tr("the folder you chose") : model.location}}
        );
    else
        count = fill(
            model.listed_files == 1 ? "1 file, {size} so far" : "{n} files, {size} so far",
            {{"n", std::to_string(model.listed_files)}, {"size", size_text(model.listed_bytes)}}
        );
    add_line(
        context,
        inner,
        inner_x,
        pad + button_height / 2,
        inner_room - cancel_width - context.px(metrics.button_gap),
        count,
        {metrics.panel_text, true, text_colour, TextRole::body}
    );
    int y = pad + button_height + context.px(12.0f);
    Item busy{};
    busy.role = ItemRole::progress_busy;
    busy.box = {inner_x, y, inner_room, std::max(2, context.px(metrics.bar_height))};
    inner.items.push_back(busy);
    y += busy.box.height + context.px(10.0f);
    y += add_text(
        context,
        inner,
        ItemRole::text,
        inner_x,
        y,
        inner_room,
        tr("Reading names and sizes only: nothing is copied or downloaded yet."),
        {metrics.note_text, false, dim_colour, TextRole::small}
    );
    y += pad;
    add_panel(column.body, {x, 0, room, y}, inner);
    column.body.height = y;
    return column;
}

/// Shows a nested folder's relative name with the arrows the locations use.
///
/// @param relative the '/'-separated name
/// @return "Games › Total Annihilation"
std::string nested_name(std::string_view relative) {
    std::string name;
    std::size_t start = 0;
    while (start <= relative.size()) {
        const std::size_t end = std::min(relative.find('/', start), relative.size());
        if (end > start) {
            if (!name.empty())
                name += " › ";
            name.append(relative.substr(start, end - start));
        }
        if (end >= relative.size())
            break;
        start = end + 1;
    }
    return name;
}

/// Lays out S2's offer of a game folder found inside the chosen one.
///
/// @param context the layout's context
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @return the column
Column nested_column(const Context& context, int x, int room) {
    const Model& model = context.model;
    const Metrics& metrics = context.metrics;
    Column column;
    Block& top = column.top;
    int y = add_title(
        context,
        top,
        x,
        room,
        tr("A game folder inside"),
        fill(
            "You chose: {location}",
            {{"location", model.location.empty() ? tr("a folder") : model.location}}
        )
    );
    y -= context.px(metrics.block_gap / 2.0f);
    const std::string offer =
        model.nested.size() == 1
            ? fill(
                  "This folder holds no game archives, but {folder} does.",
                  {{"folder", nested_name(model.nested.front())}}
              )
            : tr("This folder holds no game archives, but folders inside it do. Choose one.");
    y += add_text(
        context,
        top,
        ItemRole::text,
        x,
        y,
        room,
        offer,
        {metrics.panel_text, false, text_colour, TextRole::body}
    );
    y += context.px(metrics.block_gap);
    top.height = y;

    Block& body = column.body;
    const int row_height = context.px(min_button_points + 8.0f);
    const int pad = context.px(10.0f);
    const int icon = context.px(metrics.row_mark);
    const int gap = context.px(metrics.row_gap);
    const int button_height = context.px(min_button_points);
    int at = 0;
    for (std::size_t index = 0; index < model.nested.size(); ++index) {
        Action use{};
        use.label = tr("USE THAT FOLDER");
        use.control = {ControlKind::nested_choice, static_cast<uint16_t>(index)};
        use.command = Command::use_nested;
        use.role = index == 0 ? ItemRole::button_main : ItemRole::button;
        const int width = std::min(
            button_width(
                context, use, metrics.button_text, metrics.button_pad, metrics.button_min_width
            ),
            room / 2
        );
        add_box(body, ItemRole::card, {x, at, room, row_height});
        const int middle = at + row_height / 2;
        add_icon(body, {x + pad, middle - icon / 2, icon, icon}, Glyph::folder, dim_colour, false);
        const int text_x = x + pad + icon + gap;
        add_line(
            context,
            body,
            text_x,
            middle,
            x + room - pad - width - gap - text_x,
            nested_name(model.nested[index]),
            {metrics.row_title, true, text_colour, TextRole::row_title}
        );
        add_button(
            context,
            body,
            {x + room - pad - width, middle - button_height / 2, width, button_height},
            use,
            metrics.button_text
        );
        at += row_height + context.px(6.0f);
    }
    body.height = at > 0 ? at - context.px(6.0f) : 0;

    Action other{};
    other.label = tr("CHOOSE ANOTHER FOLDER");
    other.control = {ControlKind::choose_folder};
    other.command = Command::pick_game_folder;
    column.bottom.height = add_button_row(context, column.bottom, x, 0, room, {other}, {});
    return column;
}

/// Lays out S2's answer when the chosen folder is the game folder itself.
///
/// @param context the layout's context
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @return the column
Column already_there_column(const Context& context, int x, int room) {
    Column column;
    column.top.height = add_title(
        context, column.top, x, room, tr("This is already the game folder."), context.model.location
    );
    Action check{};
    check.label = tr("CHECK IT");
    check.control = {ControlKind::check_it};
    check.command = Command::check_in_place;
    check.role = ItemRole::button_main;
    Action other{};
    other.label = tr("CHOOSE ANOTHER FOLDER");
    other.control = {ControlKind::choose_folder};
    other.command = Command::pick_game_folder;
    column.body.height = add_button_row(context, column.body, x, 0, room, {check, other}, {});
    return column;
}

/// Returns the short-space warning of S3: the need, the free space, what to do, and which
/// switches would make it fit.
///
/// @param model the model
/// @return the warning
std::string short_space_text(const Model& model) {
    std::string text = fill(
        "These files need {need} and this {device} has {free} free. {advice}, or turn off a part "
        "above, then tap Check again.",
        {{"need", size_text(model.need_bytes)},
         {"device", platform_word(model, detail::word_device)},
         {"free", size_text(model.free_bytes)},
         {"advice", platform_word(model, detail::word_free_space_advice)}}
    );
    std::vector<std::string> names;
    for (PartKind kind : model.fitting_off)
        names.push_back(detail::part_kind_name(kind));
    if (!names.empty())
        text = fill(
            "{warning} Turning off {parts} would make them fit.",
            {{"warning", text}, {"parts", detail::join_or(names)}}
        );
    return text;
}

/// Lays out S3: the parts, what is left out, the space and the check, and COPY.
///
/// @param context the layout's context
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @return the column
Column ready_to_copy_column(const Context& context, int x, int room) {
    const Model& model = context.model;
    const Metrics& metrics = context.metrics;
    const std::string device = platform_word(model, detail::word_device);
    Column column;
    std::string source = fill(
        "From {location}",
        {{"location", model.location.empty() ? tr("the folder you chose") : model.location}}
    );
    const std::string cloud =
        model.remote_bytes > 0 ? fill(
                                     "{size} is in {cloud} only and is downloaded while copying.",
                                     {{"size", size_text(model.remote_bytes)},
                                      {"cloud", platform_word(model, detail::word_cloud_name)}}
                                 )
                               : std::string{};
    // The compact form keeps the rows' room: the cloud line joins the source's.
    if (context.phone && !cloud.empty())
        source = fill("{source} · {cloud}", {{"source", source}, {"cloud", cloud}});
    column.top.height = add_title(context, column.top, x, room, tr("Ready to copy"), source);
    column.body.height = add_rows_panel(context, column.body, x, 0, room);

    Block& bottom = column.bottom;
    const Style totals{metrics.totals_text, false, dim_colour, TextRole::body};
    const Style copies{metrics.totals_text, true, text_colour, TextRole::body};
    const int size = context.font(metrics.totals_text);
    const int line_height = context.line(size, true);
    const int gap = context.px(metrics.row_gap);
    std::string check;
    Colour check_colour = dim_colour;
    if (model.source_checked) {
        check = tr("Checked: the game can start from these files.");
        check_colour = green_colour;
    } else if (model.source_check_skipped) {
        check = tr("Checked fully once copied.");
    }
    const std::string copy_size = size_text(model.copy_bytes, true);
    // The demo's installer is moved in, not copied, so a demo copying nothing says nothing.
    std::string copies_text;
    if (!model.demo || model.copy_bytes > 0)
        copies_text = model.sizes_unknown ? fill("Copies at least {size}.", {{"size", copy_size}})
                                          : fill("Copies {size}.", {{"size", copy_size}});
    const std::string free_text =
        model.free_known ? fill(
                               "{size} free on this {device}.",
                               {{"size", size_text(model.free_bytes)}, {"device", device}}
                           )
                         : std::string{};
    const int icon = check.empty() || !model.source_checked ? 0 : size;
    const int check_width =
        check.empty() ? 0 : icon + (icon > 0 ? gap : 0) + context.width(check, size, false);
    const int totals_width = context.width(copies_text, size, true) +
                             (free_text.empty() || copies_text.empty() ? 0 : gap) +
                             context.width(free_text, size, false);
    int y = 0;
    const auto add_check = [&](int middle) {
        if (check.empty())
            return;
        int at = x;
        if (icon > 0) {
            add_icon(
                bottom, {at, middle - icon / 2, icon, icon}, Glyph::check, green_colour, false
            );
            at += icon + gap;
        }
        add_line(
            context,
            bottom,
            at,
            middle,
            x + room - at,
            check,
            {metrics.totals_text, false, check_colour, TextRole::body}
        );
    };
    const auto add_totals = [&](int middle, bool right) {
        int at = right ? x + room - std::min(room, totals_width) : x;
        const int taken = add_line(context, bottom, at, middle, x + room - at, copies_text, copies);
        if (taken > 0)
            at += taken + gap;
        if (!free_text.empty() && x + room - at > context.px(24.0f))
            add_line(context, bottom, at, middle, x + room - at, free_text, totals);
    };
    if (check_width + 2 * gap + totals_width <= room) {
        add_check(line_height / 2);
        add_totals(line_height / 2, true);
        y += line_height;
    } else {
        if (!check.empty()) {
            add_check(line_height / 2);
            y += line_height + context.px(4.0f);
        }
        add_totals(y + line_height / 2, false);
        y += line_height;
    }
    if (!context.phone && !cloud.empty()) {
        y += context.px(4.0f);
        y += add_text(context, bottom, ItemRole::text, x, y, room, cloud, totals);
    }
    if (model.space_short) {
        y += context.px(metrics.block_gap / 2.0f);
        BannerContent warning{};
        warning.shown = true;
        warning.glyph = Glyph::warning;
        warning.colour = amber_colour;
        warning.text = short_space_text(model);
        y += add_banner(context, bottom, x, y, room, warning);
    }
    y += context.px(metrics.block_gap / 2.0f);
    Action other{};
    if (model.demo) {
        other.label = tr("CHOOSE ANOTHER FILE");
        other.control = {ControlKind::choose_installer};
        other.command = Command::pick_installer;
    } else {
        other.label = tr("CHOOSE ANOTHER FOLDER");
        other.control = {ControlKind::choose_folder};
        other.command = Command::pick_game_folder;
    }
    std::vector<Action> right;
    if (model.space_short) {
        Action again{};
        again.label = tr("CHECK AGAIN");
        again.control = {ControlKind::check_space};
        again.command = Command::recheck_space;
        right.push_back(again);
    }
    Action copy{};
    copy.label = model.replace ? tr("REPLACE GAME FILES") : tr("COPY");
    copy.control = {ControlKind::copy};
    copy.command = Command::start_copy;
    copy.role = ItemRole::button_main;
    copy.enabled = !model.space_short;
    right.push_back(copy);
    y += add_button_row(context, bottom, x, y, room, {other}, right);
    bottom.height = y;
    return column;
}

/// Lays out S4: the progress, STOP, the parts ticking and the hint.
///
/// @param context the layout's context
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @return the column
Column copying_column(const Context& context, int x, int room) {
    const Model& model = context.model;
    const Metrics& metrics = context.metrics;
    Column column;
    Block& top = column.top;
    int y = add_title(
        context,
        top,
        x,
        room,
        tr("Copying"),
        fill(
            "From {location}",
            {{"location", model.location.empty() ? tr("the folder you chose") : model.location}}
        )
    );
    y += add_step_banner(context, top, x, y, room);
    Block inner;
    const int pad = context.px(metrics.panel_pad);
    const int inner_x = x + pad;
    const int inner_room = room - 2 * pad;
    const int button_height = context.px(min_button_points);
    Action stop{};
    stop.label = tr("STOP");
    stop.control = {ControlKind::stop};
    stop.role = ItemRole::button_danger;
    stop.glyph = Glyph::stop;
    const int stop_width = std::min(
        button_width(context, stop, metrics.button_text, metrics.button_pad, 0.0f), inner_room / 2
    );
    const int middle = y + pad + button_height / 2;
    add_button(
        context,
        inner,
        {inner_x + inner_room - stop_width, y + pad, stop_width, button_height},
        stop,
        metrics.button_text
    );
    const int text_room = inner_room - stop_width - context.px(metrics.button_gap);
    const int taken = add_line(
        context,
        inner,
        inner_x,
        middle,
        text_room,
        fill(
            "{done} of {total}",
            {{"done", size_text(model.progress.done_bytes, true)},
             {"total", size_text(model.progress.total_bytes, true)}}
        ),
        {metrics.panel_text, true, text_colour, TextRole::body}
    );
    const int gap = context.px(metrics.row_gap);
    if (taken > 0 && text_room - taken - gap > context.px(24.0f))
        add_line(
            context,
            inner,
            inner_x + taken + gap,
            middle,
            text_room - taken - gap,
            time_left_text(model.progress.seconds_left),
            {metrics.note_text, false, dim_colour, TextRole::small}
        );
    int at = y + pad + button_height + context.px(10.0f);
    Item track{};
    track.role = ItemRole::progress_track;
    track.box = {inner_x, at, inner_room, std::max(2, context.px(metrics.bar_height))};
    inner.items.push_back(track);
    float fraction = 0.0f;
    if (model.progress.total_bytes > 0)
        fraction = static_cast<float>(std::min(
            1.0,
            static_cast<double>(model.progress.done_bytes) /
                static_cast<double>(model.progress.total_bytes)
        ));
    Item filled{};
    filled.role = ItemRole::progress_fill;
    filled.box = track.box;
    filled.box.width =
        static_cast<int>(std::lround(static_cast<float>(track.box.width) * fraction));
    filled.fraction = fraction;
    filled.colour = green_colour;
    inner.items.push_back(filled);
    at += track.box.height + context.px(10.0f);
    std::string current = model.progress.current;
    if (model.progress.downloading && !current.empty())
        current = fill(
            "Downloading {file} from {cloud}…",
            {{"file", current}, {"cloud", platform_word(model, detail::word_cloud_name)}}
        );
    if (!current.empty()) {
        const Style style{metrics.note_text, false, dim_colour, TextRole::small};
        const int line_height = context.line(context.font(style.points), false);
        add_line(context, inner, inner_x, at + line_height / 2, inner_room, current, style);
        at += line_height;
    }
    at += pad;
    add_panel(top, {x, y, room, at - y}, inner);
    top.height = at + context.px(metrics.block_gap);

    column.body.height = add_rows_panel(context, column.body, x, 0, room);
    column.bottom.height = add_text(
        context,
        column.bottom,
        ItemRole::text,
        x,
        0,
        room,
        tr("Keep Open Annihilation open until the copy ends. If you leave it, the copy goes on for "
           "a short while, then pauses and continues when you come back."),
        {metrics.note_text, false, dim_colour, TextRole::small}
    );
    return column;
}

/// Lays out S5 while the engine checks the copy (or unpacks the demo).
///
/// @param context the layout's context
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @return the column
Column checking_column(const Context& context, int x, int room) {
    const Model& model = context.model;
    const Metrics& metrics = context.metrics;
    Column column;
    int y = add_title(context, column.top, x, room, tr("Checking"), {});
    y += add_step_banner(context, column.top, x, y, room);
    column.top.height = y;
    Block inner;
    const int pad = context.px(metrics.panel_pad);
    int at = pad;
    at += add_text(
        context,
        inner,
        ItemRole::text,
        x + pad,
        at,
        room - 2 * pad,
        model.demo ? fill(
                         "Unpacking the demo's game data ({size})…",
                         {{"size", std::string(detail::demo_data_size_text)}}
                     )
                   : tr("Checking the copied files with the game's own check…"),
        {metrics.panel_text, false, text_colour, TextRole::body}
    );
    at += context.px(12.0f);
    Item busy{};
    busy.role = ItemRole::progress_busy;
    busy.box = {x + pad, at, room - 2 * pad, std::max(2, context.px(metrics.bar_height))};
    inner.items.push_back(busy);
    at += busy.box.height + pad;
    add_panel(column.body, {x, 0, room, at}, inner);
    column.body.height = at;
    return column;
}

/// Lays out S5's Ready to play: what the game will play, the space, the warnings, the backups
/// line, the folder set aside, and PLAY.
///
/// @param context the layout's context
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @return the column
Column ready_to_play_column(const Context& context, int x, int room) {
    const Model& model = context.model;
    const Metrics& metrics = context.metrics;
    const std::string device = platform_word(model, detail::word_device);
    Column column;
    Block& top = column.top;
    const int icon = context.px(metrics.problem_icon);
    const int gap = context.px(16.0f);
    const int text_x = x + icon + gap;
    const int text_room = room - icon - gap;
    int text_height = add_text(
        context,
        top,
        ItemRole::title,
        text_x,
        0,
        text_room,
        tr("Ready to play"),
        {metrics.title, true, text_colour, TextRole::title},
        Fit::column,
        2
    );
    text_height += context.px(metrics.title_gap / 2.0f);
    text_height += add_text(
        context,
        top,
        ItemRole::text,
        text_x,
        text_height,
        text_room,
        model.ready_summary.empty() ? ready_text(model.ready_parts, model.demo)
                                    : model.ready_summary,
        {metrics.subtitle, false, text_colour, TextRole::subtitle}
    );
    add_icon(top, {x, 0, icon, icon}, Glyph::check, green_colour, true);
    int y = std::max(icon, text_height) + context.px(metrics.block_gap);
    y += add_step_banner(context, top, x, y, room);
    top.height = y;

    Block& body = column.body;
    Block inner;
    const int pad = context.px(metrics.panel_pad);
    const Style uses{metrics.note_text + 1.0f, false, text_colour, TextRole::body};
    const Style dim{metrics.note_text, false, dim_colour, TextRole::small};
    int at = pad;
    at += add_text(
        context,
        inner,
        ItemRole::text,
        x + pad,
        at,
        room - 2 * pad,
        model.free_known ? fill(
                               "Uses {size} on this {device} · {free} free.",
                               {{"size", size_text(model.uses_bytes)},
                                {"device", device},
                                {"free", size_text(model.free_bytes)}}
                           )
                         : fill(
                               "Uses {size} on this {device}.",
                               {{"size", size_text(model.uses_bytes)}, {"device", device}}
                           ),
        uses
    );
    if (!model.backed_up)
        at += context.px(2.0f) + add_text(
                                     context,
                                     inner,
                                     ItemRole::text,
                                     x + pad,
                                     at + context.px(2.0f),
                                     room - 2 * pad,
                                     fill(
                                         "These files are not in this {device}'s backups. Keep "
                                         "your original copy.",
                                         {{"device", device}}
                                     ),
                                     dim
                                 );
    at += pad;
    add_panel(body, {x, 0, room, at}, inner);
    int y_body = at;
    for (const std::string& archive : model.skipped_archives) {
        y_body += context.px(metrics.block_gap / 2.0f);
        BannerContent warning{};
        warning.shown = true;
        warning.glyph = Glyph::warning;
        warning.colour = amber_colour;
        warning.text = fill(
            "{file} could not be opened, so the game plays without it: it may be damaged. Copy it "
            "again from the original.",
            {{"file", archive}}
        );
        y_body += add_banner(context, body, x, y_body, room, warning);
    }
    if (model.old_folder_bytes > 0) {
        y_body += context.px(metrics.block_gap / 2.0f);
        BannerContent old{};
        old.shown = true;
        old.glyph = Glyph::folder;
        old.colour = amber_colour;
        old.text = fill(
            "The folder that was there before is kept as Total Annihilation (old), {size}.",
            {{"size", size_text(model.old_folder_bytes)}}
        );
        Action remove{};
        remove.label = tr("REMOVE OLD FOLDER");
        remove.control = {ControlKind::remove_old};
        remove.glyph = Glyph::trash;
        old.actions.push_back(remove);
        y_body += add_banner(context, body, x, y_body, room, old);
    }
    body.height = y_body;

    Action play{};
    play.role = ItemRole::button_main;
    if (model.management) {
        // CHECK AGAIN in the management state reports here; DONE goes back to the list.
        play.label = tr("DONE");
        play.control = {ControlKind::back};
        play.command = Command::back;
    } else {
        play.label = tr("PLAY");
        play.control = {ControlKind::play};
        play.command = Command::play;
        play.glyph = Glyph::play;
    }
    column.bottom.height = add_button_row(context, column.bottom, x, 0, room, {}, {play});
    return column;
}

/// Lays out S6: the problem's mark, title, texts and buttons.
///
/// @param context the layout's context
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @return the column
Column problem_column(const Context& context, int x, int room) {
    const Metrics& metrics = context.metrics;
    const detail::ProblemContent problem = detail::problem_content(context.model);
    Column column;
    Block& body = column.body;
    const int icon = context.px(metrics.problem_icon);
    const int gap = context.px(16.0f);
    const int text_x = x + icon + gap;
    const int text_room = room - icon - gap;
    add_icon(body, {x, 0, icon, icon}, problem.glyph, problem.colour, true);
    int y = add_text(
        context,
        body,
        ItemRole::title,
        text_x,
        0,
        text_room,
        problem.title,
        {metrics.problem_title, true, text_colour, TextRole::title},
        Fit::column,
        3
    );
    for (std::size_t index = 0; index < problem.paragraphs.size(); ++index) {
        y += context.px(6.0f);
        y += add_text(
            context,
            body,
            ItemRole::text,
            text_x,
            y,
            text_room,
            problem.paragraphs[index],
            {metrics.problem_text,
             false,
             index == 0 ? text_colour : dim_colour,
             index == 0 ? TextRole::body : TextRole::small}
        );
    }
    y += context.px(metrics.block_gap);
    y += add_button_row(context, body, text_x, y, text_room, problem.actions, {});
    body.height = std::max(icon, y);
    return column;
}

/// Lays out S8: the summary, CHECK AGAIN and ADD FILES…, the banner, the parts with REMOVE,
/// and the foot's REPLACE, REMOVE ALL and DONE.
///
/// @param context the layout's context
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @return the column
Column manage_column(const Context& context, int x, int room) {
    const Model& model = context.model;
    const Metrics& metrics = context.metrics;
    const std::string device = platform_word(model, detail::word_device);
    Column column;
    Block& top = column.top;
    Action check{};
    check.label = tr("CHECK AGAIN");
    check.control = {ControlKind::manage_check};
    check.command = Command::manage_check;
    check.glyph = Glyph::refresh;
    Action add{};
    add.label = tr("ADD FILES…");
    add.control = {ControlKind::manage_add};
    add.role = ItemRole::button_main;
    add.glyph = Glyph::plus;
    add.enabled = !model.pending_replacement;
    const int gap = context.px(metrics.button_gap);
    const int check_width =
        button_width(context, check, metrics.button_text, metrics.button_pad, 0.0f);
    const int add_width = button_width(context, add, metrics.button_text, metrics.button_pad, 0.0f);
    const int buttons = check_width + gap + add_width;
    const std::string title = tr("Game files");
    const std::string subtitle =
        model.free_known ? fill(
                               "{size} on this {device} · {free} free",
                               {{"size", size_text(model.installed_bytes)},
                                {"device", device},
                                {"free", size_text(model.free_bytes)}}
                           )
                         : fill(
                               "{size} on this {device}",
                               {{"size", size_text(model.installed_bytes)}, {"device", device}}
                           );
    const int title_width = std::max(
        context.width(title, context.font(metrics.title), true),
        context.width(subtitle, context.font(metrics.subtitle), false)
    );
    const bool beside = title_width + 2 * gap + buttons <= room;
    const int text_room = beside ? room - buttons - 2 * gap : room;
    int y = add_title(context, top, x, text_room, title, subtitle);
    const int height = context.px(min_button_points);
    if (beside) {
        add_button(
            context, top, {x + room - buttons, 0, check_width, height}, check, metrics.button_text
        );
        add_button(
            context, top, {x + room - add_width, 0, add_width, height}, add, metrics.button_text
        );
        y = std::max(y, height + context.px(metrics.block_gap));
    } else {
        y += add_button_row(context, top, x, y, room, {check, add}, {});
        y += context.px(metrics.block_gap);
    }
    y += add_step_banner(context, top, x, y, room);
    top.height = y;

    column.body.height = add_rows_panel(context, column.body, x, 0, room);

    Action replace{};
    replace.label = tr("REPLACE THE GAME FILES…");
    replace.control = {ControlKind::manage_replace};
    replace.command = Command::manage_replace;
    replace.enabled = !model.pending_replacement;
    Action remove_all{};
    remove_all.label = tr("REMOVE ALL GAME FILES…");
    remove_all.control = {ControlKind::manage_remove_all};
    remove_all.role = ItemRole::button_danger;
    remove_all.enabled = !model.pending_replacement;
    Action done{};
    done.label = tr("DONE");
    done.control = {ControlKind::manage_done};
    done.command = Command::done;
    done.role = ItemRole::button_main;
    column.bottom.height =
        add_button_row(context, column.bottom, x, 0, room, {replace, remove_all}, {done});
    return column;
}

/// Lays out the model's step.
///
/// @param context the layout's context
/// @param x the column's left, pixels
/// @param room the column's width, pixels
/// @return the column
Column step_column(const Context& context, int x, int room) {
    switch (context.model.step) {
    case Step::first_run:
        return first_run_column(context, x, room);
    case Step::looking:
        return looking_column(context, x, room);
    case Step::nested_offer:
        return nested_column(context, x, room);
    case Step::already_there:
        return already_there_column(context, x, room);
    case Step::ready_to_copy:
        return ready_to_copy_column(context, x, room);
    case Step::copying:
        return copying_column(context, x, room);
    case Step::checking:
        return checking_column(context, x, room);
    case Step::ready_to_play:
        return ready_to_play_column(context, x, room);
    case Step::problem:
        return problem_column(context, x, room);
    case Step::manage:
        return manage_column(context, x, room);
    }
    return {};
}

/// Where a column's scrolling part ended up.
struct Region {
    Rect rows{};      ///< the scrolling region; empty when nothing scrolls
    int scroll_max{}; ///< how far it scrolls, pixels
};

/// Places a column's blocks in an area: one after another when they fit; else the body
/// scrolls between the fixed top and bottom; else, when that leaves the body too little
/// room, the whole column scrolls.
///
/// @param context the layout's context
/// @param[out] items where the placed items go
/// @param area the area, pixels
/// @param[in,out] column the blocks; their items are moved out
/// @param scroll_points how far the scrolling part is scrolled, points
/// @return the scrolling region
Region place_column(
    const Context& context,
    std::vector<Item>& items,
    Rect area,
    Column& column,
    int32_t scroll_points
) {
    const int between = column.body.height > 0 && column.bottom.height > 0
                            ? context.px(context.metrics.block_gap)
                            : 0;
    const int natural = column.top.height + column.body.height + between + column.bottom.height;
    const auto put = [&items](Block& block, int dy, Rect clip) {
        for (Item& item : block.items) {
            item.box.y += dy;
            item.clip = clip;
            items.push_back(std::move(item));
        }
        block.items.clear();
    };
    Region region{};
    const int wanted = std::max(0, context.px(static_cast<float>(scroll_points)));
    if (natural <= area.height) {
        put(column.top, area.y, {});
        put(column.body, area.y + column.top.height, {});
        put(column.bottom, area.y + column.top.height + column.body.height + between, {});
        return region;
    }
    const int body_room = area.height - column.top.height - column.bottom.height - between;
    if (column.body.height > 0 && body_room >= context.px(least_body_points)) {
        region.rows = {area.x, area.y + column.top.height, area.width, body_room};
        region.scroll_max = column.body.height - body_room;
        const int scroll = std::min(wanted, region.scroll_max);
        put(column.top, area.y, {});
        put(column.body, region.rows.y - scroll, region.rows);
        put(column.bottom, region.rows.y + body_room + between, {});
        return region;
    }
    region.rows = area;
    region.scroll_max = natural - area.height;
    const int scroll = std::min(wanted, region.scroll_max);
    put(column.top, area.y - scroll, area);
    put(column.body, area.y + column.top.height - scroll, area);
    put(column.bottom, area.y + column.top.height + column.body.height + between - scroll, area);
    return region;
}

/// Lays out the model's sheet over a backdrop: its title, text, list and buttons, the list
/// scrolling when the sheet does not fit.
///
/// @param context the layout's context
/// @param[in,out] layout the layout
/// @param viewport the canvas
/// @param safe the safe area, pixels
void add_sheet(const Context& context, Layout& layout, const Viewport& viewport, Rect safe) {
    const Metrics& metrics = context.metrics;
    const detail::SheetContent sheet = detail::sheet_content(context.model);
    Item backdrop{};
    backdrop.role = ItemRole::backdrop;
    backdrop.box = {0, 0, viewport.width, viewport.height};
    layout.items.push_back(backdrop);
    const int margin = context.px(sheet_margin_points);
    const int width =
        std::max(0, std::min(context.px(metrics.sheet_width), safe.width - 2 * margin));
    const int pad = context.px(metrics.sheet_pad);
    const int inner = width - 2 * pad;
    const int x = safe.x + (safe.width - width) / 2;
    const int inner_x = x + pad;
    const int gap = context.px(metrics.sheet_gap);
    Column column;
    column.top.height = add_text(
                            context,
                            column.top,
                            ItemRole::title,
                            inner_x,
                            0,
                            inner,
                            sheet.title,
                            {metrics.sheet_title, true, text_colour, TextRole::title},
                            Fit::column,
                            3
                        ) +
                        gap;
    Block& body = column.body;
    const Style text{metrics.sheet_text, false, dim_colour, TextRole::body};
    const Style strong{metrics.sheet_text, false, text_colour, TextRole::body};
    const Style warning{metrics.sheet_text, false, amber_colour, TextRole::body};
    const Style small{metrics.sheet_text - 1.0f, false, dim_colour, TextRole::row_detail};
    int y = 0;
    const auto paragraph = [&](std::string_view words, const Style& style) {
        const int height = add_text(context, body, ItemRole::text, inner_x, y, inner, words, style);
        if (height > 0)
            y += height + gap;
    };
    for (const std::string& words : sheet.paragraphs)
        paragraph(words, text);
    for (const detail::SheetRow& row : sheet.rows) {
        const int size = context.font(strong.points);
        const int line_height = context.line(size, false);
        const int size_width = add_line(
            context,
            body,
            inner_x + inner,
            y + line_height / 2,
            inner / 3,
            row.right,
            text,
            Fit::tight_right
        );
        add_line(
            context,
            body,
            inner_x,
            y + line_height / 2,
            inner - size_width - (size_width > 0 ? gap : 0),
            row.text,
            strong
        );
        y += line_height;
        const int small_height = context.line(context.font(small.points), false);
        add_line(context, body, inner_x, y + small_height / 2, inner, row.detail, small);
        y += small_height + context.px(6.0f);
    }
    if (!sheet.rows.empty())
        y += gap - context.px(6.0f);
    for (const std::string& line : sheet.lines)
        paragraph(line, strong);
    for (const std::string& note : sheet.notes)
        paragraph(note, warning);
    for (const std::string& words : sheet.after)
        paragraph(words, text);
    body.height = y > 0 ? y - gap : 0;
    Block& bottom = column.bottom;
    const int button_height = context.px(min_button_points);
    int at = 0;
    for (const Action& action : sheet.actions) {
        add_button(
            context, bottom, {inner_x, at, inner, button_height}, action, metrics.button_text
        );
        at += button_height + gap;
    }
    bottom.height = at > 0 ? at - gap : 0;

    const int between = body.height > 0 && bottom.height > 0 ? context.px(metrics.block_gap) : 0;
    const int natural = column.top.height + body.height + between + bottom.height;
    const int room = std::max(1, safe.height - 2 * margin - 2 * pad);
    const int height = std::min(natural, room) + 2 * pad;
    const int y_sheet = safe.y + (safe.height - height) / 2;
    Item panel{};
    panel.role = ItemRole::sheet;
    panel.box = {x, y_sheet, width, height};
    panel.colour = line_colour;
    layout.items.push_back(panel);
    const Region region = place_column(
        context,
        layout.items,
        {x, y_sheet + pad, width, height - 2 * pad},
        column,
        context.model.scroll_points
    );
    layout.rows = region.rows;
    layout.scroll_max_points =
        region.scroll_max > 0
            ? static_cast<int32_t>(std::ceil(static_cast<float>(region.scroll_max) / context.scale))
            : 0;
}

/// Returns the part of an item's box that can be seen and pressed: the box, cut by its clip.
///
/// @param item the item
/// @return the visible box; empty when none of it shows
Rect visible_box(const Item& item) noexcept {
    if (item.clip.width <= 0 || item.clip.height <= 0)
        return item.box;
    const int left = std::max(item.box.x, item.clip.x);
    const int top = std::max(item.box.y, item.clip.y);
    const int right = std::min(item.box.x + item.box.width, item.clip.x + item.clip.width);
    const int bottom = std::min(item.box.y + item.box.height, item.clip.y + item.clip.height);
    if (right <= left || bottom <= top)
        return {};
    return {left, top, right - left, bottom - top};
}

/// Returns the first item a press can reach: the one after the last backdrop.
///
/// @param layout the layout
/// @return its index
std::size_t first_live_item(const Layout& layout) noexcept {
    std::size_t first = 0;
    for (std::size_t index = 0; index < layout.items.size(); ++index)
        if (layout.items[index].role == ItemRole::backdrop)
            first = index + 1;
    return first;
}

} // namespace

DeviceClass device_class(const Viewport& viewport) noexcept {
    const float scale = viewport.px_per_point > 0.0f ? viewport.px_per_point : 1.0f;
    return oa::ui::touch_hud::classify_device(
        static_cast<int>(std::lround(static_cast<float>(viewport.width) / scale)),
        static_cast<int>(std::lround(static_cast<float>(viewport.height) / scale))
    );
}

Layout lay_out(const Model& model, const Viewport& viewport, const TextMeasureHooks& measure) {
    Layout layout;
    layout.device = device_class(viewport);
    const float scale = viewport.px_per_point > 0.0f ? viewport.px_per_point : 1.0f;
    layout.px_per_point = scale;
    const Rect safe{
        viewport.safe.left,
        viewport.safe.top,
        viewport.width - viewport.safe.left - viewport.safe.right,
        viewport.height - viewport.safe.top - viewport.safe.bottom,
    };
    if (safe.width <= 0 || safe.height <= 0)
        return layout;
    // A tablet window too short for the tablet's sizes (a 640x480 window) takes the phone's
    // compact sizes; its form stays the tablet's.
    const bool compact = layout.device == DeviceClass::phone ||
                         static_cast<float>(std::min(viewport.width, viewport.height)) / scale <
                             compact_short_side_points;
    const Context context{model, measure, compact ? phone_metrics : tablet_metrics, scale, compact};
    const Metrics& metrics = context.metrics;

    Block header;
    const int header_bottom = add_header(context, header, safe);
    for (Item& item : header.items)
        layout.items.push_back(std::move(item));

    int side = context.px(metrics.side_pad);
    if (metrics.max_content_width > 0.0f)
        side = std::max(side, (safe.width - context.px(metrics.max_content_width)) / 2);
    side = std::min(side, safe.width / 4);
    const int x = safe.x + side;
    const int room = safe.width - 2 * side;
    Column column = step_column(context, x, room);
    const int safe_bottom = safe.y + safe.height;
    const int footer_top = safe_bottom - column.footer.height;
    const int area_top = header_bottom + context.px(metrics.content_top);
    const int area_bottom = footer_top - context.px(metrics.content_bottom);
    const Rect area{safe.x, area_top, safe.width, std::max(1, area_bottom - area_top)};
    const bool sheet = model.sheet != Sheet::none;
    const Region region =
        place_column(context, layout.items, area, column, sheet ? 0 : model.scroll_points);
    for (Item& item : column.footer.items) {
        item.box.y += footer_top;
        layout.items.push_back(std::move(item));
    }
    if (sheet) {
        add_sheet(context, layout, viewport, safe);
    } else {
        layout.rows = region.rows;
        layout.scroll_max_points =
            region.scroll_max > 0
                ? static_cast<int32_t>(std::ceil(static_cast<float>(region.scroll_max) / scale))
                : 0;
    }
    for (std::size_t index = first_live_item(layout); index < layout.items.size(); ++index) {
        const Item& item = layout.items[index];
        if (item.control.kind == ControlKind::none || !item.enabled)
            continue;
        if (std::find(layout.focus_order.begin(), layout.focus_order.end(), item.control) ==
            layout.focus_order.end())
            layout.focus_order.push_back(item.control);
    }
    return layout;
}

Control hit_test(const Layout& layout, Point point, float reach_px) noexcept {
    const std::size_t first = first_live_item(layout);
    Control nearest{};
    double nearest_distance = static_cast<double>(std::max(0.0f, reach_px));
    bool found = false;
    for (std::size_t index = layout.items.size(); index-- > first;) {
        const Item& item = layout.items[index];
        if (item.control.kind == ControlKind::none || !item.enabled)
            continue;
        const Rect box = visible_box(item);
        if (box.width <= 0 || box.height <= 0)
            continue;
        if (point.x >= box.x && point.x < box.x + box.width && point.y >= box.y &&
            point.y < box.y + box.height)
            return item.control;
        const int dx = point.x < box.x                ? box.x - point.x
                       : point.x >= box.x + box.width ? point.x - (box.x + box.width - 1)
                                                      : 0;
        const int dy = point.y < box.y                 ? box.y - point.y
                       : point.y >= box.y + box.height ? point.y - (box.y + box.height - 1)
                                                       : 0;
        const double distance =
            std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy);
        if (distance <= nearest_distance && (!found || distance < nearest_distance)) {
            nearest = item.control;
            nearest_distance = distance;
            found = true;
        }
    }
    return found ? nearest : Control{};
}

void mark_interaction(Layout& layout, const Interaction& interaction) noexcept {
    for (Item& item : layout.items) {
        if (item.control.kind == ControlKind::none) {
            item.focused = false;
            item.pressed = false;
            continue;
        }
        item.focused = interaction.focus_shown && item.control == interaction.focused;
        item.pressed =
            interaction.pressed.kind != ControlKind::none && item.control == interaction.pressed;
    }
}

} // namespace oa::ui::game_files
