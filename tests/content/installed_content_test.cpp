// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Installed-content sweep: mounts every archive in the directory of the
// installed game named by OA_GAME_DIR through the asset store, reads every
// entry and decodes it with the decoders the engine uses for its type. Any
// entry the engine rejects fails the sweep unless it is listed below with the
// reason the engine does without it. Every sprite frame is also drawn through
// the sprite blitter and compared with the GAF reader's decode of the frame.
// The game's own archive scan then mounts the archives a match reads, and the
// definition loaders a skirmish start runs read them: movement classes, sound
// categories, sides, weapons, line-of-sight tables, unit headers and
// definitions, build lists and download menus. The skirmish start's corpse
// features, unit categories, runtime metadata and per-unit model and script
// loads are not part of the sweep.
//
// The sweep runs on every logical core, or on as many threads as
// OA_TEST_THREADS names. It lists every entry first, in stored order, and the
// workers take the entries largest first; the game's scan and the definition
// loaders run as one more item beside them. A worker that is between items
// first helps compare the frames of any GAF another worker is drawing, then
// takes the next item. Each item keeps its own counts and report lines, and
// the main thread adds and prints them in stored order once every worker is
// done, so the output is the same for any number of threads apart from the
// time the sweep took.

#include "oa/sim/ai.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/audio/wave_chunks.hpp"
#include "oa/formats/cob.hpp"
#include "oa/data/defs/asset_files.hpp"
#include "oa/data/defs/gamedata_tables.hpp"
#include "oa/data/defs/move_classes.hpp"
#include "oa/data/defs/palette.hpp"
#include "oa/data/defs/sides.hpp"
#include "oa/data/defs/sound_categories.hpp"
#include "oa/data/defs/unit_catalog.hpp"
#include "oa/data/defs/unit_def_loader.hpp"
#include "oa/data/defs/unit_header.hpp"
#include "oa/data/defs/unit_records.hpp"
#include "oa/data/defs/weapons.hpp"
#include "oa/formats/fnt.hpp"
#include "oa/formats/gaf.hpp"
#include "oa/ui/gui_layout.hpp"
#include "oa/formats/ota.hpp"
#include "oa/present/blit.hpp"
#include "oa/present/gaf_sprites.hpp"
#include "oa/present/palette_tables.hpp"
#include "oa/present/pcx.hpp"
#include "oa/present/surface.hpp"
#include "oa/formats/tdf.hpp"
#include "oa/test/game_data.hpp"
#include "oa/formats/objects3d.hpp"
#include "oa/formats/tnt.hpp"
#include "oa/platform/system.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Archive extensions the game mounts from its directory.
constexpr std::string_view archive_extensions[] = {".hpi", ".ufo", ".ccx", ".gp3"};
// Revision in the name of the patch archive the game's scan mounts (rev31.gp3).
constexpr std::string_view archive_revision = "31";
// The running build a unit's Version is checked against, 3.1.
constexpr int8_t running_build_major = 3;
constexpr int8_t running_build_minor = 1;

// Fill values the sprite comparison draws over: a pixel is drawn when both
// canvases end with the same value.
constexpr uint8_t canvas_low = 0x00;
constexpr uint8_t canvas_high = 0xFF;

// Channels of a PCX colour map entry (red, green, blue).
constexpr std::size_t rgb_channels = 3;

/// Environment variable naming how many threads the sweep runs on; unset,
/// empty or 0 means one per logical core.
constexpr const char* threads_variable = "OA_TEST_THREADS";
/// Most threads OA_TEST_THREADS may ask for.
constexpr unsigned long max_threads = 1024;

// What the engine does with an entry of a given extension.
enum class Decoder : uint8_t {
    gaf,
    pcx,
    tnt,
    ota,
    tdf,
    fbi,
    gui,
    cob,
    model,
    fnt,
    palette,
    alpha_table,
    shade_table,
    light_table,
    wave,
    ai_profile,
    text,
    unread,
};

/// One row of the summary: the entries a decoder reads, and what they cost.
struct ContentType {
    Decoder decoder{};
    const char* label{};
    const char* extension{}; ///< lower case with its dot; null for the catch-all row
    /// Lower-case directory the row is limited to, with its '/'; null for any.
    const char* directory{};
    /// Estimated work per entry byte, about the nanoseconds an unoptimised
    /// build takes; the sweep starts the costliest entries first.
    uint32_t cost_per_byte{};
};

// One row per summary line; the last row takes every other extension.
constexpr ContentType content_types[] = {
    {Decoder::gaf, "GAF animation", ".gaf", nullptr, 230},
    {Decoder::pcx, "PCX image", ".pcx", nullptr, 85},
    {Decoder::tnt, "TNT terrain", ".tnt", nullptr, 9},
    {Decoder::ota, "OTA map", ".ota", nullptr, 35},
    {Decoder::tdf, "TDF definitions", ".tdf", nullptr, 11},
    {Decoder::fbi, "FBI unit", ".fbi", nullptr, 120},
    {Decoder::gui, "GUI layout", ".gui", nullptr, 65},
    {Decoder::cob, "COB script", ".cob", nullptr, 6},
    {Decoder::model, "3DO model", ".3do", nullptr, 15},
    {Decoder::fnt, "FNT font", ".fnt", nullptr, 130},
    {Decoder::palette, "PAL palette", ".pal", nullptr, 2},
    {Decoder::alpha_table, "ALP alpha table", ".alp", nullptr, 2},
    {Decoder::shade_table, "SHD shade table", ".shd", nullptr, 2},
    {Decoder::light_table, "LHT light table", ".lht", nullptr, 2},
    {Decoder::wave, "WAV sound", ".wav", nullptr, 5},
    {Decoder::ai_profile, "AI profile (script reader)", ".txt", "ai/", 35},
    {Decoder::text, "TXT text (read whole)", ".txt", nullptr, 4},
    {Decoder::unread, "other (read whole, not decoded)", nullptr, nullptr, 2},
};
constexpr std::size_t content_type_count = std::size(content_types);
/// Estimated work of any entry beyond its bytes, in the units of cost_per_byte.
constexpr uint64_t cost_per_entry = 20'000;

// Entries of the stock game that the engine's decoders reject, each with the
// failure it must give and why the engine does without it. A listed entry
// that decodes is reported so the list can shrink; one that fails for another
// reason fails the sweep.
struct KnownRejection {
    const char* archive{}; // archive file name, lower case
    const char* path{};    // archive entry path, lower case
    const char* failure{}; // start of the failure text the entry gives
    const char* reason{};
};

constexpr KnownRejection known_rejections[] = {
    {"totala1.hpi",
     "gamedata/unitview.tdf",
     "TDF parser: entry lacks '=' between key and value",
     "a ';' follows the closing '}' of [RESOURCES], text without '=' that 3.1c rejects as "
     "well; "
     "the file configures the separate unit viewer and the engine never reads it"},
    {"totala1.hpi",
     "guis/endgame.gui",
     "GUI layout reader: compiled/binary GUI data",
     "a compiled binary layout that no screen loads; the end screens use endmsn.gui and "
     "endmulti.gui"},
    {"totala1.hpi",
     "palettes/guipal.pcx",
     "PCX loader: not a version 5 PCX",
     "256 raw four-byte palette entries under a .pcx name, with no PCX header; the GUI palette is "
     "read from "
     "palettes/guipal.pal"},
};

struct TypeTotals {
    uint64_t entries{};
    uint64_t decoded{};
    uint64_t rejected{};
    uint64_t bytes{};
};

struct SpriteTotals {
    uint64_t frames{};
    uint64_t special{};
    uint64_t mismatched{};
};

// Counts of one extension in the catch-all row.
struct ExtensionTotals {
    std::string extension{}; // lower case with its dot; empty for none
    uint64_t entries{};
    uint64_t bytes{};
};

