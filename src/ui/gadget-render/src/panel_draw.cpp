// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Whole-panel draw, panel face blits and the engine host binding.
#include "render_internal.hpp"

#include "oa/data/defs/files.hpp"
#include "oa/ui/frontend_renderer/gadget_draw.hpp"
#include "oa/base/geometry.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace oa::ui::gadget_render {

namespace detail {

PanelSurfaces* panel_slot(GadgetRenderer& renderer, const GadgetOwner* owner, bool create) {
    PanelSurfaces* unused = nullptr;
    for (auto& slot : renderer.panels) {
        if (slot.owner == owner)
            return &slot;
        if (unused == nullptr && slot.owner == nullptr)
            unused = &slot;
    }
    if (!create || unused == nullptr)
        return nullptr;
    unused->owner = owner;
    return unused;
}

} // namespace detail

using namespace detail;
namespace panel_flag = ui::gui_input::panel_flag;

namespace {

constexpr size_t kNameBytes = 0x10; // names copied with a 16-byte bound
constexpr const char* kGadgetGafSuffix = "_gadget";
constexpr const char* kFontSuffix = ".FNT";
constexpr const char* kBackTile = "BackTile";
constexpr const char* kListSkin = "LISTBOX";
constexpr const char* kTextSkin = "TEXTINPUT";
constexpr const char* kSliders = "SLIDERS";
constexpr const char* kButtonArt = "BUTTONS0";
constexpr const char* kCheckboxArt = "CHECKBOX";
constexpr const char* kTwoStageArt = "stagebuttn1";
constexpr const char* kStageArtFormat = "stagebuttn%d";
constexpr const char* kOffOnCaption = "Off|On";
constexpr char kStageSeparator = '|';
constexpr int32_t kMaxStageArt = 4;
constexpr uint8_t kTwoStages = 2;

// Button attributes the first draw reads and writes.
constexpr uint32_t kCheckboxButton = 0x80;
constexpr uint32_t kStageArtOverride = 0x4000; // one- and two-stage buttons use stagebuttn1
constexpr uint32_t kStagedAttributes = 0x1; // a staged button keeps only kStageArtOverride and this
constexpr uint32_t kLabelWithoutLink = 0x10;
constexpr uint8_t kGadgetGafFlag = 1; // field::gaf_file bit: the record has its own GAF file
constexpr int32_t kBestFrameStart = 1000;
constexpr int32_t kButtonStateFrames = 4; // frames per size group in button art
constexpr int16_t kMaxTextChars = 0x7F;

// Scroll bars: the arrow buttons the first draw appends, their SLIDERS frames
// relative to the bar's base frame, and their attributes (repeat, no focus
// and the step direction).
constexpr int32_t kHorizontalSliders = 10;
constexpr int32_t kBackArrowFrame = 6;
constexpr int32_t kForwardArrowFrame = 8;
constexpr int32_t kKnobFrame = 5;
constexpr uint32_t kBackArrowAttributes =
    attribute::repeat | attribute::no_focus | attribute::scroll_step_back;
constexpr uint32_t kForwardArrowAttributes =
    attribute::repeat | attribute::no_focus | attribute::scroll_step_forward;
constexpr int32_t kRangeMargin = 6;
constexpr size_t scroll_clear_on_first_draw = 0x13B; // byte the first draw clears on scroll bars
// Shade level the panel loader applies below a shading panel.
constexpr int32_t kShadeBelowLevel = -0x18;
constexpr int32_t kKnobRangeMargin = 4;

int32_t last_record(const GadgetPanel& panel) {
    const auto records = panel.owner->records();
    return std::min<int32_t>(
        layout::gadget_last_index(records), static_cast<int32_t>(records.size()) - 1
    );
}

std::string name16(const GadgetRecord& record, size_t offset) {
    std::string name(layout::record_string(record, offset));
    if (name.size() > kNameBytes)
        name.resize(kNameBytes);
    return name;
}

// `prefix` + `name` with its extension replaced by GAF.
std::string gaf_path(const char* prefix, const std::string& name) {
    char joined[data::defs::path_capacity]{};
    std::strncpy(joined, prefix, sizeof(joined) - 1);
    std::strncat(joined, name.c_str(), sizeof(joined) - std::strlen(joined) - 1);
    char path[data::defs::path_capacity]{};
    data::defs::format_with_extension(joined, path, sizeof(path), kGafExtension);
    return path;
}

const void* load_gaf(const GadgetRenderer& renderer, const std::string& path) {
    return renderer.art.load_gaf != nullptr
               ? renderer.art.load_gaf(renderer.art.context, path.c_str())
               : nullptr;
}

const void* load_file(const GadgetRenderer& renderer, const std::string& path) {
    return renderer.art.load_file != nullptr
               ? renderer.art.load_file(renderer.art.context, path.c_str())
               : nullptr;
}

void release(const GadgetRenderer& renderer, const void*& file) {
    if (file != nullptr && renderer.art.release != nullptr)
        renderer.art.release(renderer.art.context, file);
    file = nullptr;
}

void set_i16(GadgetRecord& record, size_t offset, int32_t value) {
    layout::set_record_i16(record, offset, static_cast<int16_t>(value));
}

void place_root(GadgetRecord& root, uint32_t flags) {
    int16_t x = i16(root, field::x);
    int16_t y = i16(root, field::y);
    ui::gui_input::place_root(
        x,
        y,
        i16(root, field::width),
        i16(root, field::height),
        flags,
        present::display_width(),
        present::display_height(),
        ui::gui_input::hud_strip_width
    );
    set_i16(root, field::x, x);
    set_i16(root, field::y, y);
}

// Root and type-11 records: the panel's own GAF and its face skin.
void bind_panel_art(GadgetRenderer& renderer, GadgetPanel& panel) {
    GadgetRecord& root = root_of(panel);
    if (i16(root, field::y) < 0)
        set_i16(root, field::y, i16(root, field::y) + present::display_height());
    if (root.refs.panel_gaf == nullptr)
        root.refs.panel_gaf =
            load_gaf(renderer, gaf_path(panel.gaf_path.data(), name16(root, field::name)));
    const std::string skin_name = name16(root, field::panel_name);
    const void* skin = art_find(renderer, root.refs.panel_gaf, skin_name.c_str());
    if (skin == nullptr && panel.list_skin != nullptr) {
        skin = art_find(renderer, panel.list_skin, skin_name.c_str());
        if (skin == nullptr) {
            skin = art_find(renderer, panel.list_skin, kBackTile);
            clear_origins(renderer, skin);
        }
    }
    root.refs.skin = skin;
}

// Picks the frame of `art` (every fourth, the start of a state group) whose
// size is closest to the record's.
uint8_t closest_frame(const GadgetRenderer& renderer, const void* art, const GadgetRecord& record) {
    uint8_t best = 0;
    int32_t best_difference = kBestFrameStart;
    const int32_t count = art_frame_count(renderer, art);
    for (int32_t index = 0; index < count; index += kButtonStateFrames) {
        const Sprite* frame = art_frame(renderer, art, index);
        if (frame == nullptr)
            continue;
        const int32_t difference = std::abs(i16(record, field::width) - frame->width) +
                                   std::abs(i16(record, field::height) - frame->height);
        if (difference < best_difference) {
            best = static_cast<uint8_t>(index);
            best_difference = difference;
        }
    }
    return best;
}

void bind_button(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index) {
    GadgetRecord& button = record_at(panel, index);
    layout::set_record_u32(button, field::color_foreground, 0);
    if ((attributes(button) & attribute::scroll_step_mask) != 0 ||
        (button.bytes[field::gaf_file] & kGadgetGafFlag) != 0)
        return;
    ui::gui_input::assign_quick_key(panel, index);
    const std::string name = name16(button, field::name);
    button.bytes[field::button_frame_base] = 0;
    const void* art = art_find(renderer, root_of(panel).refs.panel_gaf, name.c_str());
    if (art == nullptr)
        art = art_find(renderer, panel.list_skin, name.c_str());
    if (art == nullptr && panel.list_skin != nullptr) {
        char fallback[kNameBytes + 1]{};
        const uint8_t stages = button.bytes[field::button_stages];
        if ((attributes(button) & kCheckboxButton) != 0) {
            std::strncpy(fallback, kCheckboxArt, kNameBytes);
        } else if (stages == 0) {
            std::strncpy(fallback, kButtonArt, kNameBytes);
        } else if (
            layout::record_string(button, field::text) == kOffOnCaption || stages == 1 ||
            (attributes(button) & kStageArtOverride) != 0
        ) {
            button.bytes[field::button_stages] = kTwoStages;
            std::strncpy(fallback, kTwoStageArt, kNameBytes);
            layout::set_record_u32(
                button, field::attributes, attributes(button) | kStageArtOverride
            );
        } else {
            std::snprintf(
                fallback, sizeof(fallback), kStageArtFormat, std::min<int32_t>(stages, kMaxStageArt)
            );
        }
        art = art_find(renderer, panel.list_skin, fallback);
        if (art != nullptr) {
            clear_origins(renderer, art);
            button.bytes[field::button_frame_base] = closest_frame(renderer, art, button);
        }
    }
    button.refs.sprite = art;
    if (const Sprite* frame = art_frame(renderer, art, button.bytes[field::button_frame_base])) {
        set_i16(button, field::width, frame->width);
        set_i16(button, field::height, frame->height);
    }
    if (button.bytes[field::button_stages] != 0) {
        char* text = layout::record_chars(button, field::text);
        for (size_t at = 0; at < field::text_bytes && text[at] != '\0'; ++at)
            if (text[at] == kStageSeparator)
                text[at] = '\0';
        ui::gui_input::split_stage_captions(panel, index);
        layout::set_record_u32(
            button, field::attributes, (attributes(button) & kStageArtOverride) | kStagedAttributes
        );
    }
}

void bind_list(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index) {
    GadgetRecord& list = record_at(panel, index);
    const void* skin = art_find(renderer, panel.list_skin, kListSkin);
    clear_origins(renderer, skin);
    list.refs.list_skin = skin;
    for (int32_t other = 1; other <= last_record(panel); ++other) {
        GadgetRecord& sibling = record_at(panel, other);
        if (other == index || layout::gadget_type_of(sibling) != gadget_type::list_box ||
            sibling.bytes[field::group] != list.bytes[field::group])
            continue;
        const int16_t height =
            std::max(i16(sibling, field::list_item_height), i16(list, field::list_item_height));
        layout::set_record_i16(list, field::list_item_height, height);
        layout::set_record_i16(sibling, field::list_item_height, height);
    }
}

void bind_text_box(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index) {
    GadgetRecord& box = record_at(panel, index);
    const void* skin = art_find(renderer, panel.list_skin, kTextSkin);
    clear_origins(renderer, skin);
    box.refs.text_skin = skin;
    if (i16(box, field::text_max_chars) > kMaxTextChars)
        layout::set_record_i16(box, field::text_max_chars, kMaxTextChars);
    std::memset(layout::record_chars(box, field::text), 0, field::text_bytes);
}

// Appends one arrow button of a scroll bar drawn from SLIDERS frame `frame`.
int32_t add_arrow(
    GadgetRenderer& renderer,
    GadgetPanel& panel,
    int32_t bar_index,
    const void* art,
    int32_t frame,
    uint32_t arrow_attributes
) {
    const int32_t arrow = layout::add_gadget(panel.owner->records(), gadget_type::button);
    if (arrow == layout::kNoGadget)
        return arrow;
    GadgetRecord& bar = record_at(panel, bar_index);
    GadgetRecord& button = record_at(panel, arrow);
    button.refs.sprite = art;
    button.bytes[field::button_frame_base] = static_cast<uint8_t>(frame);
    button.bytes[field::group] = bar.bytes[field::group];
    button.bytes[field::active] = bar.bytes[field::active];
    layout::set_record_u32(button, field::attributes, arrow_attributes);
    if (const Sprite* image = art_frame(renderer, art, frame)) {
        set_i16(button, field::width, image->width);
        set_i16(button, field::height, image->height);
    }
    return arrow;
}

void bind_scroll_bar(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index) {
    record_at(panel, index).bytes[scroll_clear_on_first_draw] = 0;
    const void* art = art_find(renderer, root_of(panel).refs.panel_gaf, kSliders);
    const bool from_panel = art != nullptr;
    if (!from_panel) {
        art = art_find(renderer, panel.list_skin, kSliders);
        if (art != nullptr) {
            clear_origins(renderer, art);
            GadgetRecord& bar = record_at(panel, index);
            const int32_t base =
                i16(bar, field::height) < i16(bar, field::width) ? kHorizontalSliders : 0;
            if (const Sprite* frame = art_frame(renderer, art, base)) {
                if (i16(bar, field::width) < i16(bar, field::height))
                    set_i16(bar, field::width, frame->width);
                else
                    set_i16(bar, field::height, frame->height);
            }
            bar.bytes[field::scroll_tick_frame] = static_cast<uint8_t>(base);
        }
    }
    record_at(panel, index).refs.scroll_ticks = art;
    if (art == nullptr) {
        GadgetRecord& bar = record_at(panel, index);
        const int32_t extent = std::max(i16(bar, field::width), i16(bar, field::height));
        set_i16(bar, field::scroll_range, extent - kRangeMargin);
        return;
    }
    const int32_t base = record_at(panel, index).bytes[field::scroll_tick_frame];
    const int32_t back =
        add_arrow(renderer, panel, index, art, base + kBackArrowFrame, kBackArrowAttributes);
    if (back != layout::kNoGadget) {
        GadgetRecord& bar = record_at(panel, index);
        set_i16(record_at(panel, back), field::x, i16(bar, field::x));
        set_i16(record_at(panel, back), field::y, i16(bar, field::y));
    }
    const int32_t forward =
        add_arrow(renderer, panel, index, art, base + kForwardArrowFrame, kForwardArrowAttributes);
    if (forward == layout::kNoGadget)
        return;
    GadgetRecord& bar = record_at(panel, index);
    GadgetRecord& arrow = record_at(panel, forward);
    set_i16(arrow, field::y, i16(bar, field::y));
    const Sprite* back_frame = art_frame(renderer, art, base + kBackArrowFrame);
    const int32_t back_width = back_frame != nullptr ? back_frame->width : 0;
    const int32_t back_height = back_frame != nullptr ? back_frame->height : 0;
    if (i16(bar, field::height) < i16(bar, field::width)) {
        set_i16(arrow, field::x, (i16(bar, field::width) - back_width) + i16(bar, field::x));
        set_i16(bar, field::width, i16(bar, field::width) - 2 * back_width);
        set_i16(bar, field::x, i16(bar, field::x) + back_width);
        const Sprite* knob = art_frame(renderer, art, base + kKnobFrame);
        const int32_t knob_width = knob != nullptr ? knob->width : 0;
        set_i16(bar, field::scroll_knob_size, knob_width);
        set_i16(bar, field::scroll_range, i16(bar, field::width) - knob_width - kKnobRangeMargin);
    } else {
        set_i16(arrow, field::y, (i16(bar, field::y) - back_height) + i16(bar, field::height));
        set_i16(arrow, field::x, i16(bar, field::x));
        set_i16(bar, field::height, i16(bar, field::height) - 2 * back_height);
        set_i16(bar, field::y, i16(bar, field::y) + back_height);
    }
}

void bind_image(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index) {
    GadgetRecord& image = record_at(panel, index);
    layout::set_record_u32(image, field::color_foreground, 0);
    const std::string name = name16(image, field::name);
    image.refs.image = nullptr;
    const void* art = art_find(renderer, root_of(panel).refs.panel_gaf, name.c_str());
    if (art == nullptr)
        art = art_find(renderer, panel.list_skin, name.c_str());
    if (art != nullptr)
        image.refs.image = art_frame(renderer, art, 0);
}

uint32_t current_tick(const GadgetPanel& panel) {
    return panel.host.current_tick != nullptr ? panel.host.current_tick(panel.host.context) : 0;
}

void start_progress(GadgetPanel& panel, int32_t index) {
    GadgetRecord& bar = record_at(panel, index);
    layout::set_record_i32(
        bar,
        field::progress_deadline,
        static_cast<int32_t>(current_tick(panel)) + i32(bar, field::progress_interval)
    );
}

// First-draw binding of one record's art and files.
void bind_record(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index) {
    GadgetRecord& record = record_at(panel, index);
    const std::string gui_prefix(panel.gui_path.data());
    if ((record.bytes[field::gaf_file] & kGadgetGafFlag) != 0) {
        record.refs.gadget_gaf = nullptr;
        record.refs.sprite = nullptr;
        const std::string name(layout::record_string(record, field::name));
        record.refs.gadget_gaf =
            load_gaf(renderer, gaf_path(panel.gaf_path.data(), name + kGadgetGafSuffix));
        record.refs.sprite = art_find(renderer, record.refs.gadget_gaf, name.c_str());
    }
    switch (layout::gadget_type_of(record)) {
    case gadget_type::panel:
    case gadget_type::skin:
        bind_panel_art(renderer, panel);
        break;
    case gadget_type::button:
        bind_button(renderer, panel, index);
        break;
    case gadget_type::list_box:
        bind_list(renderer, panel, index);
        break;
    case gadget_type::text_box:
        bind_text_box(renderer, panel, index);
        break;
    case gadget_type::scroll_bar:
        bind_scroll_bar(renderer, panel, index);
        break;
    case gadget_type::label:
        if (layout::record_string(record, field::label_link).empty())
            layout::set_record_u32(
                record, field::attributes, attributes(record) | kLabelWithoutLink
            );
        else
            ui::gui_input::assign_quick_key(panel, index);
        layout::set_record_u32(record, field::color_foreground, 0);
        break;
    case gadget_type::font_resource:
        record.refs.font = load_file(
            renderer,
            std::string(panel.font_path.data()) +
                std::string(layout::record_string(record, field::text)) + kFontSuffix
        );
        break;
    case gadget_type::file_resource:
        record.refs.file = load_file(
            renderer, gui_prefix + std::string(layout::record_string(record, field::text))
        );
        break;
    case gadget_type::image:
        bind_image(renderer, panel, index);
        break;
    case gadget_type::progress:
        start_progress(panel, index);
        break;
    default:
        break;
    }
}

// Creates the face over the screen pixels under the root and, unless told
// otherwise, keeps a copy of them to restore when the panel is released.
bool create_face(GadgetRenderer& renderer, GadgetPanel& panel, uint32_t flags) {
    GadgetRecord& root = root_of(panel);
    PanelSurfaces* slot = panel_slot(renderer, panel.owner.get(), true);
    if (slot == nullptr)
        return false;
    slot->face = present::create_surface(i16(root, field::width), i16(root, field::height));
    root.refs.surface = &slot->face.surface;
    present::blit_surface(&slot->face.surface, nullptr, -i16(root, field::x), -i16(root, field::y));
    slot->saved = (flags & draw_flag::no_save_under) == 0;
    if (slot->saved) {
        slot->save_under =
            present::create_surface(i16(root, field::width), i16(root, field::height));
        present::blit_surface(&slot->save_under.surface, &slot->face.surface, 0, 0);
    } else {
        slot->save_under = {};
    }
    return true;
}

void draw_records(GadgetRenderer& renderer, GadgetPanel& panel, uint32_t flags) {
    const bool first = (flags & panel_flag::first_draw) != 0;
    const bool redraw = (flags & draw_flag::redraw) != 0;
    int32_t index = 1;
    for (; index <= last_record(panel); ++index) {
        GadgetRecord& record = record_at(panel, index);
        if (record.bytes[field::active] == 0)
            continue;
        switch (layout::gadget_type_of(record)) {
        case gadget_type::button:
            if (first || (flags & (draw_flag::buttons | draw_flag::redraw)) != 0)
                draw_button(renderer, panel, index);
            break;
        case gadget_type::list_box:
            if (first)
                ui::gui_input::reset_list_scroll(panel, index);
            if (first || redraw)
                draw_list(renderer, panel, index);
            break;
        case gadget_type::text_box:
            if (first || redraw)
                draw_text_box(renderer, panel, index);
            break;
        case gadget_type::scroll_bar:
            if (first) {
                layout::set_record_i16(record, field::scroll_knob, 0);
                layout::set_record_u32(record, field::scroll_callback, 0);
                layout::set_record_u32(record, field::scroll_callback_argument, 0);
                record.refs.scroll_changed = nullptr;
            }
            if (first || redraw)
                ui::gui_input::update_scroll_knob_size(panel, index);
            break;
        case gadget_type::label:
            if (first || redraw)
                draw_label(renderer, panel, index);
            break;
        case gadget_type::hot_surface:
            if (first)
                layout::reset_hot_surface(record);
            if (first || redraw)
                draw_hot_image(renderer, panel, index);
            break;
        case gadget_type::null_resource:
            if (first || redraw)
                ui::frontend_renderer::draw_value_marker(
                    face_of(panel),
                    layout::panel_relative_rect(record),
                    attributes(record),
                    mapped_color(
                        panel, static_cast<uint32_t>(i32(record, field::color_foreground))
                    ),
                    static_cast<uint8_t>(flags)
                );
            break;
        case gadget_type::skin:
            draw_skin(renderer, panel, index, record.refs.skin);
            break;
        case gadget_type::image:
            draw_image(renderer, panel, index);
            break;
        case gadget_type::progress:
            if (first)
                start_progress(panel, index);
            if (first || redraw)
                draw_progress(renderer, panel, index);
            break;
        default:
            break;
        }
    }
    const int32_t focus = panel.owner->focus;
    if (focus == layout::kNoGadget || panel.keyboard_enabled == 0)
        return;
    const auto records = panel.owner->records();
    const uint8_t focus_type = layout::gadget_type_of(record_at(panel, focus));
    ui::gui_input::mark_text_box_focus(panel, focus);
    const int32_t enter =
        layout::find_gadget(records, layout::record_string(root_of(panel), field::enter_default));
    if (enter == layout::kNoGadget || focus_type == gadget_type::button)
        return;
    if (index < static_cast<int32_t>(records.size()) &&
        record_at(panel, index).bytes[field::active] != 0)
        ui::gui_input::mark_text_box_focus(panel, enter);
}

void release_panel(GadgetRenderer& renderer, GadgetPanel& panel) {
    GadgetRecord& root = root_of(panel);
    if (PanelSurfaces* slot = panel_slot(renderer, panel.owner.get(), false)) {
        if (slot->saved)
            present::blit_surface(
                nullptr, &slot->save_under.surface, i16(root, field::x), i16(root, field::y)
            );
        *slot = PanelSurfaces{};
    }
    root.refs.surface = nullptr;
    for (int32_t index = 0; index <= last_record(panel); ++index) {
        GadgetRecord& record = record_at(panel, index);
        if ((record.bytes[field::gaf_file] & kGadgetGafFlag) != 0)
            release(renderer, record.refs.gadget_gaf);
        const uint8_t type = layout::gadget_type_of(record);
        if (type == gadget_type::panel)
            release(renderer, record.refs.panel_gaf);
        else if (type == gadget_type::font_resource)
            release(renderer, record.refs.font);
        else if (type == gadget_type::file_resource)
            release(renderer, record.refs.file);
    }
}

} // namespace

