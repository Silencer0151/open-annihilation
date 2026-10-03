// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-render-tiers: the accelerated presentation switched on over the
// main menu and a skirmish, or a campaign mission, each presented frame read
// back and compared with what the processor composes or with the card's
// references applied to the scene; frames that depend on none before them,
// the first after the tier is switched on or the window resized among them;
// the standard tier's picture for the readers that keep one; the view drawn
// between map pixels as a slow scroll moves it, and the pointer picking what
// is drawn (runtime_smooth_pan_check.cpp); and the card's textures made once
// and its prescale targets drawn once a painted frame.
// The tier comes from the game's own decision: --hardware-acceleration
// switches it on after the start-up function test passed, and the check
// switches it off and on again as --no-hardware-acceleration and
// --hardware-acceleration would. With --native-density the window opened at
// the display's own density, and after the main menu and the loading screen
// the check runs its density case alone: the match laid out in window
// points, read back at the display's size, at zoom 1 and a whole-number
// density the processor's composition enlarged by nearest replication, and
// the unit under the pointer the one drawn there.
#include "oa/app/runtime.hpp"

#include "full_presentation.hpp"
#include "match_models.hpp"
#include "render_host.hpp"
#include "render_run.hpp"
#include "runtime_full.hpp"
#include "xrgb_conversion.hpp"

#include "oa/formats/png.hpp"
#include "oa/platform/machine.hpp"
#include "oa/platform/render_probe.hpp"
#include "oa/sim/unit_spawn/spawn_runtime.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace oa::app {

namespace {

namespace policy = render_policy;
namespace wr = oa::present::world_renderer;
namespace gw = oa::present::gpu_world;

/// Exit code of a check that skipped, which ctest reports as skipped.
constexpr int skipped_exit_code = 77;

/// A window whose chrome and front end scale by a whole number (2), and
/// one whose chrome does not (1.6).
constexpr int whole_scale_width = 1280;
constexpr int whole_scale_height = 960;
constexpr int part_scale_width = 1024;
constexpr int part_scale_height = 768;

/// Distance from the pointer that covers every cursor frame.
constexpr int cursor_reach = 64;

/// The zooms the area pass is checked at, and those the card magnifies at.
constexpr std::array<float, 3> area_zooms{0.5F, 0.6F, 0.75F};
constexpr std::array<float, 3> magnified_zooms{1.37F, 2.0F, 4.0F};
/// The zoom of the frame drawn after frames between ticks, and the
/// fractions of a tick those show.
constexpr float history_zoom = 1.37F;
constexpr std::array<float, 3> between_ticks{0.25F, 0.5F, 0.75F};
/// The zoom the capture is checked at.
constexpr float capture_zoom = 2.0F;
/// The zoom ease: from the lowest zoom to the highest and back, each frame
/// this many times the last.
constexpr float ease_step = 1.0905F;
/// The zooms the pictures for the maintainer are taken at.
constexpr std::array<float, 3> picture_zooms{0.5F, 1.0F, 2.5F};
/// The zooms the first frame after the tier is switched on, or after the
/// window is resized, is checked magnified at.
constexpr std::array<float, 2> first_frame_zooms{1.37F, 2.5F};
/// The zooms the standard tier's picture kept for a reader is checked at:
/// one the area pass reduces and one the card magnifies.
constexpr std::array<float, 2> kept_picture_zooms{0.5F, 2.0F};
/// Frames the loop presents with the HUD's prescale target counted.
constexpr int counted_hud_frames = 3;
/// The zooms the sprite stage's frames are held to the processor's
/// composition at: the whole-number zooms, where the card's terrain under
/// the sprites equals the composition's nearest fill; at zoom 0.5 the card
/// draws the terrain from its level 1, which the Full tier's own cases
/// check, and the stage's picture is app-full-sprites' to hold.
constexpr std::array<float, 2> full_sprite_zooms{1.0F, 2.0F};
/// Pixels a sprite's rectangle is grown by each side in the mask of what
/// the card draws its own way, for the card's placing between pixels.
constexpr int sprite_mask_margin = 1;
/// Units each side of the fight the check draws, and the ticks it plays
/// before its first frame, so that lasers fire and units move.
constexpr std::size_t fight_units_per_side = 10;
constexpr int fight_ticks = 150;

/// Most a channel of a frame a graphics card scaled may differ from the
/// reference: a card weighs a texel's neighbours at a precision of its own,
/// as coarse as 64ths of a texel, which moves a level by up to 4 at full
/// contrast, and rounds its own way.
constexpr int most_card_difference = 4;
/// Most a channel of a frame SDL's software renderer scaled may differ from
/// that renderer's own LINEAR (software_linear_rgb24), which it equals on a
/// processor with SSE2 or NEON; a build without either truncates twice and
/// can give one level less.
constexpr int most_software_difference = 2;
/// The most the mean difference of a channel may be, on any renderer.
constexpr double most_mean_scaled_difference = 0.5;
/// Most a channel of the HUD may differ from the composition at a whole-number scale.
constexpr int most_hud_difference = 1;
/// Most a window point may lie from a layout pixel's place at native
/// density, in window points: the view maps one onto the other exactly but
/// for the rounding of floats.
constexpr float window_point_tolerance = 0.01F;
/// The most the mean of a channel of the Full tier's two-level blend of the
/// terrain, between zoom 0.5 and 1, may differ from the standard tier's
/// box filter of the same view: the blend is the card's own filter, not
/// today's bytes, so the bound is that of the terrain of the installed
/// game's maps on SDL's software renderer, with room; a picture drawn from
/// the wrong level, or a map pixel off, strays past it. The blend itself
/// is held to the renderer's own LINEAR of each tile's quad, pass over
/// pass, within the renderer's tolerance.
constexpr double most_blend_mean_difference = 24.0;
/// The levels SDL's software renderer's blend of a texture at an alpha
/// gives: dst = ((src - dst) * alpha >> 8) + dst, within a level of the
/// exact blend, so a blended pass of the reference computed exactly keeps
/// within most_software_difference of the read-back.
constexpr int blend_shift = 8;
/// The most the mean difference of a channel may be against that model on
/// SDL's software renderer: two passes, each rounded by the renderer, and
/// the blend's own rounding, where one LINEAR draw is held to
/// most_mean_scaled_difference; a card is held to that mean.
constexpr double most_blended_mean_difference = 1.0;
/// The clear and the resolve batches a frame drawn through the zoom-in
/// target adds to its passes' batches.
constexpr uint32_t target_frame_batches = 2;
/// Pixels left out at each edge of a HUD strip when it is held to its
/// reference: a card's LINEAR read may take the HUD layer beyond the
/// strip's edge from the prescale target, where the reference clamps.
constexpr int strip_edge_inset = 1;
/// The fewest terrain pixels a zoomed-out Full frame must show under a
/// transparent overlay for its comparison to count: the fog covers most
/// of a zoomed-out view, and a unit the rest where one stands.
constexpr std::size_t least_terrain_pixels = 4096;

/// A rectangle of a frame.
struct Area {
    int x{};
    int y{};
    int w{};
    int h{};
};

/// How far two frames differ over an area.
struct Difference {
    int most{};
    double mean{};
    std::size_t pixels{}; ///< pixels compared
};

/// Compares two frames over an area, leaving out the pixels in a second area.
///
/// @param first a frame
/// @param second another of the same size
/// @param area the area compared, clipped to the frames
/// @param left_out an area not compared
/// @return the largest and the mean difference of a channel
Difference compare(
    const renderer::Surface& first,
    const renderer::Surface& second,
    const Area& area,
    const Area& left_out
) {
    Difference difference;
    if (first.width != second.width || first.height != second.height) {
        difference.most = 255;
        return difference;
    }
    double sum = 0.0;
    std::size_t channels = 0;
    const int right = std::min(area.x + area.w, static_cast<int>(first.width));
    const int bottom = std::min(area.y + area.h, static_cast<int>(first.height));
    for (int y = std::max(area.y, 0); y < bottom; ++y)
        for (int x = std::max(area.x, 0); x < right; ++x) {
            if (x >= left_out.x && x < left_out.x + left_out.w && y >= left_out.y &&
                y < left_out.y + left_out.h)
                continue;
            ++difference.pixels;
            const auto at = (static_cast<std::size_t>(y) * first.width + x) * 3U;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const int delta =
                    std::abs(int{first.rgb[at + channel]} - int{second.rgb[at + channel]});
                difference.most = std::max(difference.most, delta);
                sum += delta;
                ++channels;
            }
        }
    difference.mean = channels != 0 ? sum / static_cast<double>(channels) : 0.0;
    return difference;
}

/// How far two frames differ over an area, apart from and under a mask.
struct MaskedDifference {
    Difference beside{}; ///< over the pixels the mask leaves
    Difference under{};  ///< over the pixels the mask marks
};

/// Compares two frames over an area, leaving out the pixels in a second
/// area, the pixels a mask marks counted apart from the rest.
///
/// @param first a frame
/// @param second another of the same size
/// @param area the area compared, clipped to the frames
/// @param left_out an area not compared
/// @param mask one byte a pixel of the frames; a set byte marks the pixel
/// @return the differences beside and under the mask
MaskedDifference compare_masked(
    const renderer::Surface& first,
    const renderer::Surface& second,
    const Area& area,
    const Area& left_out,
    const std::vector<uint8_t>& mask
) {
    MaskedDifference difference;
    if (first.width != second.width || first.height != second.height ||
        mask.size() != std::size_t{first.width} * first.height) {
        difference.beside.most = 255;
        return difference;
    }
    double beside_sum = 0.0;
    double under_sum = 0.0;
    const int right = std::min(area.x + area.w, static_cast<int>(first.width));
    const int bottom = std::min(area.y + area.h, static_cast<int>(first.height));
    for (int y = std::max(area.y, 0); y < bottom; ++y)
        for (int x = std::max(area.x, 0); x < right; ++x) {
            if (x >= left_out.x && x < left_out.x + left_out.w && y >= left_out.y &&
                y < left_out.y + left_out.h)
                continue;
            const auto pixel = static_cast<std::size_t>(y) * first.width + x;
            Difference& counted = mask[pixel] != 0 ? difference.under : difference.beside;
            double& sum = mask[pixel] != 0 ? under_sum : beside_sum;
            ++counted.pixels;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const int delta = std::abs(
                    int{first.rgb[pixel * 3U + channel]} - int{second.rgb[pixel * 3U + channel]}
                );
                counted.most = std::max(counted.most, delta);
                sum += delta;
            }
        }
    if (difference.beside.pixels != 0)
        difference.beside.mean = beside_sum / (3.0 * static_cast<double>(difference.beside.pixels));
    if (difference.under.pixels != 0)
        difference.under.mean = under_sum / (3.0 * static_cast<double>(difference.under.pixels));
    return difference;
}

/// Marks a rectangle of a mask, grown by a margin on each side.
///
/// @param[in,out] mask one byte a pixel of a frame
/// @param width the frame's columns
/// @param height the frame's rows
/// @param x the rectangle's left column
/// @param y its top row
/// @param w its columns
/// @param h its rows
/// @param margin pixels it is grown by on each side
void mark_rect(
    std::vector<uint8_t>& mask,
    uint32_t width,
    uint32_t height,
    int x,
    int y,
    int w,
    int h,
    int margin
) {
    const int left = std::max(x - margin, 0);
    const int top = std::max(y - margin, 0);
    const int right = std::min(x + w + margin, static_cast<int>(width));
    const int bottom = std::min(y + h + margin, static_cast<int>(height));
    for (int row = top; row < bottom; ++row)
        for (int column = left; column < right; ++column)
            mask[static_cast<std::size_t>(row) * width + static_cast<std::size_t>(column)] = 1;
}

