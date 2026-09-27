// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The three stacked dialogs with the installed game's GUI data: geometry of
// the message box, CD prompt and help pages, their click handlers, stacking,
// keyboard defaults and composition over a frame. --screens runs them over
// the installation's store; --packed draws the message box and help pages
// from its archives. --notice opens the DEMOMSG.GUI notice over the data of
// the Total Annihilation demo (1997), unpacked from the installer the
// OA_DEMO_INSTALLER environment variable names into a temporary data folder;
// without it the test skips.
#include "oa/app/demo_installer.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/ui/frontend_renderer.hpp"
#include "oa/formats/gaf.hpp"
#include "oa/test/game_assets.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dialogs = oa::ui::frontend_dialogs;
using oa::ui::gui_layout::Gadget;

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

struct Host {
    std::string sound;
    bool panel_below = false;
    int32_t cd_check_result = 0;
    bool cd_check_shows_message = false;
    oa::app::ScreenContext* ctx = nullptr;
};

void request_screen(void*, oa::app::ScreenId) {
}

void play_sound(void*, const char*) {
}

int read_number(void*, const char*, const char*, uint32_t*) {
    return 0;
}

void write_number(void*, const char*, const char*, uint32_t) {
}

int read_string(void*, const char*, const char*, char*, std::size_t) {
    return 0;
}

void write_string(void*, const char*, const char*, const char*) {
}

void set_status(void*, const char*) {
}

constexpr oa::app::ScreenServices kServices{
    request_screen,
    play_sound,
    read_number,
    write_number,
    read_string,
    write_string,
    set_status,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr
};

void dialog_sound(void* host, const char* name) {
    static_cast<Host*>(host)->sound = name;
}

int32_t cd_check_click(void* context, const char* control) {
    auto& host = *static_cast<Host*>(context);
    if (std::strcmp(control, "OK") != 0)
        return 0;
    if (host.cd_check_shows_message)
        dialogs::open_message_box(
            host.ctx, "Please insert the Campaign CD (Disc 2) and try again", 200, 1, 1
        );
    return host.cd_check_result;
}

bool panel_below(void* context, int32_t* x, int32_t* y, int32_t* width, int32_t* height) {
    if (!static_cast<Host*>(context)->panel_below)
        return false;
    *x = 0;
    *y = 0;
    *width = 64;
    *height = 64;
    return true;
}

const Gadget& root() {
    return dialogs::dialog_resources()->layout.gadgets.front();
}

const Gadget& record(std::size_t index) {
    return dialogs::dialog_resources()->layout.gadgets.at(index);
}

std::string label_text(std::size_t index) {
    const auto* fields = std::get_if<oa::ui::gui_layout::LabelFields>(&record(index).fields);
    return fields != nullptr ? fields->text : std::string("<not a label>");
}

std::size_t record_count() {
    return dialogs::dialog_resources()->layout.gadgets.size();
}

// The caption of the open dialog's button of that name, empty without one.
std::string button_caption(std::string_view name) {
    for (const auto& gadget : dialogs::dialog_resources()->layout.gadgets)
        if (gadget.common.name == name)
            if (const auto* fields = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget.fields))
                return fields->text;
    return {};
}

oa::ui::frontend_renderer::Surface blank_frame() {
    return {640, 480, std::vector<uint8_t>(640U * 480U * 3U, 0)};
}

std::size_t lit_pixels(
    const oa::ui::frontend_renderer::Surface& surface, int left, int top, int right, int bottom
) {
    std::size_t lit = 0;
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            const auto at =
                (static_cast<std::size_t>(y) * surface.width + static_cast<std::size_t>(x)) * 3U;
            if (surface.rgb[at] != 0 || surface.rgb[at + 1] != 0 || surface.rgb[at + 2] != 0)
                ++lit;
        }
    return lit;
}

