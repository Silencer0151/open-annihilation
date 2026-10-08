// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Text drawn with the fonts that travel with the game, through FreeType, the
// same on every platform: one line of UTF-8 at a pixel size and weight,
// drawn as coverage, hinted to whole pixels in one bit per pixel, or
// anti-aliased. Each character comes from the first font of the stack that
// has it: DejaVu Sans Bold, DejaVu Sans, Noto Sans CJK, Noto Emoji.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::platform::text_font {

/// The fonts of the stack, in the order a character is looked for in them.
enum class Face : uint8_t {
    dejavu_sans_bold, ///< Latin, Greek, Cyrillic and symbols, bold
    dejavu_sans,      ///< the same scripts with more characters, regular
    noto_sans_cjk,    ///< Chinese, Japanese and Korean, bold
    noto_emoji,       ///< emoji, drawn in one colour like any character
};

/// How many fonts the stack holds.
inline constexpr std::size_t face_count = 4;

/// The file of each font in the fonts folder, by Face.
inline constexpr std::array<std::string_view, face_count> face_files{
    "DejaVuSans-Bold.ttf",
    "DejaVuSans.ttf",
    "NotoSansCJKsc-Bold.otf",
    "NotoEmoji.ttf",
};

/// The name of the folder beside the game that holds the fonts.
inline constexpr std::string_view font_folder = "fonts";

/// The largest pixel size a draw accepts.
inline constexpr int32_t max_pixel_size = 256;
/// The longest text, in UTF-8 bytes, a draw accepts.
inline constexpr std::size_t max_text_bytes = 4096;
/// The widest line, in pixels, a draw gives; the rest of a longer one is cut off.
inline constexpr int32_t max_line_width = 8192;
/// The most pixels of letter spacing a style may add after each character.
inline constexpr int32_t max_letter_spacing = 16;

/// The weight a line is drawn in.
enum class Weight : uint8_t {
    regular, ///< DejaVu Sans first; Noto Emoji at its regular weight
    bold,    ///< DejaVu Sans Bold first, then DejaVu Sans for what it lacks
};

/// How glyphs become pixels.
enum class Rendering : uint8_t {
    mono,        ///< hinted to whole pixels, one bit per pixel: coverage 0 or 255
    antialiased, ///< hinted, 256 levels of coverage
};

/// How a line is drawn.
struct Style {
    /// pixels per em of the DejaVu faces, 1..max_pixel_size; Noto Sans CJK
    /// and Noto Emoji are drawn at related_pixel_size of it
    int32_t pixel_size{14};
    Weight weight{Weight::bold};
    Rendering rendering{Rendering::mono};
    /// pixels added after each character's advance, 0..max_letter_spacing
    int32_t letter_spacing{};
    /// the least pixels per em Noto Sans CJK is drawn at, 0..max_pixel_size:
    /// smaller ideographs fill in; 0 leaves it at related_pixel_size
    int32_t least_cjk_pixel_size{};
};

/// The rows of a line in one style, which fit every font of the stack.
struct LineMetrics {
    int32_t ascent{};  ///< rows above the baseline
    int32_t descent{}; ///< rows below the baseline, the baseline's own row among them
};

/// The rows of one face at the pixel size a style draws it at.
struct FaceMetrics {
    int32_t pixel_size{}; ///< pixels per em of this face
    int32_t ascent{};     ///< rows above the baseline
    int32_t descent{};    ///< rows below the baseline, the baseline's own row among them
};

/// Pixels per em of the bold sans face beside the message log's font, at
/// the game fonts' size and a scale of 1.
inline constexpr int32_t message_log_pixel_size = 14;
/// Pixels per em of the bold sans face beside the status readouts' font.
inline constexpr int32_t status_readout_pixel_size = 11;
/// Pixels per em of the regular sans face beside the labels' and the chat
/// line's font.
inline constexpr int32_t label_pixel_size = 11;
/// The least pixels per em of the CJK face while a Chinese, Japanese or
/// Korean language is shown, so its strokes do not fill in.
inline constexpr int32_t least_cjk_language_pixel_size = 12;

/// One drawn line: how much of each pixel the text covers.
struct Coverage {
    int32_t width{};  ///< columns
    int32_t height{}; ///< rows
    /// the row of the baseline: the line's ascent, or more when a glyph
    /// rises above it
    int32_t baseline{};
    /// the column the pen starts at: 0, or more when a glyph reaches left
    /// of the pen
    int32_t origin{};
    int32_t advance{}; ///< pixels the pen moved, letter spacing included
    /// width * height bytes, top row first: 0 is untouched, 255 fully
    /// covered; a mono line holds no other value
    std::vector<uint8_t> alpha{};
};

/// One character of a line: the font that draws it and where.
struct Placement {
    char32_t character{};
    Face face{};
    int32_t pen{};     ///< the pen's column, counted from the line's start
    int32_t advance{}; ///< pixels the pen moves past it, letter spacing included
};

/// Gives the faces a weight looks a character up in, in that order.
///
/// Bold looks in DejaVu Sans Bold first, then DejaVu Sans for what it
/// lacks. Regular looks in DejaVu Sans first. Both then look in Noto Sans
/// CJK and Noto Emoji. The span lives as long as the program.
///
/// @param weight the weight of the line
/// @return the faces, the first that has a character drawing it
[[nodiscard]] std::span<const Face> fallback_chain(Weight weight) noexcept;