/// Marks the pixels of a line, as the game steps it, and those within a
/// reach of them.
///
/// @param[in,out] mask one byte a pixel of a frame
/// @param width the frame's columns
/// @param height the frame's rows
/// @param x0 the first pixel's column
/// @param y0 its row
/// @param x1 the last pixel's column
/// @param y1 its row
/// @param reach pixels each side of the line marked with it
void mark_line(
    std::vector<uint8_t>& mask,
    uint32_t width,
    uint32_t height,
    int x0,
    int y0,
    int x1,
    int y1,
    int reach
) {
    // A line off the frame is marked only where it crosses it; one far off
    // it would step for long, so its pixels are bounded first.
    const int bound = static_cast<int>(std::max(width, height)) * 2;
    if (std::abs(x0) > bound || std::abs(y0) > bound || std::abs(x1) > bound ||
        std::abs(y1) > bound)
        return;
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        mark_rect(mask, width, height, x0, y0, 1, 1, reach);
        if (x0 == x1 && y0 == y1)
            break;
        const auto twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

/// Writes a frame as a PNG file.
///
/// @param path the file
/// @param frame the frame
void write_png(const fs::path& path, const renderer::Surface& frame) {
    std::vector<uint8_t> file;
    const oa::formats::png::Header header{
        frame.width, frame.height, 8, oa::formats::png::ColorType::rgb, {}
    };
    if (!oa::formats::png::write(oa::formats::png::Image{header, {}, frame.rgb}, &file))
        throw std::runtime_error("render tiers check: cannot encode " + path.string());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size())
    );
    if (!output)
        throw std::runtime_error("render tiers check: cannot write " + path.string());
}

/// Returns a frame enlarged by nearest replication: each pixel a square
/// block of a whole number of pixels on a side.
///
/// @param frame the frame
/// @param factor pixels on a side of each block, at least 1
/// @return the enlarged frame
renderer::Surface enlarged(const renderer::Surface& frame, uint32_t factor) {
    renderer::Surface result{
        frame.width * factor,
        frame.height * factor,
        std::vector<uint8_t>(std::size_t{frame.width} * factor * frame.height * factor * 3U)
    };
    for (uint32_t y = 0; y < result.height; ++y)
        for (uint32_t x = 0; x < result.width; ++x)
            std::copy_n(
                frame.rgb.begin() + static_cast<std::ptrdiff_t>(
                                        (std::size_t{y / factor} * frame.width + x / factor) * 3U
                                    ),
                3,
                result.rgb.begin() +
                    static_cast<std::ptrdiff_t>((std::size_t{y} * result.width + x) * 3U)
            );
    return result;
}

/// Presents a frame and returns the frame as the processor composed it.
///
/// @param presented presents a frame
/// @param composed composes the frame
/// @return the composition
renderer::Surface composed_after(
    const std::function<renderer::Surface()>& presented,
    const std::function<renderer::Surface()>& composed
) {
    std::ignore = presented();
    return composed();
}

/// Returns a name's text for a zoom: "0.5", "1", "2.5".
///
/// @param zoom the zoom
/// @return its text
std::string zoom_text(float zoom) {
    std::string text = std::to_string(zoom);
    text.erase(text.find_last_not_of('0') + 1);
    if (!text.empty() && text.back() == '.')
        text.pop_back();
    return text;
}

} // namespace