struct SweepTotals {
    TypeTotals types[content_type_count]{};
    std::vector<ExtensionTotals> unread_extensions{};
    SpriteTotals sprites{};
    uint64_t archives{};
    uint64_t expected_rejections{};
    uint64_t unexpected_rejections{};
};

/// The stream a piece of report text goes to.
enum class Stream : uint8_t {
    out, ///< standard output
    err, ///< standard error
};

/// Text for one stream, in the order it is printed.
struct ReportText {
    Stream stream{};
    std::string text{};
};

/// The text one item of the sweep prints, kept until the main thread prints
/// every item's text in stored order.
using Report = std::vector<ReportText>;

/// What one item of the sweep found. Only the worker running the item writes
/// it; the main thread reads it once every worker is done.
struct ItemOutcome {
    std::size_t type{};     ///< summary row of an entry
    bool decoded{};         ///< the entry decoded
    uint64_t bytes{};       ///< bytes read from the entry
    SpriteTotals sprites{}; ///< frames of the entry's GAF comparison
    uint64_t expected_rejections{};
    uint64_t unexpected_rejections{};
    /// The exception that ended the item before it could report; null when it finished.
    std::exception_ptr stopped{};
    Report report{};
};

/// What an item of the sweep does.
enum class ItemKind : uint8_t {
    entry,            ///< read and decode one file record
    broken_directory, ///< report a directory that lists records outside its archive
    game_mounts,      ///< mount the archives as the game's scan does and run the definition loaders
};

/// One item of the sweep's work. The walk lists them in the order a serial
/// sweep meets them, which is the order their reports print in.
struct SweepItem {
    ItemKind kind{};
    const oa::HpiArchive* archive{};             ///< archive of an entry or a directory
    const std::filesystem::path* archive_path{}; ///< host path of that archive
    uint32_t index{};                            ///< file node index of an entry
    /// '/'-joined path of an entry; '/'-terminated path of a directory.
    std::string path{};
    uint64_t cost{}; ///< estimated work, in the units of ContentType::cost_per_byte
};

/// How the comparison of one frame ended.
enum class FrameVerdict : uint8_t {
    matched,    ///< the blitter drew what the GAF reader decodes, or the frame is empty
    blended,    ///< left undrawn: a child needs the blended draw, which reads the display
    mismatched, ///< see FrameResult::difference
};

/// The result of comparing one frame.
struct FrameResult {
    FrameVerdict verdict{};
    std::string difference{}; ///< what differs, for a mismatch
    /// The exception the comparison ended with, if it threw; the verdict is then not used.
    std::exception_ptr error{};
};

/// One frame as the sprite loader and the GAF reader hold it.
struct FramePair {
    const oa::Sprite* sprite{};
    const oa::formats::gaf::Frame* model{};
};

/// The frames of one GAF, compared by the worker that decoded it and by any
/// worker between items.
struct FrameComparison {
    std::vector<FramePair> frames{}; ///< in stored order
    /// One per frame, written by the worker that compared it.
    std::vector<FrameResult> results{};
    std::atomic<std::size_t> next_frame{}; ///< the next frame to take
    /// Other workers comparing frames; guarded by WorkPool::lock.
    std::size_t helpers{};
};

/// Hands out the sweep's work: the items largest first, and the frames of the
/// GAFs being compared.
struct WorkPool {
    std::vector<std::size_t> order{};     ///< item indices, largest estimated cost first
    std::atomic<std::size_t> next_item{}; ///< the next position of order to take
    std::mutex lock{};
    /// Signalled when frames are offered, when a helper leaves a GAF and when
    /// the last item finishes.
    std::condition_variable changed{};
    std::size_t unfinished_items{}; ///< guarded by lock
    /// GAFs whose frames other workers may take; guarded by lock.
    std::vector<FrameComparison*> offered{};
};

/// Appends printf-formatted text to a report.
///
/// @param[in,out] report report the text joins; text for the stream of the last piece joins that piece
/// @param stream stream the text is printed on
/// @param format printf-style format, followed by its arguments
void print_to(Report& report, Stream stream, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

void print_to(Report& report, Stream stream, const char* format, ...) {
    std::va_list arguments;
    va_start(arguments, format);
    std::va_list measured;
    va_copy(measured, arguments);
    const int length = std::vsnprintf(nullptr, 0, format, measured);
    va_end(measured);
    if (length > 0) {
        if (report.empty() || report.back().stream != stream) {
            report.push_back({stream, {}});
        }
        std::string& text = report.back().text;
        const std::size_t at = text.size();
        const auto added = static_cast<std::size_t>(length);
        text.resize(at + added + 1);
        std::vsnprintf(text.data() + at, added + 1, format, arguments);
        text.resize(at + added);
    }
    va_end(arguments);
}

/// Prints a report on standard output and standard error, in its order.
///
/// @param report text to print
void print_report(const Report& report) {
    for (const auto& piece : report) {
        std::fputs(piece.text.c_str(), piece.stream == Stream::out ? stdout : stderr);
    }
}

/// Folds ASCII upper case to lower case.
///
/// @param text text to fold
/// @return the folded copy
std::string lower(std::string_view text) {
    std::string folded(text);
    for (char& c : folded) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return folded;
}

/// Returns an entry's extension, lower case with its dot.
///
/// @param path archive entry path
/// @return the extension, or empty when the last segment has no dot
std::string extension_of(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
        return {};
    }
    return lower(path.substr(dot));
}

/// Returns the summary row an entry belongs to.
///
/// @param path archive entry path
/// @param extension the entry's lower-case extension with its dot
/// @return index into content_types
std::size_t content_type_of(std::string_view path, std::string_view extension) {
    const std::string folded = lower(path);
    for (std::size_t i = 0; i < content_type_count; ++i) {
        const ContentType& type = content_types[i];
        if (type.extension != nullptr && extension == type.extension &&
            (type.directory == nullptr || folded.starts_with(type.directory))) {
            return i;
        }
    }
    return content_type_count - 1;
}

/// Reports whether an entry is one of the GUI fonts the frontend loads as a GAF.
///
/// @param path archive entry path
/// @return true for anims/hattfont*.gaf
bool is_gui_font(std::string_view path) {
    return lower(path).starts_with("anims/hattfont");
}

/// Draws a sprite at its hotspot on a frame-sized canvas filled with one value.
///
/// The canvas holds exactly the frame's pixels, so a draw outside it is a
/// write past the allocation, which the sanitizer build reports.
///
/// @param sprite relocated frame
/// @param fill canvas fill value
/// @return the canvas pixels, sprite.width by sprite.height with pitch sprite.width
std::unique_ptr<uint8_t[]> draw_over(const oa::Sprite& sprite, uint8_t fill) {
    const std::size_t size = static_cast<std::size_t>(sprite.width) * sprite.height;
    auto pixels = std::make_unique_for_overwrite<uint8_t[]>(size);
    std::memset(pixels.get(), fill, size);
    oa::Surface canvas{};
    oa::present::init_surface(canvas, sprite.width, sprite.height, sprite.width, pixels.get());
    oa::present::draw_sprite(&canvas, &sprite, sprite.origin_x, sprite.origin_y);
    return pixels;
}

/// Reports whether any child of a frame, at any depth, is flagged for the blended draw.
///
/// @param frame the GAF reader's frame
/// @return true when a layer, or a layer of a layer, has its special render byte set
bool has_blended_child(const oa::formats::gaf::Frame& frame) {
    for (const auto& layer : frame.layers) {
        if (layer.special_render_flag != 0 || has_blended_child(layer)) {
            return true;
        }
    }
    return false;
}