// Frame pixel (x, y) against pixel (fx, fy) of a COMMONGUI.GAF frame
// expanded through the dialog palette.
bool shows_art(
    const oa::ui::frontend_renderer::Surface& surface,
    int x,
    int y,
    const oa::formats::gaf::Archive& art,
    const char* sequence,
    size_t frame,
    int fx,
    int fy
) {
    const auto& palette = dialogs::dialog_resources()->gui_palette;
    for (const auto& candidate : art.sequences) {
        if (candidate.name != sequence || frame >= candidate.frames.size())
            continue;
        const auto rendered = oa::formats::gaf::render_normal(candidate.frames[frame]);
        if (!rendered.ok())
            return false;
        const auto& pixels = rendered.frame->pixels;
        const auto index =
            pixels[static_cast<size_t>(fy) * rendered.frame->width + static_cast<size_t>(fx)];
        const auto at = static_cast<size_t>(y) * surface.width + static_cast<size_t>(x);
        const auto* rgb = &surface.rgb[at * 3U];
        const auto* expected = &palette[static_cast<size_t>(index) * oa::palette_entry_bytes];
        return rgb[0] == expected[0] && rgb[1] == expected[1] && rgb[2] == expected[2];
    }
    return false;
}

void key_event(
    oa::app::ScreenRegistry& registry,
    oa::app::ScreenContext& ctx,
    uint32_t key,
    uint16_t modifiers = 0
) {
    oa::app::ScreenInput input{};
    input.kind = oa::app::ScreenInputKind::key_down;
    input.key = key;
    input.modifiers = modifiers;
    ctx.input = &input;
    for (uint32_t index = 0; index < registry.overlay_count; ++index)
        if (registry.overlays[index].event != nullptr)
            registry.overlays[index].event(&ctx, registry.overlays[index].state);
    ctx.input = nullptr;
}

void pointer_click(
    oa::app::ScreenRegistry& registry, oa::app::ScreenContext& ctx, float x, float y
) {
    for (const auto kind :
         {oa::app::ScreenInputKind::pointer_move,
          oa::app::ScreenInputKind::pointer_down,
          oa::app::ScreenInputKind::pointer_up}) {
        oa::app::ScreenInput input{};
        input.kind = kind;
        input.button = 1;
        input.x = x;
        input.y = y;
        ctx.input = &input;
        for (uint32_t index = 0; index < registry.overlay_count; ++index)
            if (registry.overlays[index].event != nullptr)
                registry.overlays[index].event(&ctx, registry.overlays[index].state);
    }
    ctx.input = nullptr;
}

void test_message_box(oa::app::ScreenRegistry& registry, oa::app::ScreenContext& ctx) {
    const char* text = "Please insert the Multiplayer CD (Disc 1) and try again";
    expect(dialogs::open_message_box(&ctx, text, 200, 1, 1), "message box opens");
    expect(
        dialogs::dialog_kind() == dialogs::DialogKind::message_box, "message box is the top dialog"
    );
    const auto& panel = root();
    const auto& ok = record(1).common;
    expect(record_count() >= 4, "200-pixel wrap yields at least two lines");
    const auto lines = static_cast<int32_t>(record_count()) - 2;
    expect(
        panel.common.height == ok.height + lines * 0x19 + 0x28,
        "panel height follows the line count"
    );
    expect(
        panel.common.width > 100 && panel.common.width < 260,
        "fitted width follows the longest line"
    );
    expect(panel.common.x == (640 - panel.common.width) / 2, "panel is centred horizontally");
    expect(panel.common.y == (480 - panel.common.height) / 2, "panel is centred vertically");
    expect(ok.x == panel.common.width - ok.width - 0xf, "OK sits at the right inset");
    expect(ok.y == panel.common.height - ok.height - 0xf, "OK sits at the bottom inset");
    expect(ok.active != 0, "OK stays active");
    bool centred = true;
    for (std::size_t index = 2; index < record_count(); ++index)
        centred = centred && record(index).common.width == panel.common.width &&
                  record(index).common.attributes == 2 && record(index).common.x == 0;
    expect(centred, "every line spans the panel and is centred");
    expect(record(2).common.y == 0x14, "first line starts at y=20");

    auto frame = blank_frame();
    oa::ui::frontend_renderer::Surface* previous = ctx.surface;
    ctx.surface = &frame;
    dialogs::dialog_draw(&ctx);
    const int left = panel.common.x, top = panel.common.y;
    const int right = left + panel.common.width - 1, bottom = top + panel.common.height - 1;
    expect(
        lit_pixels(frame, left, top, right, bottom) > 500, "composed panel lights its rectangle"
    );
    expect(lit_pixels(frame, 0, 0, 639, top - 10) == 0, "frame above the panel stays dark");
    const auto common = oa::formats::gaf::parse(ctx.assets->read("anims/commongui.gaf").bytes);
    expect(
        common.ok() && shows_art(frame, left, top, *common.archive, "BackTile", 0, 0, 0) &&
            shows_art(frame, right, top, *common.archive, "BackTile", 2, 63, 0),
        "the panel face is tiled from BackTile"
    );
    const int ok_left = left + ok.x;
    const int ok_top = top + ok.y;
    const int ok_right = ok_left + ok.width - 1;
    const int ok_bottom = ok_top + ok.height - 1;
    expect(
        common.ok() && shows_art(frame, ok_left, ok_top, *common.archive, "BUTTONS0", 20, 0, 0) &&
            shows_art(frame, ok_right, ok_bottom, *common.archive, "BUTTONS0", 20, 79, 19),
        "OK is drawn from the 80x20 BUTTONS0 frame"
    );
    ctx.surface = previous;

    pointer_click(
        registry,
        ctx,
        static_cast<float>(left + ok.x + ok.width / 2),
        static_cast<float>(top + ok.y + ok.height / 2)
    );
    expect(dialogs::dialog_kind() == dialogs::DialogKind::none, "clicking OK releases the box");

    expect(dialogs::open_message_box(&ctx, text, 320, 1, 0), "fixed-width message box opens");
    expect(root().common.width == 320, "fixed width is kept");
    key_event(registry, ctx, 0x1b);
    expect(dialogs::dialog_kind() == dialogs::DialogKind::none, "Escape activates the OK default");

    expect(dialogs::open_message_box(&ctx, "Waiting", 200, 0, 1), "buttonless message box opens");
    expect(record(1).common.active == 0, "hidden OK is deactivated");
    key_event(registry, ctx, 0x0d);
    expect(dialogs::dialog_kind() == dialogs::DialogKind::message_box, "no default without OK");
    dialogs::close_dialog();
    expect(dialogs::dialog_count() == 0, "close_dialog releases the box");
}

