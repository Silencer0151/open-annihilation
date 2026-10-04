// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The touch layer: the controls, the radial order menu, the sheets, the
// phone's build drawer and MORE sheet frames, the banner, the status pill,
// the tip and the placement bar, painted into a layer of their own at the
// display's pixels, composed over the CPU frame and presented in every tier
// (docs/touch-controls.md). The layer is drawn again only when the HUD
// state's revision, the layout of the controls, the window's size or its
// density changes, and only its painted rows are uploaded.
#include "oa/app/runtime.hpp"
#include "render_run.hpp"
#include "touch_paint.hpp"
#include "touch_state.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

namespace oa::app {

namespace {

namespace hud = oa::ui::touch_hud;
namespace paint = touch_paint;
namespace text_font = oa::platform::text_font;
using hud::Control;

// ---- Style: the approved settings mock-up's colours --------------------------------------

/// Panels and buttons: gunmetal at 85% opacity.
constexpr paint::Rgba panel_colour{0x1b, 0x1e, 0x19, 217};
/// Sheets and the radial's ring: darker and nearly opaque, over the battlefield.
constexpr paint::Rgba sheet_colour{0x14, 0x16, 0x12, 248};
/// An empty cell of the drawer or the MORE sheet, which a 3.1c gadget covers.
constexpr paint::Rgba cell_colour{0x24, 0x28, 0x20, 230};
/// A control a finger rests on: a lighter gunmetal.
constexpr paint::Rgba pressed_colour{0x3b, 0x42, 0x35, 240};
/// The hairline round panels and buttons.
constexpr paint::Rgba edge_colour{0xff, 0xff, 0xff, 34};
/// Lit, latched and armed controls.
constexpr paint::Rgba lit_colour{0x9c, 0xcc, 0x3c, 255};
/// A lit control a finger rests on.
constexpr paint::Rgba lit_pressed_colour{0xb8, 0xde, 0x66, 255};
/// Labels and icons on a lit control.
constexpr paint::Rgba ink_colour{0x1b, 0x1e, 0x19, 255};
/// Labels and icons.
constexpr paint::Rgba label_colour{0xf2, 0xf4, 0xee, 255};
/// The quieter text of the drawer's caption and page dots.
constexpr paint::Rgba quiet_colour{0xb4, 0xb9, 0xae, 255};
/// SELF-DESTRUCT · HOLD's red: its edge, and its fill as the hold fills it.
constexpr paint::Rgba danger_colour{0xc0, 0x39, 0x2b, 255};
/// SELF-DESTRUCT · HOLD's panel before its hold fills it.
constexpr paint::Rgba danger_panel_colour{0x2a, 0x15, 0x12, 235};
/// SELF-DESTRUCT · HOLD's label before its hold fills it.
constexpr paint::Rgba danger_label_colour{0xf0, 0x8c, 0x80, 255};
/// The lines between the radial's wedges.
constexpr paint::Rgba wedge_gap_colour{0x08, 0x09, 0x07, 255};
/// Greyed items show at this opacity.
constexpr float greyed_opacity = 0.4F;

// ---- Sizes, in points ---------------------------------------------------------------------

constexpr float corner_points = 6.0F;       ///< corner radius of panels and buttons
constexpr float edge_points = 1.0F;         ///< hairline width
constexpr float lit_edge_points = 2.0F;     ///< the selected group's and the default wedge's edge
constexpr float large_icon_points = 18.0F;  ///< icon over a label on a tall button
constexpr float small_icon_points = 15.0F;  ///< icon over a label on a 44 pt button
constexpr float row_icon_points = 16.0F;    ///< icon beside a label
constexpr float label_points = 10.0F;       ///< a label under an icon on a tall button
constexpr float small_label_points = 9.0F;  ///< a label under an icon on a 44 pt button
constexpr float row_label_points = 11.0F;   ///< a label beside an icon
constexpr float sub_label_points = 7.0F;    ///< LATCHED, a group's unit count
constexpr float stack_gap_points = 3.0F;    ///< between an icon and its label
constexpr float row_gap_points = 6.0F;      ///< between an icon and its label beside it
constexpr float text_pad_points = 4.0F;     ///< kept clear inside a button's sides
constexpr float tall_button_points = 50.0F; ///< buttons this tall get the larger icon
constexpr float title_points = 11.5F;       ///< banner and drawer titles, the status selection
constexpr float hint_points = 9.5F;         ///< banner and status hints
constexpr float tip_points = 10.0F;         ///< tip bubble text
constexpr float pill_pad_points = 14.0F;    ///< text inset of the banner and the status pill
constexpr float dot_points = 5.0F;          ///< page dot diameter
constexpr float dot_gap_points = 4.0F;      ///< between page dots
/// The cap height of the bundled fonts, as a share of the pixel size: used
/// to centre capitals on a row.
constexpr float cap_share = 0.73F;

/// Translates interface text into the game's language.
using Translate = std::function<std::string(std::string_view)>;

/// What the painting needs: the painter, the fonts, the layer's scale and the translation.
struct PaintContext {
    paint::Painter& painter;       ///< paints the layer
    text_font::FontStack* fonts{}; ///< the label fonts; null draws no text
    float scale_x{1.0F};           ///< layer pixels per layout pixel, across
    float scale_y{1.0F};           ///< layer pixels per layout pixel, down
    float point{1.0F};             ///< layer pixels per point
    const Translate& translate;    ///< interface text into the game's language
    const hud::HudState& state;    ///< what the controls show
    const hud::Frame& frame;       ///< where they are
};

/// How a control's icon and label are arranged inside it.
enum class Arrangement : uint8_t {
    stacked,   ///< the icon over the label
    row,       ///< the icon left of the label
    icon_only, ///< the icon alone, as large as fits
    text_only, ///< the label alone
};

/// The looks a control can show at once.
struct ControlLook {
    bool pressed{};  ///< a finger rests on it
    bool lit{};      ///< lit: armed, latched, on, the open sheet's button
    bool lit_edge{}; ///< lit by its edge and label only (the selected group's chip)
    bool greyed{};   ///< shown at 40%: it does nothing now
    bool latched{};  ///< marked LATCHED under its label
};

/// Returns a point's size in layer pixels.
///
/// @param context the painting
/// @param points points
/// @return layer pixels
float px(const PaintContext& context, float points) {
    return points * context.point;
}

/// Returns a font size in whole layer pixels.
///
/// @param context the painting
/// @param points the size in points
/// @return pixels per em, at least 6
int font_px(const PaintContext& context, float points) {
    return std::max(6, static_cast<int>(std::lround(px(context, points))));
}

/// Returns a layout rectangle in layer pixels.
///
/// @param context the painting
/// @param rect layout pixels
/// @return the same place in layer pixels
paint::Area area_of(const PaintContext& context, const hud::Rect& rect) {
    return {
        static_cast<float>(rect.x) * context.scale_x,
        static_cast<float>(rect.y) * context.scale_y,
        static_cast<float>(rect.width) * context.scale_x,
        static_cast<float>(rect.height) * context.scale_y
    };
}

/// Returns whether a rectangle holds no pixel.
///
/// @param rect the rectangle
/// @return true when its width or height is 0 or less
bool empty(const hud::Rect& rect) {
    return rect.width <= 0 || rect.height <= 0;
}

/// Returns whether one rectangle lies inside another, a pixel's slack allowed.
///
/// @param inner the rectangle tested
/// @param outer the rectangle it may lie in
/// @return true when it does
bool inside(const hud::Rect& inner, const hud::Rect& outer) {
    return !empty(outer) && inner.x >= outer.x - 1 && inner.y >= outer.y - 1 &&
           inner.x + inner.width <= outer.x + outer.width + 1 &&
           inner.y + inner.height <= outer.y + outer.height + 1;
}

/// Returns a colour greyed when an item does nothing now.
///
/// @param colour the colour
/// @param greyed whether the item is greyed
/// @return the colour, at 40% when greyed
paint::Rgba greyed_if(paint::Rgba colour, bool greyed) {
    return greyed ? paint::with_opacity(colour, greyed_opacity) : colour;
}

/// Returns how wide a line of text is drawn, in layer pixels.
///
/// @param context the painting
/// @param text the text
/// @param size_points its size
/// @return pixels; 0 without fonts
float text_width(const PaintContext& context, std::string_view text, float size_points) {
    if (context.fonts == nullptr || text.empty())
        return 0.0F;
    return static_cast<float>(
        paint::text_width(*context.fonts, text, font_px(context, size_points), true)
    );
}

/// Draws a line of capitals from a left edge with their middle on a row,
/// shortened to a width.
///
/// @param context the painting
/// @param text the text
/// @param left the pen's start, layer pixels
/// @param middle the row the capitals' middle lies on, layer pixels
/// @param size_points the text's size
/// @param colour its colour
/// @param max_width the widest it may be, layer pixels
/// @return the width drawn, layer pixels
float text_from(
    const PaintContext& context,
    std::string_view text,
    float left,
    float middle,
    float size_points,
    paint::Rgba colour,
    float max_width
) {
    if (context.fonts == nullptr || text.empty() || max_width <= 0.0F)
        return 0.0F;
    const int size = font_px(context, size_points);
    const std::string fitted =
        paint::fit_text(*context.fonts, text, size, true, static_cast<int>(max_width));
    if (fitted.empty())
        return 0.0F;
    const auto line = paint::draw_line(*context.fonts, fitted, size, true);
    const int baseline = static_cast<int>(std::lround(middle + cap_share * size * 0.5F));
    paint::paint_line(context.painter, line, static_cast<int>(std::lround(left)), baseline, colour);
    return static_cast<float>(line.coverage.advance);
}

/// Draws a line of capitals centred on a point, shortened to a width.
///
/// @param context the painting
/// @param text the text
/// @param centre_x the column its middle lies on, layer pixels
/// @param middle the row the capitals' middle lies on, layer pixels
/// @param size_points the text's size
/// @param colour its colour
/// @param max_width the widest it may be, layer pixels
void text_centred(
    const PaintContext& context,
    std::string_view text,
    float centre_x,
    float middle,
    float size_points,
    paint::Rgba colour,
    float max_width
) {
    if (context.fonts == nullptr || text.empty() || max_width <= 0.0F)
        return;
    const int size = font_px(context, size_points);
    const std::string fitted =
        paint::fit_text(*context.fonts, text, size, true, static_cast<int>(max_width));
    if (fitted.empty())
        return;
    const float width = static_cast<float>(paint::text_width(*context.fonts, fitted, size, true));
    text_from(context, fitted, centre_x - width * 0.5F, middle, size_points, colour, max_width + 1);
}

/// Returns the icon an order shows on the rail, the radial and the MORE sheet.
///
/// @param order the order
/// @return its icon
paint::Icon order_icon(hud::Order order) {
    switch (order) {
    case hud::Order::move:
        return paint::Icon::arrow;
    case hud::Order::attack:
        return paint::Icon::crosshair;
    case hud::Order::patrol:
        return paint::Icon::patrol;
    case hud::Order::guard:
        return paint::Icon::shield;
    case hud::Order::stop:
        return paint::Icon::square;
    case hud::Order::blast:
        return paint::Icon::star;
    case hud::Order::reclaim:
        return paint::Icon::tray_down;
    case hud::Order::repair:
        return paint::Icon::wrench;
    case hud::Order::capture:
        return paint::Icon::flag;
    case hud::Order::load:
        return paint::Icon::tray_up;
    case hud::Order::unload:
        return paint::Icon::tray_down;
    }
    return paint::Icon::none;
}

/// Returns the icon of a radial item.
///
/// @param item the item
/// @return its icon
paint::Icon radial_icon(hud::RadialItem item) {
    if (item == hud::RadialItem::info)
        return paint::Icon::info;
    if (item == hud::RadialItem::type)
        return paint::Icon::box_select;
    const auto order = hud::radial_order(item);
    return order ? order_icon(*order) : paint::Icon::none;
}

/// Returns the label of a radial item: its order's label, INFO or TYPE; the
/// repair wedge reads ASSIST when a plain tap would assist.
///
/// @param item the item
/// @param state what the controls show
/// @return the label, before translation
std::string_view radial_label(hud::RadialItem item, const hud::HudState& state) {
    if (item == hud::RadialItem::info)
        return "INFO";
    if (item == hud::RadialItem::type)
        return "TYPE";
    if (item == hud::RadialItem::repair && state.tap_action == hud::TapAction::assist)
        return "ASSIST";
    const auto order = hud::radial_order(item);
    return order ? hud::order_label(*order) : std::string_view{};
}

/// Returns the rail slot a control shows, when its index names one in use.
///
/// @param state what the controls show
/// @param index the slot
/// @return the slot; null beyond rail_count
const hud::RailSlot* rail_slot(const hud::HudState& state, uint8_t index) {
    if (index >= state.rail_count || index >= state.rail.size())
        return nullptr;
    return &state.rail[index];
}

/// Returns the icon a control shows.
///
/// @param control the control
/// @param state what the controls show
/// @return its icon; none for a label alone
paint::Icon control_icon(const hud::ControlRect& control, const hud::HudState& state) {
    switch (control.control) {
    case Control::queue:
        return paint::Icon::shift_arrow;
    case Control::add:
    case Control::group_store:
        return paint::Icon::plus;
    case Control::clear:
    case Control::place_cancel:
    case Control::banner_cancel:
        return paint::Icon::cross;
    case Control::select_menu:
        return paint::Icon::box_select;
    case Control::pause:
        return paint::Icon::pause;
    case Control::speed:
        return paint::Icon::gauge;
    case Control::chat:
        return paint::Icon::speech;
    case Control::centre:
        return paint::Icon::crosshair;
    case Control::follow:
        return paint::Icon::eye;
    case Control::next_unit:
        return paint::Icon::chevrons;
    case Control::info:
        return paint::Icon::info;
    case Control::menu:
        return paint::Icon::menu;
    case Control::build_drawer:
        return paint::Icon::grid;
    case Control::zoom_out:
        return paint::Icon::magnifier_minus;
    case Control::zoom_in:
        return paint::Icon::magnifier_plus;
    case Control::order_slot:
        if (const auto* slot = rail_slot(state, control.index))
            return slot->more ? paint::Icon::dots : order_icon(slot->order);
        return paint::Icon::none;
    case Control::more:
        return paint::Icon::dots;
    case Control::drawer_close:
    case Control::drawer_prev:
        return paint::Icon::chevron_left;
    case Control::drawer_next:
        return paint::Icon::chevron_right;
    case Control::more_item:
        return control.index == static_cast<uint8_t>(hud::MoreItem::self_destruct)
                   ? paint::Icon::warning
                   : paint::Icon::info;
    case Control::none:
    case Control::times_five:
    case Control::group_chip:
    case Control::drawer_build_tab:
    case Control::drawer_orders_tab:
    case Control::menu_item:
    case Control::radial_item:
    case Control::radial_hub:
    case Control::sheet_outside:
        return paint::Icon::none;
    }
    return paint::Icon::none;
}

/// Returns a control's label, before translation: touch_hud's, else the
/// control's own name for the few the model leaves without one.
///
/// @param control the control
/// @param state what the controls show
/// @return the label
std::string_view control_text(const hud::ControlRect& control, const hud::HudState& state) {
    if (control.control == Control::menu_item)
        return hud::menu_item_label(state.sheet, control.index);
    const auto label = hud::control_label(control.control, control.index, state);
    if (!label.empty())
        return label;
    switch (control.control) {
    case Control::queue:
        return "QUEUE";
    case Control::add:
        return "ADD";
    case Control::times_five:
        return "x5";
    case Control::clear:
        return "CLEAR";
    case Control::select_menu:
        return "SELECT";
    case Control::group_store:
        return "STORE";
    case Control::pause:
        return "PAUSE";
    case Control::speed:
        return "SPEED";
    case Control::chat:
        return "CHAT";
    case Control::centre:
        return "CENTRE";
    case Control::follow:
        return "FOLLOW";
    case Control::next_unit:
        return "NEXT";
    case Control::info:
        return "INFO";
    case Control::menu:
        return "MENU";
    case Control::build_drawer:
        return state.builder_selected ? "BUILD" : "ORDERS";
    case Control::order_slot:
        if (const auto* slot = rail_slot(state, control.index))
            return slot->more ? std::string_view{"MORE"} : hud::order_label(slot->order);
        return {};
    case Control::more:
        return "MORE";
    case Control::drawer_build_tab:
        return "BUILD";
    case Control::drawer_orders_tab:
        return "ORDERS";
    case Control::place_cancel:
        return "CANCEL";
    case Control::more_item:
        return control.index == static_cast<uint8_t>(hud::MoreItem::self_destruct)
                   ? std::string_view{"SELF-DESTRUCT \xC2\xB7 HOLD"}
                   : std::string_view{"INFO"};
    default:
        return {};
    }
}

/// Returns whether a finger rests on a control.
///
/// @param state what the controls show
/// @param control the control
/// @param index its index
/// @return true when one of the pressed entries names it
bool is_pressed(const hud::HudState& state, Control control, uint8_t index) {
    return std::any_of(state.pressed.begin(), state.pressed.end(), [&](const hud::Pressed& entry) {
        return entry.control != Control::none && entry.control == control && entry.index == index;
    });
}

/// Returns the looks a control shows now, from the HUD state.
///
/// @param control the control
/// @param state what the controls show
/// @return its looks
ControlLook look_of(const hud::ControlRect& control, const hud::HudState& state) {
    ControlLook look{};
    look.pressed = is_pressed(state, control.control, control.index);
    const auto latch = [&](hud::Latch which) {
        look.lit = state.latches.active(which);
        look.latched = state.latches.latched(which);
    };
    switch (control.control) {
    case Control::queue:
        latch(hud::Latch::queue);
        break;
    case Control::add:
        latch(hud::Latch::add);
        break;
    case Control::times_five:
        latch(hud::Latch::times_five);
        break;
    case Control::select_menu:
        look.lit = state.sheet == hud::Sheet::select_menu;
        break;
    case Control::group_chip:
        look.lit_edge = control.index != 0 && state.selected_group == control.index;
        break;
    case Control::pause:
        look.lit = state.paused;
        break;
    case Control::speed:
        look.lit = state.sheet == hud::Sheet::speed;
        break;
    case Control::chat:
        look.lit = state.chat_open;
        break;
    case Control::follow:
        look.lit = state.following;
        break;
    case Control::info:
        look.greyed = !state.has_selection;
        break;
    case Control::menu:
        look.lit = state.menu_open || state.sheet == hud::Sheet::phone_menu;
        break;
    case Control::build_drawer:
        look.lit = state.sheet == hud::Sheet::drawer;
        break;
    case Control::order_slot:
        if (const auto* slot = rail_slot(state, control.index)) {
            look.lit = slot->more ? state.sheet == hud::Sheet::more : slot->lit;
            look.greyed = !slot->more && !slot->available;
        }
        break;
    case Control::more:
        look.lit = state.sheet == hud::Sheet::more;
        break;
    case Control::drawer_build_tab:
        look.lit = state.drawer_tab == hud::DrawerTab::build;
        break;
    case Control::drawer_orders_tab:
        look.lit = state.drawer_tab == hud::DrawerTab::orders;
        break;
    case Control::drawer_prev:
        look.greyed = state.drawer_page == 0;
        break;
    case Control::drawer_next:
        look.greyed = state.drawer_page + 1 >= state.drawer_pages;
        break;
    case Control::more_item:
        look.greyed =
            control.index == static_cast<uint8_t>(hud::MoreItem::info) && !state.has_selection;
        break;
    default:
        break;
    }
    return look;
}

/// Returns how a control's icon and label are arranged, from its kind and size.
///
/// @param control the control
/// @param width_points its width in points
/// @param height_points its height in points
/// @param icon its icon
/// @param label its label
/// @return the arrangement
Arrangement arrangement_of(
    Control control,
    float width_points,
    float height_points,
    paint::Icon icon,
    std::string_view label
) {
    if (icon == paint::Icon::none)
        return Arrangement::text_only;
    if (label.empty())
        return Arrangement::icon_only;
    switch (control) {
    case Control::zoom_out:
    case Control::zoom_in:
    case Control::drawer_close:
    case Control::drawer_prev:
    case Control::drawer_next:
    case Control::banner_cancel:
        return Arrangement::icon_only;
    case Control::pause:
    case Control::menu:
        if (width_points < 56.0F && height_points < 56.0F)
            return Arrangement::icon_only;
        return width_points >= height_points * 1.3F ? Arrangement::row : Arrangement::stacked;
    case Control::group_store:
        if (label == "+" || width_points < 56.0F)
            return Arrangement::icon_only;
        break;
    case Control::build_drawer:
    case Control::place_cancel:
    case Control::more_item:
        return Arrangement::row;
    default:
        break;
    }
    return height_points >= 40.0F ? Arrangement::stacked : Arrangement::row;
}

/// Paints a button's face: its fill and edge for its looks.
///
/// @param context the painting
/// @param area the button, layer pixels
/// @param look its looks
void paint_face(const PaintContext& context, paint::Area area, const ControlLook& look) {
    const float radius = px(context, corner_points);
    paint::Rgba fill = panel_colour;
    if (look.lit)
        fill = look.pressed ? lit_pressed_colour : lit_colour;
    else if (look.pressed)
        fill = pressed_colour;
    context.painter.fill_rounded_rect(area, radius, fill);
    if (look.lit_edge)
        context.painter.outline_rounded_rect(
            area, radius, px(context, lit_edge_points), lit_colour
        );
    else if (!look.lit)
        context.painter.outline_rounded_rect(area, radius, px(context, edge_points), edge_colour);
}

/// Returns the colour a control's icon and label are drawn in.
///
/// @param look its looks
/// @return the colour
paint::Rgba content_colour(const ControlLook& look) {
    if (look.lit)
        return greyed_if(ink_colour, look.greyed);
    if (look.lit_edge)
        return lit_colour;
    return greyed_if(label_colour, look.greyed);
}

/// Paints an icon and a label inside an area, arranged as asked.
///
/// @param context the painting
/// @param area the area, layer pixels
/// @param arrangement how they are arranged
/// @param icon the icon
/// @param label the label, translated
/// @param sub_label a smaller line under the label (LATCHED); empty for none
/// @param colour their colour
/// @param text_points the label's size; 0 picks the arrangement's
void paint_content(
    const PaintContext& context,
    paint::Area area,
    Arrangement arrangement,
    paint::Icon icon,
    std::string_view label,
    std::string_view sub_label,
    paint::Rgba colour,
    float text_points = 0.0F
) {
    const float height_points = area.height / context.point;
    const float pad = px(context, text_pad_points);
    const float centre_x = area.x + area.width * 0.5F;
    const float centre_y = area.y + area.height * 0.5F;
    const float room = area.width - 2.0F * pad;
    // A sub-label that does not fit whole is left out: the look still says it.
    if (!sub_label.empty() && text_width(context, sub_label, sub_label_points) > room)
        sub_label = {};
    switch (arrangement) {
    case Arrangement::icon_only: {
        const float side = std::min(px(context, 22.0F), std::min(area.width, area.height) * 0.55F);
        context.painter.draw_icon(
            icon, paint::Area{centre_x - side * 0.5F, centre_y - side * 0.5F, side, side}, colour
        );
        return;
    }
    case Arrangement::text_only: {
        const float size = text_points > 0.0F ? text_points : row_label_points;
        if (sub_label.empty()) {
            text_centred(context, label, centre_x, centre_y, size, colour, room);
            return;
        }
        const float cap = cap_share * static_cast<float>(font_px(context, size));
        const float sub_cap = cap_share * static_cast<float>(font_px(context, sub_label_points));
        const float gap = px(context, stack_gap_points);
        const float top = centre_y - (cap + gap + sub_cap) * 0.5F;
        text_centred(context, label, centre_x, top + cap * 0.5F, size, colour, room);
        text_centred(
            context,
            sub_label,
            centre_x,
            top + cap + gap + sub_cap * 0.5F,
            sub_label_points,
            colour,
            room
        );
        return;
    }
    case Arrangement::row: {
        const float side = px(context, row_icon_points);
        const float gap = px(context, row_gap_points);
        const float size = text_points > 0.0F ? text_points : row_label_points;
        const float width = std::min(text_width(context, label, size), room - side - gap);
        const float left = centre_x - (side + gap + width) * 0.5F;
        context.painter.draw_icon(
            icon, paint::Area{left, centre_y - side * 0.5F, side, side}, colour
        );
        text_from(context, label, left + side + gap, centre_y, size, colour, width + 1.0F);
        return;
    }
    case Arrangement::stacked: {
        const bool tall = height_points >= tall_button_points;
        const float side = px(context, tall ? large_icon_points : small_icon_points);
        const float size =
            text_points > 0.0F ? text_points : (tall ? label_points : small_label_points);
        const float cap = cap_share * static_cast<float>(font_px(context, size));
        const float sub_cap =
            sub_label.empty() ? 0.0F
                              : cap_share * static_cast<float>(font_px(context, sub_label_points));
        const float gap = px(context, stack_gap_points);
        const float block = side + gap + cap + (sub_label.empty() ? 0.0F : gap + sub_cap);
        const float top = centre_y - block * 0.5F;
        context.painter.draw_icon(
            icon, paint::Area{centre_x - side * 0.5F, top, side, side}, colour
        );
        text_centred(context, label, centre_x, top + side + gap + cap * 0.5F, size, colour, room);
        if (!sub_label.empty())
            text_centred(
                context,
                sub_label,
                centre_x,
                top + side + gap + cap + gap + sub_cap * 0.5F,
                sub_label_points,
                colour,
                room
            );
        return;
    }
    }
}

/// Paints a group chip: its number large and its unit count small; the
/// selection's group lit by its edge and number.
///
/// @param context the painting
/// @param control the chip
/// @param look its looks
void paint_group_chip(
    const PaintContext& context, const hud::ControlRect& control, const ControlLook& look
) {
    const auto area = area_of(context, control.rect);
    paint_face(context, area, look);
    const auto colour = content_colour(look);
    const uint8_t group = control.index;
    const uint16_t units =
        group < context.state.group_counts.size() ? context.state.group_counts[group] : uint16_t{0};
    const bool wide = area.width / context.point >= 64.0F;
    const float number_points = wide ? 17.0F : 15.0F;
    const std::string number = std::to_string(group);
    std::string count = std::to_string(units);
    if (wide)
        count = units == 1 ? context.translate("1 UNIT")
                           : std::to_string(units) + " " + context.translate("UNITS");
    const float cap = cap_share * static_cast<float>(font_px(context, number_points));
    const float sub_cap = cap_share * static_cast<float>(font_px(context, sub_label_points + 1.0F));
    const float gap = px(context, 5.0F);
    const float top = area.y + (area.height - (cap + gap + sub_cap)) * 0.5F;
    const float centre_x = area.x + area.width * 0.5F;
    const float room = area.width - 2.0F * px(context, text_pad_points);
    text_centred(context, number, centre_x, top + cap * 0.5F, number_points, colour, room);
    text_centred(
        context,
        count,
        centre_x,
        top + cap + gap + sub_cap * 0.5F,
        sub_label_points + 1.0F,
        look.lit_edge ? label_colour : colour,
        room
    );
}

/// Paints SELF-DESTRUCT · HOLD: a red-edged panel that fills red from the
/// left as the hold runs.
///
/// @param context the painting
/// @param control the item
/// @param look its looks
void paint_self_destruct(
    const PaintContext& context, const hud::ControlRect& control, const ControlLook& look
) {
    const auto area = area_of(context, control.rect);
    const float radius = px(context, corner_points);
    context.painter.fill_rounded_rect(
        area, radius, look.pressed ? pressed_colour : danger_panel_colour
    );
    const float progress = std::clamp(context.state.self_destruct_progress, 0.0F, 1.0F);
    if (progress > 0.0F) {
        // The fill keeps the panel's rounded corners: clip a full red face.
        const auto saved = context.painter.clip();
        const paint::Box fill{
            static_cast<int>(std::floor(area.x)),
            static_cast<int>(std::floor(area.y)),
            static_cast<int>(std::lround(area.width * progress)),
            static_cast<int>(std::ceil(area.height)) + 1
        };
        context.painter.set_clip(paint::intersect(saved, fill));
        context.painter.fill_rounded_rect(area, radius, danger_colour);
        context.painter.set_clip(saved);
    }
    context.painter.outline_rounded_rect(area, radius, px(context, edge_points), danger_colour);
    const auto label = context.translate(control_text(control, context.state));
    const auto colour = progress > 0.0F ? label_colour : danger_label_colour;
    paint_content(context, area, Arrangement::row, paint::Icon::warning, label, {}, colour, 10.0F);
}

/// Paints one control with its looks, icon and label.
///
/// @param context the painting
/// @param control the control
void paint_control(const PaintContext& context, const hud::ControlRect& control) {
    if (empty(control.rect))
        return;
    switch (control.control) {
    case Control::none:
    case Control::radial_item:
    case Control::radial_hub:
    case Control::sheet_outside:
        return; // the radial is painted whole; outside a sheet nothing shows
    default:
        break;
    }
    const auto look = look_of(control, context.state);
    if (control.control == Control::group_chip) {
        paint_group_chip(context, control, look);
        return;
    }
    if (control.control == Control::more_item &&
        control.index == static_cast<uint8_t>(hud::MoreItem::self_destruct)) {
        paint_self_destruct(context, control, look);
        return;
    }
    const auto area = area_of(context, control.rect);
    const auto icon = control_icon(control, context.state);
    auto label = context.translate(control_text(control, context.state));
    // The drawer slides out (BUILD ▸) and SELECT opens a menu (SELECT ▾), as
    // the mock-ups mark them, where the mark fits beside the label whole.
    const auto mark = control.control == Control::build_drawer  ? std::string_view{"\xE2\x96\xB8"}
                      : control.control == Control::select_menu ? std::string_view{"\xE2\x96\xBE"}
                                                                : std::string_view{};
    if (!mark.empty() && !label.empty() && label.find(mark) == std::string::npos) {
        const std::string marked = label + " " + std::string(mark);
        const float room =
            area_of(context, control.rect).width - 2.0F * px(context, text_pad_points);
        const float icon_room = control.control == Control::build_drawer
                                    ? px(context, row_icon_points + row_gap_points)
                                    : 0.0F;
        const float size =
            control.control == Control::build_drawer ? row_label_points
            : area_of(context, control.rect).height / context.point >= tall_button_points
                ? label_points
                : small_label_points;
        if (text_width(context, marked, size) <= room - icon_room)
            label = marked;
    }

    const float width_points = area.width / context.point;
    const float height_points = area.height / context.point;
    const auto arrangement =
        arrangement_of(control.control, width_points, height_points, icon, label);
    if (control.control == Control::banner_cancel) {
        // The banner's ✕ sits on the banner: only a resting finger shows a face.
        if (look.pressed)
            context.painter.fill_circle(
                {area.x + area.width * 0.5F, area.y + area.height * 0.5F},
                std::min(area.width, area.height) * 0.45F,
                pressed_colour
            );
        const float side = std::min(px(context, 14.0F), std::min(area.width, area.height) * 0.6F);
        context.painter.draw_icon(
            icon,
            {area.x + (area.width - side) * 0.5F, area.y + (area.height - side) * 0.5F, side, side},
            label_colour
        );
        return;
    }
    paint_face(context, area, look);
    float text_points = 0.0F;
    if (control.control == Control::times_five)
        text_points = 14.0F;
    else if (
        control.control == Control::drawer_build_tab ||
        control.control == Control::drawer_orders_tab
    )
        text_points = 10.5F;
    else if (control.control == Control::menu_item)
        text_points = 11.0F;
    const std::string sub_label = look.latched ? context.translate("LATCHED") : std::string{};
    paint_content(
        context, area, arrangement, icon, label, sub_label, content_colour(look), text_points
    );
}

/// Paints a pill: a panel whose ends are half circles.
///
/// @param context the painting
/// @param area the pill, layer pixels
/// @param edge the colour of its edge
/// @param edge_width its edge's width, layer pixels
void paint_pill(const PaintContext& context, paint::Area area, paint::Rgba edge, float edge_width) {
    const float radius = area.height * 0.5F;
    context.painter.fill_rounded_rect(area, radius, sheet_colour);
    context.painter.outline_rounded_rect(area, radius, edge_width, edge);
}

/// Paints a title and a hint side by side, centred between two columns:
/// the title in white, the hint in green, the hint shortened first.
///
/// @param context the painting
/// @param title the title, translated
/// @param hint the hint, translated
/// @param left the leftmost column, layer pixels
/// @param right the rightmost column, layer pixels
/// @param middle the row their capitals' middle lies on
void paint_title_and_hint(
    const PaintContext& context,
    std::string_view title,
    std::string_view hint,
    float left,
    float right,
    float middle
) {
    const float room = right - left;
    if (room <= 0.0F)
        return;
    const float gap = px(context, 10.0F);
    const float title_width = std::min(text_width(context, title, title_points), room);
    const float hint_room = room - title_width - (title.empty() ? 0.0F : gap);
    float hint_width = 0.0F;
    std::string fitted_hint;
    if (!hint.empty() && hint_room > px(context, 24.0F) && context.fonts != nullptr) {
        fitted_hint = paint::fit_text(
            *context.fonts, hint, font_px(context, hint_points), true, static_cast<int>(hint_room)
        );
        hint_width = text_width(context, fitted_hint, hint_points);
    }
    const float total =
        title_width + (fitted_hint.empty() || title.empty() ? 0.0F : gap) + hint_width;
    float pen = left + (room - total) * 0.5F;
    if (!title.empty()) {
        text_from(context, title, pen, middle, title_points, label_colour, title_width + 1.0F);
        pen += title_width + gap;
    }
    if (!fitted_hint.empty())
        text_from(context, fitted_hint, pen, middle, hint_points, lit_colour, hint_width + 1.0F);
}

/// Paints the armed-order or placement banner, under its ✕.
///
/// @param context the painting
void paint_banner(const PaintContext& context) {
    const auto& state = context.state;
    const auto& frame = context.frame;
    if (!state.banner.shown || empty(frame.banner))
        return;
    const auto area = area_of(context, frame.banner);
    paint_pill(context, area, lit_colour, px(context, 1.5F));
    float right = area.x + area.width - px(context, pill_pad_points);
    for (uint8_t i = 0; i < frame.control_count && i < frame.controls.size(); ++i) {
        const auto& control = frame.controls[i];
        if (control.control == Control::banner_cancel && inside(control.rect, frame.banner))
            right = std::min(right, area_of(context, control.rect).x - px(context, 4.0F));
    }
    paint_title_and_hint(
        context,
        context.translate(state.banner.title),
        context.translate(state.banner.hint),
        area.x + px(context, pill_pad_points),
        right,
        area.y + area.height * 0.5F
    );
}

/// Paints the phone's status pill: the selection and what a tap does.
///
/// @param context the painting
void paint_status(const PaintContext& context) {
    const auto& state = context.state;
    const auto& frame = context.frame;
    if (empty(frame.status) || (state.banner.shown && !empty(frame.banner)))
        return;
    const auto area = area_of(context, frame.status);
    paint_pill(context, area, edge_colour, px(context, edge_points));
    const std::string hint =
        context.translate(hud::status_hint(state.tap_action, state.enemy_action));
    paint_title_and_hint(
        context,
        context.translate(state.selection_text),
        hint,
        area.x + px(context, pill_pad_points),
        area.x + area.width - px(context, pill_pad_points),
        area.y + area.height * 0.5F
    );
}

/// Paints the tip bubble a long press shows, its text wrapped to fit.
///
/// @param context the painting
void paint_tip(const PaintContext& context) {
    const auto& state = context.state;
    const auto& frame = context.frame;
    if (empty(frame.tip) || state.tip.until_ms == 0 || state.tip.text.empty())
        return;
    const auto area = area_of(context, frame.tip);
    const float radius = px(context, corner_points);
    context.painter.fill_rounded_rect(area, radius, sheet_colour);
    context.painter.outline_rounded_rect(area, radius, px(context, edge_points), lit_colour);
    if (context.fonts == nullptr)
        return;
    const std::string text = context.translate(state.tip.text);
    const float pad = px(context, 8.0F);
    const int room = static_cast<int>(area.width - 2.0F * pad);
    // The largest size whose lines fit the bubble's height, down to 7 pt.
    for (float size = tip_points; size >= 7.0F; size -= 1.0F) {
        const int pixel_size = font_px(context, size);
        const auto lines = paint::wrap_text(*context.fonts, text, pixel_size, true, room);
        const float line_height = static_cast<float>(pixel_size) * 1.25F;
        const float block = line_height * static_cast<float>(lines.size());
        if (block > area.height - pad && size > 7.0F)
            continue;
        float middle = area.y + (area.height - block) * 0.5F + line_height * 0.5F;
        for (const auto& line : lines) {
            if (middle > area.y + area.height)
                break;
            text_centred(
                context,
                line,
                area.x + area.width * 0.5F,
                middle,
                size,
                label_colour,
                static_cast<float>(room)
            );
            middle += line_height;
        }
        return;
    }
}

/// Returns the widest stretch of a band, between two columns, that no
/// rectangle crossing the band covers.
///
/// @param left the band's left column, layout pixels
/// @param right the band's right column
/// @param top the band's top row
/// @param bottom the band's bottom row
/// @param taken the rectangles that may cover parts of it
/// @return the stretch's left and right columns; equal when the band is covered
std::pair<int, int>
widest_gap(int left, int right, int top, int bottom, const std::vector<hud::Rect>& taken) {
    std::vector<std::pair<int, int>> covered;
    for (const auto& rect : taken)
        if (rect.y < bottom && rect.y + rect.height > top)
            covered.emplace_back(rect.x, rect.x + rect.width);
    std::sort(covered.begin(), covered.end());
    std::pair<int, int> best{left, left};
    int from = left;
    for (const auto& [start, end] : covered) {
        if (start > from && std::min(start, right) - from > best.second - best.first)
            best = {from, std::min(start, right)};
        from = std::max(from, end);
    }
    if (right - from > best.second - best.first)
        best = {from, right};
    return best;
}

/// Paints page dots, the shown page's filled, centred on a point.
///
/// @param context the painting
/// @param centre_x the column their middle lies on, layer pixels
/// @param middle the row their middle lies on, layer pixels
void paint_page_dots(const PaintContext& context, float centre_x, float middle) {
    const auto pages = context.state.drawer_pages;
    const float diameter = px(context, dot_points);
    const float gap = px(context, dot_gap_points);
    const float width = static_cast<float>(pages) * diameter + static_cast<float>(pages - 1) * gap;
    float x = centre_x - width * 0.5F + diameter * 0.5F;
    for (uint8_t page = 0; page < pages; ++page, x += diameter + gap) {
        if (page == context.state.drawer_page)
            context.painter.fill_circle({x, middle}, diameter * 0.5F, label_colour);
        else
            context.painter.outline_circle(
                {x, middle}, diameter * 0.5F - px(context, 0.6F), px(context, 1.2F), quiet_colour
            );
    }
}

/// Paints the open sheet's panel, and for the drawer and the MORE sheet the
/// empty cells the 3.1c gadgets are placed over, the drawer's title and its
/// page dots.
///
/// @param context the painting
void paint_sheet(const PaintContext& context) {
    const auto& state = context.state;
    const auto& frame = context.frame;
    if (state.sheet == hud::Sheet::none || empty(frame.sheet))
        return;
    const auto area = area_of(context, frame.sheet);
    const float radius = px(context, corner_points);
    context.painter.fill_rounded_rect(area, radius, sheet_colour);
    context.painter.outline_rounded_rect(area, radius, px(context, edge_points), edge_colour);
    const auto cell = [&](const hud::Rect& rect) {
        if (empty(rect))
            return;
        const auto at = area_of(context, rect);
        context.painter.fill_rounded_rect(at, radius, cell_colour);
        context.painter.outline_rounded_rect(at, radius, px(context, edge_points), edge_colour);
    };
    if (state.sheet == hud::Sheet::more) {
        for (uint8_t i = 0; i < frame.more_cell_count && i < frame.more_cells.size(); ++i)
            cell(frame.more_cells[i]);
        return;
    }
    if (state.sheet != hud::Sheet::drawer)
        return;
    for (uint8_t i = 0; i < frame.drawer_cell_count && i < frame.drawer_cells.size(); ++i)
        cell(frame.drawer_cells[i]);

    // The header: the band above the cells. Its controls stand in rows; the
    // title goes in the first row with room beside them, the page dots in
    // the first row with room left after it; else both go under the cells.
    const auto& sheet = frame.sheet;
    const auto& grid = frame.drawer_grid;
    const float layout_point = context.point / context.scale_x; // layout pixels per point
    const int inset = static_cast<int>(std::lround(8.0F * layout_point));
    const int band_top = sheet.y;
    const int band_bottom = !empty(grid) && grid.y > band_top
                                ? grid.y
                                : sheet.y + static_cast<int>(std::lround(44.0F * layout_point));
    std::vector<hud::Rect> taken;
    std::vector<std::pair<int, int>> rows; // top and bottom of each row of header controls
    for (std::size_t i = 0; i < std::min<std::size_t>(frame.control_count, frame.controls.size());
         ++i) {
        const auto& rect = frame.controls[i].rect;
        if (!inside(rect, sheet) || rect.y >= band_bottom || empty(rect))
            continue;
        taken.push_back(rect);
        const int top = rect.y;
        const int bottom = rect.y + rect.height;
        auto row = std::find_if(rows.begin(), rows.end(), [&](const std::pair<int, int>& r) {
            return top < r.second && bottom > r.first;
        });
        if (row == rows.end())
            rows.emplace_back(top, bottom);
        else
            *row = {std::min(row->first, top), std::max(row->second, bottom)};
    }
    if (rows.empty())
        rows.emplace_back(band_top, band_bottom);
    std::sort(rows.begin(), rows.end());
    const auto room_in = [&](const std::pair<int, int>& row) {
        return widest_gap(
            sheet.x + inset, sheet.x + sheet.width - inset, row.first, row.second, taken
        );
    };
    // Under the cells: from the last row of cells (or the grid) to the panel's foot.
    int cells_bottom = empty(grid) ? sheet.y + sheet.height : grid.y + grid.height;
    if (frame.drawer_cell_count > 0) {
        cells_bottom = sheet.y;
        for (uint8_t i = 0; i < frame.drawer_cell_count && i < frame.drawer_cells.size(); ++i)
            cells_bottom =
                std::max(cells_bottom, frame.drawer_cells[i].y + frame.drawer_cells[i].height);
    }
    const float below_grid = static_cast<float>(sheet.y + sheet.height - cells_bottom);
    const float caption_middle =
        (static_cast<float>(cells_bottom) + std::min(below_grid, 28.0F * layout_point) * 0.5F) *
        context.scale_y;
    const bool caption_room = below_grid >= 16.0F * layout_point;
    const std::string title = context.translate(
        state.drawer_title.empty() ? std::string_view{"BUILD"}
                                   : std::string_view{state.drawer_title}
    );
    bool title_drawn = false;
    bool caption_used = false;
    for (const auto& row : rows) {
        const auto gap = room_in(row);
        const float gap_width = static_cast<float>(gap.second - gap.first) * context.scale_x;
        if (gap_width < px(context, 56.0F))
            continue;
        const float margin = px(context, 6.0F);
        const float drawn = text_from(
                                context,
                                title,
                                static_cast<float>(gap.first) * context.scale_x + margin,
                                static_cast<float>(row.first + row.second) * 0.5F * context.scale_y,
                                title_points,
                                label_colour,
                                gap_width - margin
                            ) +
                            margin;
        taken.push_back(
            {gap.first,
             row.first,
             static_cast<int>(std::ceil(drawn / context.scale_x)) + inset,
             row.second - row.first}
        );
        title_drawn = true;
        break;
    }
    if (!title_drawn && caption_room) {
        text_from(
            context,
            title,
            area.x + px(context, 8.0F),
            caption_middle,
            hint_points,
            quiet_colour,
            area.width * 0.6F
        );
        caption_used = true;
    }
    if (state.drawer_pages <= 1)
        return;
    const float dots_width =
        static_cast<float>(state.drawer_pages) * px(context, dot_points) +
        static_cast<float>(state.drawer_pages - 1) * px(context, dot_gap_points);
    // The dots go in the lowest row with room: beside the tabs and x5.
    for (auto row_at = rows.rbegin(); row_at != rows.rend(); ++row_at) {
        const auto& row = *row_at;
        const auto gap = room_in(row);
        if (static_cast<float>(gap.second - gap.first) * context.scale_x <
            dots_width + px(context, 4.0F))
            continue;
        paint_page_dots(
            context,
            static_cast<float>(gap.first + gap.second) * 0.5F * context.scale_x,
            static_cast<float>(row.first + row.second) * 0.5F * context.scale_y
        );
        return;
    }
    if (caption_room) {
        const float centre_x = caption_used
                                   ? area.x + area.width - px(context, 8.0F) - dots_width * 0.5F
                                   : area.x + area.width * 0.5F;
        paint_page_dots(context, centre_x, caption_middle);
    }
}

/// Paints the open radial order menu: twelve wedges round the QUEUE hub, the
/// greyed ones at 40%, the plain tap's item ringed, the pressed one lit.
///
/// @param context the painting
void paint_radial(const PaintContext& context) {
    const auto& state = context.state;
    if (!state.radial)
        return;
    const auto& radial = *state.radial;
    constexpr float pi = std::numbers::pi_v<float>;
    constexpr float slot_angle = 2.0F * pi / static_cast<float>(hud::radial_slot_count);
    const paint::Spot centre{
        static_cast<float>(radial.centre.x) * context.scale_x,
        static_cast<float>(radial.centre.y) * context.scale_y
    };
    const float inner = static_cast<float>(radial.inner_radius) * context.scale_x;
    const float outer = static_cast<float>(radial.outer_radius) * context.scale_x;
    if (outer <= inner || inner <= 0.0F)
        return;
    // The ring, then each wedge's lit or ringed look, then the gaps between them.
    context.painter.fill_sector(centre, 0.0F, outer, 0.0F, pi, sheet_colour);
    for (std::size_t slot = 0; slot < radial.wedges.size(); ++slot) {
        const auto& wedge = radial.wedges[slot];
        const float middle = static_cast<float>(slot) * slot_angle;
        const bool pressed =
            wedge.available &&
            is_pressed(state, Control::radial_item, static_cast<uint8_t>(wedge.item));
        if (pressed)
            context.painter.fill_sector(
                centre, inner, outer, middle, slot_angle * 0.5F, lit_colour
            );
        else if (wedge.default_item)
            context.painter.outline_sector(
                centre,
                inner,
                outer,
                middle,
                slot_angle * 0.5F,
                px(context, lit_edge_points),
                lit_colour
            );
    }
    for (std::size_t slot = 0; slot < radial.wedges.size(); ++slot) {
        const float angle = (static_cast<float>(slot) + 0.5F) * slot_angle;
        const paint::Spot from{
            centre.x + std::sin(angle) * inner, centre.y - std::cos(angle) * inner
        };
        const paint::Spot to{
            centre.x + std::sin(angle) * outer, centre.y - std::cos(angle) * outer
        };
        context.painter.stroke_line(from, to, px(context, 1.5F), wedge_gap_colour);
    }
    context.painter.outline_circle(
        centre, outer - px(context, 0.5F), px(context, 1.0F), edge_colour
    );
    // Each wedge's icon over its label.
    const float side = px(context, 16.0F);
    const float size_points = 8.5F;
    const float cap = cap_share * static_cast<float>(font_px(context, size_points));
    const float gap = px(context, 3.0F);
    const float room = (outer - inner) * 1.1F;
    for (const auto& wedge : radial.wedges) {
        const bool pressed =
            wedge.available &&
            is_pressed(state, Control::radial_item, static_cast<uint8_t>(wedge.item));
        paint::Rgba colour = wedge.default_item ? lit_colour : label_colour;
        if (pressed)
            colour = ink_colour;
        colour = greyed_if(colour, !wedge.available);
        const float x = static_cast<float>(wedge.label.x) * context.scale_x;
        const float y = static_cast<float>(wedge.label.y) * context.scale_y;
        const float top = y - (side + gap + cap) * 0.5F;
        context.painter.draw_icon(
            radial_icon(wedge.item), paint::Area{x - side * 0.5F, top, side, side}, colour
        );
        text_centred(
            context,
            context.translate(radial_label(wedge.item, state)),
            x,
            top + side + gap + cap * 0.5F,
            size_points,
            colour,
            room
        );
    }
    // The hub: QUEUE for the next pick, lit once tapped.
    const float hub = inner - px(context, 3.0F);
    const bool hub_pressed = is_pressed(state, Control::radial_hub, 0);
    paint::Rgba hub_fill = panel_colour;
    if (radial.queue_hub)
        hub_fill = hub_pressed ? lit_pressed_colour : lit_colour;
    else if (hub_pressed)
        hub_fill = pressed_colour;
    context.painter.fill_circle(centre, hub, hub_fill);
    if (!radial.queue_hub)
        context.painter.outline_circle(centre, hub, px(context, edge_points), edge_colour);
    const auto hub_colour = radial.queue_hub ? ink_colour : label_colour;
    const float hub_side = px(context, 15.0F);
    const float hub_top = centre.y - (hub_side + gap + cap) * 0.5F;
    context.painter.draw_icon(
        paint::Icon::shift_arrow,
        paint::Area{centre.x - hub_side * 0.5F, hub_top, hub_side, hub_side},
        hub_colour
    );
    text_centred(
        context,
        context.translate("QUEUE"),
        centre.x,
        hub_top + hub_side + gap + cap * 0.5F,
        size_points,
        hub_colour,
        hub * 1.8F
    );
    // Where the order is given, when the ring had to move off it.
    const float anchor_x = static_cast<float>(radial.anchor.x) * context.scale_x;
    const float anchor_y = static_cast<float>(radial.anchor.y) * context.scale_y;
    if (std::hypot(anchor_x - centre.x, anchor_y - centre.y) > hub) {
        context.painter.outline_circle(
            {anchor_x, anchor_y}, px(context, 7.0F), px(context, 2.0F), label_colour
        );
        context.painter.fill_circle({anchor_x, anchor_y}, px(context, 2.0F), label_colour);
    }
}

/// Paints every touch control the frame lays out, in the order they stack:
/// the status pill and the banner, the controls on the battlefield, the
/// open sheet and its controls, the radial menu, and the tip on top.
///
/// @param context the painting
void paint_controls(const PaintContext& context) {
    const auto& frame = context.frame;
    paint_status(context);
    paint_banner(context);
    const bool sheet_open = context.state.sheet != hud::Sheet::none && !empty(frame.sheet);
    // The sheet's own controls go over its panel; every other control, even
    // one the panel covers (the phone's left column under the drawer), under it.
    const auto in_sheet = [&](const hud::ControlRect& control) {
        switch (control.control) {
        case Control::menu_item:
        case Control::more_item:
        case Control::drawer_build_tab:
        case Control::drawer_orders_tab:
        case Control::drawer_close:
        case Control::drawer_prev:
        case Control::drawer_next:
            return true;
        case Control::times_five:
            return sheet_open && context.state.sheet == hud::Sheet::drawer &&
                   inside(control.rect, frame.sheet);
        default:
            return false;
        }
    };
    const std::size_t count = std::min<std::size_t>(frame.control_count, frame.controls.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto& control = frame.controls[i];
        // A control the open sheet covers whole stays hidden under it.
        if (!in_sheet(control) && !(sheet_open && inside(control.rect, frame.sheet)))
            paint_control(context, control);
    }
    paint_sheet(context);
    for (std::size_t i = 0; i < count; ++i)
        if (in_sheet(frame.controls[i]))
            paint_control(context, frame.controls[i]);
    paint_radial(context);
    paint_tip(context);
}

/// Returns whether two layouts of the controls are the same.
///
/// @param a a frame
/// @param b another frame
/// @return true when every control and region matches
bool same_frame(const hud::Frame& a, const hud::Frame& b) {
    const auto same = [](const hud::Rect& x, const hud::Rect& y) {
        return x.x == y.x && x.y == y.y && x.width == y.width && x.height == y.height;
    };
    if (a.control_count != b.control_count || a.drawer_cell_count != b.drawer_cell_count ||
        a.more_cell_count != b.more_cell_count)
        return false;
    for (std::size_t i = 0; i < std::min<std::size_t>(a.control_count, a.controls.size()); ++i) {
        const auto& x = a.controls[i];
        const auto& y = b.controls[i];
        if (x.control != y.control || x.index != y.index || !same(x.rect, y.rect))
            return false;
    }
    for (std::size_t i = 0; i < a.drawer_cells.size(); ++i)
        if (!same(a.drawer_cells[i], b.drawer_cells[i]))
            return false;
    for (std::size_t i = 0; i < a.more_cells.size(); ++i)
        if (!same(a.more_cells[i], b.more_cells[i]))
            return false;
    return same(a.minimap, b.minimap) && same(a.resources, b.resources) &&
           same(a.status, b.status) && same(a.banner, b.banner) && same(a.tip, b.tip) &&
           same(a.sheet, b.sheet) && same(a.drawer_grid, b.drawer_grid) &&
           same(a.more_grid, b.more_grid) && same(a.placement_bar, b.placement_bar) &&
           same(a.panel_sheet, b.panel_sheet) && same(a.clear, b.clear);
}

} // namespace

bool TouchDrawAccess::refresh_layer(Runtime& runtime) {
    if (runtime.touch_ == nullptr || !runtime.touch_controls_active() ||
        runtime.screen_ != Screen::match)
        return false;
    auto& state = *runtime.touch_;
    auto& layer = state.layer;
    const auto& match = runtime.match_layout_;
    if (!state.frame_ready || match.width <= 0 || match.height <= 0)
        return false;
    double density = runtime.match_display_density();
    if (!std::isfinite(density) || density <= 0.0)
        density = 1.0;
    TouchLayer::Look look{};
    look.revision = state.hud.revision;
    look.width = match.width;
    look.height = match.height;
    look.layer_width = std::max(1, static_cast<int32_t>(std::lround(match.width * density)));
    look.layer_height = std::max(1, static_cast<int32_t>(std::lround(match.height * density)));
    look.px_per_point = state.viewport.px_per_point > 0.0F ? state.viewport.px_per_point : 1.0F;
    if (layer.drawn == look && same_frame(layer.drawn_frame, state.frame))
        return !layer.bounds.empty();

    if (!layer.fonts_tried) {
        layer.fonts_tried = true;
        try {
            layer.fonts = text_font::FontStack::open(text_font::bundled_font_directory());
        } catch (const std::exception&) {
            layer.fonts.reset();
        }
    }
    if (layer.canvas.width != look.layer_width || layer.canvas.height != look.layer_height) {
        layer.canvas = paint::make_canvas(look.layer_width, look.layer_height);
        layer.bounds = {};
    }
    paint::Painter painter(layer.canvas);
    painter.clear_box(layer.bounds);
    const Translate translate = [&runtime](std::string_view text) {
        return runtime.translate_ui(text);
    };
    const float scale_x = static_cast<float>(look.layer_width) / static_cast<float>(look.width);
    const float scale_y = static_cast<float>(look.layer_height) / static_cast<float>(look.height);
    const PaintContext context{
        painter,
        layer.fonts.get(),
        scale_x,
        scale_y,
        look.px_per_point * scale_x,
        translate,
        state.hud,
        state.frame
    };
    paint_controls(context);
    layer.bounds = painter.painted();
    layer.drawn = look;
    layer.drawn_frame = state.frame;
    layer.drawn_revision = look.revision;
    layer.uploaded.reset();
    return !layer.bounds.empty();
}

void TouchDrawAccess::forget_textures(Runtime& runtime) noexcept {
    if (runtime.touch_ == nullptr)
        return;
    auto& layer = runtime.touch_->layer;
    layer.texture.reset();
    layer.uploaded.reset();
    layer.texture_renderer = nullptr;
}

uint32_t TouchDrawAccess::device_resets(const Runtime& runtime) noexcept {
    return runtime.render_run_ ? runtime.render_run_->resets_handled : 0U;
}

void Runtime::compose_touch_layer(renderer::Surface& frame) {
    if (!TouchDrawAccess::refresh_layer(*this))
        return;
    const auto& layer = touch_->layer;
    const auto width = static_cast<int32_t>(frame.width);
    const auto height = static_cast<int32_t>(frame.height);
    if (!layer.drawn || layer.drawn->width != width || layer.drawn->height != height ||
        frame.rgb.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U)
        return;
    const auto& canvas = layer.canvas;
    const auto& bounds = layer.bounds;
    const bool one_to_one = canvas.width == width && canvas.height == height;
    const double across = static_cast<double>(canvas.width) / width;
    const double down = static_cast<double>(canvas.height) / height;
    // The frame's pixels the painted box reaches.
    const int32_t left = std::max(0, static_cast<int32_t>(std::floor(bounds.x / across)));
    const int32_t top = std::max(0, static_cast<int32_t>(std::floor(bounds.y / down)));
    const int32_t right =
        std::min(width, static_cast<int32_t>(std::ceil((bounds.x + bounds.width) / across)));
    const int32_t bottom =
        std::min(height, static_cast<int32_t>(std::ceil((bounds.y + bounds.height) / down)));
    for (int32_t row = top; row < bottom; ++row)
        for (int32_t column = left; column < right; ++column) {
            // The layer pixel, or the average of the layer pixels, under this frame pixel,
            // its colour counted by its opacity.
            uint32_t alpha = 0;
            std::array<uint32_t, 3> colour{};
            if (one_to_one) {
                const auto* source = canvas.rgba.data() +
                                     (static_cast<std::size_t>(row) * canvas.width + column) * 4U;
                alpha = source[3];
                for (std::size_t channel = 0; channel < 3; ++channel)
                    colour[channel] = source[channel];
            } else {
                const int32_t x0 = static_cast<int32_t>(std::floor(column * across));
                const int32_t y0 = static_cast<int32_t>(std::floor(row * down));
                const int32_t x1 = std::min(
                    canvas.width,
                    std::max(x0 + 1, static_cast<int32_t>(std::floor((column + 1) * across)))
                );
                const int32_t y1 = std::min(
                    canvas.height,
                    std::max(y0 + 1, static_cast<int32_t>(std::floor((row + 1) * down)))
                );
                uint64_t weighted_alpha = 0;
                std::array<uint64_t, 3> weighted{};
                for (int32_t y = y0; y < y1; ++y)
                    for (int32_t x = x0; x < x1; ++x) {
                        const auto* source = canvas.rgba.data() +
                                             (static_cast<std::size_t>(y) * canvas.width + x) * 4U;
                        weighted_alpha += source[3];
                        for (std::size_t channel = 0; channel < 3; ++channel)
                            weighted[channel] += static_cast<uint64_t>(source[channel]) * source[3];
                    }
                const auto samples =
                    static_cast<uint64_t>(x1 - x0) * static_cast<uint64_t>(y1 - y0);
                alpha = static_cast<uint32_t>((weighted_alpha + samples / 2U) / samples);
                if (weighted_alpha != 0)
                    for (std::size_t channel = 0; channel < 3; ++channel)
                        colour[channel] = static_cast<uint32_t>(
                            (weighted[channel] + weighted_alpha / 2U) / weighted_alpha
                        );
            }
            if (alpha == 0)
                continue;
            auto* target = frame.rgb.data() +
                           (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(column)) *
                               3U;
            // As the settings layer goes over the composed frame.
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const unsigned shown =
                    gamma_identity_ ? colour[channel] : gamma_table_[colour[channel]];
                target[channel] =
                    static_cast<uint8_t>((shown * alpha + target[channel] * (255U - alpha)) / 255U);
            }
        }
}