/// Compares one relocated frame, drawn through the blitter, with the GAF reader's frame.
///
/// A frame the GAF reader declines only because a child needs the blended
/// draw, which reads the display's alpha table, is left undrawn; any other
/// render failure is a mismatch.
///
/// @param frame the relocated frame and the GAF reader's frame
/// @return how the comparison ended
FrameResult compare_frame(const FramePair& frame) {
    const oa::Sprite& sprite = *frame.sprite;
    const oa::formats::gaf::Frame& model = *frame.model;
    if (sprite.width != model.width || sprite.height != model.height ||
        sprite.origin_x != model.origin_x || sprite.origin_y != model.origin_y ||
        sprite.key != model.transparency_index || sprite.child_count != model.layer_count) {
        return {FrameVerdict::mismatched, "relocated frame header differs from the GAF reader"};
    }
    const auto rendered = oa::formats::gaf::render_normal(model);
    if (!rendered.ok()) {
        const bool blended =
            rendered.error.has_value() &&
            rendered.error->code == oa::formats::gaf::ErrorCode::unsupported_special_render &&
            has_blended_child(model);
        if (!blended) {
            return {
                FrameVerdict::mismatched,
                "GAF reader cannot render the frame: " +
                    (rendered.error ? rendered.error->message : std::string("no frame"))
            };
        }
        return {FrameVerdict::blended, {}};
    }
    if (sprite.width == 0 || sprite.height == 0) {
        return {};
    }
    const auto low_canvas = draw_over(sprite, canvas_low);
    const auto high_canvas = draw_over(sprite, canvas_high);
    const std::size_t pixel_count = static_cast<std::size_t>(sprite.width) * sprite.height;
    // Plain pointers keep the per-pixel loop free of calls in unoptimised builds.
    const uint8_t* const low = low_canvas.get();
    const uint8_t* const high = high_canvas.get();
    const uint8_t* const coverage = rendered.frame->coverage.data();
    const uint8_t* const pixels = rendered.frame->pixels.data();
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const bool drawn = low[i] == high[i];
        if (drawn != (coverage[i] != 0) || (drawn && low[i] != pixels[i])) {
            return {
                FrameVerdict::mismatched,
                "drawn frame differs from the GAF reader at pixel " + std::to_string(i)
            };
        }
    }
    return {};
}

/// Compares frames of a GAF, taking the next one left until none is.
///
/// A comparison that throws records the exception as that frame's result.
///
/// @param[in,out] comparison the GAF's frames; receives their results
void compare_frames(FrameComparison& comparison) noexcept {
    for (;;) {
        const std::size_t frame = comparison.next_frame.fetch_add(1, std::memory_order_relaxed);
        if (frame >= comparison.frames.size()) {
            return;
        }
        FrameResult& result = comparison.results[frame];
        try {
            result = compare_frame(comparison.frames[frame]);
        } catch (...) {
            result.error = std::current_exception();
        }
    }
}

/// Offers a GAF's frames to workers between items, for as long as it lives.
class FrameOffer {
  public:

    /// Offers the frames and wakes any worker waiting for work.
    ///
    /// @param[in,out] pool the sweep's work
    /// @param[in,out] comparison the frames; must outlive the offer
    FrameOffer(WorkPool& pool, FrameComparison& comparison) : pool_(pool), comparison_(comparison) {
        {
            const std::lock_guard guard(pool_.lock);
            pool_.offered.push_back(&comparison_);
        }
        pool_.changed.notify_all();
    }

    /// Withdraws the frames and waits until no other worker is comparing any.
    ~FrameOffer() {
        std::unique_lock guard(pool_.lock);
        std::erase(pool_.offered, &comparison_);
        pool_.changed.wait(guard, [this] { return comparison_.helpers == 0; });
    }

    FrameOffer(const FrameOffer&) = delete;
    FrameOffer& operator=(const FrameOffer&) = delete;

  private:

    WorkPool& pool_;
    FrameComparison& comparison_;
};

/// Lists a GAF's frames in stored order, up to the first sequence or frame the two readers disagree on.
///
/// @param gaf the sprite loader's GAF
/// @param archive the GAF reader's GAF, with as many sequences as `gaf`
/// @param[out] frames receives the frames listed
/// @return an empty string when every frame is listed, otherwise the disagreement
std::string list_frames(
    const oa::present::GafSprites& gaf,
    const oa::formats::gaf::Archive& archive,
    std::vector<FramePair>& frames
) {
    for (std::size_t s = 0; s < gaf.sequences.size(); ++s) {
        const auto& sequence = gaf.sequences[s];
        const auto& model = archive.sequences[s];
        if (sequence.frame_count != model.frames.size()) {
            return "sprite loader and GAF reader disagree on a frame count";
        }
        for (int32_t k = 0; k < sequence.frame_count; ++k) {
            const oa::Sprite* sprite = oa::present::gaf_frame(&sequence, k);
            if (sprite == nullptr) {
                return "sprite loader lost a frame";
            }
            frames.push_back({sprite, &model.frames[static_cast<std::size_t>(k)]});
        }
    }
    return {};
}

/// Decodes a GAF with the GAF reader and the sprite loader, and draws every frame.
///
/// The frames are compared on this worker and on any worker between items,
/// then counted in stored order up to the first difference, where a serial
/// comparison stops; a frame whose comparison threw rethrows its exception
/// there. A GAF none of whose frames could be compared fails. GUI fonts are
/// also loaded as the frontend loads them.
///
/// @param path archive entry path
/// @param bytes entry bytes
/// @param[in,out] pool the sweep's work, where the frames are offered
/// @param[in,out] totals the entry's frame counts
/// @return an empty string on success, otherwise the first failure
std::string decode_gaf(
    std::string_view path, std::span<const uint8_t> bytes, WorkPool& pool, SpriteTotals& totals
) {
    const auto parsed = oa::formats::gaf::parse(bytes);
    if (!parsed.ok()) {
        return "GAF reader: " + (parsed.error ? parsed.error->message : std::string("no archive"));
    }
    oa::present::GafSprites gaf;
    const auto status = oa::present::relocate_gaf(bytes, gaf);
    if (status != oa::present::GafStatus::ok) {
        return std::string("sprite loader: ") + oa::present::gaf_status_text(status);
    }
    const auto& archive = *parsed.archive;
    if (gaf.sequences.size() != archive.sequences.size()) {
        return "sprite loader and GAF reader disagree on the sequence count";
    }
    FrameComparison comparison;
    const std::string disagreement = list_frames(gaf, archive, comparison.frames);
    comparison.results.resize(comparison.frames.size());
    {
        const FrameOffer offer(pool, comparison);
        compare_frames(comparison);
    }
    for (const FrameResult& result : comparison.results) {
        ++totals.frames;
        if (result.error) {
            std::rethrow_exception(result.error);
        }
        if (result.verdict == FrameVerdict::blended) {
            ++totals.special;
        } else if (result.verdict == FrameVerdict::mismatched) {
            ++totals.mismatched;
            return result.difference;
        }
    }
    if (!disagreement.empty()) {
        return disagreement;
    }
    if (totals.frames != 0 && totals.special == totals.frames) {
        return "no frame of the GAF could be compared: every one needs the blended child draw";
    }
    if (is_gui_font(path)) {
        const auto font = oa::formats::fnt::parse_gaf(bytes);
        if (!font.ok()) {
            return std::string("GUI font: ") + font.error.message;
        }
    }
    return {};
}