void test_keyboard_focus(oa::app::ScreenRegistry& registry, oa::app::ScreenContext& ctx) {
    expect(dialogs::open_help(&ctx), "keyboard help opens");
    auto page_frame = blank_frame();
    auto* previous = ctx.surface;
    ctx.surface = &page_frame;
    dialogs::dialog_draw(&ctx);
    // HELP.GUI starts on Page. Focus order sorts its two buttons by y*5000+x;
    // Tab advances to OK, and Shift reverses that direction.
    key_event(registry, ctx, 9);
    auto ok_frame = blank_frame();
    ctx.surface = &ok_frame;
    dialogs::dialog_draw(&ctx);
    expect(page_frame.rgb != ok_frame.rgb, "Tab moves the drawn focus outline");
    key_event(registry, ctx, 9, 0x0001);
    key_event(registry, ctx, 0x20);
    expect(
        dialogs::dialog_count() == 1 && dialogs::help_page() == 1,
        "Shift Tab returns to Page and Space activates it"
    );
    key_event(registry, ctx, 9, 0x0002);
    key_event(registry, ctx, 0x20);
    expect(dialogs::dialog_count() == 0, "right Shift Tab wraps to OK and Space closes help");
    ctx.surface = previous;
}

void test_cd_check(oa::app::ScreenContext& ctx, Host& host) {
    expect(dialogs::open_cd_check(&ctx), "CD check opens");
    expect(dialogs::dialog_kind() == dialogs::DialogKind::cd_check, "CD check is the top dialog");
    expect(
        root().common.x == (640 - 348) / 2 && root().common.y == (480 - 117) / 2,
        "CD check is centred"
    );
    host.cd_check_result = 0;
    host.cd_check_shows_message = true;
    expect(dialogs::dialog_click(&ctx, "OK"), "OK is a control");
    expect(
        dialogs::dialog_count() == 2 && dialogs::dialog_kind() == dialogs::DialogKind::message_box,
        "missing disc stacks a message box"
    );
    expect(dialogs::dialog_click(&ctx, "OK"), "message OK is a control");
    expect(
        dialogs::dialog_count() == 1 && dialogs::dialog_kind() == dialogs::DialogKind::cd_check,
        "message box releases back to the CD check"
    );
    host.cd_check_result = 1;
    host.cd_check_shows_message = false;
    expect(dialogs::dialog_click(&ctx, "OK"), "OK is still a control");
    expect(dialogs::dialog_count() == 0, "disc present releases the CD check");
}