int Runtime::check_render_tiers() {
    const auto fail = [](const std::string& what) {
        throw std::runtime_error("render tiers check: " + what);
    };
    if (sdl_.renderer == nullptr || sdl_.window == nullptr || !render_run_ ||
        render_run_->host == nullptr)
        fail("needs the SDL renderer and the renderer host that made it");
    // Under the 2 GiB threshold the accelerated tier never runs, whatever
    // the flags; on a renderer the probe rejects it runs only when forced.
    const auto machine = oa::platform::read_machine_traits();
    auto& inputs = render_run_->host->tier_inputs();
    if (inputs.memory < policy::smallest_accelerated_memory) {
        std::cout << "render tiers check: skipped: the machine reports less than the 2 GiB "
                     "threshold of memory\n";
        return skipped_exit_code;
    }
    // What the renderer host found of the renderer when it made it, and the
    // capability the tier was decided from.
    const auto& facts = render_run_->host->facts();
    if (inputs.capability != policy::Capability::capable && !options_.force_capable) {
        std::cout << "render tiers check: skipped: the renderer " << facts.renderer
                  << " is not capable of the accelerated tier\n";
        return skipped_exit_code;
    }
    using oa::ui::engine_settings::HardwareAcceleration;
    if (!options_.hardware_acceleration ||
        *options_.hardware_acceleration == HardwareAcceleration::off)
        fail("needs --hardware-acceleration, which switches the accelerated tier on");
    // The tier as the flags decide it: the Basic cases switch it as
    // --hardware-acceleration=basic and --no-hardware-acceleration would,
    // and the Full cases, which need --hardware-acceleration=full, set the
    // level themselves.
    const HardwareAcceleration asked_level = *options_.hardware_acceleration;
    const auto set_level = [&](HardwareAcceleration level) {
        options_.hardware_acceleration = level;
        update_render_tier();
    };
    const auto switch_tier = [&](bool on) {
        set_level(on ? HardwareAcceleration::basic : HardwareAcceleration::off);
        if (accelerated_presentation() != on)
            fail(
                on ? "--hardware-acceleration=basic did not switch the accelerated tier on"
                   : "--no-hardware-acceleration did not switch the accelerated tier off"
            );
        if (on && full_presentation())
            fail("--hardware-acceleration=basic drew in the full tier");
    };
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    // SDL's software renderer is held to its own LINEAR exactly; a card to
    // the references within its precision.
    const bool software = facts.renderer == oa::platform::render_probe::software_renderer;
    const int most_scaled_difference = software ? most_software_difference : most_card_difference;
    const auto scaled_reference = [&](const wr::RgbSource& source,
                                      const CardScale& card,
                                      const SDL_Rect& rectangle,
                                      const wr::RgbTarget& target) {
        const wr::ScenePlacement placement{
            static_cast<double>(rectangle.w) / source.width,
            static_cast<double>(rectangle.h) / source.height,
            static_cast<double>(rectangle.x),
            static_cast<double>(rectangle.y)
        };
        switch (card.filter) {
        case policy::ScaleFilter::nearest:
            wr::nearest_rgb24(source, placement, target);
            break;
        case policy::ScaleFilter::pixelart:
            // SDL's software renderer draws its pixel-art mode NEAREST.
            if (software)
                wr::nearest_rgb24(source, placement, target);
            else
                wr::pixelart_rgb24(source, placement, target);
            break;
        case policy::ScaleFilter::sharp_bilinear:
            if (software)
                software_linear_rgb24(source, card.factor, rectangle, target);
            else
                wr::sharp_bilinear_rgb24(source, placement, card.factor, target);
            break;
        case policy::ScaleFilter::linear:
            if (software)
                software_linear_rgb24(source, 1, rectangle, target);
            else
                wr::bilinear_rgb24(source, placement, target);
            break;
        }
    };

    // The rung every case draws at: the full scene budget with the area
    // pass, magnify on, the chrome filtered, and the card's magnification
    // by PIXELART where the renderer has it, else by sharp-bilinear.
    policy::LadderState rung{};
    rung.method = policy::ZoomOutMethod::area;
    rung.budget = policy::SceneBudget::full;
    rung.magnify = true;
    rung.filtered_chrome = true;
    rung.card =
        oa::app::probe_pixelart(sdl_.renderer, nullptr)
            ? policy::CardFilter::pixelart
            : (oa::platform::light_machine(machine) || oa::platform::running_on_raspberry_pi()
                   ? policy::CardFilter::prescale_quarter
                   : policy::CardFilter::prescale_full);
    rung.standard = false;
    const auto& counts = accelerated_.counts;
    const auto resize = [&](int width, int height) {
        if (!SDL_SetWindowSize(sdl_.window, width, height) || !SDL_SyncWindow(sdl_.window))
            fail(std::string("SDL_SetWindowSize: ") + SDL_GetError());
        apply_output_mode();
    };
    const auto presented = [&]() {
        renderer::Surface frame;
        capture_frame_ = &frame;
        render();
        capture_frame_ = nullptr;
        return frame;
    };
    const auto gamma_of = [&](std::vector<uint8_t> bytes) {
        if (!gamma_identity_)
            for (auto& byte : bytes)
                byte = gamma_table_[byte];
        return bytes;
    };

    // The start-up function test passed, so the tier the flag asks for is
    // accelerated; from here it draws at the check's rung.
    if (inputs.function_test != policy::FunctionTest::passed)
        fail("the start-up function test did not pass");
    render_run_->rung = rung;
    switch_tier(false);
    switch_tier(true);
    if (render_run_->tier.tier != policy::RenderTier::accelerated)
        fail("the tier decided for --hardware-acceleration=basic is not the accelerated tier");

    // The main menu, letterboxed at 2.25 by the card's filter, and at 2 by
    // NEAREST with no prescale target.
    for (const auto& [width, height] :
         {std::pair{kDefaultWindowWidth, kDefaultWindowHeight},
          std::pair{whole_scale_width, whole_scale_height}}) {
        resize(width, height);
        const uint64_t draws_before = counts.prescale_draws;
        const auto read = presented();
        if (!accelerated_presentation())
            fail("the accelerated presentation stopped over the main menu");
        SDL_FRect letterbox{};
        if (!SDL_GetRenderLogicalPresentationRect(sdl_.renderer, &letterbox))
            fail(std::string("SDL_GetRenderLogicalPresentationRect: ") + SDL_GetError());
        const double scale = static_cast<double>(letterbox.w) / surface_.width;
        const auto card = accelerated_card_scale(
            policy::chrome_filter(accelerated_.rung, scale),
            scale,
            surface_.width,
            surface_.height,
            accelerated_.screen_prescale
        );
        const bool whole = std::floor(scale) == scale;
        if (whole &&
            (card.filter != policy::ScaleFilter::nearest || counts.prescale_draws != draws_before))
            fail("the main menu at a whole-number scale was drawn through a prescale target");
        const auto source_rgb = gamma_of(surface_.rgb);
        const wr::RgbSource source{
            source_rgb.data(), surface_.width, surface_.height, surface_.width
        };
        renderer::Surface reference{
            read.width, read.height, std::vector<uint8_t>(read.rgb.size(), 0)
        };
        const wr::RgbTarget target{reference.rgb.data(), read.width, read.height, read.width};
        // The read-back covers the letterbox alone.
        scaled_reference(
            source,
            card,
            {0, 0, static_cast<int>(letterbox.w), static_cast<int>(letterbox.h)},
            target
        );
        if (read.width != static_cast<uint32_t>(letterbox.w) ||
            read.height != static_cast<uint32_t>(letterbox.h))
            fail("the main menu's read-back is not its letterbox");
        const Area area{0, 0, static_cast<int>(read.width), static_cast<int>(read.height)};
        const auto difference = compare(read, reference, area, {});
        std::cout << "render tiers check: main menu at " << width << 'x' << height << ", scale "
                  << scale << ", filter " << static_cast<int>(card.filter) << " x" << card.factor
                  << ": most " << difference.most << ", mean " << difference.mean << '\n';
        if (difference.pixels == 0 || difference.most > (whole ? 0 : most_scaled_difference) ||
            difference.mean > most_mean_scaled_difference) {
            write_png(report_directory / "native-render-tiers-menu-presented.png", read);
            write_png(report_directory / "native-render-tiers-menu-reference.png", reference);
            fail("the main menu's presented frame strays from the card's reference");
        }
    }

    // A skirmish, or the campaign mission --campaign and --mission name, its
    // loading screen letterboxed by the card's filter, each of its frames
    // drawn into the prescale target, then drawn at a window whose chrome
    // scales by 2. With Full asked, the match loads in Full, so that its
    // loading screen makes the Full tier's pages.
    resize(kDefaultWindowWidth, kDefaultWindowHeight);
    if (asked_level == HardwareAcceleration::full)
        set_level(HardwareAcceleration::full);
    const uint64_t draws_before_loading = counts.prescale_draws;
    if (options_.campaign_mission)
        std::ignore = start_headless_campaign_mission();
    else
        start_benchmark_skirmish();
    if (!accelerated_presentation())
        fail("the accelerated presentation stopped over the loading screen");
    if (rung.card != policy::CardFilter::pixelart && counts.prescale_draws == draws_before_loading)
        fail("the loading screen was not drawn by the card's filter");
    std::cout << "render tiers check: the loading screen drew "
              << counts.prescale_draws - draws_before_loading << " frames by the card's filter\n";
    // The loading screen opened the executor, ran the Full function test
    // and built and uploaded the terrain pages with the terrain, before the
    // world was built, letting the atlas's texels go once uploaded; the
    // first match frame draws from those pages and makes none.
    if (asked_level == HardwareAcceleration::full) {
        if (!full_presentation() || !full_ || render_run_->tier.tier != policy::RenderTier::full)
            fail("the full tier was not on as the match loaded");
        const auto& full = *full_;
        if (!full.pages_from_load || full.pages.empty() ||
            full.pages.size() != full.atlas.pages.size())
            fail("the loading screen did not make the terrain pages");
        const uint64_t pages_alive = full.executor.counts().pages_alive;
        if (pages_alive != full.pages.size())
            fail("the terrain pages alive are not the loading screen's");
        for (const auto& page : full.atlas.pages)
            if (!page.texels.empty())
                fail("the atlas kept a page's texels after the page was filled");
        std::cout << "render tiers check: the loading screen made the full tier's terrain pages: "
                  << full.pages.size() << " pages of " << full.atlas.slot_tiles.size()
                  << " slots within " << full.atlas_page_edge << ", "
                  << full.page_bytes / 1024 / 1024 << " MiB, built in "
                  << full.atlas_build_ns / 1000 << " us and filled in "
                  << full.page_upload_ns / 1000 << " us\n";
        const uint64_t frames_before = full.frames;
        std::ignore = presented();
        if (!full_->drawn || full_->frames != frames_before + 1)
            fail("the first match frame was not drawn by the card");
        if (!full_->pages_from_load || full_->executor.counts().pages_alive != pages_alive)
            fail("the first match frame made a terrain page");
        std::cout << "render tiers check: the first match frame drew from the loading screen's "
                     "pages and made none\n";
        // The Basic cases run at basic.
        switch_tier(true);
    }
    uint16_t anchor = 0;
    for (const auto& slot : match_->world().slots)
        if (slot.unit_index != 0 && slot.unit != nullptr && slot.record.type_index != 0 &&
            oa::world_unit_at(&match_->state(), slot.unit_index)->owner_index ==
                match_local_player_) {
            anchor = slot.unit_index;
            break;
        }
    if (anchor == 0)
        fail("found no local unit");
    const auto at_zoom = [&](float zoom) {
        match_zoom_ = match_zoom_target_ = zoom;
        center_camera_on_unit(anchor);
    };
    const auto cursor = [&]() {
        return Area{
            static_cast<int>(match_pointer_x_) - cursor_reach,
            static_cast<int>(match_pointer_y_) - cursor_reach,
            2 * cursor_reach,
            2 * cursor_reach
        };
    };
    const auto battlefield = [&]() {
        return Area{
            match_layout_.left,
            match_layout_.top,
            match_layout_.battlefield_width(),
            match_layout_.battlefield_height()
        };
    };
    const auto composed = [&]() {
        renderer::Surface frame;
        compose_match_frame(frame);
        return frame;
    };
    // The HUD strips of a presented frame, held to the chrome's filter at
    // the display's scale as sharp_draw draws them from the HUD layer:
    // each strip against its own source scaled by the card's filter, a
    // pixel in from the strip's edges and the pointer's reach left out. At
    // a whole-number scale the strips are NEAREST and equal; at another,
    // where the rung filters the chrome through a prescale target, they
    // are sharp-bilinear from it, within the renderer's tolerance.
    const auto check_hud_strips = [&](const renderer::Surface& read, const std::string& which) {
        const double chrome = match_layout_.scale * match_display_density();
        const auto card = accelerated_card_scale(
            policy::chrome_filter(accelerated_.rung, chrome),
            chrome,
            match_hud_cpu_.width,
            match_hud_cpu_.height,
            accelerated_.hud_prescale
        );
        const bool whole = std::floor(chrome) == chrome;
        if (!whole && rung.filtered_chrome && rung.card != policy::CardFilter::pixelart &&
            card.filter != policy::ScaleFilter::sharp_bilinear)
            fail(which + ": the HUD strips are not drawn sharp-bilinear from the prescale target");
        const auto hud_rgb = gamma_of(match_hud_cpu_.rgb);
        const Area pointer = cursor();
        Difference difference;
        double sum = 0.0;
        std::size_t channels = 0;
        renderer::Surface reference = read;
        for (const auto& strip : match_hud_strips()) {
            if (strip.source_w <= 0 || strip.source_h <= 0 || strip.w <= 0 || strip.h <= 0)
                continue;
            const wr::RgbSource source{
                hud_rgb.data() + (static_cast<std::size_t>(strip.source_y) * match_hud_cpu_.width +
                                  static_cast<std::size_t>(strip.source_x)) *
                                     3U,
                static_cast<uint32_t>(strip.source_w),
                static_cast<uint32_t>(strip.source_h),
                match_hud_cpu_.width
            };
            // The card's references write every pixel of their target, so
            // each strip is drawn and compared before the next.
            scaled_reference(
                source,
                card,
                {strip.x, strip.y, strip.w, strip.h},
                {reference.rgb.data(), reference.width, reference.height, reference.width}
            );
            const int right =
                std::min(strip.x + strip.w - strip_edge_inset, static_cast<int>(read.width));
            const int bottom =
                std::min(strip.y + strip.h - strip_edge_inset, static_cast<int>(read.height));
            for (int y = strip.y + strip_edge_inset; y < bottom; ++y)
                for (int x = strip.x + strip_edge_inset; x < right; ++x) {
                    if (x >= pointer.x && x < pointer.x + pointer.w && y >= pointer.y &&
                        y < pointer.y + pointer.h)
                        continue;
                    ++difference.pixels;
                    const auto at = (static_cast<std::size_t>(y) * read.width + x) * 3U;
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        const int delta = std::abs(
                            int{read.rgb[at + channel]} - int{reference.rgb[at + channel]}
                        );
                        difference.most = std::max(difference.most, delta);
                        sum += delta;
                        ++channels;
                    }
                }
        }
        difference.mean = channels != 0 ? sum / static_cast<double>(channels) : 0.0;
        std::cout << "render tiers check: " << which << ": the HUD strips at chrome scale "
                  << chrome << " by filter " << static_cast<int>(card.filter) << " x" << card.factor
                  << " over " << difference.pixels << " pixels: most " << difference.most
                  << ", mean " << difference.mean << '\n';
        if (difference.pixels == 0 || difference.most > (whole ? 0 : most_scaled_difference) ||
            difference.mean > most_mean_scaled_difference) {
            write_png(report_directory / "native-render-tiers-hud-presented.png", read);
            write_png(report_directory / "native-render-tiers-hud-reference.png", reference);
            fail(which + ": the HUD strips stray from the chrome's filter");
        }
    };

    // --native-density: the window opened at the display's own density, and
    // this case alone runs, since every other compares the read-back with
    // the processor's composition pixel for pixel.
    if (options_.native_density) {
        if (!native_density_window())
            fail("--native-density did not open the window at native density");
        // The match is laid out in window points.
        int points_w = 0;
        int points_h = 0;
        int pixels_w = 0;
        int pixels_h = 0;
        if (!SDL_GetWindowSize(sdl_.window, &points_w, &points_h) ||
            !SDL_GetWindowSizeInPixels(sdl_.window, &pixels_w, &pixels_h))
            fail(std::string("SDL window size: ") + SDL_GetError());
        const auto in_points = oa::ui::display_layout::make_match_layout(points_w, points_h);
        if (match_layout_.width != in_points.width || match_layout_.height != in_points.height ||
            match_layout_.left != in_points.left || match_layout_.top != in_points.top ||
            match_layout_.bottom != in_points.bottom || match_layout_.scale != in_points.scale)
            fail("the match is not laid out in window points");
        const double density = match_display_density();
        // The read-back has the display's size.
        update_pointer(
            static_cast<float>(match_layout_.width - 1),
            static_cast<float>(match_layout_.height - 1)
        );
        at_zoom(1.0F);
        const auto read = presented();
        if (!accelerated_presentation())
            fail("the accelerated presentation stopped at native density");
        if (accelerated_.frame.method != SceneMethod::none)
            fail("zoom 1 was split from the world layer at native density");
        if (read.width != static_cast<uint32_t>(pixels_w) ||
            read.height != static_cast<uint32_t>(pixels_h))
            fail("the read-back at native density is not the display's size");
        std::cout << "render tiers check: native density " << density << ", " << points_w << 'x'
                  << points_h << " points on " << pixels_w << 'x' << pixels_h << " pixels\n";
        // At zoom 1 and a whole-number density the battlefield is the
        // processor's enlarged by nearest replication, and the chrome too
        // where its own scale is a whole number.
        if (std::floor(density) == density && density >= 1.0) {
            const auto factor = static_cast<int>(density);
            const auto on_display = [&](const Area& area) {
                return Area{area.x * factor, area.y * factor, area.w * factor, area.h * factor};
            };
            const auto expected = enlarged(composed(), static_cast<uint32_t>(factor));
            const auto world =
                compare(read, expected, on_display(battlefield()), on_display(cursor()));
            const bool chrome_whole = std::floor(match_layout_.scale) == match_layout_.scale;
            const auto whole = compare(
                read,
                expected,
                on_display({0, 0, match_layout_.width, match_layout_.height}),
                on_display(cursor())
            );
            std::cout << "render tiers check: zoom 1 at native density: battlefield most "
                      << world.most << ", whole frame most " << whole.most << '\n';
            if (world.pixels == 0 || world.most != 0 ||
                (chrome_whole && whole.most > most_hud_difference)) {
                write_png(report_directory / "native-render-tiers-density-presented.png", read);
                write_png(report_directory / "native-render-tiers-density-composed.png", expected);
                fail(
                    "zoom 1 at native density is not the composition enlarged by nearest "
                    "replication"
                );
            }
        } else {
            std::cout << "render tiers check: the density is not a whole number; the frame is "
                         "drawn by the chrome's filter\n";
        }
        // The unit under the pointer is the one drawn there: its place in
        // layout pixels is its place in window points, and SDL maps the
        // pointer back through the view.
        const auto& slots = match_->world().slots;
        const auto point = project_match_point(
            live_viewport(
                static_cast<uint32_t>(std::max(0, match_camera_x_)),
                static_cast<uint32_t>(std::max(0, match_camera_z_))
            ),
            slots[anchor].unit->position
        );
        float window_x = 0.0F;
        float window_y = 0.0F;
        if (!SDL_RenderCoordinatesToWindow(
                sdl_.renderer,
                static_cast<float>(point.x),
                static_cast<float>(point.y),
                &window_x,
                &window_y
            ))
            fail(std::string("SDL_RenderCoordinatesToWindow: ") + SDL_GetError());
        if (std::abs(window_x - static_cast<float>(point.x)) > window_point_tolerance ||
            std::abs(window_y - static_cast<float>(point.y)) > window_point_tolerance)
            fail("a layout pixel is not a window point at native density");
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.windowID = SDL_GetWindowID(sdl_.window);
        motion.motion.x = window_x;
        motion.motion.y = window_y;
        bool running = true;
        dispatch_event(motion, running);
        if (hovered_match_unit_ != anchor)
            fail("the unit under the pointer at native density is not the one drawn there");
        std::cout << "render tiers check: at native density the match is laid out in window "
                     "points, read back at the display's size, and picks the unit drawn under "
                     "the pointer\n";
        // The Full tier at zoom 1 and a whole-number density: the card's
        // terrain and the overlay, stretched by logical presentation, equal
        // the composition enlarged by nearest replication too.
        if (asked_level == HardwareAcceleration::full && std::floor(density) == density &&
            density >= 1.0) {
            update_pointer(
                static_cast<float>(match_layout_.width - 1),
                static_cast<float>(match_layout_.height - 1)
            );
            set_level(HardwareAcceleration::full);
            at_zoom(1.0F);
            const auto full_read = presented();
            if (!full_presentation() || !full_ || !full_->drawn)
                fail("the full tier did not draw at native density");
            const auto factor = static_cast<int>(density);
            const auto expected = enlarged(composed(), static_cast<uint32_t>(factor));
            const Area field{
                battlefield().x * factor,
                battlefield().y * factor,
                battlefield().w * factor,
                battlefield().h * factor
            };
            const Area pointer{
                cursor().x * factor, cursor().y * factor, cursor().w * factor, cursor().h * factor
            };
            const auto world = compare(full_read, expected, field, pointer);
            std::cout << "render tiers check: full tier at zoom 1 and native density: "
                         "battlefield most "
                      << world.most << '\n';
            if (world.pixels == 0 || world.most != 0) {
                write_png(report_directory / "native-render-tiers-density-full.png", full_read);
                fail(
                    "the full tier at zoom 1 and native density is not the composition "
                    "enlarged by nearest replication"
                );
            }
            set_level(HardwareAcceleration::off);
        }
        return 0;
    }

    switch_tier(false);
    resize(whole_scale_width, whole_scale_height);
    // The cursor waits in the blank corner right of the bottom bar.
    update_pointer(
        static_cast<float>(match_layout_.width - 1), static_cast<float>(match_layout_.height - 1)
    );
    // A fight beside the commander, played until lasers fire, where the
    // game's data has its units; otherwise the match's own units, played as
    // long.
    const bool fight = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "ARMPW") != 0 &&
                       oa::sim::unit_spawn::find_type_index(spawn_type_names_, "CORAK") != 0;
    if (fight)
        spawn_combat_armies(fight_units_per_side);
    else
        std::cout
            << "render tiers check: the data has no fight's units; the match's own are drawn\n";
    for (int tick = 0; tick < fight_ticks; ++tick)
        step_match_simulation();

    // The pictures for the maintainer: one moment at each zoom in both tiers.
    for (const float zoom : picture_zooms)
        for (const bool on : {false, true}) {
            switch_tier(on);
            at_zoom(zoom);
            const auto read = presented();
            write_png(
                report_directory /
                    ("native-render-tiers-" + std::string(on ? "accelerated" : "standard") +
                     "-zoom-" + zoom_text(zoom) + ".png"),
                read
            );
        }

    // At zoom 0.5 the area pass of the terrain drawn at one pixel per map
    // pixel is, byte for byte, the terrain the standard tier's box filter
    // averages.
    {
        switch_tier(false);
        at_zoom(kMinBattlefieldZoom);
        std::ignore = presented();
        const auto boxed = match_terrain_cache_;
        switch_tier(true);
        std::ignore = presented();
        const auto& scene = match_terrain_cache_;
        if (accelerated_.frame.method != SceneMethod::area || accelerated_.frame.draw_scale != 1.0F)
            fail("zoom 0.5 was not drawn at one pixel per map pixel");
        wr::AreaPlan plan;
        std::vector<uint8_t> averaged(boxed.rgb.size());
        if (wr::plan_area_filter(plan, wr::area_scale_min, boxed.width, boxed.height) !=
                wr::AreaError::none ||
            wr::area_filter_rgb24(
                plan,
                {scene.rgb.data(), scene.width, scene.height, scene.width},
                {averaged.data(), boxed.width, boxed.height, boxed.width}
            ) != wr::AreaError::none)
            fail("the area pass refused the terrain at zoom 0.5");
        if (averaged != boxed.rgb)
            fail("the area pass of the terrain at zoom 0.5 differs from the box filter's");
        std::cout << "render tiers check: at zoom 0.5 the area pass of the terrain equals the "
                     "box filter's, "
                  << boxed.width << 'x' << boxed.height << '\n';
    }
    switch_tier(true);
    // Zoom 1 at a whole-number chrome scale: the frame the processor composes.
    at_zoom(1.0F);
    {
        const auto read = presented();
        const auto expected = composed();
        if (accelerated_.frame.method != SceneMethod::none)
            fail("zoom 1 was split from the world layer");
        const auto world = compare(read, expected, battlefield(), cursor());
        const auto whole =
            compare(read, expected, {0, 0, match_layout_.width, match_layout_.height}, cursor());
        if (world.most != 0 || whole.most > most_hud_difference) {
            write_png(report_directory / "native-render-tiers-zoom-1-presented.png", read);
            write_png(report_directory / "native-render-tiers-zoom-1-composed.png", expected);
            fail("zoom 1 differs from compose_match_frame");
        }
    }
    // Zoomed out: the area pass's picture, uploaded as the processor composed it.
    for (const float zoom : area_zooms) {
        at_zoom(zoom);
        const auto read = presented();
        const auto expected = composed();
        if (accelerated_.frame.method != SceneMethod::area)
            fail("zoom " + zoom_text(zoom) + " was not reduced by the area pass");
        const auto difference = compare(read, expected, battlefield(), cursor());
        std::cout << "render tiers check: zoom " << zoom_text(zoom)
                  << " by the area pass at draw scale " << accelerated_.frame.draw_scale
                  << ": most " << difference.most << '\n';
        if (difference.most != 0) {
            write_png(
                report_directory /
                    ("native-render-tiers-zoom-" + zoom_text(zoom) + "-presented.png"),
                read
            );
            write_png(
                report_directory /
                    ("native-render-tiers-zoom-" + zoom_text(zoom) + "-composed.png"),
                expected
            );
            fail("zoom " + zoom_text(zoom) + " differs from the area pass's composition");
        }
    }
    // Zoomed in: the scene by the card's filter, then the overlay, against
    // the references applied to the same scene. A frame read back is held
    // to them as the frame the card magnified.
    const auto check_magnified = [&](float zoom,
                                     const renderer::Surface& read,
                                     const std::string& which) {
        if (accelerated_.frame.method != SceneMethod::magnify || !accelerated_.magnified)
            fail(which + " at zoom " + zoom_text(zoom) + " was not magnified");
        const uint32_t bf_w = match_world_cpu_.width;
        const uint32_t bf_h = match_world_cpu_.height;
        const uint32_t scene_w = match_scene_cpu_.width;
        const uint32_t scene_h = match_scene_cpu_.height;
        const auto corner = [&](uint32_t battlefield_extent, uint32_t scene_extent) {
            return std::min(
                static_cast<uint32_t>(
                    std::ceil(static_cast<double>(battlefield_extent) / static_cast<double>(zoom))
                ),
                scene_extent
            );
        };
        const uint32_t width = corner(bf_w, scene_w);
        const uint32_t height = corner(bf_h, scene_h);
        const auto card = accelerated_card_scale(
            policy::world_filter(accelerated_.rung, zoom),
            zoom,
            std::min(width + 1, scene_w),
            std::min(height + 1, scene_h),
            accelerated_.world_prescale
        );
        const auto scene_rgb = gamma_of(match_scene_cpu_.rgb);
        const wr::RgbSource source{scene_rgb.data(), width, height, scene_w};
        renderer::Surface reference = read;
        renderer::Surface scaled = reference;
        scaled_reference(
            source,
            card,
            {match_layout_.left,
             match_layout_.top,
             static_cast<int>(std::lround(static_cast<double>(width) * zoom)),
             static_cast<int>(std::lround(static_cast<double>(height) * zoom))},
            {scaled.rgb.data(), scaled.width, scaled.height, scaled.width}
        );
        // The battlefield of the reference is the scaled scene, then the
        // overlay of what the painters changed.
        std::vector<uint8_t> overlay(std::size_t{bf_w} * bf_h * 4U);
        std::vector<uint8_t> bands(platform::job_pool::bands_of_rows(bf_h, xrgb_band_rows));
        convert_rgb24_overlay_argb(
            match_world_cpu_.rgb.data(),
            accelerated_.base.data(),
            bf_w,
            bf_h,
            overlay.data(),
            std::size_t{bf_w} * 4U,
            gamma_identity_ ? nullptr : &gamma_table_,
            bands,
            nullptr
        );
        renderer::Surface field{bf_w, bf_h, std::vector<uint8_t>(std::size_t{bf_w} * bf_h * 3U)};
        for (uint32_t y = 0; y < bf_h; ++y)
            std::copy_n(
                scaled.rgb.begin() +
                    static_cast<std::ptrdiff_t>(
                        ((static_cast<std::size_t>(y) + match_layout_.top) * scaled.width +
                         match_layout_.left) *
                        3U
                    ),
                bf_w * 3U,
                field.rgb.begin() + static_cast<std::ptrdiff_t>(std::size_t{y} * bf_w * 3U)
            );
        wr::overlay_rgb24(
            {field.rgb.data(), bf_w, bf_h, bf_w},
            reinterpret_cast<const uint32_t*>(overlay.data()),
            bf_w
        );
        for (uint32_t y = 0; y < bf_h; ++y)
            std::copy_n(
                field.rgb.begin() + static_cast<std::ptrdiff_t>(std::size_t{y} * bf_w * 3U),
                bf_w * 3U,
                reference.rgb.begin() +
                    static_cast<std::ptrdiff_t>(
                        ((static_cast<std::size_t>(y) + match_layout_.top) * reference.width +
                         match_layout_.left) *
                        3U
                    )
            );
        // The last column and row may read past the corner, which renderers
        // clamp differently.
        auto inner = battlefield();
        inner.w -= 1;
        inner.h -= 1;
        const auto difference = compare(read, reference, inner, cursor());
        std::cout << "render tiers check: " << which << " at zoom " << zoom_text(zoom)
                  << " by filter " << static_cast<int>(card.filter) << " x" << card.factor
                  << ": most " << difference.most << ", mean " << difference.mean << '\n';
        const bool whole = std::floor(zoom) == zoom;
        if (difference.most > (whole ? 0 : most_scaled_difference) ||
            difference.mean > most_mean_scaled_difference) {
            write_png(
                report_directory /
                    ("native-render-tiers-zoom-" + zoom_text(zoom) + "-presented.png"),
                read
            );
            write_png(
                report_directory /
                    ("native-render-tiers-zoom-" + zoom_text(zoom) + "-reference.png"),
                reference
            );
            fail(which + " at zoom " + zoom_text(zoom) + " strays from the card's reference");
        }
    };
    for (const float zoom : magnified_zooms) {
        at_zoom(zoom);
        const auto read = presented();
        check_magnified(zoom, read, "a magnified frame");
        // The capture has the window's size and is the presented picture.
        if (zoom == capture_zoom) {
            int window_w = 0;
            int window_h = 0;
            SDL_GetWindowSizeInPixels(sdl_.window, &window_w, &window_h);
            if (read.width != static_cast<uint32_t>(window_w) ||
                read.height != static_cast<uint32_t>(window_h))
                fail("the capture at zoom 2 is not the window's size");
        }
    }
    // The very first frame after the tier is switched on, and the first
    // after each change of the window's size, is magnified as every other.
    for (const float zoom : first_frame_zooms) {
        switch_tier(false);
        at_zoom(zoom);
        std::ignore = presented();
        switch_tier(true);
        check_magnified(zoom, presented(), "the first frame after the tier was switched on");
    }
    for (const float zoom : first_frame_zooms)
        for (const auto& [width, height] :
             {std::pair{part_scale_width, part_scale_height},
              std::pair{whole_scale_width, whole_scale_height}}) {
            resize(width, height);
            update_pointer(
                static_cast<float>(match_layout_.width - 1),
                static_cast<float>(match_layout_.height - 1)
            );
            at_zoom(zoom);
            check_magnified(
                zoom,
                presented(),
                "the first frame at " + std::to_string(width) + 'x' + std::to_string(height)
            );
        }
    // The standard tier's picture of the same moment, which screenshots,
    // film frames and the backdrops keep: drawn again with the tier on, it
    // is the standard tier's battlefield, and it changes neither the
    // frame's counts of units drawn nor the HUD's resource readout, which
    // saves keep.
    for (const float zoom : kept_picture_zooms) {
        switch_tier(false);
        at_zoom(zoom);
        std::ignore = presented();
        const auto standard = composed();
        switch_tier(true);
        std::ignore = presented();
        if (accelerated_.frame.method == SceneMethod::none)
            fail("zoom " + zoom_text(zoom) + " was drawn as the standard tier draws it");
        const auto draws = frame_draws_;
        const auto readout = match_->state().game.resource_readout;
        ensure_screen_world();
        const auto kept = composed();
        const auto difference = compare(kept, standard, battlefield(), cursor());
        if (difference.pixels == 0 || difference.most != 0) {
            write_png(
                report_directory / ("native-render-tiers-kept-zoom-" + zoom_text(zoom) + ".png"),
                kept
            );
            write_png(
                report_directory /
                    ("native-render-tiers-standard-kept-zoom-" + zoom_text(zoom) + ".png"),
                standard
            );
            fail(
                "the picture kept at zoom " + zoom_text(zoom) +
                " is not the standard tier's battlefield"
            );
        }
        if (frame_draws_.units_drawn != draws.units_drawn ||
            frame_draws_.units_between_ticks != draws.units_between_ticks ||
            frame_draws_.probe_unit != draws.probe_unit ||
            frame_draws_.probe_drawn != draws.probe_drawn ||
            frame_draws_.probe_x != draws.probe_x || frame_draws_.probe_z != draws.probe_z)
            fail("drawing the kept picture changed the frame's counts of units drawn");
        const auto& after = match_->state().game.resource_readout;
        if (std::memcmp(&after, &readout, sizeof readout) != 0)
            fail("drawing the kept picture eased the HUD's resource readout");
        std::cout << "render tiers check: the picture kept at zoom " << zoom_text(zoom)
                  << " is the standard tier's\n";
    }
    // History: a whole-tick frame drawn after frames between ticks shows the
    // battlefield the one drawn before them showed.
    {
        at_zoom(history_zoom);
        set_presentation_alpha(1.0F);
        const auto before = presented();
        for (const float fraction : between_ticks) {
            set_presentation_alpha(fraction);
            std::ignore = presented();
        }
        set_presentation_alpha(1.0F);
        const auto after = presented();
        // The battlefield: the HUD's resource readout eases toward the
        // stores at every draw, whatever the tier.
        if (compare(before, after, battlefield(), cursor()).most != 0) {
            write_png(report_directory / "native-render-tiers-history-before.png", before);
            write_png(report_directory / "native-render-tiers-history-after.png", after);
            fail("a whole-tick frame's battlefield differs after frames between ticks");
        }
    }
    // Needless work: at a whole-number chrome scale the HUD has no prescale
    // target, and a zoom ease makes and destroys nothing.
    {
        at_zoom(kMinBattlefieldZoom);
        std::ignore = presented();
        if (accelerated_.hud_prescale.made())
            fail("the HUD has a prescale target at a whole-number chrome scale");
        const uint64_t made = counts.textures_created;
        const uint64_t destroyed = counts.textures_destroyed;
        std::vector<float> ease;
        for (float zoom = kMinBattlefieldZoom; zoom < kMaxBattlefieldZoom; zoom *= ease_step)
            ease.push_back(zoom);
        ease.push_back(kMaxBattlefieldZoom);
        const std::vector<float> back(ease.rbegin(), ease.rend());
        ease.insert(ease.end(), back.begin(), back.end());
        for (const float zoom : ease) {
            match_zoom_ = match_zoom_target_ = zoom;
            std::ignore = presented();
        }
        if (counts.textures_created != made || counts.textures_destroyed != destroyed)
            fail(
                "a zoom ease made " + std::to_string(counts.textures_created - made) +
                " and destroyed " + std::to_string(counts.textures_destroyed - destroyed) +
                " textures"
            );
        at_zoom(capture_zoom);
        const uint64_t draws = counts.prescale_draws;
        std::ignore = presented();
        if (counts.prescale_draws != draws)
            fail("zoom 2 drew a prescale target");
        std::cout << "render tiers check: a zoom ease of " << ease.size()
                  << " frames made and destroyed no texture\n";
    }
    // Smooth panning: the view drawn between map pixels as a slow scroll
    // moves it, at zoom 2.5 and 0.5, and the pointer picking what is drawn.
    // It runs after the zoom ease: it switches the tier off and on, which
    // drops the prescale targets the frames before made, and the ease
    // counts on finding them made.
    check_smooth_panning(switch_tier, at_zoom, anchor);
    // At a chrome scale of 1.6 the HUD strips are the chrome's filter from
    // the HUD's prescale target, which is drawn into once on each frame
    // that paints the layer, which every frame the loop presents does, and
    // on none presented again without a paint.
    {
        resize(part_scale_width, part_scale_height);
        update_pointer(
            static_cast<float>(match_layout_.width - 1),
            static_cast<float>(match_layout_.height - 1)
        );
        at_zoom(1.0F);
        const auto hud_read = presented();
        if (!accelerated_.hud_prescale.made() && rung.card != policy::CardFilter::pixelart)
            fail("the HUD has no prescale target at a chrome scale of 1.6");
        check_hud_strips(hud_read, "the basic tier at a chrome scale of 1.6");
        const uint64_t draws = counts.prescale_draws;
        for (int frame = 0; frame < counted_hud_frames; ++frame)
            std::ignore = presented();
        const uint64_t expected =
            rung.card == policy::CardFilter::pixelart ? draws : draws + counted_hud_frames;
        if (counts.prescale_draws != expected)
            fail(
                "the HUD was drawn into its prescale target " +
                std::to_string(counts.prescale_draws - draws) + " times over " +
                std::to_string(counted_hud_frames) + " frames"
            );
        const uint64_t painted = counts.prescale_draws;
        present_match_layers();
        if (counts.prescale_draws != painted)
            fail("the HUD was drawn into its prescale target with its layer unchanged");
    }
    // The Full tier's sprite stage, where --hardware-acceleration asked for
    // Full: switched on, the graphics card draws the frame's sprites,
    // particle squares and lines over the world layer the bands left them
    // out of, under the overlay of what the painters changed. At a
    // whole-number zoom the frame equals the processor's composition
    // exactly, but under the sprites the alpha table blends, which the
    // card blends to the true mean, along the lines, which the card draws
    // as quads at least a pixel wide, and where a painter painted the
    // base's own colour over a sprite, which the overlay by difference
    // cannot carry. The world layer drawn whole for a reader differs from
    // the one the frame kept, since the bands left the card's kinds out.
    if (asked_level == HardwareAcceleration::full) {
        resize(whole_scale_width, whole_scale_height);
        update_pointer(
            static_cast<float>(match_layout_.width - 1),
            static_cast<float>(match_layout_.height - 1)
        );
        set_level(HardwareAcceleration::full);
        if (!full_presentation())
            fail("--hardware-acceleration=full did not switch the full tier on for the stages");
        set_full_stages(full::stage_sprites);
        for (const float zoom : full_sprite_zooms) {
            at_zoom(zoom);
            const auto read = presented();
            if (!full_presentation() || !full_frame_drawn())
                fail("the sprite stage did not draw the frame at zoom " + zoom_text(zoom));
            const auto stage = full_sprite_result();
            if (stage.pages_overflowed)
                fail("the sprite pages overflowed at zoom " + zoom_text(zoom));
            // What the card draws its own way, marked: the sprites the
            // alpha table blends, every sprite at a zoom that is not a
            // whole number, the lines, grown by their width, the units,
            // projectiles, debris and fragments the processor draws, which
            // the card's sprites lie over until the models stage draws
            // them in the list's order, and every sprite that reaches a
            // fog tile that is not wholly clear, which the fog's masks cut
            // where the card draws the sprite whole, greyed or not at all
            // by its cell.
            const auto& list = match_models().draws;
            std::vector<uint8_t> mask(std::size_t{read.width} * read.height, 0);
            const bool whole = std::floor(zoom) == zoom;
            const int reach = static_cast<int>(std::ceil(std::max(1.0F, zoom)));
            const Area field = battlefield();
            const auto scaled = [&](int32_t pixels) {
                return static_cast<int>(
                    std::lround(static_cast<double>(pixels) * static_cast<double>(zoom))
                );
            };
            // The fog tiles that are not wholly clear, as the frame's fog
            // laid them (apply_match_fog), each grown by a pixel for the
            // terrain's stepping of their edges.
            std::vector<uint8_t> fogged(mask.size(), 0);
            std::span<const uint8_t> coverage;
            try {
                coverage = match_->player_coverage(match_view_player());
            } catch (const std::exception&) {
                coverage = {};
            }
            const auto& sight = match_->sight();
            const bool los_on = match_line_of_sight_on();
            const bool mapping_on = match_mapping_on();
            // The viewer's sight as the sprite stage reads it.
            full::SightView viewer_sight;
            if (!coverage.empty() && sight.width > 0 && sight.height > 0) {
                viewer_sight.coverage = coverage;
                viewer_sight.player_bits = sight.player_bits;
                viewer_sight.width = sight.width;
                viewer_sight.height = sight.height;
                viewer_sight.viewer_bit =
                    static_cast<uint16_t>(1U << (sight.viewpoint_player & 0x1fU));
                viewer_sight.line_of_sight = los_on;
                viewer_sight.mapping = mapping_on;
            }
            if (!coverage.empty() && sight.width > 0 && sight.height > 0 &&
                (los_on || mapping_on)) {
                const auto zoom_fp =
                    static_cast<uint32_t>(std::lround(static_cast<double>(zoom) * 65536.0));
                const auto grid = wr::build_fog_grid(
                    sight,
                    coverage,
                    {los_on, mapping_on},
                    match_camera_x_,
                    match_camera_z_,
                    wr::fog_map_span(zoom_fp, field.w),
                    wr::fog_map_span(zoom_fp, field.h)
                );
                const int tile = static_cast<int>(std::ceil(wr::fog_cell_pixels * zoom)) + 2;
                for (int32_t row = 0; row < grid.height; ++row)
                    for (int32_t column = 0; column < grid.width; ++column) {
                        const auto& masks = grid.at(column, row);
                        // The black over never-mapped ground is the
                        // processor's own, over the card's sprites too; the
                        // gray is by cell on the card.
                        if (masks.unseen == 0)
                            continue;
                        mark_rect(
                            fogged,
                            read.width,
                            read.height,
                            field.x + scaled(grid.offset_x + column * wr::fog_cell_pixels) - 1,
                            field.y + scaled(grid.offset_z + row * wr::fog_cell_pixels) - 1,
                            tile,
                            tile,
                            0
                        );
                    }
            }
            const auto touches_fog = [&](int x, int y, int w, int h) {
                for (int row = std::max(y, 0); row < std::min(y + h, static_cast<int>(read.height));
                     ++row)
                    for (int column = std::max(x, 0);
                         column < std::min(x + w, static_cast<int>(read.width));
                         ++column)
                        if (fogged
                                [static_cast<std::size_t>(row) * read.width +
                                 static_cast<std::size_t>(column)] != 0)
                            return true;
                return false;
            };
            // A region the bridge captures, in map pixels about the scene's
            // corner, on the window at the zoom.
            const auto mark_region = [&](const oa::Rect32& region) {
                mark_rect(
                    mask,
                    read.width,
                    read.height,
                    field.x + scaled(region.x1),
                    field.y + scaled(region.y1),
                    scaled(region.x2 - region.x1 + 1),
                    scaled(region.y2 - region.y1 + 1),
                    reach
                );
            };
            uint32_t kinds_planned = 0;
            uint32_t sprites_fogged = 0;
            for (const auto& draw : list.draws) {
                switch (draw.kind) {
                case WorldDrawKind::sprite:
                case WorldDrawKind::blended_sprite: {
                    ++kinds_planned;
                    const auto& sprite = list.sprites[draw.index];
                    const int left = field.x + sprite.screen.x - scaled(sprite.frame->origin_x);
                    const int top = field.y + sprite.screen.y - scaled(sprite.frame->origin_y);
                    const int w = std::max(1, scaled(sprite.frame->width));
                    const int h = std::max(1, scaled(sprite.frame->height));
                    const bool fog_cut = touches_fog(
                        left - sprite_mask_margin,
                        top - sprite_mask_margin,
                        w + 2 * sprite_mask_margin,
                        h + 2 * sprite_mask_margin
                    );
                    // A sprite the card draws greyed, by the cell under its
                    // drawn point, differs wherever the gray's masks leave
                    // the processor's in colour.
                    const bool greyed_by_card =
                        full::cell_fog(
                            viewer_sight,
                            match_camera_x_ + static_cast<int32_t>(std::floor(
                                                  static_cast<float>(sprite.screen.x) / zoom
                                              )),
                            match_camera_z_ + static_cast<int32_t>(std::floor(
                                                  static_cast<float>(sprite.screen.y) / zoom
                                              ))
                        ) == full::CellFog::unseen;
                    if (fog_cut || greyed_by_card)
                        ++sprites_fogged;
                    if (whole && draw.kind == WorldDrawKind::sprite && !fog_cut && !greyed_by_card)
                        break;
                    mark_rect(mask, read.width, read.height, left, top, w, h, sprite_mask_margin);
                    break;
                }
                case WorldDrawKind::model:
                    mark_region(list.models[draw.index].plan.region);
                    break;
                case WorldDrawKind::projectile:
                    mark_region(list.projectiles[draw.index].region);
                    break;
                case WorldDrawKind::debris:
                    mark_region(list.debris[draw.index].region);
                    break;
                case WorldDrawKind::fragment:
                    mark_region(list.fragments[draw.index].region);
                    break;
                case WorldDrawKind::line: {
                    ++kinds_planned;
                    const auto& line = list.lines[draw.index];
                    mark_line(
                        mask,
                        read.width,
                        read.height,
                        field.x + line.x0,
                        field.y + line.y0,
                        field.x + line.x1,
                        field.y + line.y1,
                        reach
                    );
                    break;
                }
                case WorldDrawKind::selection_line: {
                    ++kinds_planned;
                    // Map pixels about the camera, at the zoom.
                    const auto& line = list.lines[draw.index];
                    const auto at = [&](int32_t pixel) {
                        return static_cast<int>(
                            std::lround((static_cast<double>(pixel) + 0.5) * zoom)
                        );
                    };
                    mark_line(
                        mask,
                        read.width,
                        read.height,
                        field.x + at(line.x0),
                        field.y + at(line.y0),
                        field.x + at(line.x1),
                        field.y + at(line.y1),
                        reach + 1
                    );
                    break;
                }
                case WorldDrawKind::pixel_square:
                    ++kinds_planned;
                    break;
                default:
                    break;
                }
            }
            // The world layer the frame kept, without the card's kinds, and
            // the base it was painted over.
            const auto kept = match_world_cpu_;
            const auto base = full_base();
            ensure_screen_world();
            if (full_frame_drawn())
                fail("the standard tier's draw for a reader left the card's kinds out");
            const auto& whole_world = match_world_cpu_;
            // A pixel a painter painted in the base's own colour over a
            // sprite shows the card's sprite, since the overlay by
            // difference cannot tell the paint from the base.
            uint32_t painted_over = 0;
            if (base.width == kept.width && base.height == kept.height &&
                base.rgb.size() == kept.rgb.size() && base.rgb.size() == whole_world.rgb.size()) {
                const auto same = [](const std::vector<uint8_t>& one,
                                     const std::vector<uint8_t>& other,
                                     std::size_t at) {
                    return one[at] == other[at] && one[at + 1] == other[at + 1] &&
                           one[at + 2] == other[at + 2];
                };
                const auto shown = [&](uint8_t channel) {
                    return gamma_identity_ ? channel : gamma_table_[channel];
                };
                for (uint32_t row = 0; row < base.height; ++row)
                    for (uint32_t column = 0; column < base.width; ++column) {
                        const auto at = (std::size_t{row} * base.width + column) * 3U;
                        if (!same(kept.rgb, base.rgb, at) || !same(whole_world.rgb, base.rgb, at))
                            continue;
                        const auto marked =
                            static_cast<std::size_t>(field.y + static_cast<int>(row)) * read.width +
                            static_cast<std::size_t>(field.x + static_cast<int>(column));
                        if (marked >= mask.size() || mask[marked] != 0)
                            continue;
                        // The pixel counts only where the card drew another
                        // colour; else no sprite covered it.
                        const auto seen = marked * 3U;
                        if (read.rgb[seen] != shown(base.rgb[at]) ||
                            read.rgb[seen + 1] != shown(base.rgb[at + 1]) ||
                            read.rgb[seen + 2] != shown(base.rgb[at + 2])) {
                            mask[marked] = 1;
                            ++painted_over;
                        }
                    }
            }
            const auto expected = composed();
            const auto difference = compare_masked(read, expected, field, cursor(), mask);
            std::cout << "render tiers check: the sprite stage at zoom " << zoom_text(zoom)
                      << " drew " << stage.sprites << " sprites (" << stage.greyed << " greyed, "
                      << stage.refused << " refused, " << sprites_fogged
                      << " at the gray's edge, line of sight " << (los_on ? "on" : "off")
                      << ", mapping " << (mapping_on ? "on" : "off") << "), " << stage.squares
                      << " squares and " << stage.lines << " lines in " << stage.batches
                      << " batches, " << painted_over
                      << " pixels painted over in the base's colour: beside the blends, lines, "
                         "models and fog edges most "
                      << difference.beside.most << " over " << difference.beside.pixels
                      << " pixels; under them most " << difference.under.most << ", mean "
                      << difference.under.mean << " over " << difference.under.pixels << '\n';
            if (difference.beside.pixels == 0 || difference.beside.most != 0) {
                write_png(
                    report_directory / ("native-render-tiers-full-sprites-zoom-" + zoom_text(zoom) +
                                        "-presented.png"),
                    read
                );
                write_png(
                    report_directory / ("native-render-tiers-full-sprites-zoom-" + zoom_text(zoom) +
                                        "-composed.png"),
                    expected
                );
                // Where they differ: the composition dimmed, the mask in
                // blue, and each pixel that differs beside the mask white.
                renderer::Surface differing = expected;
                for (std::size_t pixel = 0; pixel < mask.size(); ++pixel) {
                    auto* shown = differing.rgb.data() + pixel * 3U;
                    const auto* seen = read.rgb.data() + pixel * 3U;
                    const bool differs =
                        shown[0] != seen[0] || shown[1] != seen[1] || shown[2] != seen[2];
                    if (mask[pixel] != 0) {
                        shown[0] /= 4;
                        shown[1] /= 4;
                        shown[2] = static_cast<uint8_t>(shown[2] / 4 + 96);
                    } else if (differs) {
                        shown[0] = shown[1] = shown[2] = 255;
                    } else {
                        shown[0] /= 2;
                        shown[1] /= 2;
                        shown[2] /= 2;
                    }
                }
                write_png(
                    report_directory / ("native-render-tiers-full-sprites-zoom-" + zoom_text(zoom) +
                                        "-differing.png"),
                    differing
                );
                fail(
                    "the sprite stage's frame at zoom " + zoom_text(zoom) +
                    " differs from the processor's composition beside the blends and lines"
                );
            }
            if (kinds_planned != 0 && stage.sprites + stage.squares + stage.lines == 0)
                fail("the card drew none of the frame's sprites at zoom " + zoom_text(zoom));
            if (stage.sprites + stage.squares + stage.lines != 0 && kept.rgb == whole_world.rgb)
                fail("the bands drew the card's kinds at zoom " + zoom_text(zoom));
        }
        set_full_stages(0);
    }
    // Switched off, every frame is the standard tier's again.
    switch_tier(false);
    resize(whole_scale_width, whole_scale_height);
    update_pointer(
        static_cast<float>(match_layout_.width - 1), static_cast<float>(match_layout_.height - 1)
    );
    // The Full tier: the terrain drawn by the card from the atlas pages, the
    // rest by the processor over it.
    if (asked_level == HardwareAcceleration::full)
        check_full_render_tier(
            set_level,
            at_zoom,
            presented,
            composed,
            resize,
            check_hud_strips,
            report_directory,
            software
        );
    else
        std::cout << "render tiers check: the Full cases need --hardware-acceleration=full; "
                     "skipped\n";
    // Switched off, every frame is the standard tier's again.
    switch_tier(false);
    for (const float zoom : {0.5F, 1.0F, 2.0F}) {
        at_zoom(zoom);
        const auto read = presented();
        if (compare(read, composed(), {0, 0, match_layout_.width, match_layout_.height}, cursor())
                .most != 0)
            fail(
                "a frame after the accelerated presentation was switched off differs at zoom " +
                zoom_text(zoom)
            );
    }
    std::cout << "render tiers check: the accelerated presentation draws within its references at "
                 "every zoom, with "
              << counts.textures_created << " textures made and " << counts.prescale_draws
              << " prescale draws; pictures in " << report_directory.string() << '\n';
    return 0;
}