/// Decodes a PCX with both of the engine's PCX readers and compares their results.
///
/// The two must agree on the size, every palette index and the 256 colours.
///
/// @param bytes entry bytes
/// @return an empty string on success, otherwise the first failure
std::string decode_pcx(std::span<const uint8_t> bytes) {
    oa::present::MemoryReader reader{bytes, 0};
    oa::present::ByteStream stream = oa::present::memory_reader_stream(reader);
    oa::present::PcxImage image;
    const auto status = oa::present::load_pcx_image(stream, image);
    if (status != oa::present::PcxStatus::ok) {
        return std::string("PCX loader: ") + oa::present::pcx_status_text(status);
    }
    const auto read = oa::decode_pcx(bytes);
    if (!read.ok()) {
        return std::string("PCX image reader: ") + read.error.message;
    }
    {
        const auto& decoded = *read.value;
        if (decoded.width != static_cast<uint32_t>(image.width) ||
            decoded.height != static_cast<uint32_t>(image.height)) {
            return "PCX readers disagree on the image size";
        }
        if (decoded.indices.empty() || !decoded.palette) {
            return "PCX image reader finds three colour planes, which the PCX loader reads as one";
        }
        if (decoded.indices != image.pixels) {
            const auto differs =
                std::mismatch(decoded.indices.begin(), decoded.indices.end(), image.pixels.begin());
            return "PCX readers disagree at pixel " +
                   std::to_string(std::distance(decoded.indices.begin(), differs.first));
        }
        for (std::size_t color = 0; color < oa::palette_color_count; ++color) {
            for (std::size_t channel = 0; channel < rgb_channels; ++channel) {
                if ((*decoded.palette)[color * oa::palette_entry_bytes + channel] !=
                    image.color_map[color * rgb_channels + channel]) {
                    return "PCX readers disagree on colour " + std::to_string(color);
                }
            }
        }
    }
    return {};
}

/// Parses TDF-family text with the engine's TDF parser.
///
/// @param bytes entry bytes
/// @return an empty string on success, otherwise the parse error
std::string parse_tdf_text(std::span<const uint8_t> bytes) {
    if (bytes.size() > oa::formats::tdf::max_input_bytes) {
        return "TDF text larger than the parser accepts";
    }
    oa::formats::tdf::Document document;
    oa::formats::tdf::document_init(&document);
    oa::formats::tdf::ParseError error{};
    const bool ok = oa::formats::tdf::parse_text(
        &document,
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<uint32_t>(bytes.size()),
        true,
        &error
    );
    oa::formats::tdf::document_free(&document);
    if (ok) {
        return {};
    }
    return std::string("TDF parser: ") + oa::formats::tdf::parse_status_message(error.status) +
           " at offset " + std::to_string(error.offset) + " in [" + error.block_name + "]";
}

/// Runs an AI profile through the computer players' script reader at each difficulty.
///
/// With no computer player present the reader tokenizes every line and
/// follows the plan lines, and its weights and limits reach no controller.
///
/// @param text entry text
void read_ai_profile(std::string_view text) {
    auto world = std::make_unique<oa::World>();
    oa::sim::ai::ComputerPlayers players{};
    oa::sim::ai::ComputerHost host{};
    host.world = world.get();
    for (const int32_t difficulty :
         {OA_DIFFICULTY_EASY, OA_DIFFICULTY_MEDIUM, OA_DIFFICULTY_HARD}) {
        host.difficulty = difficulty;
        oa::sim::ai::computer_profile_apply(&players, host, text);
    }
}

/// Decodes one entry with the engine's decoder for its type.
///
/// @param decoder decoder for the entry's extension
/// @param path archive entry path
/// @param bytes entry bytes
/// @param[in,out] pool the sweep's work, where a GAF's frames are offered
/// @param[in,out] sprites the entry's frame counts of the GAF comparison
/// @return an empty string on success, otherwise the first failure
std::string decode_entry(
    Decoder decoder,
    std::string_view path,
    std::span<const uint8_t> bytes,
    WorkPool& pool,
    SpriteTotals& sprites
) {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    switch (decoder) {
    case Decoder::gaf:
        return decode_gaf(path, bytes, pool, sprites);
    case Decoder::pcx:
        return decode_pcx(bytes);
    case Decoder::tnt: {
        const auto parsed = oa::formats::tnt::parse(bytes);
        return parsed.ok() ? std::string() : "TNT reader: " + parsed.error->message;
    }
    case Decoder::ota: {
        std::string failure = parse_tdf_text(bytes);
        if (!failure.empty()) {
            return failure;
        }
        const auto parsed = oa::formats::ota::parse(text);
        return parsed.ok() ? std::string() : "OTA reader: " + parsed.error->message;
    }
    case Decoder::tdf:
        return parse_tdf_text(bytes);
    case Decoder::fbi: {
        std::string failure = parse_tdf_text(bytes);
        if (!failure.empty()) {
            return failure;
        }
        // The unit loaders read the [UNITINFO] section; the full loads run
        // in the definitions sweep.
        oa::formats::tdf::Document document;
        oa::formats::tdf::document_init(&document);
        const bool unit_info =
            oa::formats::tdf::parse_text(
                &document, text.data(), static_cast<uint32_t>(text.size()), true, nullptr
            ) &&
            oa::formats::tdf::select_section(&document, "UNITINFO");
        oa::formats::tdf::document_free(&document);
        return unit_info ? std::string() : "FBI loader: FBI has no [UNITINFO] section";
    }
    case Decoder::gui: {
        // Screens load layouts with the GUI reader alone, which keeps the
        // last gadget of a file that ends without its closing braces.
        const auto parsed = oa::ui::gui_layout::parse(bytes);
        return parsed.ok() ? std::string() : "GUI layout reader: " + parsed.error->message;
    }
    case Decoder::cob: {
        const auto parsed = oa::formats::cob::parse_cob(bytes);
        return parsed.ok() ? std::string() : std::string("COB reader: ") + parsed.error.message;
    }
    case Decoder::model: {
        const auto parsed = oa::formats::objects3d::load_3do(std::as_bytes(bytes));
        return parsed.ok() ? std::string() : std::string("3DO reader: ") + parsed.error.message;
    }
    case Decoder::fnt: {
        const auto parsed = oa::formats::fnt::parse_fnt(bytes);
        return parsed.ok() ? std::string() : std::string("FNT reader: ") + parsed.error.message;
    }
    case Decoder::palette:
        return bytes.size() >= oa::data::defs::palette_file_bytes
                   ? std::string()
                   : "palette shorter than 256 entries";
    case Decoder::alpha_table:
        return bytes.size() == static_cast<std::size_t>(oa::present::alpha_table_size)
                   ? std::string()
                   : "alpha table is not 256x256 bytes";
    case Decoder::shade_table:
        return bytes.size() == static_cast<std::size_t>(oa::present::shade_table_size)
                   ? std::string()
                   : "shade table is not 32x256 bytes";
    case Decoder::light_table:
        return bytes.size() == static_cast<std::size_t>(oa::present::light_table_size)
                   ? std::string()
                   : "light table is not 32x256 bytes";
    case Decoder::wave: {
        oa::audio::WaveCursor cursor =
            oa::audio::wave_cursor(bytes.data(), static_cast<uint32_t>(bytes.size()));
        oa::audio::WaveLayout layout{};
        if (!oa::audio::describe_wave(cursor, layout)) {
            return "sound loader rejects the container";
        }
        if (oa::audio::wave_remaining(cursor) < layout.data_bytes) {
            return "sound samples run past the end of the file";
        }
        return {};
    }
    case Decoder::ai_profile:
        read_ai_profile(text);
        return {};
    case Decoder::text:
    case Decoder::unread:
        return {};
    }
    return "no decoder";
}

/// Finds the listing of an entry the engine rejects by design.
///
/// @param archive archive file name
/// @param path archive entry path
/// @return the listing, or null when the entry is not listed
const KnownRejection* known_rejection(std::string_view archive, std::string_view path) {
    const std::string folded_archive = lower(archive);
    const std::string folded_path = lower(path);
    for (const auto& known : known_rejections) {
        if (folded_archive == known.archive && folded_path == known.path) {
            return &known;
        }
    }
    return nullptr;
}

/// Adds an entry of the catch-all row to its extension's counts.
///
/// @param extension lower-case extension with its dot, empty for none
/// @param bytes entry size in bytes
/// @param[in,out] totals sweep counts
void count_unread_extension(const std::string& extension, uint64_t bytes, SweepTotals& totals) {
    for (auto& known : totals.unread_extensions) {
        if (known.extension == extension) {
            ++known.entries;
            known.bytes += bytes;
            return;
        }
    }
    totals.unread_extensions.push_back({extension, 1, bytes});
}