void test_help(oa::app::ScreenContext& ctx, Host& host) {
    expect(dialogs::open_help(&ctx), "help opens");
    expect(dialogs::dialog_kind() == dialogs::DialogKind::help, "help is the top dialog");
    expect(
        root().common.x == (640 - 0x80 - 492) / 2 + 0x80,
        "help panel is centred right of the HUD strip"
    );
    expect(root().common.y == (480 - 419) / 2, "help panel is centred vertically");
    expect(record_count() == 4 + 17 * 2, "page 1 holds seventeen command rows");
    expect(label_text(4) == "CTRL+A" && label_text(5) == "Select all units", "first row is CTRL+A");
    expect(record(4).common.x == 0x28 && record(4).common.width == 0x4e, "key column geometry");
    expect(
        record(5).common.x == 0x7d && record(5).common.width == 300, "description column geometry"
    );
    expect(record(4).common.y == 0x32 && record(6).common.y == 0x32 + 0x12, "rows step by 18");
    expect(
        record(4).common.attributes == 1 && record(5).common.attributes == 1,
        "rows are left-aligned"
    );
    expect(
        label_text(4 + 12 * 2) == " " && label_text(5 + 12 * 2).empty(),
        "a bar-only line is a blank row"
    );

    auto frame = blank_frame();
    auto* previous = ctx.surface;
    ctx.surface = &frame;
    dialogs::dialog_draw(&ctx);
    const auto& panel = root().common;
    expect(
        lit_pixels(frame, panel.x, panel.y, panel.x + panel.width - 1, panel.y + panel.height - 1) >
            10000,
        "help face is drawn"
    );
    ctx.surface = previous;

    host.sound.clear();
    expect(dialogs::dialog_click(&ctx, "Page"), "Page is a control");
    expect(host.sound == "Options", "Page plays the Options sound");
    expect(dialogs::help_page() == 1, "Page advances to the second page");
    expect(
        label_text(4) == "E" && label_text(5) == "Give a reclaim order",
        "second page starts at Line17"
    );
    expect(dialogs::dialog_click(&ctx, "Page") && dialogs::help_page() == 2, "third page");
    expect(record_count() == 4 + 15 * 2, "third page holds the remaining fifteen rows");
    expect(dialogs::dialog_click(&ctx, "Page") && dialogs::help_page() == 0, "pages wrap around");
    expect(record_count() == 4 + 17 * 2, "first page is refilled");
    host.sound.clear();
    expect(dialogs::dialog_click(&ctx, "OK"), "OK is a control");
    expect(host.sound == "Options", "OK plays the Options sound");
    expect(dialogs::dialog_count() == 0, "OK releases the help panel");
}

// A dialog opened against one frame size is placed again when it is drawn
// on a frame of another size, and a shading dialog darkens only the panel
// the host reports below it.
void test_placement_and_shade(oa::app::ScreenContext& ctx, Host& host) {
    expect(dialogs::open_message_box(&ctx, "Placed", 200, 1, 1), "message box opens");
    const auto width = root().common.width;
    const auto height = root().common.height;
    oa::ui::frontend_renderer::Surface large{
        1280, 960, std::vector<uint8_t>(1280U * 960U * 3U, 0x80)
    };
    host.panel_below = true;
    auto* previous = ctx.surface;
    ctx.surface = &large;
    dialogs::dialog_draw(&ctx);
    ctx.surface = previous;
    expect(
        root().common.x == (1280 - width) / 2 && root().common.y == (960 - height) / 2,
        "drawing on a larger frame centres the box on it"
    );
    const auto at = [&large](int x, int y) {
        return large.rgb[(static_cast<std::size_t>(y) * 1280U + static_cast<std::size_t>(x)) * 3U];
    };
    expect(at(1, 1) != 0x80, "the reported panel is darkened");
    expect(at(1279, 959) == 0x80, "the frame outside the reported panel keeps its colour");
    host.panel_below = false;
    dialogs::close_dialog();
}