void Runtime::present_touch_layer() {
    if (sdl_.renderer == nullptr || !TouchDrawAccess::refresh_layer(*this))
        return;
    auto& layer = touch_->layer;
    const auto& drawn = *layer.drawn;
    if (layer.texture_renderer != sdl_.renderer) {
        // Made on a renderer since replaced: its textures went with it.
        layer.texture.reset();
        layer.uploaded.reset();
        layer.texture_renderer = sdl_.renderer;
    }
    if (layer.texture.ensure(
            sdl_.renderer,
            SDL_PIXELFORMAT_RGBA32,
            drawn.layer_width,
            drawn.layer_height,
            render_texture_limit(),
            SDL_BLENDMODE_BLEND
        ))
        layer.uploaded.reset();
    const uint32_t resets = TouchDrawAccess::device_resets(*this);
    const auto& bounds = layer.bounds;
    if (layer.uploaded != layer.drawn || layer.uploaded_gamma != gamma_table_ ||
        layer.texture_resets != resets) {
        // Only the painted rows: the draw below reads no other.
        const auto first_row = static_cast<uint32_t>(bounds.y);
        const auto end_row = static_cast<uint32_t>(bounds.y + bounds.height);
        const std::size_t pitch = static_cast<std::size_t>(drawn.layer_width) * 4U;
        const uint8_t* pixels = layer.canvas.rgba.data();
        if (!gamma_identity_) {
            layer.corrected.resize(layer.canvas.rgba.size());
            const std::size_t from = first_row * pitch;
            const std::size_t bytes = (end_row - first_row) * pitch;
            std::copy_n(layer.canvas.rgba.data() + from, bytes, layer.corrected.data() + from);
            apply_gamma_rgb(layer.corrected.data() + from, bytes / 4U, 4);
            pixels = layer.corrected.data();
        }
        layer.texture.update(pixels, static_cast<int>(pitch), 4, first_row, end_row);
        layer.uploaded = layer.drawn;
        layer.uploaded_gamma = gamma_table_;
        layer.texture_resets = resets;
    }
    const float across = static_cast<float>(drawn.layer_width) / static_cast<float>(drawn.width);
    const float down = static_cast<float>(drawn.layer_height) / static_cast<float>(drawn.height);
    const SDL_FRect source{
        static_cast<float>(bounds.x),
        static_cast<float>(bounds.y),
        static_cast<float>(bounds.width),
        static_cast<float>(bounds.height)
    };
    const SDL_FRect destination{
        source.x / across, source.y / down, source.w / across, source.h / down
    };
    // The layer holds the display's pixels, laid over the layout's.
    draw_one_to_one(sdl_.renderer, layer.texture, &source, &destination, one_to_one_scale_mode());
}

} // namespace oa::app