/// Reads the whole decoded content of a file node.
///
/// As HpiArchive::read_node does, a stored entry that ends early reads as
/// zero past its end, and a chunk that cannot be read throws. The storage
/// holds exactly the entry, so a decoder that reads past the entry reads
/// past the allocation, which the sanitizer build reports.
///
/// @param archive mounted archive
/// @param index file node index
/// @return the entry's bytes, as many as the node's size
std::unique_ptr<uint8_t[]> read_entry(const oa::HpiArchive& archive, uint32_t index) {
    const oa::ArchiveNode& node = archive.nodes()[index];
    auto bytes = std::make_unique_for_overwrite<uint8_t[]>(node.size);
    const auto read = archive.read_node_range(index, 0, std::span<uint8_t>(bytes.get(), node.size));
    if (!read.ok()) {
        throw std::runtime_error(std::string(read.error.message) + ": " + node.name);
    }
    if (*read.value < node.size) {
        std::memset(bytes.get() + *read.value, 0, node.size - *read.value);
    }
    return bytes;
}

/// Reads one file record through the archive and decodes it, recording the outcome.
///
/// @param item the entry
/// @param[in,out] pool the sweep's work
/// @param[out] outcome receives the entry's type, counts and report
void sweep_entry(const SweepItem& item, WorkPool& pool, ItemOutcome& outcome) {
    const oa::HpiArchive& archive = *item.archive;
    const std::string& path = item.path;
    outcome.type = content_type_of(path, extension_of(path));
    std::string failure;
    try {
        const auto stored = read_entry(archive, item.index);
        const std::span<const uint8_t> bytes(stored.get(), archive.nodes()[item.index].size);
        outcome.bytes = bytes.size();
        if (bytes.size() != archive.nodes()[item.index].size) {
            failure = "read " + std::to_string(bytes.size()) + " bytes of " +
                      std::to_string(archive.nodes()[item.index].size);
        } else {
            failure = decode_entry(
                content_types[outcome.type].decoder, path, bytes, pool, outcome.sprites
            );
        }
    } catch (const std::exception& error) {
        failure = std::string("archive read: ") + error.what();
    }
    const std::string archive_name = item.archive_path->filename().string();
    const KnownRejection* known = known_rejection(archive_name, path);
    if (failure.empty()) {
        outcome.decoded = true;
        if (known != nullptr) {
            print_to(
                outcome.report,
                Stream::out,
                "note: %s: %s now loads (listed as: %s)\n",
                archive_name.c_str(),
                path.c_str(),
                known->reason
            );
        }
        return;
    }
    if (known != nullptr && failure.starts_with(known->failure)) {
        ++outcome.expected_rejections;
        print_to(
            outcome.report,
            Stream::out,
            "expected: %s: %s: %s (%s)\n",
            archive_name.c_str(),
            path.c_str(),
            failure.c_str(),
            known->reason
        );
        return;
    }
    ++outcome.unexpected_rejections;
    print_to(
        outcome.report,
        Stream::err,
        "FAIL: %s: %s: %s\n",
        archive_name.c_str(),
        path.c_str(),
        failure.c_str()
    );
    if (known != nullptr) {
        print_to(
            outcome.report, Stream::err, "  listed as failing with \"%s...\"\n", known->failure
        );
    }
}

/// Estimates the work of an entry, for starting the costliest entries first.
///
/// @param path archive entry path
/// @param size decoded size in bytes
/// @return the estimate, in the units of ContentType::cost_per_byte
uint64_t estimated_cost(std::string_view path, uint32_t size) {
    const ContentType& type = content_types[content_type_of(path, extension_of(path))];
    return cost_per_entry + static_cast<uint64_t>(type.cost_per_byte) * size;
}

/// Lists the records below one directory node, depth first in stored order.
///
/// @param archive mounted archive
/// @param archive_path host path of the archive
/// @param directory directory node index; 0 is the root
/// @param prefix '/'-terminated path of the directory, empty for the root
/// @param depth directories above this one, bounded by the node count
/// @param[in,out] items receives an item per file record, or one for a directory that lists
///        records outside the archive
void list_directory(
    const oa::HpiArchive& archive,
    const std::filesystem::path& archive_path,
    uint32_t directory,
    const std::string& prefix,
    std::size_t depth,
    std::vector<SweepItem>& items
) {
    const auto nodes = archive.nodes();
    const auto& node = nodes[directory];
    if (depth > nodes.size() || node.first_child > nodes.size() ||
        node.child_count > nodes.size() - node.first_child) {
        items.push_back(
            {ItemKind::broken_directory, &archive, &archive_path, directory, prefix, 0}
        );
        return;
    }
    for (uint32_t i = 0; i < node.child_count; ++i) {
        const uint32_t child = node.first_child + i;
        const std::string path = prefix + nodes[child].name;
        if (nodes[child].directory()) {
            list_directory(archive, archive_path, child, path + "/", depth + 1, items);
        } else {
            items.push_back(
                {ItemKind::entry,
                 &archive,
                 &archive_path,
                 child,
                 path,
                 estimated_cost(path, nodes[child].size)}
            );
        }
    }
}

/// Records a definition load the engine refuses.
///
/// @param what the loader and what it refused
/// @param[in,out] outcome the definitions' counts and report
void definition_failure(const std::string& what, ItemOutcome& outcome) {
    ++outcome.unexpected_rejections;
    print_to(outcome.report, Stream::err, "FAIL: definitions: %s\n", what.c_str());
}

// Counts what the line-of-sight table load hands over.
struct LosTableCount {
    int32_t announced{};
    int32_t received{};
};

/// Takes TABLEINFO's table count.
///
/// @param context LosTableCount
/// @param table_count numtables
/// @return true
bool los_resize(void* context, int16_t table_count) {
    static_cast<LosTableCount*>(context)->announced = table_count;
    return true;
}

/// Counts one table.
///
/// @param context LosTableCount
/// @param index zero-based table index
/// @param section the table's section
/// @param line_count numlines
/// @return true when the section is there and its line count is not negative
bool los_table(
    void* context, int16_t index, const oa::formats::tdf::Block* section, int16_t line_count
) {
    (void)index;
    ++static_cast<LosTableCount*>(context)->received;
    return section != nullptr && line_count >= 0;
}

/// Resets the named-sound count when the sound cache is cleared.
///
/// @param[out] context uint32_t count
void count_cache_clear(void* context) {
    *static_cast<uint32_t*>(context) = 0;
}

/// Counts one named sound.
///
/// @param[in,out] context uint32_t count
/// @param name sound name
/// @param sound WAV name
void count_cache_add(void* context, const char* name, const char* sound) {
    (void)name;
    (void)sound;
    ++*static_cast<uint32_t*>(context);
}

/// Counts one listed file.
///
/// @param[in,out] user uint32_t count
/// @param name file name
void count_listed(void* user, const char* name) {
    (void)name;
    ++*static_cast<uint32_t*>(user);
}