int32_t draw_panel(GadgetRenderer& renderer, GadgetPanel& panel, uint32_t flags) {
    if (!panel.owner)
        return 0;
    GadgetRecord& root = root_of(panel);
    place_root(root, flags);
    const bool first = (flags & panel_flag::first_draw) != 0;
    if (first) {
        if (panel.host.pop_key != nullptr)
            while (panel.host.pop_key(panel.host.context) != 0) {
            }
        if (panel.clear_quick_keys_on_draw != 0)
            for (int32_t index = 0; index <= last_record(panel); ++index)
                if (layout::gadget_type_of(record_at(panel, index)) == gadget_type::button)
                    record_at(panel, index).bytes[field::button_quick_key] = 0;
        if (present::display_width() < i16(root, field::width) ||
            present::display_height() < i16(root, field::height))
            return 0;
        root.refs.panel_gaf = nullptr;
        for (int32_t index = 0; index <= last_record(panel); ++index)
            bind_record(renderer, panel, index);
        if (!create_face(renderer, panel, flags))
            return 0;
    }
    if ((flags & draw_flag::records) != 0 || first || (flags & draw_flag::redraw) != 0) {
        if (first || (flags & draw_flag::redraw) != 0) {
            if (panel.owner->backdrop != nullptr)
                present::blit_surface(
                    face_of(panel), static_cast<const Surface*>(panel.owner->backdrop), 0, 0
                );
            else if ((flags & panel_flag::modal_backdrop) == 0)
                draw_skin(renderer, panel, 0, root.refs.skin);
        }
        draw_records(renderer, panel, flags);
    }
    if ((flags & draw_flag::release) != 0)
        release_panel(renderer, panel);
    return 1;
}