void Runtime::check_full_render_tier(
    const std::function<void(oa::ui::engine_settings::HardwareAcceleration)>& set_level,
    const std::function<void(float)>& at_zoom,
    const std::function<renderer::Surface()>& presented,
    const std::function<renderer::Surface()>& composed,
    const std::function<void(int, int)>& resize,
    const std::function<void(const renderer::Surface&, const std::string&)>& check_hud_strips,
    const fs::path& report_directory,
    bool software
) {
    using oa::ui::engine_settings::HardwareAcceleration;
    const auto fail = [](const std::string& what) {
        throw std::runtime_error("render tiers check: full tier: " + what);
    };
    // The pages alive at the first Full frame of a spell of the tier: no
    // frame after it may make one. A spell ends when the level leaves Full,
    // which frees the pages; the next spell's first frame makes them again.
    std::optional<uint64_t> spell_pages;
    const auto level = [&](HardwareAcceleration to) {
        set_level(to);
        if (to != HardwareAcceleration::full)
            spell_pages.reset();
    };
    const auto battlefield = [&]() {
        return Area{
            match_layout_.left,
            match_layout_.top,
            match_layout_.battlefield_width(),
            match_layout_.battlefield_height()
        };
    };
    const auto cursor = [&]() {
        return Area{
            static_cast<int>(match_pointer_x_) - cursor_reach,
            static_cast<int>(match_pointer_y_) - cursor_reach,
            2 * cursor_reach,
            2 * cursor_reach
        };
    };
    const auto picture = [&](const std::string& name) {
        return report_directory / ("native-render-tiers-full-" + name + ".png");
    };
    const auto gamma_of = [&](std::vector<uint8_t> bytes) {
        if (!gamma_identity_)
            for (auto& byte : bytes)
                byte = gamma_table_[byte];
        return bytes;
    };
    // A Full frame, which never runs the box filter, makes no page after
    // the spell's first, and draws each pass in no more batches than the
    // atlas has pages, the clear and the resolve of the zoom-in target
    // beside them.
    const auto full_frame = [&]() {
        const uint64_t runs = terrain_box_filter_runs_;
        auto frame = presented();
        if (terrain_box_filter_runs_ != runs)
            fail("the box filter ran for a full frame");
        if (!full_presentation() || !full_ || !full_->drawn)
            fail("the frame at zoom " + zoom_text(match_zoom()) + " was not drawn by the card");
        const auto& full = *full_;
        const uint64_t alive = full.executor.counts().pages_alive;
        if (alive != full.atlas.pages.size() || full.pages.size() != alive)
            fail("the terrain pages alive are not the atlas's pages");
        if (spell_pages && *spell_pages != alive)
            fail(
                "a terrain page was made after the first full frame, at zoom " +
                zoom_text(match_zoom())
            );
        spell_pages = alive;
        const auto most_batches =
            static_cast<uint32_t>(full.plan.pass_count * full.atlas.pages.size()) +
            (full.drawn_through_target ? target_frame_batches : 0U);
        if (full.drawn_batches > most_batches)
            fail(
                "zoom " + zoom_text(match_zoom()) + " drew " + std::to_string(full.drawn_batches) +
                " batches from " + std::to_string(full.atlas.pages.size()) + " pages in " +
                std::to_string(full.plan.pass_count) + " passes"
            );
        return frame;
    };
    // What the terrain cost the processor in the frame just presented.
    const auto cost = [&]() {
        const auto& full = *full_;
        return "quads " + std::to_string(full.drawn_quads) + " in " +
               std::to_string(full.drawn_batches) + " batches; processor cost: build " +
               std::to_string(full.build_ns / 1000) + " us, card " +
               std::to_string(full.execute_ns / 1000) + " us, overlay " +
               std::to_string(full.overlay_ns / 1000) + " us";
    };

    level(HardwareAcceleration::full);
    if (!render_run_ || render_run_->tier.tier != policy::RenderTier::full)
        fail("the tier decided for --hardware-acceleration=full is not the full tier");
    if (!full_presentation())
        fail("--hardware-acceleration=full did not switch the full tier on");

    // Whole-number zooms: level 0 NEAREST, the processor's picture exactly,
    // the terrain the card's and the rest the overlay's. The check's own
    // atlas of the map, built as the tier builds its own, holds the texels
    // the references read: the tier lets its own go once uploaded.
    gw::TerrainAtlas reference_atlas;
    for (const float zoom : {1.0F, 2.0F, 4.0F}) {
        at_zoom(zoom);
        const auto read = full_frame();
        const auto expected = composed();
        if (full_->plan.pass_count != 1 || full_->plan.passes[0].level != 0 ||
            full_->plan.passes[0].sampling != card::Sampling::nearest || full_->plan.through_target)
            fail("zoom " + zoom_text(zoom) + " was not drawn from level 0 NEAREST");
        const auto world = compare(read, expected, battlefield(), cursor());
        const auto whole =
            compare(read, expected, {0, 0, match_layout_.width, match_layout_.height}, cursor());
        std::cout << "render tiers check: full tier zoom " << zoom_text(zoom)
                  << ": battlefield most " << world.most << ", whole frame most " << whole.most
                  << "; " << cost() << '\n';
        write_png(picture("zoom-" + zoom_text(zoom)), read);
        if (world.pixels == 0 || world.most != 0 || whole.most > most_hud_difference) {
            write_png(picture("zoom-" + zoom_text(zoom) + "-composed"), expected);
            fail("zoom " + zoom_text(zoom) + " differs from compose_match_frame");
        }
        if (zoom == 1.0F) {
            const auto& full = *full_;
            std::cout << "render tiers check: full tier terrain atlas: " << full.atlas.pages.size()
                      << " pages of " << full.atlas.slot_tiles.size() << " slots within "
                      << full.atlas_page_edge << ", " << full.page_bytes / 1024 / 1024
                      << " MiB uploaded in " << full.page_upload_ns / 1000 << " us, built in "
                      << full.atlas_build_ns / 1000 << " us"
                      << (full.pages_from_load ? ", as the match loaded" : ", at a frame") << '\n';
            for (const auto& page : full.atlas.pages)
                if (!page.texels.empty())
                    fail("the atlas kept a page's texels after the page was filled");
            if (!selected_tnt_)
                fail("the match has no map");
            const auto atlas_start = std::chrono::steady_clock::now();
            if (gw::build_terrain_atlas(
                    *selected_tnt_,
                    match_palette_,
                    gamma_identity_ ? nullptr : &gamma_table_,
                    full.atlas_page_edge,
                    reference_atlas
                ) != gw::TerrainAtlasError::none)
                fail("the check could not build its own atlas of the map");
            if (reference_atlas.grid != full.atlas.grid ||
                reference_atlas.pages.size() != full.atlas.pages.size())
                fail("the check's atlas of the map differs from the tier's");
            std::cout << "render tiers check: the check's own atlas of the map built in "
                      << std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - atlas_start
                         )
                             .count()
                      << " us\n";
        }
    }

    // The state case: what the match reads back from a Full frame equals
    // the standard tier's at the same moment.
    {
        at_zoom(1.0F);
        std::ignore = full_frame();
        std::ignore = full_frame();
        const auto full_read = match_draw_read_back();
        level(HardwareAcceleration::off);
        std::ignore = presented();
        const auto standard_read = match_draw_read_back();
        if (const char* differs = draw_read_back_difference(full_read, standard_read);
            differs != nullptr)
            fail(
                std::string("a full frame left ") + differs +
                " otherwise than the standard tier's frame of the same moment"
            );
        std::cout << "render tiers check: full tier: a full frame leaves what the match reads as "
                     "the standard tier's frame does\n";
    }

    // Zoomed out: the card's levels, never the box filter. The terrain under
    // a transparent overlay, where the standard tier shows its terrain too,
    // is held to the standard tier's box filter of the same moment: exactly
    // at zoom 0.5 with the camera on an even map pixel, where level 1 drawn
    // 1:1 is that filter; at 0.75 the blend of the two levels is the card's
    // own filter, held to a mean difference and printed.
    for (const float zoom : {kMinBattlefieldZoom, 0.75F}) {
        const bool exact = zoom == kMinBattlefieldZoom;
        level(HardwareAcceleration::full);
        at_zoom(zoom);
        match_camera_x_ &= ~1;
        match_camera_z_ &= ~1;
        const auto read = full_frame();
        const auto& plan = full_->plan;
        if (plan.passes[0].level != full_terrain::far_level ||
            plan.passes[0].sampling != card::Sampling::linear || plan.through_target)
            fail("zoom " + zoom_text(zoom) + " was not drawn from level 1 LINEAR");
        if (exact ? plan.pass_count != 1
                  : (plan.pass_count != 2 || plan.passes[1].level != 0 ||
                     plan.passes[1].blend != card::Blend::alpha ||
                     std::abs(
                         plan.passes[1].alpha -
                         static_cast<float>(1.0 - std::log2(1.0 / static_cast<double>(zoom)))
                     ) > 1.0e-5F))
            fail("zoom " + zoom_text(zoom) + " did not take the level rule's passes");
        const std::string full_cost = cost();
        const auto overlay = full_->overlay;
        const uint32_t cam_x = terrain_cache_cam_x_;
        const uint32_t cam_y = terrain_cache_cam_y_;
        if (cam_x % 2 != 0 || cam_y % 2 != 0)
            fail("the camera is not on an even map pixel at zoom " + zoom_text(zoom));
        write_png(picture("zoom-" + zoom_text(zoom)), read);
        // The renderer's own picture of the passes, each tile's quad drawn
        // LINEAR from the check's atlas and the level-0 pass blended over
        // the level-1 pass at its alpha. On SDL's software renderer each
        // quad is a texture copy: its rectangle truncated to whole pixels,
        // the tile's texels at the level stretched into it as that renderer
        // draws a texture LINEAR (software_linear_rgb24), and the blend in
        // that renderer's 256ths. On a card each quad covers the pixels
        // whose centres lie within it, each sampled LINEAR at its centre
        // from the tile, clamped at the tile's edge as the gutter clamps it
        // (bilinear_rgb24), and the blend is exact, rounded once.
        renderer::Surface renderer_reference = read;
        {
            const auto& atlas = reference_atlas;
            const Area field = battlefield();
            const auto within_field = [&](int x, int y) {
                return x >= field.x && x < field.x + field.w && y >= field.y &&
                       y < field.y + field.h;
            };
            full_terrain::TerrainView view;
            view.camera_x = cam_x;
            view.camera_y = cam_y;
            view.origin_x = static_cast<float>(field.x);
            view.origin_y = static_cast<float>(field.y);
            view.scale = zoom;
            view.width = static_cast<uint32_t>(field.w);
            view.height = static_cast<uint32_t>(field.h);
            const auto range = full_terrain::visible_tiles(atlas, view);
            const auto tile_pixels = static_cast<float>(gw::tile_edge) * zoom;
            // Lays a drawn tile over the reference at a pixel, replacing or
            // blending by the pass.
            const auto lay = [&](int sx,
                                 int sy,
                                 const uint8_t* drawn,
                                 card::Blend blend,
                                 int alpha_256ths,
                                 double alpha) {
                if (!within_field(sx, sy))
                    return;
                const auto at = (static_cast<std::size_t>(sy) * renderer_reference.width + sx) * 3U;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    const int source_level = drawn[channel];
                    uint8_t& destination = renderer_reference.rgb[at + channel];
                    if (blend == card::Blend::none)
                        destination = static_cast<uint8_t>(source_level);
                    else if (software)
                        destination = static_cast<uint8_t>(
                            (((source_level - destination) * alpha_256ths) >> blend_shift) +
                            destination
                        );
                    else
                        destination = static_cast<uint8_t>(
                            std::lround(destination + (source_level - destination) * alpha)
                        );
                }
            };
            for (uint32_t pass = 0; pass < plan.pass_count; ++pass) {
                const auto& draw = plan.passes[pass];
                const double alpha = std::clamp(static_cast<double>(draw.alpha), 0.0, 1.0);
                const auto alpha_256ths = static_cast<int>(std::lround(alpha * 255.0));
                for (uint32_t row = range.first_row; row < range.end_row; ++row)
                    for (uint32_t column = range.first_column; column < range.end_column;
                         ++column) {
                        const auto rect = gw::tile_rect(
                            atlas,
                            atlas.grid[static_cast<std::size_t>(row) * atlas.grid_width + column],
                            draw.level
                        );
                        if (!rect)
                            fail("the atlas lacks a tile at zoom " + zoom_text(zoom));
                        const auto& page = atlas.pages[rect->page];
                        const auto& level = page.levels[draw.level];
                        std::vector<uint8_t> tile(std::size_t{rect->edge} * rect->edge * 3U);
                        for (uint32_t ty = 0; ty < rect->edge; ++ty)
                            for (uint32_t tx = 0; tx < rect->edge; ++tx)
                                std::memcpy(
                                    &tile[(std::size_t{ty} * rect->edge + tx) * 3U],
                                    &page.texels
                                         [level.offset +
                                          ((std::size_t{rect->y} + ty) * level.width + rect->x +
                                           tx) *
                                              4U],
                                    3
                                );
                        const wr::RgbSource source{tile.data(), rect->edge, rect->edge, rect->edge};
                        // The quad's corner and size as the builder gives them.
                        const auto corner_x = static_cast<float>(
                            static_cast<double>(view.origin_x) +
                            (static_cast<double>(column) * gw::tile_edge -
                             static_cast<double>(cam_x)) *
                                zoom
                        );
                        const auto corner_y = static_cast<float>(
                            static_cast<double>(view.origin_y) +
                            (static_cast<double>(row) * gw::tile_edge -
                             static_cast<double>(cam_y)) *
                                zoom
                        );
                        if (software) {
                            const SDL_Rect landed{
                                static_cast<int>(corner_x),
                                static_cast<int>(corner_y),
                                static_cast<int>((corner_x + tile_pixels) - corner_x),
                                static_cast<int>((corner_y + tile_pixels) - corner_y)
                            };
                            if (landed.w <= 0 || landed.h <= 0)
                                continue;
                            const auto landed_w = static_cast<uint32_t>(landed.w);
                            const auto landed_h = static_cast<uint32_t>(landed.h);
                            std::vector<uint8_t> stretched(std::size_t{landed_w} * landed_h * 3U);
                            software_linear_rgb24(
                                source,
                                1,
                                {0, 0, landed.w, landed.h},
                                {stretched.data(), landed_w, landed_h, landed_w}
                            );
                            for (int y = 0; y < landed.h; ++y)
                                for (int x = 0; x < landed.w; ++x)
                                    lay(landed.x + x,
                                        landed.y + y,
                                        &stretched
                                            [(static_cast<std::size_t>(y) * landed_w + x) * 3U],
                                        draw.blend,
                                        alpha_256ths,
                                        alpha);
                            continue;
                        }
                        // The pixels whose centres the quad covers, by the
                        // top-left rule: from the first centre at or past the
                        // quad's left and top edges to the last before its
                        // right and bottom edges.
                        const double left = corner_x;
                        const double top = corner_y;
                        const double size = tile_pixels;
                        const auto first_x = static_cast<int>(std::ceil(left - 0.5));
                        const auto first_y = static_cast<int>(std::ceil(top - 0.5));
                        const auto end_x = static_cast<int>(std::ceil(left + size - 0.5));
                        const auto end_y = static_cast<int>(std::ceil(top + size - 0.5));
                        if (end_x <= first_x || end_y <= first_y)
                            continue;
                        const auto covered_w = static_cast<uint32_t>(end_x - first_x);
                        const auto covered_h = static_cast<uint32_t>(end_y - first_y);
                        std::vector<uint8_t> drawn(std::size_t{covered_w} * covered_h * 3U);
                        wr::bilinear_rgb24(
                            source,
                            {size / rect->edge,
                             size / rect->edge,
                             left - static_cast<double>(first_x),
                             top - static_cast<double>(first_y)},
                            {drawn.data(), covered_w, covered_h, covered_w}
                        );
                        for (uint32_t y = 0; y < covered_h; ++y)
                            for (uint32_t x = 0; x < covered_w; ++x)
                                lay(first_x + static_cast<int>(x),
                                    first_y + static_cast<int>(y),
                                    &drawn[(std::size_t{y} * covered_w + x) * 3U],
                                    draw.blend,
                                    alpha_256ths,
                                    alpha);
                    }
            }
        }
        // The standard tier's frame of the same moment, whose terrain base
        // is the box filter's.
        level(HardwareAcceleration::off);
        const uint64_t filter_ns = terrain_box_filter_ns_;
        const uint64_t filter_runs = terrain_box_filter_runs_;
        const auto standard = composed_after(presented, composed);
        if (terrain_cache_cam_x_ != cam_x || terrain_cache_cam_y_ != cam_y ||
            terrain_box_filter_runs_ == filter_runs)
            fail("the standard tier did not box-filter the same view at zoom " + zoom_text(zoom));
        const auto boxed = gamma_of(match_terrain_cache_.rgb);
        const Area field = battlefield();
        const Area pointer = cursor();
        const auto bf_w = static_cast<uint32_t>(field.w);
        Difference difference;
        double sum = 0.0;
        std::size_t channels = 0;
        std::size_t covered = 0;
        for (int y = 0; y < field.h; ++y)
            for (int x = 0; x < field.w; ++x) {
                const int sx = field.x + x;
                const int sy = field.y + y;
                if (sx >= pointer.x && sx < pointer.x + pointer.w && sy >= pointer.y &&
                    sy < pointer.y + pointer.h)
                    continue;
                const auto cell = static_cast<std::size_t>(y) * bf_w + static_cast<uint32_t>(x);
                if (overlay[cell * 4U + 3U] != 0) {
                    ++covered;
                    continue;
                }
                const auto at = (static_cast<std::size_t>(sy) * read.width + sx) * 3U;
                if (std::memcmp(&standard.rgb[at], &boxed[cell * 3U], 3) != 0) {
                    ++covered;
                    continue;
                }
                ++difference.pixels;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    const int delta =
                        std::abs(int{read.rgb[at + channel]} - int{boxed[cell * 3U + channel]});
                    difference.most = std::max(difference.most, delta);
                    sum += delta;
                    ++channels;
                }
            }
        difference.mean = channels != 0 ? sum / static_cast<double>(channels) : 0.0;
        std::cout << "render tiers check: full tier zoom " << zoom_text(zoom) << ": terrain of "
                  << difference.pixels << " pixels against the box filter (" << covered
                  << " under the overlay or a unit): most " << difference.most << ", mean "
                  << difference.mean << "; " << full_cost
                  << "; the standard tier's box filter took "
                  << (terrain_box_filter_ns_ - filter_ns) / 1000 << " us\n";
        const bool enough = difference.pixels >= least_terrain_pixels;
        if (!enough ||
            (exact ? difference.most != 0 : difference.mean > most_blend_mean_difference)) {
            write_png(picture("zoom-" + zoom_text(zoom) + "-standard"), standard);
            fail(
                "the terrain at zoom " + zoom_text(zoom) +
                (exact ? " differs from the box filter" : " strays from the box filter")
            );
        }
        // The blend is held to the renderer's own LINEAR of each tile,
        // pass over pass, within the renderer's tolerance: SDL's software
        // renderer to its own, a card to the card's.
        if (!exact) {
            Difference modelled;
            double model_sum = 0.0;
            std::size_t model_channels = 0;
            for (int y = 0; y < field.h; ++y)
                for (int x = 0; x < field.w; ++x) {
                    const int sx = field.x + x;
                    const int sy = field.y + y;
                    if (sx >= pointer.x && sx < pointer.x + pointer.w && sy >= pointer.y &&
                        sy < pointer.y + pointer.h)
                        continue;
                    const auto cell = static_cast<std::size_t>(y) * bf_w + static_cast<uint32_t>(x);
                    if (overlay[cell * 4U + 3U] != 0)
                        continue;
                    const auto at = (static_cast<std::size_t>(sy) * read.width + sx) * 3U;
                    ++modelled.pixels;
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        const int delta = std::abs(
                            int{read.rgb[at + channel]} - int{renderer_reference.rgb[at + channel]}
                        );
                        modelled.most = std::max(modelled.most, delta);
                        model_sum += delta;
                        ++model_channels;
                    }
                }
            modelled.mean =
                model_channels != 0 ? model_sum / static_cast<double>(model_channels) : 0.0;
            const int most_allowed = software ? most_software_difference : most_card_difference;
            const double mean_allowed =
                software ? most_blended_mean_difference : most_mean_scaled_difference;
            std::cout << "render tiers check: full tier zoom " << zoom_text(zoom) << ": terrain of "
                      << modelled.pixels << " pixels against the "
                      << (software ? "software renderer's" : "card's")
                      << " LINEAR of each tile, blended: most " << modelled.most << ", mean "
                      << modelled.mean << '\n';
            if (modelled.pixels < least_terrain_pixels || modelled.most > most_allowed ||
                modelled.mean > mean_allowed) {
                write_png(picture("zoom-" + zoom_text(zoom) + "-reference"), renderer_reference);
                fail(
                    "the terrain at zoom " + zoom_text(zoom) +
                    " strays from the renderer's own LINEAR of each tile"
                );
            }
        }
    }

    // A zoom above 1 that is not whole: level 0 through the target at the
    // next whole number, drawn LINEAR to the window, the Basic tier's
    // sharp-bilinear; the terrain under a transparent overlay is held to
    // the reference of the level-0 view enlarged that many times and drawn
    // as the renderer draws a texture LINEAR, within the renderer's
    // tolerance. A renderer with the pixel-art sampling mode draws straight,
    // held to that filter's reference.
    {
        const float zoom = 1.37F;
        level(HardwareAcceleration::full);
        at_zoom(zoom);
        const auto read = full_frame();
        const auto& full = *full_;
        const auto& plan = full.plan;
        if (plan.pass_count != 1 || plan.passes[0].level != 0)
            fail("zoom " + zoom_text(zoom) + " was not drawn from level 0");
        write_png(picture("zoom-" + zoom_text(zoom)), read);
        renderer::Surface reference = read;
        const uint32_t cam_x = terrain_cache_cam_x_;
        const uint32_t cam_y = terrain_cache_cam_y_;
        const Area field = battlefield();
        bool compared = true;
        std::string how;
        if (full.drawn_through_target) {
            const uint32_t factor = plan.target_zoom;
            const uint32_t view_w = full.target_width / factor;
            const uint32_t view_h = full.target_height / factor;
            std::vector<uint8_t> rgba(std::size_t{view_w} * view_h * 4U);
            if (gw::read_terrain_view(
                    reference_atlas,
                    0,
                    cam_x,
                    cam_y,
                    view_w,
                    view_h,
                    rgba.data(),
                    std::size_t{view_w} * 4U
                ) != gw::TerrainAtlasError::none)
                fail("the atlas refused the level-0 view at zoom " + zoom_text(zoom));
            std::vector<uint8_t> rgb(std::size_t{view_w} * view_h * 3U);
            for (std::size_t i = 0; i < std::size_t{view_w} * view_h; ++i)
                std::memcpy(&rgb[i * 3U], &rgba[i * 4U], 3);
            const SDL_Rect landed{
                field.x,
                field.y,
                static_cast<int>(std::lround(
                    static_cast<double>(full.target_width) * zoom / static_cast<double>(factor)
                )),
                static_cast<int>(std::lround(
                    static_cast<double>(full.target_height) * zoom / static_cast<double>(factor)
                ))
            };
            const wr::RgbSource source{rgb.data(), view_w, view_h, view_w};
            const wr::RgbTarget target{
                reference.rgb.data(), reference.width, reference.height, reference.width
            };
            if (software)
                software_linear_rgb24(source, factor, landed, target);
            else
                wr::sharp_bilinear_rgb24(
                    source,
                    {static_cast<double>(landed.w) / view_w,
                     static_cast<double>(landed.h) / view_h,
                     static_cast<double>(landed.x),
                     static_cast<double>(landed.y)},
                    factor,
                    target
                );
            how = "through the target at " + std::to_string(factor);
        } else if (plan.passes[0].sampling == card::Sampling::pixel_art) {
            const auto view_w = static_cast<uint32_t>(std::ceil(field.w / zoom));
            const auto view_h = static_cast<uint32_t>(std::ceil(field.h / zoom));
            std::vector<uint8_t> rgba(std::size_t{view_w} * view_h * 4U);
            if (gw::read_terrain_view(
                    reference_atlas,
                    0,
                    cam_x,
                    cam_y,
                    view_w,
                    view_h,
                    rgba.data(),
                    std::size_t{view_w} * 4U
                ) != gw::TerrainAtlasError::none)
                fail("the atlas refused the level-0 view at zoom " + zoom_text(zoom));
            std::vector<uint8_t> rgb(std::size_t{view_w} * view_h * 3U);
            for (std::size_t i = 0; i < std::size_t{view_w} * view_h; ++i)
                std::memcpy(&rgb[i * 3U], &rgba[i * 4U], 3);
            wr::pixelart_rgb24(
                {rgb.data(), view_w, view_h, view_w},
                {zoom, zoom, static_cast<double>(field.x), static_cast<double>(field.y)},
                {reference.rgb.data(), reference.width, reference.height, reference.width}
            );
            how = "by the pixel-art sampling mode";
        } else {
            compared = false;
            how = "LINEAR straight, with no target";
        }
        if (compared) {
            const auto overlay = full.overlay;
            const auto bf_w = static_cast<uint32_t>(field.w);
            // The last column and row may read past the corner, which
            // renderers clamp differently.
            Difference difference;
            double sum = 0.0;
            std::size_t channels = 0;
            const Area pointer = cursor();
            for (int y = 0; y + 1 < field.h; ++y)
                for (int x = 0; x + 1 < field.w; ++x) {
                    const int sx = field.x + x;
                    const int sy = field.y + y;
                    if (sx >= pointer.x && sx < pointer.x + pointer.w && sy >= pointer.y &&
                        sy < pointer.y + pointer.h)
                        continue;
                    const auto cell = static_cast<std::size_t>(y) * bf_w + static_cast<uint32_t>(x);
                    if (overlay[cell * 4U + 3U] != 0)
                        continue;
                    const auto at = (static_cast<std::size_t>(sy) * read.width + sx) * 3U;
                    ++difference.pixels;
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        const int delta = std::abs(
                            int{read.rgb[at + channel]} - int{reference.rgb[at + channel]}
                        );
                        difference.most = std::max(difference.most, delta);
                        sum += delta;
                        ++channels;
                    }
                }
            difference.mean = channels != 0 ? sum / static_cast<double>(channels) : 0.0;
            const int most_allowed = software ? most_software_difference : most_card_difference;
            std::cout << "render tiers check: full tier zoom " << zoom_text(zoom) << " " << how
                      << ": terrain of " << difference.pixels << " pixels: most " << difference.most
                      << ", mean " << difference.mean << "; " << cost() << '\n';
            if (difference.pixels == 0 || difference.most > most_allowed ||
                difference.mean > most_mean_scaled_difference) {
                write_png(picture("zoom-" + zoom_text(zoom) + "-reference"), reference);
                fail(
                    "the terrain at zoom " + zoom_text(zoom) + " strays from the card's reference"
                );
            }
        } else {
            std::cout << "render tiers check: full tier zoom " << zoom_text(zoom) << " drawn "
                      << how << ", which no reference holds; " << cost() << '\n';
        }
    }
    // At a chrome scale of 1.6 the HUD strips are drawn by the chrome's
    // filter from the HUD layer's prescale target, as Basic draws them.
    {
        level(HardwareAcceleration::full);
        resize(part_scale_width, part_scale_height);
        update_pointer(
            static_cast<float>(match_layout_.width - 1),
            static_cast<float>(match_layout_.height - 1)
        );
        at_zoom(1.0F);
        const auto read = full_frame();
        write_png(picture("hud-scale-1.6"), read);
        check_hud_strips(read, "the full tier at a chrome scale of 1.6");
        resize(whole_scale_width, whole_scale_height);
        update_pointer(
            static_cast<float>(match_layout_.width - 1),
            static_cast<float>(match_layout_.height - 1)
        );
    }
    std::cout << "render tiers check: the full tier drew its terrain on the card at every zoom, "
              << full_->frames << " frames, with the box filter never run; pictures in "
              << report_directory.string() << '\n';
    level(HardwareAcceleration::off);
}

} // namespace oa::app