/// Loads every unit of the catalog, header and definition, then the build lists and download menus.
///
/// Each enumerated FBI gives a header; the catalog drops the unavailable and
/// sorts the rest by unit name, and each kept type then takes its definition
/// from the FBI its unitname names.
///
/// @param files file boundary over the game's mounts
/// @param moves loaded movement classes
/// @param weapons loaded weapon table
/// @param sounds loaded sound categories
/// @param[in,out] outcome the definitions' counts and report
void load_units(
    const oa::data::defs::Files& files,
    const oa::data::defs::MoveClassTable& moves,
    const oa::data::defs::WeaponTable& weapons,
    const oa::data::defs::SoundCategoryTable& sounds,
    ItemOutcome& outcome
) {
    // The header pass over every units/*.FBI, the catalog's compaction and
    // name order, then each kept type's FBI by its unit name.
    std::vector<std::string> unit_files;
    files.list(
        files.context,
        "units",
        "FBI",
        [](void* user, const char* name) {
            static_cast<std::vector<std::string>*>(user)->emplace_back(name);
        },
        &unit_files
    );
    oa::data::defs::WeaponTdfSet weapon_files;
    oa::data::defs::weapon_tdf_set_init(&weapon_files);
    if (!oa::data::defs::load_weapon_tdf_set(&files, nullptr, false, &weapon_files)) {
        definition_failure("the weapon files do not list", outcome);
    } else if (weapon_files.count != weapon_files.capacity) {
        definition_failure(
            std::to_string(weapon_files.capacity - weapon_files.count) +
                " weapon files fail to load for the unit header pass",
            outcome
        );
    }
    oa::data::defs::UnitDefTables tables;
    oa::data::defs::unit_def_tables_init(&tables);
    if (!oa::data::defs::unit_def_tables_allocate(
            &tables, static_cast<uint32_t>(unit_files.size() + 1)
        )) {
        definition_failure("the unit catalog does not fit the unit table", outcome);
        oa::data::defs::weapon_tdf_set_free(&weapon_files);
        return;
    }
    const oa::data::defs::UnitHeaderSources header_sources{
        "", &weapon_files, running_build_major, running_build_minor, false, false
    };
    const oa::data::defs::UnitDefSources unit_sources{
        "", &moves, weapons.defs, &sounds, &tables.categories, &tables.blocks, nullptr
    };
    for (std::size_t index = 0; index < unit_files.size(); ++index) {
        char fbi_path[oa::data::defs::path_capacity];
        oa::data::defs::build_variant_path(
            &files, fbi_path, sizeof fbi_path, "units", unit_files[index].c_str(), "FBI", nullptr
        );
        bool refused = false;
        if (!oa::data::defs::load_unit_header(
                &files, fbi_path, tables.records[index + 1], header_sources, &refused
            ))
            definition_failure("unit header " + std::string(fbi_path), outcome);
    }
    tables.count = oa::data::defs::unit_defs_finalize_catalog(tables.records, tables.count);
    uint32_t loaded = 0;
    for (uint32_t type_id = 1; type_id < tables.count; ++type_id) {
        oa::UnitDef& record = tables.records[type_id];
        char fbi_path[oa::data::defs::path_capacity];
        oa::data::defs::build_variant_path(
            &files, fbi_path, sizeof fbi_path, "units", record.unit_name, "FBI", nullptr
        );
        if (!oa::data::defs::load_unit_def(&files, fbi_path, record, unit_sources)) {
            definition_failure("unit definition " + std::string(fbi_path), outcome);
            continue;
        }
        ++loaded;
    }
    if (!oa::data::defs::load_build_lists(&files, nullptr, &tables)) {
        definition_failure("the build lists of gamedata/sidedata.tdf", outcome);
    }
    uint32_t builders = 0;
    for (uint32_t index = 1; index < tables.count; ++index) {
        builders += tables.records[index].build_ids != 0 ? 1 : 0;
    }
    if (!oa::data::defs::load_download_menu(&files, nullptr, &tables)) {
        definition_failure("the download menus", outcome);
    }
    print_to(
        outcome.report,
        Stream::out,
        "  %-26s %8u of %zu (%u builders with a build list, %u download menus)\n",
        "unit types",
        loaded,
        unit_files.size(),
        builders,
        tables.downloads.count
    );
    oa::data::defs::unit_def_tables_free(&tables);
    oa::data::defs::weapon_tdf_set_free(&weapon_files);
}

/// Runs the definition loaders a skirmish start runs over the game's mounts.
///
/// Movement classes, sound categories and the named-sound cache, sides,
/// weapons, line-of-sight tables, then every unit with the build lists and
/// download menus. Any load the engine refuses fails the sweep.
///
/// @param store asset store holding the game's mounts
/// @param[in,out] outcome the definitions' counts and report
void sweep_definitions(const oa::AssetStore& store, ItemOutcome& outcome) {
    const oa::data::defs::Files files = oa::data::defs::asset_store_files(&store);
    print_to(outcome.report, Stream::out, "definitions through the game's mounts:\n");

    auto moves = std::make_unique<oa::data::defs::MoveClassTable>();
    oa::formats::tdf::ParseError move_error{};
    if (!oa::data::defs::load_move_classes(&files, moves.get(), nullptr, &move_error)) {
        definition_failure(
            std::string("gamedata/moveinfo.tdf: ") +
                oa::formats::tdf::parse_status_message(move_error.status),
            outcome
        );
    }
    uint32_t move_classes = 0;
    for (const auto& move_class : moves->classes) {
        move_classes += move_class.name != 0 ? 1 : 0;
    }
    print_to(outcome.report, Stream::out, "  %-26s %8u\n", "movement classes", move_classes);

    oa::data::defs::SoundCategoryTable sounds{};
    if (!oa::data::defs::load_sound_categories(&files, &sounds, nullptr)) {
        definition_failure("gamedata/sound.tdf", outcome);
    }
    uint32_t named_sounds = 0;
    oa::data::defs::SoundCategoryTable startup_sounds{};
    oa::data::defs::load_all_sounds(
        &files, nullptr, {&named_sounds, count_cache_clear, count_cache_add}, &startup_sounds
    );
    if (startup_sounds.count != sounds.count) {
        definition_failure(
            "the start-up sound load and gamedata/sound.tdf disagree on the category count", outcome
        );
    }
    oa::data::defs::sound_category_table_free(&startup_sounds);
    print_to(
        outcome.report,
        Stream::out,
        "  %-26s %8u (%u named sounds)\n",
        "sound categories",
        sounds.count,
        named_sounds
    );

    auto sides = std::make_unique<oa::data::defs::SideTable>();
    if (!oa::data::defs::load_side_data(&files, sides.get(), nullptr, nullptr)) {
        definition_failure(std::string("gamedata/sidedata.tdf: ") + sides->error, outcome);
    }
    print_to(outcome.report, Stream::out, "  %-26s %8u\n", "sides", sides->count);

    uint32_t weapon_files = 0;
    files.list(files.context, "weapons", "tdf", count_listed, &weapon_files);
    auto weapons = std::make_unique<oa::data::defs::WeaponTable>();
    const uint32_t weapons_parsed =
        oa::data::defs::load_weapon_defs(&files, weapons.get(), nullptr);
    if (weapons_parsed != weapon_files) {
        definition_failure(
            std::to_string(weapon_files - weapons_parsed) + " of " + std::to_string(weapon_files) +
                " weapon files fail to parse",
            outcome
        );
    }
    if (weapons->rejected_ids != 0) {
        definition_failure(
            std::to_string(weapons->rejected_ids) + " weapon sections have an ID outside 0..255",
            outcome
        );
    }
    print_to(
        outcome.report,
        Stream::out,
        "  %-26s %8u of %u\n",
        "weapon files",
        weapons_parsed,
        weapon_files
    );

    LosTableCount los{};
    if (!oa::data::defs::load_gamedata_tables(&files, nullptr, {&los, los_resize, los_table})) {
        definition_failure("gamedata/los.tdf", outcome);
    } else if (los.received != los.announced) {
        definition_failure(
            "gamedata/los.tdf announces " + std::to_string(los.announced) + " tables and holds " +
                std::to_string(los.received),
            outcome
        );
    }
    print_to(outcome.report, Stream::out, "  %-26s %8d\n", "line-of-sight tables", los.received);

    load_units(files, *moves, *weapons, sounds, outcome);
    oa::data::defs::weapon_table_free(weapons.get());
    oa::data::defs::sound_category_table_free(&sounds);
}

