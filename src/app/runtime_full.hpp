// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Full tier's scene builder: the stages that turn the planner's draw
// list (world_draws.hpp) into the card's command list (oa/app/card.hpp).
// Each stage is one function that emits its batches into the frame's
// shared CardFrame in the list's order, so that the stages compose in the
// painter's order the planner laid down, and each can be switched on by
// itself (full_stages.hpp) while the processor still draws the rest of the
// battlefield, so that a stage lands and is checked on its own. This
// header holds what every stage shares and the stages built so far;
// runtime_full_sprites.cpp holds the sprite stage, full_terrain.hpp the
// terrain, which every Full frame draws before the stages, and
// runtime_full.cpp the branch of the presentation that runs them all on the
// executor. Nothing here reads the Runtime, so a stage is tested on its own.
#pragma once

#include "full_stages.hpp"
#include "oa/app/card.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/present/gpu_world/sprite_pages.hpp"
#include "oa/present/world_renderer.hpp"
#include "world_draws.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace oa::app::full {

/// A call only the Full tier makes failed: a page or target the executor
/// could not make or fill, or a frame it refused. The presentation drops
/// the tier's stages for the run and presents the frame as Basic does.
class CardError : public std::runtime_error {
  public:

    using std::runtime_error::runtime_error;
};

/// The view a frame's stages draw through: where the battlefield lies in
/// the target, the zoom the list was planned at, and what every vertex is
/// moved and scaled by.
struct SceneView {
    /// The battlefield's top-left corner in the target, in target pixels.
    float origin_x{};
    float origin_y{};
    /// Target pixels per layout pixel: 1 where the renderer's logical
    /// presentation maps layout pixels onto the display itself.
    float scale{1.0F};
    /// Screen pixels per map pixel the list was planned at: the zoom.
    float zoom{1.0F};
    /// How far past the camera's map pixel the view is drawn, in map
    /// pixels from 0 to 1: every vertex moves by its negative times the
    /// zoom, so that everything drawn moves together between map pixels.
    oa::present::world_renderer::ViewOffset offset{};
    /// The battlefield in layout pixels, which every batch's scissor clips to.
    int32_t width{};
    int32_t height{};
    /// The map pixel the battlefield's top-left corner shows: the camera,
    /// from which the fog's cells are counted, so that a sprite takes the
    /// fog's state at the point it is drawn at, as the fog lays its tiles
    /// over the picture by map column and row alone.
    int32_t camera_x{};
    int32_t camera_y{};
    /// The target the stages draw into; none for the frame's final target.
    card::TargetHandle target{};
};

/// The viewer's sight, from which a stage tells the fog's state at a cell:
/// the sight grid's cells of fog_cell_pixels map pixels a side.
struct SightView {
    std::span<const uint8_t> coverage{};     ///< units of the viewer seeing each cell
    std::span<const uint16_t> player_bits{}; ///< the players that have mapped each cell
    int32_t width{};                         ///< cells across
    int32_t height{};                        ///< cells down
    uint16_t viewer_bit{};                   ///< the viewer's bit in player_bits
    bool line_of_sight{};                    ///< the line-of-sight rule is on
    bool mapping{true};                      ///< the mapping rule is on
};

/// What the fog shows of a cell.
enum class CellFog : uint8_t {
    seen,   ///< in sight: drawn in colour
    unseen, ///< out of sight, under the line-of-sight rule: drawn greyed
    /// Never mapped, under the mapping rule, and not out of sight: drawn
    /// in colour, under the black pass that covers never-mapped ground.
    unmapped,
};

/// Returns the fog's state at a map pixel, as the fog grid marks the
/// cell's corners: out of sight where the line-of-sight rule grays it,
/// else never mapped where the mapping rule blacks it out, else in sight.
/// A pixel off the sight grid, or off the map, is in sight, since the fog
/// marks nothing there.
///
/// @param sight the viewer's sight
/// @param map_x map pixel column
/// @param map_z map pixel row
/// @return the state
[[nodiscard]] CellFog cell_fog(const SightView& sight, int32_t map_x, int32_t map_z) noexcept;

/// What the sprite stage asks of its host: the card's page that holds a
/// sprite page's texels.
struct SpritePageHooks {
    void* context{};
    /// Returns the card's page holding the texels of a page of the sprite
    /// pages (SpritePages::pages), making it when the page is new or has a
    /// new size; the host uploads the texels the page changed after the
    /// stage ran. A handle of none, or a null hook, leaves the sprite
    /// undrawn and counted as refused.
    card::PageHandle (*card_page)(void* context, uint32_t page){};
};

/// What the sprite stage reads.
struct SpriteStageInputs {
    const WorldDrawList* list{}; ///< the frame's draws, in order
    SceneView view{};
    SightView sight{};
    /// The match's palette, 4 bytes a colour, for a selection line's colour.
    const oa::PaletteBytes* palette{};
    /// The display gamma's table, applied to every flat colour as the
    /// pages apply it to their texels; null for a gamma of 1.
    const std::array<uint8_t, 256>* gamma{};
};

