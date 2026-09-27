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
#include "oa/data/unit_definitions.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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

struct ContentType {
    Decoder decoder{};
    const char* label{};
    const char* extension{}; // lower case with its dot; null for the catch-all row
    const char*
        directory{}; // lower-case directory the row is limited to, with its '/'; null for any
};

// One row per summary line; the last row takes every other extension.
constexpr ContentType content_types[] = {
    {Decoder::gaf, "GAF animation", ".gaf"},
    {Decoder::pcx, "PCX image", ".pcx"},
    {Decoder::tnt, "TNT terrain", ".tnt"},
    {Decoder::ota, "OTA map", ".ota"},
    {Decoder::tdf, "TDF definitions", ".tdf"},
    {Decoder::fbi, "FBI unit", ".fbi"},
    {Decoder::gui, "GUI layout", ".gui"},
    {Decoder::cob, "COB script", ".cob"},
    {Decoder::model, "3DO model", ".3do"},
    {Decoder::fnt, "FNT font", ".fnt"},
    {Decoder::palette, "PAL palette", ".pal"},
    {Decoder::alpha_table, "ALP alpha table", ".alp"},
    {Decoder::shade_table, "SHD shade table", ".shd"},
    {Decoder::light_table, "LHT light table", ".lht"},
    {Decoder::wave, "WAV sound", ".wav"},
    {Decoder::ai_profile, "AI profile (script reader)", ".txt", "ai/"},
    {Decoder::text, "TXT text (read whole)", ".txt"},
    {Decoder::unread, "other (read whole, not decoded)", nullptr},
};
constexpr std::size_t content_type_count = std::size(content_types);

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
/// @param sprite relocated frame
/// @param fill canvas fill value
/// @return the canvas pixels
std::vector<uint8_t> draw_over(const oa::Sprite& sprite, uint8_t fill) {
    auto canvas = oa::present::create_surface(sprite.width, sprite.height);
    std::fill(canvas.pixels.begin(), canvas.pixels.end(), fill);
    oa::present::draw_sprite(&canvas.surface, &sprite, sprite.origin_x, sprite.origin_y);
    return canvas.pixels;
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
/// draw, which reads the display's alpha table, is counted and not drawn; any
/// other render failure is a mismatch.
///
/// @param sprite relocated frame
/// @param model the GAF reader's frame
/// @param[in,out] totals frame counts
/// @return an empty string when they agree, otherwise what differs
std::string compare_frame(
    const oa::Sprite& sprite, const oa::formats::gaf::Frame& model, SpriteTotals& totals
) {
    ++totals.frames;
    if (sprite.width != model.width || sprite.height != model.height ||
        sprite.origin_x != model.origin_x || sprite.origin_y != model.origin_y ||
        sprite.key != model.transparency_index || sprite.child_count != model.layer_count) {
        ++totals.mismatched;
        return "relocated frame header differs from the GAF reader";
    }
    const auto rendered = oa::formats::gaf::render_normal(model);
    if (!rendered.ok()) {
        const bool blended =
            rendered.error.has_value() &&
            rendered.error->code == oa::formats::gaf::ErrorCode::unsupported_special_render &&
            has_blended_child(model);
        if (!blended) {
            ++totals.mismatched;
            return "GAF reader cannot render the frame: " +
                   (rendered.error ? rendered.error->message : std::string("no frame"));
        }
        ++totals.special;
        return {};
    }
    if (sprite.width == 0 || sprite.height == 0) {
        return {};
    }
    const auto low = draw_over(sprite, canvas_low);
    const auto high = draw_over(sprite, canvas_high);
    const auto& frame = *rendered.frame;
    for (std::size_t i = 0; i < low.size(); ++i) {
        const bool drawn = low[i] == high[i];
        if (drawn != (frame.coverage[i] != 0) || (drawn && low[i] != frame.pixels[i])) {
            ++totals.mismatched;
            return "drawn frame differs from the GAF reader at pixel " + std::to_string(i);
        }
    }
    return {};
}

/// Decodes a GAF with the GAF reader and the sprite loader, and draws every frame.
///
/// A GAF none of whose frames could be compared fails. GUI fonts are also
/// loaded as the frontend loads them.
///
/// @param path archive entry path
/// @param bytes entry bytes
/// @param[in,out] totals frame counts
/// @return an empty string on success, otherwise the first failure
std::string
decode_gaf(std::string_view path, std::span<const uint8_t> bytes, SpriteTotals& totals) {
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
    const SpriteTotals before = totals;
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
            std::string difference =
                compare_frame(*sprite, model.frames[static_cast<std::size_t>(k)], totals);
            if (!difference.empty()) {
                return difference;
            }
        }
    }
    const uint64_t frames = totals.frames - before.frames;
    if (frames != 0 && totals.special - before.special == frames) {
        return "no frame of the GAF could be compared: every one needs the blended child draw";
    }
    if (is_gui_font(path)) {
        try {
            (void)oa::formats::fnt::parse_gaf(bytes);
        } catch (const std::exception& error) {
            return std::string("GUI font: ") + error.what();
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
    try {
        const auto decoded = oa::decode_pcx(bytes);
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
    } catch (const std::exception& error) {
        return std::string("PCX image reader: ") + error.what();
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
/// @param[in,out] sprites frame counts of the GAF comparison
/// @return an empty string on success, otherwise the first failure
std::string decode_entry(
    Decoder decoder, std::string_view path, std::span<const uint8_t> bytes, SpriteTotals& sprites
) {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    switch (decoder) {
    case Decoder::gaf:
        return decode_gaf(path, bytes, sprites);
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
        const auto definition = oa::data::unit_definitions::load_fbi(text, std::string(path));
        return definition ? std::string() : "FBI loader: " + definition.error.message;
    }
    case Decoder::gui: {
        // Screens load layouts with the GUI reader alone, which keeps the
        // last gadget of a file that ends without its closing braces.
        const auto parsed = oa::ui::gui_layout::parse(bytes);
        return parsed.ok() ? std::string() : "GUI layout reader: " + parsed.error->message;
    }
    case Decoder::cob: {
        const auto parsed = oa::formats::cob::parse_cob(bytes);
        return parsed ? std::string() : "COB reader: " + parsed.error;
    }
    case Decoder::model:
        try {
            (void)oa::formats::objects3d::load_3do(std::as_bytes(bytes));
        } catch (const std::exception& error) {
            return std::string("3DO reader: ") + error.what();
        }
        return {};
    case Decoder::fnt:
        try {
            (void)oa::formats::fnt::parse_fnt(bytes);
        } catch (const std::exception& error) {
            return std::string("FNT reader: ") + error.what();
        }
        return {};
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

/// Reads one file record through the archive and decodes it, counting the outcome.
///
/// @param archive mounted archive
/// @param index file node index
/// @param path '/'-joined path of the record
/// @param[in,out] totals sweep counts
void sweep_entry(
    const oa::HpiArchive& archive, uint32_t index, const std::string& path, SweepTotals& totals
) {
    const std::string extension = extension_of(path);
    const std::size_t type = content_type_of(path, extension);
    TypeTotals& counts = totals.types[type];
    ++counts.entries;
    if (content_types[type].decoder == Decoder::unread) {
        count_unread_extension(extension, archive.nodes()[index].size, totals);
    }
    std::string failure;
    try {
        const std::vector<uint8_t> bytes = archive.read_node(index);
        counts.bytes += bytes.size();
        if (bytes.size() != archive.nodes()[index].size) {
            failure = "read " + std::to_string(bytes.size()) + " bytes of " +
                      std::to_string(archive.nodes()[index].size);
        } else {
            failure = decode_entry(content_types[type].decoder, path, bytes, totals.sprites);
        }
    } catch (const std::exception& error) {
        failure = std::string("archive read: ") + error.what();
    }
    const std::string archive_name = archive.path().filename().string();
    const KnownRejection* known = known_rejection(archive_name, path);
    if (failure.empty()) {
        ++counts.decoded;
        if (known != nullptr) {
            std::printf(
                "note: %s: %s now loads (listed as: %s)\n",
                archive_name.c_str(),
                path.c_str(),
                known->reason
            );
        }
        return;
    }
    ++counts.rejected;
    if (known != nullptr && failure.starts_with(known->failure)) {
        ++totals.expected_rejections;
        std::printf(
            "expected: %s: %s: %s (%s)\n",
            archive_name.c_str(),
            path.c_str(),
            failure.c_str(),
            known->reason
        );
        return;
    }
    ++totals.unexpected_rejections;
    std::fprintf(stderr, "FAIL: %s: %s: %s\n", archive_name.c_str(), path.c_str(), failure.c_str());
    if (known != nullptr) {
        std::fprintf(stderr, "  listed as failing with \"%s...\"\n", known->failure);
    }
}

/// Sweeps the records below one directory node, depth first in stored order.
///
/// @param archive mounted archive
/// @param directory directory node index; 0 is the root
/// @param prefix '/'-terminated path of the directory, empty for the root
/// @param depth directories above this one, bounded by the node count
/// @param[in,out] totals sweep counts
void sweep_directory(
    const oa::HpiArchive& archive,
    uint32_t directory,
    const std::string& prefix,
    std::size_t depth,
    SweepTotals& totals
) {
    const auto nodes = archive.nodes();
    const auto& node = nodes[directory];
    if (depth > nodes.size() || node.first_child > nodes.size() ||
        node.child_count > nodes.size() - node.first_child) {
        ++totals.unexpected_rejections;
        std::fprintf(
            stderr,
            "FAIL: %s: directory %s lists records outside the archive\n",
            archive.path().filename().string().c_str(),
            prefix.c_str()
        );
        return;
    }
    for (uint32_t i = 0; i < node.child_count; ++i) {
        const uint32_t child = node.first_child + i;
        const std::string path = prefix + nodes[child].name;
        if (nodes[child].directory()) {
            sweep_directory(archive, child, path + "/", depth + 1, totals);
        } else {
            sweep_entry(archive, child, path, totals);
        }
    }
}

/// Records a definition load the engine refuses.
///
/// @param what the loader and what it refused
/// @param[in,out] totals sweep counts
void definition_failure(const std::string& what, SweepTotals& totals) {
    ++totals.unexpected_rejections;
    std::fprintf(stderr, "FAIL: definitions: %s\n", what.c_str());
}

// Lists and reads unit files through an asset store for the unit catalog.
class StoreCatalogReader final : public oa::data::unit_definitions::CatalogAssetReader {
  public:

    /// Wraps a store.
    ///
    /// @param store asset store; must outlive the reader
    explicit StoreCatalogReader(const oa::AssetStore& store) : store_(store) {}

    /// Lists a directory's winning files with an extension.
    ///
    /// @param directory resource directory
    /// @param extension required suffix
    /// @return the logical paths, or the store's error
    oa::data::unit_definitions::Result<std::vector<std::string>>
    list_effective(std::string_view directory, std::string_view extension) const override {
        try {
            return {store_.list_effective(directory, extension), {}};
        } catch (const std::exception& error) {
            return {{}, {oa::data::unit_definitions::ErrorCode::io, 0, error.what()}};
        }
    }

    /// Reads a file's text.
    ///
    /// @param path logical path
    /// @return the text, or the store's error
    oa::data::unit_definitions::Result<std::string> read(std::string_view path) const override {
        try {
            const auto bytes = store_.read(path).bytes;
            return {std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()), {}};
        } catch (const std::exception& error) {
            return {{}, {oa::data::unit_definitions::ErrorCode::io, 0, error.what()}};
        }
    }

  private:

    const oa::AssetStore& store_;
};

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
/// Each type takes its header from the enumerated FBI, then its definition
/// from the FBI its unitname names.
///
/// @param files file boundary over the game's mounts
/// @param store the same mounts, for the catalog
/// @param moves loaded movement classes
/// @param weapons loaded weapon table
/// @param sounds loaded sound categories
/// @param[in,out] totals sweep counts
void load_units(
    const oa::data::defs::Files& files,
    const oa::AssetStore& store,
    const oa::data::defs::MoveClassTable& moves,
    const oa::data::defs::WeaponTable& weapons,
    const oa::data::defs::SoundCategoryTable& sounds,
    SweepTotals& totals
) {
    const StoreCatalogReader reader(store);
    const auto catalog = oa::data::unit_definitions::load_unit_catalog(reader);
    if (!catalog) {
        definition_failure("unit catalog: " + catalog.error.message, totals);
        return;
    }
    oa::data::defs::WeaponTdfSet weapon_files;
    oa::data::defs::weapon_tdf_set_init(&weapon_files);
    if (!oa::data::defs::load_weapon_tdf_set(&files, nullptr, false, &weapon_files)) {
        definition_failure("the weapon files do not list", totals);
    } else if (weapon_files.count != weapon_files.capacity) {
        definition_failure(
            std::to_string(weapon_files.capacity - weapon_files.count) +
                " weapon files fail to load for the unit header pass",
            totals
        );
    }
    oa::data::defs::UnitDefTables tables;
    oa::data::defs::unit_def_tables_init(&tables);
    if (!oa::data::defs::unit_def_tables_allocate(
            &tables, static_cast<uint32_t>(catalog.value.entries.size() + 1)
        )) {
        definition_failure("the unit catalog does not fit the unit table", totals);
        oa::data::defs::weapon_tdf_set_free(&weapon_files);
        return;
    }
    const oa::data::defs::UnitHeaderSources header_sources{
        "", &weapon_files, running_build_major, running_build_minor, false, false
    };
    const oa::data::defs::UnitDefSources unit_sources{
        "", &moves, weapons.defs, &sounds, &tables.categories, &tables.blocks, nullptr
    };
    uint32_t loaded = 0;
    for (std::size_t index = 0; index < catalog.value.entries.size(); ++index) {
        const auto& logical_path = catalog.value.entries[index].logical_path;
        const auto type_id = static_cast<uint16_t>(index + 1);
        oa::UnitDef& record = tables.records[type_id];
        record.type_id = type_id;
        const auto slash = logical_path.find_last_of("/\\");
        const std::string file_name =
            logical_path.substr(slash == std::string::npos ? 0 : slash + 1);
        char fbi_path[oa::data::defs::path_capacity];
        oa::data::defs::build_variant_path(
            &files, fbi_path, sizeof fbi_path, "units", file_name.c_str(), "FBI", nullptr
        );
        bool refused = false;
        if (!oa::data::defs::load_unit_header(&files, fbi_path, record, header_sources, &refused)) {
            definition_failure("unit header " + std::string(fbi_path), totals);
            continue;
        }
        oa::data::defs::build_variant_path(
            &files, fbi_path, sizeof fbi_path, "units", record.unit_name, "FBI", nullptr
        );
        if (!oa::data::defs::load_unit_def(&files, fbi_path, record, unit_sources)) {
            definition_failure("unit definition " + std::string(fbi_path), totals);
            continue;
        }
        ++loaded;
    }
    if (!oa::data::defs::load_build_lists(&files, nullptr, &tables)) {
        definition_failure("the build lists of gamedata/sidedata.tdf", totals);
    }
    uint32_t builders = 0;
    for (uint32_t index = 1; index < tables.count; ++index) {
        builders += tables.records[index].build_ids != 0 ? 1 : 0;
    }
    if (!oa::data::defs::load_download_menu(&files, nullptr, &tables)) {
        definition_failure("the download menus", totals);
    }
    std::printf(
        "  %-26s %8u of %zu (%u builders with a build list, %u download menus)\n",
        "unit types",
        loaded,
        catalog.value.entries.size(),
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
/// @param[in,out] totals sweep counts
void sweep_definitions(const oa::AssetStore& store, SweepTotals& totals) {
    const oa::data::defs::Files files = oa::data::defs::asset_store_files(&store);
    std::printf("definitions through the game's mounts:\n");

    auto moves = std::make_unique<oa::data::defs::MoveClassTable>();
    oa::formats::tdf::ParseError move_error{};
    if (!oa::data::defs::load_move_classes(&files, moves.get(), nullptr, &move_error)) {
        definition_failure(
            std::string("gamedata/moveinfo.tdf: ") +
                oa::formats::tdf::parse_status_message(move_error.status),
            totals
        );
    }
    uint32_t move_classes = 0;
    for (const auto& move_class : moves->classes) {
        move_classes += move_class.name != 0 ? 1 : 0;
    }
    std::printf("  %-26s %8u\n", "movement classes", move_classes);

    oa::data::defs::SoundCategoryTable sounds{};
    if (!oa::data::defs::load_sound_categories(&files, &sounds, nullptr)) {
        definition_failure("gamedata/sound.tdf", totals);
    }
    uint32_t named_sounds = 0;
    oa::data::defs::SoundCategoryTable startup_sounds{};
    oa::data::defs::load_all_sounds(
        &files, nullptr, {&named_sounds, count_cache_clear, count_cache_add}, &startup_sounds
    );
    if (startup_sounds.count != sounds.count) {
        definition_failure(
            "the start-up sound load and gamedata/sound.tdf disagree on the category count", totals
        );
    }
    oa::data::defs::sound_category_table_free(&startup_sounds);
    std::printf("  %-26s %8u (%u named sounds)\n", "sound categories", sounds.count, named_sounds);

    auto sides = std::make_unique<oa::data::defs::SideTable>();
    if (!oa::data::defs::load_side_data(&files, sides.get(), nullptr, nullptr)) {
        definition_failure(std::string("gamedata/sidedata.tdf: ") + sides->error, totals);
    }
    std::printf("  %-26s %8u\n", "sides", sides->count);

    uint32_t weapon_files = 0;
    files.list(files.context, "weapons", "tdf", count_listed, &weapon_files);
    auto weapons = std::make_unique<oa::data::defs::WeaponTable>();
    const uint32_t weapons_parsed =
        oa::data::defs::load_weapon_defs(&files, weapons.get(), nullptr);
    if (weapons_parsed != weapon_files) {
        definition_failure(
            std::to_string(weapon_files - weapons_parsed) + " of " + std::to_string(weapon_files) +
                " weapon files fail to parse",
            totals
        );
    }
    if (weapons->rejected_ids != 0) {
        definition_failure(
            std::to_string(weapons->rejected_ids) + " weapon sections have an ID outside 0..255",
            totals
        );
    }
    std::printf("  %-26s %8u of %u\n", "weapon files", weapons_parsed, weapon_files);

    LosTableCount los{};
    if (!oa::data::defs::load_gamedata_tables(&files, nullptr, {&los, los_resize, los_table})) {
        definition_failure("gamedata/los.tdf", totals);
    } else if (los.received != los.announced) {
        definition_failure(
            "gamedata/los.tdf announces " + std::to_string(los.announced) + " tables and holds " +
                std::to_string(los.received),
            totals
        );
    }
    std::printf("  %-26s %8d\n", "line-of-sight tables", los.received);

    load_units(files, store, *moves, *weapons, sounds, totals);
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
/// @param swept host paths of the archives the sweep read
/// @param[in,out] store empty store over `root`; receives the mounts
/// @param[in,out] totals sweep counts
void mount_as_the_game(
    const fs::path& root,
    const std::vector<fs::path>& swept,
    oa::AssetStore& store,
    SweepTotals& totals
) {
    const fs::path disc_roots[]{root};
    for (const auto& outcome : store.discover(archive_revision, disc_roots)) {
        if (!outcome.mounted && !outcome.already_mounted) {
            ++totals.unexpected_rejections;
            std::fprintf(
                stderr,
                "FAIL: the game's scan cannot mount %s: %s\n",
                outcome.path.filename().string().c_str(),
                outcome.error.c_str()
            );
        }
    }
    std::printf("the game's scan mounts, in lookup order:");
    for (const auto& mounted : store.mount_paths()) {
        const std::string name = lower(mounted.filename().string());
        std::printf(" %s", mounted.filename().string().c_str());
        const bool read = std::any_of(swept.begin(), swept.end(), [&](const fs::path& path) {
            return lower(path.filename().string()) == name;
        });
        if (!read) {
            ++totals.unexpected_rejections;
            std::fprintf(
                stderr,
                "\nFAIL: the game's scan mounts %s, which the sweep did not read\n",
                mounted.filename().string().c_str()
            );
        }
    }
    std::printf("\n");
    if (store.mount_paths().empty()) {
        ++totals.unexpected_rejections;
        std::fprintf(stderr, "FAIL: the game's scan mounts no archive\n");
    }
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

} // namespace

int main() {
    const fs::path root = oa::test::require_game_directory("the installed-content sweep");
    const auto started = std::chrono::steady_clock::now();
    const std::vector<fs::path> archives = installed_archives(root);
    if (archives.empty()) {
        std::fprintf(
            stderr, "FAIL: %s holds no .hpi, .ufo, .ccx or .gp3 archive\n", root.string().c_str()
        );
        return 1;
    }
    oa::AssetStore store(root);
    SweepTotals totals;
    for (const auto& path : archives) {
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
        sweep_directory(store.mounted(mount), 0, std::string(), 0, totals);
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf(
        "%s: %llu of %zu archives mounted, swept in %.1f s\n",
        root.string().c_str(),
        static_cast<unsigned long long>(totals.archives),
        archives.size(),
        seconds
    );
    print_summary(totals);
    oa::AssetStore game_store(root);
    mount_as_the_game(root, archives, game_store, totals);
    sweep_definitions(game_store, totals);
    std::printf(
        "expected rejections: %llu, unexpected: %llu\n",
        static_cast<unsigned long long>(totals.expected_rejections),
        static_cast<unsigned long long>(totals.unexpected_rejections)
    );
    return totals.unexpected_rejections == 0 && totals.sprites.mismatched == 0 ? 0 : 1;
}