/// Mounts the archives the game's own scan picks and reports them in lookup order.
///
/// As the engine's start-up does, the directory stands in for the disc root,
/// so the archives past the ten-HPI limit mount last. A candidate that fails
/// to open, or a mount the sweep did not read, fails the sweep.
///
/// @param root installed game directory
/// @param swept host paths of the archives the sweep reads
/// @param[in,out] store empty store over `root`; receives the mounts
/// @param[in,out] outcome the scan's counts and report
void mount_as_the_game(
    const fs::path& root,
    const std::vector<fs::path>& swept,
    oa::AssetStore& store,
    ItemOutcome& outcome
) {
    const fs::path disc_roots[]{root};
    for (const auto& scanned : store.discover(archive_revision, disc_roots)) {
        if (!scanned.mounted && !scanned.already_mounted) {
            ++outcome.unexpected_rejections;
            print_to(
                outcome.report,
                Stream::err,
                "FAIL: the game's scan cannot mount %s: %s\n",
                scanned.path.filename().string().c_str(),
                scanned.error.c_str()
            );
        }
    }
    print_to(outcome.report, Stream::out, "the game's scan mounts, in lookup order:");
    for (const auto& mounted : store.mount_paths()) {
        const std::string name = lower(mounted.filename().string());
        print_to(outcome.report, Stream::out, " %s", mounted.filename().string().c_str());
        const bool read = std::any_of(swept.begin(), swept.end(), [&](const fs::path& path) {
            return lower(path.filename().string()) == name;
        });
        if (!read) {
            ++outcome.unexpected_rejections;
            print_to(
                outcome.report,
                Stream::err,
                "\nFAIL: the game's scan mounts %s, which the sweep did not read\n",
                mounted.filename().string().c_str()
            );
        }
    }
    print_to(outcome.report, Stream::out, "\n");
    if (store.mount_paths().empty()) {
        ++outcome.unexpected_rejections;
        print_to(outcome.report, Stream::err, "FAIL: the game's scan mounts no archive\n");
    }
}

/// What every worker reads: the items and the installation they come from.
struct SweepPlan {
    fs::path root{};                  ///< installed game directory
    std::vector<fs::path> archives{}; ///< host paths of the archives the sweep reads
    std::vector<SweepItem> items{};   ///< in stored order; the game's scan comes last
};

/// Runs one item of the sweep.
///
/// @param plan the sweep's items and installation
/// @param item index of the item in plan.items
/// @param[in,out] pool the sweep's work
/// @param[out] outcome receives the item's counts and report
void run_item(const SweepPlan& plan, std::size_t item, WorkPool& pool, ItemOutcome& outcome) {
    const SweepItem& work = plan.items[item];
    switch (work.kind) {
    case ItemKind::entry:
        sweep_entry(work, pool, outcome);
        return;
    case ItemKind::broken_directory:
        ++outcome.unexpected_rejections;
        print_to(
            outcome.report,
            Stream::err,
            "FAIL: %s: directory %s lists records outside the archive\n",
            work.archive_path->filename().string().c_str(),
            work.path.c_str()
        );
        return;
    case ItemKind::game_mounts: {
        oa::AssetStore game_store(plan.root);
        mount_as_the_game(plan.root, plan.archives, game_store, outcome);
        sweep_definitions(game_store, outcome);
        return;
    }
    }
}

/// Marks one item finished, waking every waiting worker after the last one.
///
/// @param[in,out] pool the sweep's work
void finish_item(WorkPool& pool) {
    bool last = false;
    {
        const std::lock_guard guard(pool.lock);
        last = --pool.unfinished_items == 0;
    }
    if (last) {
        pool.changed.notify_all();
    }
}

/// Returns the offered GAF with the most frames left to take.
///
/// @param offered the GAFs on offer
/// @return that GAF, or null when no offered frame is left
FrameComparison* most_frames_left(const std::vector<FrameComparison*>& offered) {
    FrameComparison* best = nullptr;
    std::size_t best_left = 0;
    for (FrameComparison* comparison : offered) {
        const std::size_t taken = comparison->next_frame.load(std::memory_order_relaxed);
        const std::size_t count = comparison->frames.size();
        const std::size_t left = taken < count ? count - taken : 0;
        if (left > best_left) {
            best = comparison;
            best_left = left;
        }
    }
    return best;
}

/// Takes the offered GAF with the most frames left, as one of its helpers.
///
/// @param[in,out] pool the sweep's work
/// @param wait true to wait until frames are offered or every item has finished
/// @return the GAF, whose helpers now count this worker; null when no offered
///         frame is left, which after waiting means every item has finished
FrameComparison* take_offered_frames(WorkPool& pool, bool wait) {
    std::unique_lock guard(pool.lock);
    FrameComparison* comparison = most_frames_left(pool.offered);
    if (comparison == nullptr && wait) {
        pool.changed.wait(guard, [&] {
            comparison = most_frames_left(pool.offered);
            return comparison != nullptr || pool.unfinished_items == 0;
        });
    }
    if (comparison != nullptr) {
        ++comparison->helpers;
    }
    return comparison;
}

/// Compares frames of a GAF taken with take_offered_frames, then leaves its helpers.
///
/// @param[in,out] pool the sweep's work
/// @param[in,out] comparison the GAF's frames; receives their results
void help_with_frames(WorkPool& pool, FrameComparison& comparison) noexcept {
    compare_frames(comparison);
    {
        const std::lock_guard guard(pool.lock);
        --comparison.helpers;
    }
    pool.changed.notify_all();
}

/// Runs the sweep's work on one thread until every item has finished.
///
/// Between items the worker first helps compare the frames of any GAF on
/// offer, then takes the largest item left; once none is left to start, it
/// waits for frames to help with until every item has finished.
///
/// @param plan the sweep's items and installation
/// @param[in,out] pool the sweep's work
/// @param[out] outcomes one per item; this worker writes those of the items it runs
void work(const SweepPlan& plan, WorkPool& pool, std::vector<ItemOutcome>& outcomes) noexcept {
    for (;;) {
        if (FrameComparison* frames = take_offered_frames(pool, false)) {
            help_with_frames(pool, *frames);
            continue;
        }
        const std::size_t position = pool.next_item.fetch_add(1, std::memory_order_relaxed);
        if (position < pool.order.size()) {
            const std::size_t item = pool.order[position];
            try {
                run_item(plan, item, pool, outcomes[item]);
            } catch (...) {
                outcomes[item].stopped = std::current_exception();
            }
            finish_item(pool);
            continue;
        }
        FrameComparison* frames = take_offered_frames(pool, true);
        if (frames == nullptr) {
            return;
        }
        help_with_frames(pool, *frames);
    }
}

/// Runs every item of the sweep on a number of threads, the calling one included.
///
/// When the system refuses a thread, the sweep runs on those it has.
///
/// @param plan the sweep's items and installation
/// @param thread_count threads to run on, at least 1
/// @return one outcome per item
std::vector<ItemOutcome> run_sweep(const SweepPlan& plan, unsigned thread_count) {
    std::vector<ItemOutcome> outcomes(plan.items.size());
    WorkPool pool;
    pool.order.resize(plan.items.size());
    for (std::size_t i = 0; i < pool.order.size(); ++i) {
        pool.order[i] = i;
    }
    std::stable_sort(pool.order.begin(), pool.order.end(), [&](std::size_t a, std::size_t b) {
        return plan.items[a].cost > plan.items[b].cost;
    });
    pool.unfinished_items = plan.items.size();
    std::vector<std::thread> threads;
    // Reserved before any thread starts, so that adding one never reallocates.
    threads.reserve(thread_count - 1);
    for (unsigned i = 1; i < thread_count; ++i) {
        try {
            threads.emplace_back(work, std::cref(plan), std::ref(pool), std::ref(outcomes));
        } catch (const std::exception&) {
            break;
        }
    }
    work(plan, pool, outcomes);
    for (auto& thread : threads) {
        thread.join();
    }
    return outcomes;
}

/// Returns what an exception says.
///
/// @param error a caught exception
/// @return its message, or an empty string for one not derived from std::exception
std::string exception_text(const std::exception_ptr& error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& caught) {
        return caught.what();
    } catch (...) {
        return {};
    }
}