/// What the sprite stage did.
struct SpriteStageResult {
    uint32_t sprites{}; ///< sprite draws emitted, greyed ones among them
    uint32_t greyed{};  ///< sprites drawn from their greyed cells
    uint32_t refused{}; ///< sprites the pages or the host refused
    uint32_t squares{}; ///< particle squares emitted
    uint32_t lines{};   ///< lines and selection lines emitted
    uint32_t batches{}; ///< batches appended to the frame
    /// A frame placed on the pages for this draw was evicted before the
    /// draw ended, since the frame's distinct sprites exceed the pages'
    /// memory: the stage took its batches back and drew nothing.
    bool pages_overflowed{};
};

/// Emits the frame's sprites, particle squares, lines and selection lines
/// into the card's frame, in the list's order: one quad a draw, consecutive
/// draws that share their page and blend in one batch, every batch into
/// the view's target under the battlefield's scissor.
///
/// A sprite is its frame's cell on the pages (SpritePages::frame, keyed by
/// the frame the planner drew from), a quad `w * zoom` by `h * zoom` with
/// the hotspot scaled exactly, drawn by premultiplied alpha, which keeps
/// the key transparent: at a vertex alpha of one half where the planner
/// blends it through the alpha table, else opaque. Under a cell out of
/// sight it is drawn from its greyed cell when the pages hold a gray table;
/// under never-mapped ground it is drawn as under any other, since the
/// black pass over that ground goes over it. A page's texels are sampled
/// nearest at a whole-number zoom,
/// linear below 1 and pixel-art above. A particle's square is the
/// planner's rectangle, solid; a line is a quad `max(1, zoom)` layout
/// pixels wide through the centres of its end pixels, a selection line the
/// same from map pixels at the zoom, each in its colour through the gamma.
/// Every other kind of draw is left to the other stages.
///
/// @param inputs the list, the view, the sight, the palette and the gamma
/// @param[in,out] pages the sprite pages, which place the frames drawn this frame
/// @param hooks the host's pages for the sprite pages
/// @param[in,out] frame the card's frame the batches are appended to
/// @return what was emitted
SpriteStageResult emit_sprites(
    const SpriteStageInputs& inputs,
    oa::present::gpu_world::SpritePages& pages,
    const SpritePageHooks& hooks,
    card::CardFrame& frame
);

/// Returns the bits (card_kind_bit) of the draw kinds the card draws when
/// stages are on, which the bands leave undrawn: for the sprite stage the
/// sprites, blended sprites, particle squares, lines and selection lines.
///
/// @param stages the stages on (full_stages.hpp)
/// @return the kinds' bits; 0 for no stage
[[nodiscard]] uint16_t card_kinds(uint8_t stages) noexcept;

/// Returns how a sprite's page is sampled at a zoom: nearest at a
/// whole-number zoom, where the card gives the game's pixels exactly;
/// linear below 1; pixel-art above 1, read as nearest where the renderer
/// lacks the mode.
///
/// @param zoom screen pixels per map pixel
/// @return the sampling
[[nodiscard]] card::Sampling sprite_sampling(float zoom) noexcept;

/// Returns how wide a thin line is drawn: `max(1, zoom)` layout pixels, at
/// the view's scale, so that it stays about one screen pixel wide zoomed
/// out and grows with the zoom zoomed in.
///
/// @param view the view
/// @return the width in target pixels
[[nodiscard]] float line_width(const SceneView& view) noexcept;

/// Returns a flat colour through the display gamma, as the card takes it:
/// each channel from 0 to 1.
///
/// @param rgb the colour before the gamma
/// @param gamma the gamma's table; null for a gamma of 1
/// @return the colour, opaque
[[nodiscard]] card::Colour
flat_colour(const std::array<uint8_t, 3>& rgb, const std::array<uint8_t, 256>* gamma) noexcept;

/// Appends a line as a quad of one colour: the segment between the centres
/// of its first and last pixels, `width` pixels wide and drawn out half a
/// width past each end, so that one pixel wide along a row or a column it
/// covers exactly the pixels the game's line covers, and on a slant the
/// pixels nearest the segment. Positions are target pixels.
///
/// @param[in,out] frame the frame
/// @param x0 the first pixel's centre column
/// @param y0 the first pixel's centre row
/// @param x1 the last pixel's centre column
/// @param y1 the last pixel's centre row
/// @param width pixels across the line, above 0
/// @param colour the colour of every corner
void append_line_quad(
    card::CardFrame& frame,
    float x0,
    float y0,
    float x1,
    float y1,
    float width,
    const card::Colour& colour
);

} // namespace oa::app::full