// As a layer the message box is opaque over its whole rectangle and matches
// its composition over a black frame, including the palette index 0 pixels
// of glyphs 0xAB and 0xAC, the index the layer's uncovered colour maps to.
void test_layer(oa::app::ScreenContext& ctx) {
    expect(dialogs::open_message_box(&ctx, "\xAB\xAC \xAB\xAC", 200, 1, 1), "message box opens");
    auto frame = blank_frame();
    auto* previous = ctx.surface;
    ctx.surface = &frame;
    dialogs::dialog_draw(&ctx);
    ctx.surface = previous;
    std::vector<uint8_t> rgba;
    dialogs::dialog_draw_layer(frame.width, frame.height, rgba);
    const auto& panel = root().common;
    bool opaque = true;
    bool same = true;
    for (int y = panel.y; y < panel.y + panel.height; ++y)
        for (int x = panel.x; x < panel.x + panel.width; ++x) {
            const auto at = static_cast<size_t>(y) * frame.width + static_cast<size_t>(x);
            opaque = opaque && rgba[at * 4U + 3U] == 0xff;
            for (size_t channel = 0; channel < 3; ++channel)
                same = same && rgba[at * 4U + channel] == frame.rgb[at * 3U + channel];
        }
    expect(opaque, "the layer covers the whole message box");
    expect(same, "the layer shows the colours the frame composition shows");
    dialogs::close_dialog();
}

// The watch prompt chooses CHOICE1 on Enter and CHOICE2 on Escape; its
// handler sounds BigButton and dispatches only either named choice.
void test_continue_watching(oa::app::ScreenContext& ctx, Host& host) {
    struct Choice {
        int count{};
        bool keep{};
    } choice;

    const auto callback = [](void* context, bool keep) {
        auto& value = *static_cast<Choice*>(context);
        ++value.count;
        value.keep = keep;
    };
    for (bool keep : {true, false}) {
        expect(dialogs::open_continue_watching(&ctx, &choice, callback), "open watcher prompt");
        expect(
            dialogs::dialog_kind() == dialogs::DialogKind::continue_watching, "watcher dialog kind"
        );
        const auto* panel = std::get_if<oa::ui::gui_layout::PanelFields>(&root().fields);
        expect(
            panel != nullptr && panel->carriage_return_default == "CHOICE1" &&
                panel->escape_default == "CHOICE2",
            "watcher keyboard defaults"
        );
        bool title = false;
        for (const auto& gadget : dialogs::dialog_resources()->layout.gadgets)
            if (const auto* label = std::get_if<oa::ui::gui_layout::LabelFields>(&gadget.fields))
                title |= label->text == "You're out!  Continue Watching?";
        expect(title, "watcher title");
        expect(dialogs::dialog_click(&ctx, keep ? "CHOICE1" : "CHOICE2"), "watcher choice");
        expect(choice.keep == keep && choice.count == (keep ? 1 : 2), "watcher callback");
        expect(
            host.sound == "BigButton" && dialogs::dialog_count() == 0, "watcher sound and close"
        );
    }
}