int32_t blit_panels(GadgetOwner* owner, Surface* screen, const Rect32* region) {
    if (owner == nullptr)
        return 0;
    blit_panels(owner->next.get(), screen, region);
    const GadgetRecord& root = owner->records()[0];
    const Rect32 rect{
        i16(root, field::x),
        i16(root, field::y),
        i16(root, field::width) - 1 + i16(root, field::x),
        i16(root, field::height) - 1 + i16(root, field::y)
    };
    if (owner->redraw != 1 && (region == nullptr || !base::geometry::rects_overlap(&rect, region)))
        return 1;
    owner->redraw = 0;
    present::blit_surface(
        screen,
        static_cast<const Surface*>(root.refs.surface),
        i16(root, field::x),
        i16(root, field::y)
    );
    return 1;
}

void blit_panel_stack(GadgetPanel& panel, Surface* screen, const Rect32* region) {
    blit_panels(panel.owner.get(), screen, region);
}

Surface* panel_face(const GadgetPanel& panel) {
    return panel.owner ? static_cast<Surface*>(panel.owner->table.records[0].refs.surface)
                       : nullptr;
}

void draw_request(
    GadgetRenderer& renderer, GadgetPanel& panel, ui::gui_input::GadgetDraw what, int32_t index
) {
    using ui::gui_input::GadgetDraw;
    switch (what) {
    case GadgetDraw::button:
        draw_button(renderer, panel, index);
        break;
    case GadgetDraw::list:
        draw_list(renderer, panel, index);
        break;
    case GadgetDraw::scroll_bar:
        draw_scroll_bar(renderer, panel, index);
        break;
    case GadgetDraw::text_box:
        draw_text_box(renderer, panel, index);
        break;
    case GadgetDraw::image:
        draw_image(renderer, panel, index);
        break;
    case GadgetDraw::progress:
        draw_progress(renderer, panel, index);
        break;
    case GadgetDraw::focus_outline:
        ui::frontend_renderer::draw_focus_outline(
            face_of(panel), layout::panel_relative_rect(record_at(panel, index))
        );
        break;
    case GadgetDraw::shade_panel:
        // The panel loader darkens the panel below a shading panel, or the
        // whole display when there is none.
        if (!panel.owner) {
            present::shade_rect_level(nullptr, nullptr, kShadeBelowLevel);
        } else {
            Rect32 rect = record_rect(root_of(panel));
            present::shade_rect_level(face_of(panel), &rect, kShadeBelowLevel);
        }
        break;
    }
}