/// Adds one item's outcome to the sweep's counts and prints its report.
///
/// @param item the item
/// @param outcome what the item found
/// @param[in,out] totals sweep counts
void report_item(const SweepItem& item, const ItemOutcome& outcome, SweepTotals& totals) {
    print_report(outcome.report);
    totals.expected_rejections += outcome.expected_rejections;
    totals.unexpected_rejections += outcome.unexpected_rejections;
    if (outcome.stopped) {
        ++totals.unexpected_rejections;
        const std::string text = exception_text(outcome.stopped);
        const std::string said = text.empty() ? std::string() : ": " + text;
        if (item.kind == ItemKind::game_mounts) {
            std::fprintf(
                stderr,
                "FAIL: the game's scan or a definition load ended with an exception%s\n",
                said.c_str()
            );
        } else {
            std::fprintf(
                stderr,
                "FAIL: %s: %s: its sweep ended with an exception%s\n",
                item.archive_path->filename().string().c_str(),
                item.path.c_str(),
                said.c_str()
            );
        }
    }
    if (item.kind != ItemKind::entry) {
        return;
    }
    TypeTotals& counts = totals.types[outcome.type];
    ++counts.entries;
    if (content_types[outcome.type].decoder == Decoder::unread) {
        count_unread_extension(
            extension_of(item.path), item.archive->nodes()[item.index].size, totals
        );
    }
    counts.bytes += outcome.bytes;
    if (outcome.decoded) {
        ++counts.decoded;
    } else {
        ++counts.rejected;
    }
    totals.sprites.frames += outcome.sprites.frames;
    totals.sprites.special += outcome.sprites.special;
    totals.sprites.mismatched += outcome.sprites.mismatched;
}

/// Lists the archives the game would consider in its directory, in name order.
///
/// @param root installed game directory
/// @return host paths of every regular file with an archive extension
std::vector<fs::path> installed_archives(const fs::path& root) {
    std::vector<fs::path> archives;
    for (const auto& item : fs::directory_iterator(root)) {
        const std::string extension = lower(item.path().extension().string());
        const bool archive_extension =
            std::find(std::begin(archive_extensions), std::end(archive_extensions), extension) !=
            std::end(archive_extensions);
        if (item.is_regular_file() && archive_extension) {
            archives.push_back(item.path());
        }
    }
    std::sort(archives.begin(), archives.end(), [](const fs::path& a, const fs::path& b) {
        return lower(a.filename().string()) < lower(b.filename().string());
    });
    return archives;
}

/// Prints the per-type summary.
///
/// @param totals sweep counts
void print_summary(const SweepTotals& totals) {
    std::printf("%-34s %8s %8s %8s %14s\n", "type", "entries", "decoded", "rejected", "bytes");
    uint64_t entries = 0;
    uint64_t bytes = 0;
    for (std::size_t i = 0; i < content_type_count; ++i) {
        const TypeTotals& t = totals.types[i];
        if (t.entries == 0) {
            continue;
        }
        entries += t.entries;
        bytes += t.bytes;
        std::printf(
            "%-34s %8llu %8llu %8llu %14llu\n",
            content_types[i].label,
            static_cast<unsigned long long>(t.entries),
            static_cast<unsigned long long>(t.decoded),
            static_cast<unsigned long long>(t.rejected),
            static_cast<unsigned long long>(t.bytes)
        );
    }
    std::printf(
        "%-34s %8llu %8s %8s %14llu\n",
        "all entries",
        static_cast<unsigned long long>(entries),
        "",
        "",
        static_cast<unsigned long long>(bytes)
    );
    std::vector<ExtensionTotals> unread = totals.unread_extensions;
    std::sort(unread.begin(), unread.end(), [](const ExtensionTotals& a, const ExtensionTotals& b) {
        return a.extension < b.extension;
    });
    std::printf("read whole, not decoded, by extension:\n");
    for (const auto& row : unread) {
        std::printf(
            "  %-32s %8llu %32llu\n",
            row.extension.empty() ? "(none)" : row.extension.c_str(),
            static_cast<unsigned long long>(row.entries),
            static_cast<unsigned long long>(row.bytes)
        );
    }
    std::printf(
        "sprite frames: %llu (%llu left undrawn for their blended children, %llu mismatched)\n",
        static_cast<unsigned long long>(totals.sprites.frames),
        static_cast<unsigned long long>(totals.sprites.special),
        static_cast<unsigned long long>(totals.sprites.mismatched)
    );
}

/// Returns how many threads the sweep runs on.
///
/// @param[out] count OA_TEST_THREADS when it names a count, otherwise one per logical core; at
///        least 1
/// @return false when OA_TEST_THREADS holds anything but a count from 0 to max_threads
bool sweep_thread_count(unsigned& count) {
    const auto named_value = oa::platform::environment_value(threads_variable);
    const char* named = named_value ? named_value->c_str() : nullptr;
    unsigned long requested = 0;
    if (named != nullptr && *named != '\0') {
        char* end = nullptr;
        requested = std::strtoul(named, &end, 10);
        if (*named < '0' || *named > '9' || *end != '\0' || requested > max_threads) {
            std::fprintf(
                stderr,
                "FAIL: %s is \"%s\"; set it to a number of threads from 1 to %lu, or 0 or "
                "nothing for one per logical core\n",
                threads_variable,
                named,
                max_threads
            );
            return false;
        }
    }
    count = requested != 0 ? static_cast<unsigned>(requested)
                           : std::max(1U, std::thread::hardware_concurrency());
    return true;
}

} // namespace

int main() {
    const fs::path root = oa::test::require_game_directory("the installed-content sweep");
    unsigned thread_count = 1;
    if (!sweep_thread_count(thread_count)) {
        return 1;
    }
    const auto started = std::chrono::steady_clock::now();
    SweepPlan plan{root, installed_archives(root), {}};
    if (plan.archives.empty()) {
        std::fprintf(
            stderr, "FAIL: %s holds no .hpi, .ufo, .ccx or .gp3 archive\n", root.string().c_str()
        );
        return 1;
    }
    oa::AssetStore store(root);
    SweepTotals totals;
    for (const auto& path : plan.archives) {
        std::string error;
        if (!store.try_mount(path, &error)) {
            ++totals.unexpected_rejections;
            std::fprintf(
                stderr,
                "FAIL: %s does not mount: %s\n",
                path.filename().string().c_str(),
                error.c_str()
            );
        }
    }
    for (std::size_t mount = 0; mount < store.mount_paths().size(); ++mount) {
        ++totals.archives;
        list_directory(
            store.mounted(mount), store.mount_paths()[mount], 0, std::string(), 0, plan.items
        );
    }
    // The game's scan and the definition loaders read a store of their own
    // and start first, beside the entries.
    plan.items.push_back(
        {ItemKind::game_mounts,
         nullptr,
         nullptr,
         0,
         std::string(),
         std::numeric_limits<uint64_t>::max()}
    );
    const std::vector<ItemOutcome> outcomes = run_sweep(plan, thread_count);
    const std::size_t game_mounts = plan.items.size() - 1;
    for (std::size_t item = 0; item < game_mounts; ++item) {
        report_item(plan.items[item], outcomes[item], totals);
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf(
        "%s: %llu of %zu archives mounted, swept in %.1f s\n",
        root.string().c_str(),
        static_cast<unsigned long long>(totals.archives),
        plan.archives.size(),
        seconds
    );
    print_summary(totals);
    report_item(plan.items[game_mounts], outcomes[game_mounts], totals);
    std::printf(
        "expected rejections: %llu, unexpected: %llu\n",
        static_cast<unsigned long long>(totals.expected_rejections),
        static_cast<unsigned long long>(totals.unexpected_rejections)
    );
    return totals.unexpected_rejections == 0 && totals.sprites.mismatched == 0 ? 0 : 1;
}