/// Gives the pixel size Noto Sans CJK and Noto Emoji are drawn at next to the
/// DejaVu faces at a size.
///
/// Ideographs stand about as tall as DejaVu's capitals with one or two
/// rows more, as they do at 12 px beside the 14-px log font: six sevenths
/// of the size, rounded, but no less than the size itself or 12, whichever
/// is smaller.
///
/// @param pixel_size the DejaVu faces' pixels per em
/// @return the CJK and emoji pixels per em
[[nodiscard]] int32_t related_pixel_size(int32_t pixel_size) noexcept;

/// Decodes UTF-8 text.
///
/// @param text the bytes
/// @return the code points; empty for bytes that are not UTF-8: a stray or
///         missing continuation byte, an overlong form, a surrogate or a
///         value above U+10FFFF
[[nodiscard]] std::optional<std::vector<char32_t>> decode_utf8(std::string_view text);

/// Tells whether a character takes no room and draws nothing: a control
/// character, a variation selector, a zero-width space or joiner, or the
/// byte-order mark.
///
/// @param character the code point
/// @return true when the line passes over it
[[nodiscard]] bool is_invisible(char32_t character) noexcept;

/// Gives the release of FreeType the stack draws with.
///
/// @return the release, such as "2.14.3"
[[nodiscard]] std::string freetype_version();

/// Gives the folder the game's fonts travel in: the fonts folder of the
/// application bundle's resources on macOS, else the fonts folder beside
/// the executable.
///
/// @return the folder; empty when the executable's own path is unknown
[[nodiscard]] std::filesystem::path bundled_font_directory();

/// The fonts of the stack, opened from one folder, with the glyphs drawn so
/// far kept for the lines that draw them again.
///
/// The fonts are read from their files as glyphs are needed. One thread at a
/// time uses a stack.
class FontStack {
  public:

    /// Opens the stack's fonts.
    ///
    /// @param directory the folder that holds every file of face_files
    /// @return the stack; null when a file is missing or FreeType cannot
    ///         read it
    [[nodiscard]] static std::unique_ptr<FontStack> open(const std::filesystem::path& directory);

    /// Closes the fonts and the FreeType library they were opened with.
    ~FontStack();
    FontStack(const FontStack&) = delete;
    FontStack& operator=(const FontStack&) = delete;

    /// Finds the font that draws a character.
    ///
    /// @param character the code point
    /// @param weight the weight of the line
    /// @return the first font of the weight's chain that has the character,
    ///         else the chain's first font, which draws its missing-glyph box;
    ///         empty for a character is_invisible names
    [[nodiscard]] std::optional<Face> face_for(char32_t character, Weight weight) const;

    /// Gives the rows of a line in a style.
    ///
    /// @param style the style; its pixel size must be 1..max_pixel_size
    /// @return the ascent and descent of the tallest font of the stack at
    ///         its size; empty for a size out of range
    [[nodiscard]] std::optional<LineMetrics> metrics(const Style& style);

    /// Gives the rows of one face at the pixel size a style draws it at.
    ///
    /// The sans faces take the style's pixel size. Noto Sans CJK and Noto
    /// Emoji take related_pixel_size of it, and Noto Sans CJK no less than
    /// the style's least size. Noto Emoji is set to the style's weight.
    ///
    /// @param face the face
    /// @param style the style; its pixel size must be 1..max_pixel_size
    /// @return the face's size and rows; empty for a size out of range
    [[nodiscard]] std::optional<FaceMetrics> face_metrics(Face face, const Style& style);

    /// Lays a line out without drawing it.
    ///
    /// @param text UTF-8 text, at most max_text_bytes
    /// @param style the style
    /// @return each visible character with its font and pen position; empty
    ///         for text that is not UTF-8 or too long, or a style out of range
    [[nodiscard]] std::optional<std::vector<Placement>>
    layout(std::string_view text, const Style& style);

    /// Draws a line.
    ///
    /// Each character is drawn at the pen, which starts at origin and moves
    /// by the character's advance, rounded to a whole pixel, and the
    /// style's letter spacing. Characters are not shaped, kerned or
    /// reordered. Where two glyphs cover one pixel, the greater coverage is
    /// kept.
    ///
    /// @param text UTF-8 text, at most max_text_bytes
    /// @param style the style
    /// @return the coverage, at least the line's ascent and descent tall;
    ///         empty for text that is not UTF-8 or too long, or a style out
    ///         of range
    [[nodiscard]] std::optional<Coverage> draw(std::string_view text, const Style& style);

    /// Tells how many drawn glyphs the stack keeps.
    ///
    /// @return glyphs kept, at most kept_glyphs
    [[nodiscard]] std::size_t cached_glyphs() const noexcept;

    /// Tells how many glyphs the stack has drawn since it opened, each
    /// counted again when it is drawn again after the store forgot it.
    ///
    /// @return glyphs drawn
    [[nodiscard]] std::size_t drawn_glyphs() const noexcept;

    /// The most drawn glyphs a stack keeps; past it, the glyph used longest
    /// ago is forgotten first.
    static constexpr std::size_t kept_glyphs = 4096;

  private:

    struct Fonts;
    /// Takes the opened fonts (open makes them).
    ///
    /// @param fonts the FreeType library, the faces and the glyph store
    explicit FontStack(std::unique_ptr<Fonts> fonts) noexcept;
    std::unique_ptr<Fonts> fonts_{};
};

} // namespace oa::platform::text_font
