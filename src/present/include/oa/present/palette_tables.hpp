// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Display palette application (gamma), the display lookup tables (alpha,
// shade, light, gray, blue), nearest-colour matching and the lens table.

#include "oa/present/display.hpp"
#include "oa/present/surface.hpp"

#include <cstdint>
#include <span>

namespace oa::present {

inline constexpr int32_t alpha_table_size = 0x10000; // 256 x 256 blend results
inline constexpr int32_t ramp_table_rows = 32;       // shade and light tables: 32 rows of 256
inline constexpr int32_t shade_table_size = 0x2000;
inline constexpr int32_t light_table_size = 0x2000;
inline constexpr int32_t gray_table_size = 0x100;
inline constexpr int32_t blue_table_size = 0x100;
inline constexpr uint8_t gamma_channel_max = 255;
// Brightness window (sum of R+G+B) searched around a target colour.
inline constexpr int32_t nearest_color_window = 40;
inline constexpr int32_t nearest_color_no_match = 1000000000;
// Lens table entry for pixels outside the lens radius.
inline constexpr uint16_t lens_outside = 32000;

/// Stores palette entries and computes their gamma-corrected device colours.
///
/// entries[0..count) go to palette slots start.., and each device channel
/// is the truncated channel * gamma, clamped above at 255 only; device flags
/// are cleared.
///
/// @param[in,out] display display whose palette and gamma are used
/// @param entries source entries
/// @param start first palette slot
/// @param count number of entries; non-positive changes nothing and succeeds
/// @param[out] device device palette whose slots start.. are recomputed
/// @return false, changing nothing, when the range leaves the palette
/// @quirk The device colours are read from entries[start..start+count), not
///     from the slots just stored, so a non-zero start reads the source past
///     `count`.
bool apply_palette_entries(
    DisplayContext& display,
    const PaletteEntry* entries,
    int32_t start,
    int32_t count,
    Palette& device
) noexcept;

/// Sets the display gamma and recomputes the whole device palette.
///
/// @param[in,out] display display whose gamma is set
/// @param gamma channel multiplier
/// @param[out] device device palette to recompute
void set_palette_gamma(DisplayContext& display, float gamma, Palette& device) noexcept;

/// Gamma-corrects one palette channel as apply_palette_entries does.
///
/// @param channel channel value
/// @param gamma channel multiplier
/// @return the truncated channel * gamma, clamped at 255
[[nodiscard]] uint8_t gamma_channel(uint8_t channel, float gamma) noexcept;

/// Reports whether a presentation target exists to realise the palette on.
///
/// The device palette goes with each RenderSink::present call, so nothing
/// else needs realising.
///
/// @param display display to check
/// @return true when the sink has a present callback
[[nodiscard]] bool set_display_palette(const DisplayContext& display) noexcept;

/// Allocates a zeroed 256x256 alpha table, replacing any previous one.
///
/// @param[in,out] display display that owns the table
/// @return false when the allocation fails
bool load_alpha_table(DisplayContext& display) noexcept;

/// Frees the alpha table.
///
/// @param[in,out] display display whose table pointer is cleared
void free_alpha_table(DisplayContext& display) noexcept;

/// Allocates a zeroed shade table (32 rows of 256), replacing any previous one.
///
/// @param[in,out] display display that owns the table
/// @return false when the allocation fails
bool load_shade_table(DisplayContext& display) noexcept;

/// Frees the shade table.
///
/// @param[in,out] display display whose table pointer is cleared
void free_shade_table(DisplayContext& display) noexcept;

/// Allocates a zeroed light table (32 rows of 256), replacing any previous one.
///
/// @param[in,out] display display that owns the table
/// @return false when the allocation fails
bool load_light_table(DisplayContext& display) noexcept;

/// Frees the light table.
///
/// @param[in,out] display display whose table pointer is cleared
void free_light_table(DisplayContext& display) noexcept;

/// Allocates a zeroed 256-entry gray table, replacing any previous one.
///
/// @param[in,out] display display that owns the table
/// @return false when the allocation fails
bool load_gray_table(DisplayContext& display) noexcept;

/// Frees the gray table.
///
/// @param[in,out] display display whose table pointer is cleared
void free_gray_table(DisplayContext& display) noexcept;

/// Allocates a zeroed 256-entry blue table, replacing any previous one.
///
/// @param[in,out] display display that owns the table
/// @return false when the allocation fails
bool load_blue_table(DisplayContext& display) noexcept;

/// Frees the blue table.
///
/// @param[in,out] display display whose table pointer is cleared
void free_blue_table(DisplayContext& display) noexcept;

/// Sums R+G+B of every palette entry and exchange-sorts the sums ascending with a parallel index order.
///
/// @param palette palette to sort
/// @param[out] brightness 256 sums in ascending order
/// @param[out] order palette index of each sorted sum
void sort_palette_by_brightness(
    const Palette& palette, int32_t brightness[OA_PALETTE_COLORS], uint8_t order[OA_PALETTE_COLORS]
) noexcept;

/// Finds the palette index nearest a colour by squared distance among entries of similar brightness.
///
/// Only entries within +-40 of the target brightness compete; the first
/// closest in sorted order wins.
///
/// @param palette palette to search
/// @param brightness ascending brightness sums from sort_palette_by_brightness
/// @param order palette indices from sort_palette_by_brightness
/// @param r target red
/// @param g target green
/// @param b target blue
/// @return the nearest palette index
/// @quirk With no candidate the result is order[(uint8)i] for the sorted
///     position i where the scan stopped (0 after a full pass).
[[nodiscard]] uint8_t find_nearest_sorted_color(
    const Palette& palette,
    const int32_t brightness[OA_PALETTE_COLORS],
    const uint8_t order[OA_PALETTE_COLORS],
    uint8_t r,
    uint8_t g,
    uint8_t b
) noexcept;

/// Fills the alpha table with the nearest colour to the average of each index pair.
///
/// Entry [a][a] is a itself.
///
/// @param[in,out] display display whose alpha table is filled
/// @param palette game palette
/// @return the table, or null when display_flag_alpha_table is clear or no table is loaded
uint8_t* build_alpha_table(DisplayContext& display, const Palette& palette) noexcept;

/// Copies a cached 256x256 pair table (PALETTE.ALP) into the alpha table.
///
/// Nothing happens unless display_flag_alpha_table is set and a table is loaded.
///
/// @param[in,out] display display whose alpha table is filled
/// @param table 0x10000 bytes; null copies nothing
void copy_alpha_table(DisplayContext& display, const uint8_t* table) noexcept;

/// Fills the shade table: row r maps each entry to the nearest colour of the entry scaled by r * 0.06875.
///
/// @param[in,out] display display whose shade table is filled
/// @param palette game palette
/// @return the table, or null when display_flag_shade_table is clear or no table is loaded
/// @quirk A scaled channel whose low 16 bits exceed 0xFF becomes 0xFF.
uint8_t* build_shade_table(DisplayContext& display, const Palette& palette) noexcept;

/// Fills the light table: row r maps each entry to the nearest colour of the entry scaled by 1 + r/30.
///
/// Scaled channels are truncated and capped at 0xFF; the scale is rounded
/// twice (the step product, then the sum), as the game computes it.
///
/// @param[in,out] display display whose light table is filled
/// @param palette game palette
/// @return the table, or null when display_flag_light_table is clear or no table is loaded
uint8_t* build_light_table(DisplayContext& display, const Palette& palette) noexcept;

/// Fills the gray table: each entry maps to the nearest colour to the gray of its (r+g+b)/3.
///
/// @param[in,out] display display whose gray table is filled
/// @param palette game palette
/// @return the table, or null when display_flag_gray_table is clear or no table is loaded
uint8_t* build_gray_table(DisplayContext& display, const Palette& palette) noexcept;

/// Fills the blue (underwater) table: each entry maps to the nearest colour to its half brightness with blue lifted.
///
/// The target is (r/2, g/2, b/2 + 0x32).
///
/// @param[in,out] display display whose blue table is filled
/// @param palette game palette
/// @return the table, or null when display_flag_blue_table is clear or no table is loaded
/// @quirk Blue becomes 0xFF once b/2 + 0x3C, not b/2 + 0x32, passes 0xFF.
uint8_t* build_blue_table(DisplayContext& display, const Palette& palette) noexcept;

/// Copies a loaded shade table into the display's table.
///
/// Nothing happens unless display_flag_shade_table is set and a table is loaded.
///
/// @param[in,out] display display whose shade table is filled
/// @param table 0x2000 bytes; null copies nothing
void copy_shade_table(DisplayContext& display, const uint8_t* table) noexcept;

/// Copies a loaded light table into the display's table.
///
/// Nothing happens unless display_flag_light_table is set and a table is loaded.
///
/// @param[in,out] display display whose light table is filled
/// @param table 0x2000 bytes; null copies nothing
void copy_light_table(DisplayContext& display, const uint8_t* table) noexcept;

// Where a session table came from.
enum class TableSource : uint8_t {
    file,     // the palettes/PALETTE.* bytes
    built,    // no file: built from the game palette
    rejected, // the file had the wrong size: built from the game palette
};

/// Installs palettes/PALETTE.ALP, or builds the alpha table from the game palette when the file is absent or malformed.
///
/// A built table is never written back to the game directory; 3.1c saves it
/// there as PALETTE.ALP.
///
/// @param[in,out] display display whose alpha table is filled
/// @param palette game palette the table is built from
/// @param file PALETTE.ALP bytes; empty when the file is absent
/// @return where the table came from
TableSource load_session_alpha_table(
    DisplayContext& display, const Palette& palette, std::span<const uint8_t> file
) noexcept;

/// Installs palettes/PALETTE.SHD, or builds the shade table from the game palette when the file is absent or malformed.
///
/// A built table is never written back to the game directory.
///
/// @param[in,out] display display whose shade table is filled
/// @param palette game palette the table is built from
/// @param file PALETTE.SHD bytes; empty when the file is absent
/// @return where the table came from
TableSource load_session_shade_table(
    DisplayContext& display, const Palette& palette, std::span<const uint8_t> file
) noexcept;

/// Installs palettes/PALETTE.LHT, or builds the light table from the game palette when the file is absent or malformed.
///
/// A built table is never written back to the game directory.
///
/// @param[in,out] display display whose light table is filled
/// @param palette game palette the table is built from
/// @param file PALETTE.LHT bytes; empty when the file is absent
/// @return where the table came from
TableSource load_session_light_table(
    DisplayContext& display, const Palette& palette, std::span<const uint8_t> file
) noexcept;

/// Builds the session gray table from the game palette.
///
/// @param[in,out] display display whose gray table is filled
/// @param palette game palette
void init_session_gray_table(DisplayContext& display, const Palette& palette) noexcept;

/// Builds the session blue table from the game palette.
///
/// @param[in,out] display display whose blue table is filled
/// @param palette game palette
void init_session_blue_table(DisplayContext& display, const Palette& palette) noexcept;

// palettes/PALETTE.ALP, .SHD and .LHT; an empty span stands for a missing file.
struct PaletteTableFiles {
    std::span<const uint8_t> alpha{};
    std::span<const uint8_t> shade{};
    std::span<const uint8_t> light{};
};

struct PaletteTableSources {
    TableSource alpha = TableSource::built;
    TableSource shade = TableSource::built;
    TableSource light = TableSource::built;
};

/// Fills the display lookup tables at the game's start-up.
///
/// Alpha, shade and light come from their files or are built, then the gray
/// and blue tables are built, all from the game palette (PALETTE.PAL).
///
/// @param[in,out] display display whose tables are filled
/// @param palette game palette
/// @param files PALETTE.ALP, .SHD and .LHT bytes
/// @return where the alpha, shade and light tables came from
PaletteTableSources load_session_tables(
    DisplayContext& display, const Palette& palette, const PaletteTableFiles& files
) noexcept;

/// Allocates a sprite whose data and aux planes share one zeroed block.
///
/// @param width width in pixels; stored truncated to 16 bits
/// @param height height in rows; stored truncated to 16 bits
/// @return the sprite, or an empty buffer when a size is negative or the
///     plane exceeds the loader bound (16 Mi pixels)
[[nodiscard]] SpriteBuffer create_two_plane_sprite(int32_t width, int32_t height);

/// Builds a lens: per pixel, the source offset seen through a magnifier.
///
/// The data plane holds 16-bit offsets, lens_outside past radius width/4;
/// aux leaves room for the captured pixels. The hotspot is the centre.
///
/// @param width lens width in pixels; 0..0x7FFF
/// @param height lens height in rows
/// @param scale magnifier strength
/// @return the lens sprite, or an empty buffer for a width out of range
[[nodiscard]] SpriteBuffer build_lens_frame(int32_t width, int32_t height, int32_t scale);

/// Reads one 16-bit entry of a lens frame's data plane.
///
/// @param lens lens sprite from build_lens_frame
/// @param index pixel index (row * width + column)
/// @return the stored offset, or lens_outside
[[nodiscard]] uint16_t lens_offset(const Sprite& lens, int32_t index) noexcept;

/// Halves a sprite into another, blending each 2x2 block through the display alpha table.
///
/// Each row pair is blended horizontally first; the result is
/// alpha[blend(top) * 256 + blend(bottom)]. Nothing happens without an alpha table.
///
/// @param display display that owns the alpha table
/// @param source sprite to halve; rows are its width apart
/// @param[in,out] target sprite whose size gives the output size
void blend_downsample_sprite(
    const DisplayContext& display, const Sprite& source, Sprite& target
) noexcept;

} // namespace oa::present