// A rectangle of the frame, inclusive.
struct Area {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

bool inside(const std::vector<Area>& areas, int x, int y) {
    for (const auto& area : areas)
        if (x >= area.left && x <= area.right && y >= area.top && y <= area.bottom)
            return true;
    return false;
}

// The pixels an indexed image covers when drawn with its top-left at (x, y)
// outside `skip`, and how many of them the frame shows in their colour
// through the dialog palette.
struct ArtMatch {
    std::size_t covered = 0;
    std::size_t shown = 0;
};

// `coverage` empty covers every pixel; `stride` is the width of a source row.
ArtMatch match_art(
    const oa::ui::frontend_renderer::Surface& surface,
    int x,
    int y,
    int width,
    int height,
    std::size_t stride,
    const std::vector<uint8_t>& indices,
    const std::vector<uint8_t>& coverage,
    const std::vector<Area>& skip
) {
    const auto& palette = dialogs::dialog_resources()->gui_palette;
    const int surface_width = static_cast<int>(surface.width);
    const int surface_height = static_cast<int>(surface.height);
    ArtMatch match;
    for (int row = 0; row < height; ++row)
        for (int column = 0; column < width; ++column) {
            const auto source = static_cast<size_t>(row) * stride + static_cast<size_t>(column);
            const int px = x + column;
            const int py = y + row;
            if ((!coverage.empty() && coverage[source] == 0) || px < 0 || py < 0 ||
                px >= surface_width || py >= surface_height || inside(skip, px, py))
                continue;
            ++match.covered;
            const auto pixel = static_cast<size_t>(py) * surface.width + static_cast<size_t>(px);
            const auto at = pixel * 3U;
            const auto entry = static_cast<size_t>(indices[source]) * oa::palette_entry_bytes;
            if (surface.rgb[at] == palette[entry] && surface.rgb[at + 1] == palette[entry + 1] &&
                surface.rgb[at + 2] == palette[entry + 2])
                ++match.shown;
        }
    return match;
}

const oa::formats::gaf::Sequence*
find_sequence(const oa::formats::gaf::ParseResult& parsed, const char* name, std::size_t frames) {
    if (!parsed.ok())
        return nullptr;
    for (const auto& sequence : parsed.archive->sequences)
        if (sequence.name == name && sequence.frames.size() >= frames)
            return &sequence;
    return nullptr;
}

// Width of `text` in the glyphs of the button font, one frame per character.
int glyph_width(const oa::formats::gaf::ParseResult& font, const char* text) {
    if (!font.ok() || font.archive->sequences.empty())
        return 0;
    const auto& glyphs = font.archive->sequences.front().frames;
    int width = 0;
    for (const auto* p = reinterpret_cast<const unsigned char*>(text); *p != 0; ++p)
        if (*p < glyphs.size())
            width += glyphs[*p].width;
    return width;
}

// Lit outside the rectangle of the focused or Enter-default control, unless
// that control is a label, list box or text box.
constexpr int kFocusRings = 6;

// With the GUI data only inside the game's archives, as the application
// mounts them, the message box and the help panel draw their art: every
// pixel the art covers that no caption, label or focus outline draws over.
void test_packed(oa::AssetStore& assets, Host& host) {
    auto frame = blank_frame();
    oa::app::ScreenContext ctx{&host, &kServices, &assets, &frame, nullptr, nullptr, 0};
    host.ctx = &ctx;
    expect(
        dialogs::open_message_box(
            &ctx, "Please insert the Multiplayer CD (Disc 1) and try again", 200, 1, 1
        ),
        "message box opens from the archives"
    );
    if (dialogs::dialog_kind() == dialogs::DialogKind::message_box) {
        dialogs::dialog_draw(&ctx);
        const auto& panel = root().common;
        const auto& ok = record(1).common;
        const auto common = oa::formats::gaf::parse(assets.read("anims/commongui.gaf").bytes);
        const auto* buttons = find_sequence(common, "BUTTONS0", 21);
        const auto* tiles = find_sequence(common, "BackTile", 3);
        expect(buttons != nullptr && tiles != nullptr, "the archived COMMONGUI art is found");
        const int right = panel.x + panel.width - 1;
        expect(
            tiles != nullptr &&
                shows_art(frame, panel.x, panel.y, *common.archive, "BackTile", 0, 0, 0) &&
                shows_art(frame, right, panel.y, *common.archive, "BackTile", 2, 63, 0),
            "the panel face is tiled from the archived BackTile"
        );
        oa::formats::gaf::RenderResult face;
        if (buttons != nullptr)
            face = oa::formats::gaf::render_normal(buttons->frames[20]);
        if (face.ok()) {
            // The caption is centred on the button; one column either side
            // allows for the centring's rounding and the pressed offset.
            const auto font = oa::formats::gaf::parse(assets.read("anims/hattfont12.gaf").bytes);
            const int caption = glyph_width(font, "OK");
            const int left = panel.x + ok.x;
            const int top = panel.y + ok.y;
            const Area band{
                left + (ok.width - caption) / 2 - 1,
                top,
                left + (ok.width + caption) / 2 + 1,
                top + ok.height - 1
            };
            const auto& art = *face.frame;
            const auto match = match_art(
                frame, left, top, art.width, art.height, art.width, art.pixels, art.coverage, {band}
            );
            expect(
                caption > 0 && match.covered * 2U > static_cast<size_t>(ok.width) * ok.height &&
                    match.shown == match.covered,
                "OK shows the archived BUTTONS0 face on every covered pixel beside its caption"
            );
        }
    }
    dialogs::reset_dialogs();
    frame = blank_frame();
    expect(dialogs::open_help(&ctx), "help opens from the archives");
    if (dialogs::dialog_kind() == dialogs::DialogKind::help) {
        dialogs::dialog_draw(&ctx);
        const auto& panel = root().common;
        const auto backdrop = oa::decode_pcx(assets.read("bitmaps/dhelp.pcx").bytes);
        std::vector<Area> drawn_over;
        for (std::size_t index = 1; index < record_count(); ++index) {
            const auto& gadget = record(index).common;
            const bool outlined = gadget.type != oa::ui::gui_layout::GadgetType::label &&
                                  gadget.type != oa::ui::gui_layout::GadgetType::list_box &&
                                  gadget.type != oa::ui::gui_layout::GadgetType::text_box;
            const int grow = outlined ? kFocusRings : 0;
            const int left = panel.x + gadget.x;
            const int top = panel.y + gadget.y;
            drawn_over.push_back(
                {left - grow,
                 top - grow,
                 left + gadget.width - 1 + grow,
                 top + gadget.height - 1 + grow}
            );
        }
        const auto backdrop_pixels = static_cast<std::size_t>(backdrop.width) * backdrop.height;
        const bool fits = backdrop.width >= static_cast<uint32_t>(panel.width) &&
                          backdrop.height >= static_cast<uint32_t>(panel.height) &&
                          backdrop.indices.size() == backdrop_pixels;
        expect(fits, "the archived dhelp backdrop spans the help panel");
        if (fits) {
            const auto match = match_art(
                frame,
                panel.x,
                panel.y,
                panel.width,
                panel.height,
                backdrop.width,
                backdrop.indices,
                {},
                drawn_over
            );
            expect(
                match.covered > static_cast<std::size_t>(panel.width) * panel.height / 3U &&
                    match.shown == match.covered,
                "the help face shows the archived dhelp backdrop wherever no control draws"
            );
        }
    }
    dialogs::reset_dialogs();
}

void test_stack_limit(oa::app::ScreenContext& ctx) {
    for (int index = 0; index < 4; ++index)
        expect(
            dialogs::open_message_box(&ctx, "stack", 200, 1, 1),
            "the dialog stack holds four dialogs"
        );
    expect(!dialogs::open_message_box(&ctx, "stack", 200, 1, 1), "a fifth dialog is refused");
    dialogs::reset_dialogs();
    expect(dialogs::dialog_count() == 0, "reset releases every dialog");
}

struct NoticeClosed {
    int calls = 0;
    dialogs::NoticeChoice choice = dialogs::NoticeChoice::ok;
};

void notice_closed(void* context, dialogs::NoticeChoice choice) {
    auto& closed = *static_cast<NoticeClosed*>(context);
    ++closed.calls;
    closed.choice = choice;
}

// DEMOMSG.GUI over demotextbg from game data that holds both, such as the
// Total Annihilation demo (1997): the text column, the data's buttons, the
// website button's caption, and which button closed it.
void test_notice(oa::app::ScreenRegistry& registry, oa::app::ScreenContext& ctx, Host& host) {
    NoticeClosed closed;
    constexpr std::string_view website_caption = "Go to www.example.org";
    expect(
        dialogs::open_notice(
            &ctx, "First line\nSecond line", website_caption, &closed, notice_closed
        ),
        "the notice opens"
    );
    expect(
        button_caption("GotoWebsite") == website_caption,
        "the website button names the address it opens"
    );
    expect(button_caption("OK") == "OK", "OK keeps the data's caption");
    expect(dialogs::dialog_kind() == dialogs::DialogKind::notice, "the notice is the top dialog");
    expect(
        root().common.x == 0 && root().common.y == 0 && root().common.width == 640 &&
            root().common.height == 480,
        "the notice fills the screen"
    );
    const auto count = record_count();
    expect(count >= 5, "the notice holds its buttons and two text lines");
    if (count < 5)
        return;
    expect(
        label_text(count - 2) == "First line" && label_text(count - 1) == "Second line",
        "each text line is a label of its own"
    );
    expect(
        record(count - 2).common.x == 50 && record(count - 2).common.y == 105 &&
            record(count - 1).common.y == 120 && record(count - 1).common.width == 540,
        "the text column starts at 50,105 with lines 15 pixels apart"
    );
    auto frame = blank_frame();
    ctx.surface = &frame;
    dialogs::dialog_draw(&ctx);
    expect(lit_pixels(frame, 0, 0, 639, 479) > 640U * 480U / 2U, "the notice draws its bitmap");

    expect(dialogs::dialog_click(&ctx, "GotoWebsite"), "the notice has the website button");
    expect(
        closed.calls == 1 && closed.choice == dialogs::NoticeChoice::website,
        "the website button reports itself"
    );
    expect(dialogs::dialog_count() == 0, "the website button closes the notice");
    expect(host.sound == "BigButton", "the website button plays BigButton");

    expect(
        dialogs::open_notice(&ctx, "Again", {}, &closed, notice_closed), "the notice opens again"
    );
    expect(
        !button_caption("GotoWebsite").empty() && button_caption("GotoWebsite") != website_caption,
        "without a caption the website button keeps the data's"
    );
    constexpr uint32_t enter_key = 0x0d;
    key_event(registry, ctx, enter_key);
    expect(
        closed.calls == 2 && closed.choice == dialogs::NoticeChoice::ok &&
            dialogs::dialog_count() == 0,
        "Enter closes the notice with OK"
    );
}

// The data of the Total Annihilation demo (1997), unpacked from its installer
// into a temporary data folder that is removed with it.
struct DemoData {
    std::filesystem::path folder;

