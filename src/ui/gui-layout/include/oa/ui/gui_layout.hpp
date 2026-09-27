// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace oa::ui::gui_layout {

namespace limit {
// A panel holds 200 gadget records of 0x15B bytes, the root included.
inline constexpr std::size_t gadgets = 200;
inline constexpr std::size_t input_bytes = 4U * 1024U * 1024U;
inline constexpr std::size_t nesting_depth = 8;
inline constexpr std::size_t sections = gadgets * 3;
inline constexpr std::size_t fields_per_section = 128;
inline constexpr std::size_t section_name_bytes = 255;
inline constexpr std::size_t field_name_bytes = 63;
inline constexpr std::size_t source_value_bytes = 4096;
inline constexpr std::size_t name_bytes = 15;
inline constexpr std::size_t help_bytes = 127;
inline constexpr std::size_t text_bytes = 127;
inline constexpr std::size_t link_bytes = 15;
inline constexpr std::size_t panel_name_bytes = 15;
inline constexpr std::size_t filename_bytes = 31;
inline constexpr std::size_t quick_key_source_bytes = 18;
inline constexpr int16_t text_box_max_characters = 128;
} // namespace limit

enum class GadgetType : uint8_t {
    panel = 0,
    button = 1,
    list_box = 2,
    text_box = 3,
    scroll_bar = 4,
    label = 5,
    hot_surface = 6,
    // Types 7 and 8 carry only `filename` beyond the common fields. A type-7
    // record names an FNT font, without its suffix, in the panel's font
    // directory; a type-8 record names a file in the panel's GUI directory.
    // The gadget engine loads either file when it first draws the panel.
    font_resource = 7,
    file_resource = 8,
    // Neither the GUI loader nor the gadget engine handles type 9; its record
    // holds the common fields only.
    unhandled = 9,
    null_resource = 10,
    // An image record. Present in shipped layouts; the GUI loader reads only
    // its common fields, and the gadget engine draws its image.
    image = 12,
};

struct CommonFields {
    GadgetType type = GadgetType::panel;
    uint8_t association = 0;
    std::string name;
    int16_t x = 0;
    int16_t y = 0;
    int16_t width = 0;
    int16_t height = 0;
    int32_t attributes = 0;
    uint16_t foreground_color = 0;
    uint16_t background_color = 0;
    int8_t texture_number = 0;
    int8_t font_number = 0;
    int8_t active = 0;
    int8_t common_attributes = 0;
    bool gaf_file = false;

    // The GUI loader reads `help`, immediately clears its 0x81-byte
    // destination, then translates the empty buffer. source_help keeps the
    // bounded source value for diagnostics; runtime_help is the translation of
    // the empty buffer.
    std::string source_help;
    std::string runtime_help;

    bool operator==(const CommonFields&) const = default;
};

struct Version {
    int8_t major = 0;
    int8_t minor = 0;
    int8_t revision = 0;
    bool operator==(const Version&) const = default;
};

struct PanelFields {
    int16_t declared_total_gadgets = 0;
    // The GUI loader overwrites the first record's count with the parsed
    // record count - 1.
    int16_t loaded_total_gadgets = 0;
    Version version;
    std::string panel;
    std::string carriage_return_default;
    std::string escape_default;
    std::string default_focus;
    bool operator==(const PanelFields&) const = default;
};

struct ButtonFields {
    int16_t status = 0;
    std::string source_text;
    std::string text;
    int8_t quick_key = 0;
    bool grayed_out = false;
    int8_t stages = 0;
    bool operator==(const ButtonFields&) const = default;
};

struct ListBoxFields {
    int16_t item_height = 0;
    bool operator==(const ListBoxFields&) const = default;
};

struct TextBoxFields {
    // Only values greater than 128 are clamped; negative values remain.
    int16_t max_characters = 0;
    std::string source_text;
    std::string text;
    bool operator==(const TextBoxFields&) const = default;
};

struct ScrollBarFields {
    int16_t range = 0;
    int32_t thickness = 0; // sign-extended from an int16
    int16_t knob_position = 0;
    int16_t knob_size = 0;
    std::string source_text;
    std::string text;
    bool operator==(const ScrollBarFields&) const = default;
};

struct LabelFields {
    std::string source_text;
    std::string text;
    std::string link;
    bool operator==(const LabelFields&) const = default;
};

struct HotSurfaceFields {
    bool hot = false;
    bool operator==(const HotSurfaceFields&) const = default;
};

struct FileResourceFields {
    std::string filename;
    bool operator==(const FileResourceFields&) const = default;
};

struct NullResourceFields {
    int32_t nuttin = 0;
    bool operator==(const NullResourceFields&) const = default;
};

struct CommonOnlyFields {
    bool operator==(const CommonOnlyFields&) const = default;
};

using TypeFields = std::variant<
    PanelFields,
    ButtonFields,
    ListBoxFields,
    TextBoxFields,
    ScrollBarFields,
    LabelFields,
    HotSurfaceFields,
    FileResourceFields,
    NullResourceFields,
    CommonOnlyFields>;

struct Gadget {
    CommonFields common;
    TypeFields fields = CommonOnlyFields{};
    bool operator==(const Gadget&) const = default;
};

struct Layout {
    std::vector<Gadget> gadgets;
    bool operator==(const Layout&) const = default;
};

// Screen rectangle with inclusive edges: right = left + width - 1 and
// bottom = top + height - 1.
struct Rect {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = -1;
    int32_t bottom = -1;

    bool operator==(const Rect&) const = default;
};

/// Returns a gadget's screen rectangle.
///
/// A zero type byte keeps that record's stored xpos/ypos. Any other type adds
/// the root record's stored xpos/ypos. The root is index 0, not a type-zero
/// origin forced to (0,0) the way the panel-relative rectangle does.
///
/// @param gadgets Gadget records of one layout; index 0 is the root.
/// @param index Record to measure.
/// @return The rectangle with inclusive edges, or nullopt for an index past the span.
/// @quirk A nonzero type at index 0 adds its own origin twice.
[[nodiscard]] std::optional<Rect>
gadget_rectangle(std::span<const Gadget> gadgets, std::size_t index) noexcept;

enum class ErrorCode {
    none,
    input_limit,
    binary_format,
    malformed_syntax,
    nesting_limit,
    section_limit,
    field_limit,
    gadget_limit,
    invalid_integer,
    invalid_root,
    translated_text_limit,
};

struct Error {
    ErrorCode code = ErrorCode::none;
    std::size_t offset = 0;
    std::string message;
};

struct ParseResult {
    std::optional<Layout> layout;
    std::optional<Error> error;

    /// Reports whether parsing produced a layout.
    [[nodiscard]] bool ok() const noexcept { return layout.has_value(); }
};

// Text translation returns a replacement only when the translation table
// holds the source string; nullopt stands for a missing table or key.
using TranslationLookup = std::function<std::optional<std::string>(std::string_view source)>;

/// Parses a text GUI layout (a .GUI file) into gadget records.
///
/// String fields are bounded to the game's record widths and text fields are
/// passed through the translation lookup. The first record must be the panel;
/// its loaded_total_gadgets becomes the parsed record count - 1.
///
/// @param bytes File contents; at most 4 MiB, without NUL bytes.
/// @param translation_lookup Text translation; empty for none.
/// @return The layout, or the first error with its byte offset.
/// @quirk A file that ends without its final top-level `}` still yields its
///        last gadget, as shipped SCORE.GUI needs.
[[nodiscard]] ParseResult
parse(std::span<const uint8_t> bytes, const TranslationLookup& translation_lookup = {});

} // namespace oa::ui::gui_layout
