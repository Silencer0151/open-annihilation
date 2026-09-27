// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// GUI art for the gadget draw host: the frames of parsed GAF files as present
// sprites (raw frames stay keyed, compressed and layered frames become row
// runs that keep their exact coverage), the GUI fonts loaded as the frontend
// does at startup, and the palette tables the draws read.
#include "dialog_internal.hpp"

#include "oa/formats/gaf.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <string>

namespace oa::ui::frontend_dialogs {

namespace {

constexpr const char* kGuiArtDirectory = "anims"; // set_gaf_path appends the separator
constexpr const char* kCommonGuiArt = "commongui";
constexpr const char* kLightTable = "palettes/palette.lht";
constexpr const char* kDefaultFont = "fonts/comix.fnt";
// GUI font slots filled at startup: the caption font, then the
// label font.
constexpr const char* kGuiFonts[] = {"hattfont12", "hattfont11"};
constexpr size_t kTableBytes = 32 * 256;

// Run limits of the row-run stream the sprite draws decode.
constexpr size_t kSkipRun = 0x7F;
constexpr size_t kLiteralRun = 0x40;
constexpr uint8_t kSkipCommand = 1;
constexpr int32_t kLiteralShift = 2;

std::string fold_path(std::string_view path) {
    std::string folded(path);
    for (char& c : folded)
        c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return folded;
}

// One row of covered pixels as literal runs and uncovered ones as skips,
// after its little-endian byte count. The row decoder reads a row's commands
// only inside that count and stops at a row whose commands do not cover the
// width, so an uncovered tail is written out as skips too.
void encode_row(
    std::vector<uint8_t>& out, const uint8_t* pixels, const uint8_t* coverage, size_t width
) {
    const size_t header = out.size();
    out.push_back(0);
    out.push_back(0);
    size_t x = 0;
    while (x < width) {
        size_t run = 0;
        if (coverage[x] == 0) {
            while (x + run < width && run < kSkipRun && coverage[x + run] == 0)
                ++run;
            out.push_back(static_cast<uint8_t>(run << 1 | kSkipCommand));
        } else {
            while (x + run < width && run < kLiteralRun && coverage[x + run] != 0)
                ++run;
            out.push_back(static_cast<uint8_t>((run - 1) << kLiteralShift));
            out.insert(out.end(), pixels + x, pixels + x + run);
        }
        x += run;
    }
    const size_t length = out.size() - header - 2;
    out[header] = static_cast<uint8_t>(length);
    out[header + 1] = static_cast<uint8_t>(length >> 8);
}

void encode_frame(
    present::SpriteBuffer& out,
    const std::vector<uint8_t>& pixels,
    const std::vector<uint8_t>& coverage,
    uint16_t width,
    uint16_t height
) {
    out.pixels.clear();
    for (size_t row = 0; row < height; ++row)
        encode_row(out.pixels, pixels.data() + row * width, coverage.data() + row * width, width);
    out.sprite.encoding = OA_SPRITE_ROW_RLE;
}

} // namespace

bool convert_gaf_frame(const formats::gaf::Frame& frame, present::SpriteBuffer& out) {
    out = {};
    out.sprite.width = frame.width;
    out.sprite.height = frame.height;
    out.sprite.origin_x = frame.origin_x;
    out.sprite.origin_y = frame.origin_y;
    out.sprite.key = frame.transparency_index;
    const size_t area = static_cast<size_t>(frame.width) * frame.height;
    if (!frame.layers.empty()) {
        const auto rendered = formats::gaf::render_normal(frame);
        if (!rendered.ok())
            return false;
        encode_frame(
            out, rendered.frame->pixels, rendered.frame->coverage, frame.width, frame.height
        );
    } else if (frame.pixels.size() != area || frame.coverage.size() != area) {
        return false;
    } else if (frame.compressed) {
        encode_frame(out, frame.pixels, frame.coverage, frame.width, frame.height);
    } else {
        out.pixels = frame.pixels;
        out.sprite.encoding = OA_SPRITE_RAW;
    }
    out.sprite.data = out.pixels.data();
    return true;
}

namespace {

std::unique_ptr<ArtFile> convert_archive(const formats::gaf::Archive& archive, std::string path) {
    auto file = std::make_unique<ArtFile>();
    file->path = std::move(path);
    file->sequences.resize(archive.sequences.size());
    for (size_t index = 0; index < archive.sequences.size(); ++index) {
        const auto& source = archive.sequences[index];
        auto& sequence = file->sequences[index];
        sequence.name = source.name;
        sequence.frames.resize(source.frames.size());
        sequence.valid.assign(source.frames.size(), 0);
        for (size_t frame = 0; frame < source.frames.size(); ++frame)
            sequence.valid[frame] =
                convert_gaf_frame(source.frames[frame], sequence.frames[frame]) ? 1 : 0;
    }
    return file;
}

bool same_name(const std::string& a, const char* b) {
    const size_t length = std::char_traits<char>::length(b);
    if (a.size() != length)
        return false;
    for (size_t index = 0; index < length; ++index)
        if (std::tolower(static_cast<unsigned char>(a[index])) !=
            std::tolower(static_cast<unsigned char>(b[index])))
            return false;
    return true;
}

const void* load_gaf(void* context, const char* path) {
    auto& art = *static_cast<DialogArt*>(context);
    const std::string key = fold_path(path);
    for (const auto& file : art.files)
        if (file->path == key)
            return file.get();
    if (std::find(art.missing.begin(), art.missing.end(), key) != art.missing.end())
        return nullptr;
    std::optional<std::vector<uint8_t>> bytes;
    if (art.assets != nullptr && art.assets->file_size(key) != 0)
        bytes = art.assets->load_file_contents(key);
    const auto parsed = bytes ? formats::gaf::parse(*bytes) : formats::gaf::ParseResult{};
    if (!parsed.ok()) {
        art.missing.push_back(key);
        return nullptr;
    }
    art.files.push_back(convert_archive(*parsed.archive, key));
    return art.files.back().get();
}

const void* find_sequence(void*, const void* file, const char* name) {
    for (const auto& sequence : static_cast<const ArtFile*>(file)->sequences)
        if (same_name(sequence.name, name))
            return &sequence;
    return nullptr;
}

const void* first_sequence(void*, const void* file) {
    const auto& sequences = static_cast<const ArtFile*>(file)->sequences;
    return sequences.empty() ? nullptr : &sequences.front();
}

Sprite* frame(void*, const void* sequence, int32_t index) {
    auto& art = *const_cast<ArtSequence*>(static_cast<const ArtSequence*>(sequence));
    if (index < 0 || static_cast<size_t>(index) >= art.frames.size() ||
        art.valid[static_cast<size_t>(index)] == 0)
        return nullptr;
    return &art.frames[static_cast<size_t>(index)].sprite;
}

int32_t frame_count(void*, const void* sequence) {
    return static_cast<int32_t>(static_cast<const ArtSequence*>(sequence)->frames.size());
}

bool read_table(AssetStore& assets, const char* name, std::vector<uint8_t>& table) {
    try {
        table = assets.read(name).bytes;
    } catch (const std::exception&) {
        return false;
    }
    return table.size() == kTableBytes;
}

} // namespace

DialogArt* dialog_art(AssetStore& assets) {
    auto& stack = dialog_stack();
    if (stack.art && stack.art->assets == &assets)
        return stack.art.get();
    auto art = std::make_unique<DialogArt>();
    art->assets = &assets;
    if (!read_table(assets, kLightTable, art->light_table) ||
        !read_table(assets, kShadeTable, art->shade_table))
        return nullptr;
    // The shared renderer takes the new art only once it has loaded, so open
    // dialogs keep drawing with the old art on a failure.
    ui::gadget_render::GadgetRenderer renderer{};
    renderer.art.context = art.get();
    renderer.art.load_gaf = load_gaf;
    renderer.art.find_sequence = find_sequence;
    renderer.art.first_sequence = first_sequence;
    renderer.art.frame = frame;
    renderer.art.frame_count = frame_count;
    auto panel = std::make_unique<ui::gui_input::GadgetPanel>();
    ui::gui_input::init_gadget_panel(*panel);
    ui::gui_input::set_gaf_path(*panel, kGuiArtDirectory);
    for (size_t slot = 0; slot < std::size(kGuiFonts); ++slot)
        ui::gadget_render::load_gui_font(
            renderer, *panel, kGuiFonts[slot], static_cast<int32_t>(slot)
        );
    ui::gadget_render::load_common_gaf(renderer, *panel, kCommonGuiArt);
    art->fonts = panel->gaf_fonts;
    art->common = panel->list_skin;
    try {
        art->default_font = assets.read(kDefaultFont).bytes;
    } catch (const std::exception&) {
        art->default_font.clear();
    }
    if (art->fonts[0] == nullptr || art->common == nullptr)
        return nullptr;
    stack.renderer.art = renderer.art;
    stack.art = std::move(art);
    return stack.art.get();
}

} // namespace oa::ui::frontend_dialogs