    DemoData() = default;
    DemoData(const DemoData&) = delete;
    DemoData& operator=(const DemoData&) = delete;

    ~DemoData() { remove(); }

    void remove() {
        std::error_code error;
        if (!folder.empty())
            std::filesystem::remove_all(folder, error);
    }

    // Unpacks the installer OA_DEMO_INSTALLER names and mounts its archive;
    // without the variable the test skips, and a failed unpacking fails it.
    oa::AssetStore open() {
        const char* named = std::getenv("OA_DEMO_INSTALLER");
        if (named == nullptr || *named == '\0')
            oa::test::skip_test(
                "the DEMOMSG.GUI notice",
                "OA_DEMO_INSTALLER is not set; set it to the Total Annihilation demo (1997) "
                "installer"
            );
        folder = std::filesystem::temp_directory_path() /
                 ("oa-frontend-dialogs-notice-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto demo =
            oa::app::set_up_demo(oa::app::path_from_utf8(named).parent_path(), folder);
        if (demo.outcome != oa::app::DemoOutcome::ready) {
            std::fprintf(
                stderr,
                "FAILED: the demo's data could not be unpacked from %s%s%s\n",
                named,
                demo.problem.empty() ? "" : ": ",
                demo.problem.c_str()
            );
            remove();
            std::exit(1);
        }
        return oa::test::open_game_assets(demo.folder);
    }
};

} // namespace

int main(int argc, char** argv) {
    const bool packed = argc == 2 && std::strcmp(argv[1], "--packed") == 0;
    const bool notice = argc == 2 && std::strcmp(argv[1], "--notice") == 0;
    if (argc != 2 || (!packed && !notice && std::strcmp(argv[1], "--screens") != 0)) {
        std::fprintf(stderr, "usage: %s --screens | --packed | --notice\n", argv[0]);
        return 2;
    }
    DemoData demo;
    auto assets =
        notice
            ? demo.open()
            : oa::test::require_game_assets(packed ? "the archived dialogs" : "the dialog screens");
    Host host;
    dialogs::dialogs_bind_host(
        {&host,
         dialog_sound,
         nullptr,
         cd_check_click,
         nullptr,
         panel_below,
         nullptr,
         nullptr,
         nullptr}
    );
    if (packed) {
        test_packed(assets, host);
    } else if (notice) {
        expect(
            assets.file_size("guis/demomsg.gui") != 0 &&
                assets.file_size("bitmaps/demotextbg.pcx") != 0,
            "the demo's data holds DEMOMSG.GUI and demotextbg"
        );
        oa::ui::frontend_renderer::Surface surface;
        oa::app::ScreenContext ctx{&host, &kServices, &assets, &surface, nullptr, nullptr, 0};
        host.ctx = &ctx;
        oa::app::ScreenRegistry registry{};
        oa::app::register_frontend_dialog_screens(&registry);
        test_notice(registry, ctx, host);
    } else {
        oa::ui::frontend_renderer::Surface surface;
        oa::app::ScreenContext ctx{&host, &kServices, &assets, &surface, nullptr, nullptr, 0};
        host.ctx = &ctx;
        oa::app::ScreenRegistry registry{};
        oa::app::register_frontend_dialog_screens(&registry);
        expect(
            registry.overlay_count == 1 && registry.overlays[0].screen == oa::app::kScreenAny,
            "overlay registers for every screen"
        );
        test_message_box(registry, ctx);
        test_keyboard_focus(registry, ctx);
        test_cd_check(ctx, host);
        test_help(ctx, host);
        test_placement_and_shade(ctx, host);
        test_layer(ctx);
        test_continue_watching(ctx, host);
        test_stack_limit(ctx);
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts(
        packed   ? "frontend dialogs from archives: ok"
        : notice ? "frontend dialogs notice: ok"
                 : "frontend dialogs: ok"
    );
    return 0;
}