void bind_renderer(GadgetPanel& panel, GadgetRenderer& renderer) {
    panel.host.context = &renderer;
    panel.host.draw =
        [](void* context, GadgetPanel& target, ui::gui_input::GadgetDraw what, int32_t index) {
            draw_request(*static_cast<GadgetRenderer*>(context), target, what, index);
        };
    panel.host.draw_panel = [](void* context, GadgetPanel& target, uint32_t flags) {
        return draw_panel(*static_cast<GadgetRenderer*>(context), target, flags);
    };
    panel.host.text_width = [](void* context, const void* gaf_font, const char* text) {
        return measure_with_font(*static_cast<const GadgetRenderer*>(context), gaf_font, text);
    };
    panel.host.line_height = [](void* context, const void* gaf_font) {
        return line_height_with_font(*static_cast<const GadgetRenderer*>(context), gaf_font);
    };
    panel.host.sprite_frames = [](void* context, const void* sprite) {
        return art_frame_count(*static_cast<const GadgetRenderer*>(context), sprite);
    };
    panel.host.select_font = [](void*, const void* font) { present::set_active_font(font); };
    panel.host.list_image_size = [](void* context,
                                    const layout::GadgetRecord& list,
                                    int32_t item,
                                    int32_t& width,
                                    int32_t& height) {
        const Sprite* image = list_image(*static_cast<const GadgetRenderer*>(context), list, item);
        if (image == nullptr)
            return false;
        width = image->width;
        height = image->height;
        return true;
    };
}

} // namespace oa::ui::gadget_render
