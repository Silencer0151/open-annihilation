// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The installed game's MAINMENU.GUI layout: totala1.hpi's, which fits
// FrontendX's pipe frames and is the main menu when no overlay is drawn, and
// the rule that picks it: with no overlay drawn, the layout comes from the
// archive that provides FrontendX, checked over add-on archives and loose
// files made from the installed ones.
#include "oa/ui/frontend_renderer.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/test/game_assets.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace renderer = oa::ui::frontend_renderer;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

struct Rect {
    int16_t x{};
    int16_t y{};
    int16_t width{};
    int16_t height{};
};

bool has_rect(const renderer::MainMenuResources& menu, std::string_view name, Rect expected) {
    for (const auto& gadget : menu.layout.gadgets) {
        if (gadget.common.name != name)
            continue;
        const auto& common = gadget.common;
        return common.x == expected.x && common.y == expected.y && common.width == expected.width &&
               common.height == expected.height;
    }
    return false;
}

const oa::formats::gaf::Sequence*
sequence_named(const oa::formats::gaf::Archive& archive, std::string_view name) {
    for (const auto& sequence : archive.sequences)
        if (sequence.name == name)
            return &sequence;
    return nullptr;
}

// The frame's covered texels in columns [0, columns) drawn at (x, y) through
// FrontendX's palette.
bool frame_drawn_at(
    const renderer::Surface& surface,
    const renderer::MainMenuResources& menu,
    const oa::formats::gaf::Frame& frame,
    int x,
    int y,
    int columns
) {
    const auto rendered = oa::formats::gaf::render_normal(frame);
    if (!rendered.ok() || !menu.background.palette)
        return false;
    const auto& image = *rendered.frame;
    const auto& palette = *menu.background.palette;
    for (int row = 0; row < image.height; ++row)
        for (int column = 0; column < columns; ++column) {
            const auto source =
                static_cast<std::size_t>(row) * image.width + static_cast<std::size_t>(column);
            if (image.coverage[source] == 0)
                continue;
            const auto color =
                static_cast<std::size_t>(image.pixels[source]) * oa::palette_entry_bytes;
            const auto target = (static_cast<std::size_t>(y + row) * surface.width +
                                 static_cast<std::size_t>(x + column)) *
                                3U;
            if (!std::equal(
                    palette.begin() + static_cast<std::ptrdiff_t>(color),
                    palette.begin() + static_cast<std::ptrdiff_t>(color + 3),
                    surface.rgb.begin() + static_cast<std::ptrdiff_t>(target)
                ))
                return false;
        }
    return true;
}

// Rows [top, bottom] of columns [left, right] show FrontendX unchanged.
bool background_kept(
    const renderer::Surface& surface,
    const renderer::MainMenuResources& menu,
    int left,
    int right,
    int top,
    int bottom
) {
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            const auto offset =
                (static_cast<std::size_t>(y) * surface.width + static_cast<std::size_t>(x)) * 3U;
            if (!std::equal(
                    surface.rgb.begin() + static_cast<std::ptrdiff_t>(offset),
                    surface.rgb.begin() + static_cast<std::ptrdiff_t>(offset + 3),
                    menu.background.rgb.begin() + static_cast<std::ptrdiff_t>(offset)
                ))
                return false;
        }
    return true;
}

/// Checks totala1.hpi's layout, drawn with COMMONGUI.GAF's buttons in the frames.
///
/// @param assets the installed game's store
void check_base_layout(oa::AssetStore& assets) {
    const auto base = renderer::load_main_menu(assets, renderer::MainMenuLayout::base_game);
    check(has_rect(base, "SINGLE", {139, 393, 96, 20}), "base SINGLE");
    check(has_rect(base, "MULTI", {139, 430, 96, 20}), "base MULTI");
    check(has_rect(base, "INTRO", {409, 393, 96, 20}), "base INTRO");
    check(has_rect(base, "EXIT", {409, 430, 96, 20}), "base EXIT");

    // MAINMENU.GAF has no sequence for the four buttons, so the first panel draw takes
    // COMMONGUI.GAF BUTTONS0's 96x20 group, which starts at frame 12.
    const auto* buttons = sequence_named(base.shared_sprites, "BUTTONS0");
    check(buttons != nullptr && buttons->frames.size() > 12, "COMMONGUI.GAF BUTTONS0");
    if (buttons == nullptr || buttons->frames.size() <= 12)
        return;
    const auto& normal = buttons->frames[12];
    check(normal.width == 96 && normal.height == 20, "BUTTONS0 frame 12 is 96x20");
    constexpr int caption_free_columns = 8;
    const auto drawn = renderer::render_main_menu(base);
    for (const auto& origin : {Rect{139, 393}, Rect{139, 430}, Rect{409, 393}, Rect{409, 430}})
        check(
            frame_drawn_at(drawn, base, normal, origin.x, origin.y, caption_free_columns),
            "each base button is BUTTONS0 frame 12 at its GUI rectangle"
        );
    // Nothing is drawn beside the frames' slots: FrontendX stays as it is
    // there.
    check(background_kept(drawn, base, 82, 130, 393, 449), "nothing drawn left of the frames");
    check(background_kept(drawn, base, 512, 559, 393, 449), "nothing drawn right of the frames");
}

// The resources the rule cases read, by their paths in the store.
constexpr std::string_view kLayout = "guis/mainmenu.gui";
constexpr std::string_view kBackground = "bitmaps/frontendx.pcx";

/// A folder that stands in for an installation's loose files and holds the
/// archives a case mounts; removed with its contents when it goes.
class ScratchFolder {
  public:

    /// Creates an empty folder under the system's temporary folder.
    ScratchFolder() {
        static int created = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("oa-main-menu-layout-" + std::to_string(stamp) + "-" + std::to_string(created++));
        std::filesystem::create_directories(path_);
    }

    ~ScratchFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    ScratchFolder(const ScratchFolder&) = delete;
    ScratchFolder& operator=(const ScratchFolder&) = delete;

    /// Writes a file below the folder, creating its folders.
    ///
    /// @param name '/'-separated path below the folder
    /// @param bytes the file's content
    /// @return the file's path
    std::filesystem::path write(std::string_view name, const std::vector<uint8_t>& bytes) const {
        const auto target = path_ / std::filesystem::path(name);
        std::filesystem::create_directories(target.parent_path());
        std::ofstream stream(target, std::ios::binary);
        stream.write(
            reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
        );
        return target;
    }

    /// Returns the folder.
    ///
    /// @return its path
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

  private:

    std::filesystem::path path_;
};

/// Returns a copy of a MAINMENU.GUI with its two button columns moved.
///
/// @param layout the installed layout, whose columns are at x 139 and 409
/// @param left the new x of SINGLE and MULTI
/// @param right the new x of INTRO and EXIT
/// @return the moved layout; empty when a column is not where it should be
std::vector<uint8_t> moved_layout(const std::vector<uint8_t>& layout, int left, int right) {
    std::string text(layout.begin(), layout.end());
    const auto move = [&text](std::string_view from, int to) {
        int moved = 0;
        const std::string replacement = "xpos=" + std::to_string(to) + ";";
        for (auto at = text.find(from); at != std::string::npos; at = text.find(from, at)) {
            text.replace(at, from.size(), replacement);
            at += replacement.size();
            ++moved;
        }
        return moved == 2;
    };
    if (!move("xpos=139;", left) || !move("xpos=409;", right))
        return {};
    return {text.begin(), text.end()};
}

/// Returns an archive that holds some of the main menu's files.
///
/// @param files each file's path in the archive and its content
/// @return the archive's bytes
std::vector<uint8_t>
archive_of(std::initializer_list<std::pair<std::string_view, const std::vector<uint8_t>*>> files) {
    std::vector<oa::HpiWriteFile> entries;
    for (const auto& [path, bytes] : files)
        entries.push_back({std::string(path), *bytes});
    return oa::write_hpi(entries);
}

/// Opens a store over a scratch folder's loose files, with archives mounted
/// above the installation's.
///
/// @param folder the scratch folder, whose loose files come first
/// @param archives archives in the folder, mounted in this order first
/// @param installed the installed game's store, whose archives follow
/// @return the store
oa::AssetStore store_over(
    const ScratchFolder& folder,
    std::initializer_list<std::filesystem::path> archives,
    const oa::AssetStore& installed
) {
    oa::AssetStore assets(folder.path());
    for (const auto& archive : archives)
        assets.mount(archive);
    for (const auto& archive : installed.mount_paths())
        assets.mount(archive);
    return assets;
}

/// Returns the x of the main menu's SINGLE button.
///
/// @param menu the loaded main menu
/// @return its x, or -1 when the layout has no SINGLE
int single_x(const renderer::MainMenuResources& menu) {
    for (const auto& gadget : menu.layout.gadgets)
        if (gadget.common.name == "SINGLE")
            return gadget.common.x;
    return -1;
}

/// Checks which MAINMENU.GUI each layout takes in one case.
///
/// @param assets the case's store
/// @param without_overlay SINGLE's x expected with no overlay drawn
/// @param with_overlay SINGLE's x expected with an overlay drawn
/// @param what the case, for the failure line
void check_case(oa::AssetStore& assets, int without_overlay, int with_overlay, const char* what) {
    const auto base = renderer::load_main_menu(assets, renderer::MainMenuLayout::base_game);
    const auto overlaid = renderer::load_main_menu(assets, renderer::MainMenuLayout::with_overlay);
    if (single_x(base) != without_overlay || single_x(overlaid) != with_overlay) {
        std::fprintf(
            stderr,
            "FAIL: %s: SINGLE at x %d without an overlay (expected %d), %d with one (expected "
            "%d)\n",
            what,
            single_x(base),
            without_overlay,
            single_x(overlaid),
            with_overlay
        );
        ++failures;
    }
}

/// Checks the rule over add-on archives and loose files made from the
/// installed layout and background: with no overlay the layout comes from the
/// archive that provides FrontendX; the top copy is taken when FrontendX or
/// the layout is loose, when that archive holds no MAINMENU.GUI, and with an
/// overlay drawn.
///
/// @param installed the installed game's store
void check_layout_rule(const oa::AssetStore& installed) {
    const auto installed_archive = installed.providing_archive(kBackground);
    check(installed_archive.has_value(), "the installation's FrontendX comes from an archive");
    if (!installed_archive)
        return;
    const auto layout = oa::HpiArchive(*installed_archive).read(kLayout).value.value();
    const auto background = installed.read(kBackground).bytes;
    const auto overlay_layout = moved_layout(layout, 82, 464);
    const auto own_layout = moved_layout(layout, 100, 450);
    const auto loose_layout = moved_layout(layout, 120, 420);
    check(!overlay_layout.empty(), "the installed layout's columns are at x 139 and 409");
    if (overlay_layout.empty())
        return;

    {
        // An add-on archive with a layout and no background is passed over.
        ScratchFolder folder;
        const auto addon = folder.write("layout.hpi", archive_of({{kLayout, &overlay_layout}}));
        auto assets = store_over(folder, {addon}, installed);
        check_case(assets, 139, 82, "a layout without a background");
    }
    {
        // A loose background leaves the top copy.
        ScratchFolder folder;
        const auto addon = folder.write("layout.hpi", archive_of({{kLayout, &overlay_layout}}));
        (void)folder.write(kBackground, background);
        auto assets = store_over(folder, {addon}, installed);
        check_case(assets, 82, 82, "a loose background");
    }
    {
        // An add-on archive with its own background brings its own layout,
        // whatever archive above it carries another.
        ScratchFolder folder;
        const auto addon = folder.write("layout.hpi", archive_of({{kLayout, &overlay_layout}}));
        const auto art = folder.write(
            "art.hpi", archive_of({{kLayout, &own_layout}, {kBackground, &background}})
        );
        auto assets = store_over(folder, {addon, art}, installed);
        check_case(assets, 100, 82, "an add-on background with its own layout");
    }
    {
        // A loose layout comes first, as loose files do.
        ScratchFolder folder;
        const auto art = folder.write(
            "art.hpi", archive_of({{kLayout, &own_layout}, {kBackground, &background}})
        );
        (void)folder.write(kLayout, loose_layout);
        auto assets = store_over(folder, {art}, installed);
        check_case(assets, 120, 120, "a loose layout");
    }
    {
        // The archive that provides the background holds no layout: the top
        // copy.
        ScratchFolder folder;
        const auto addon = folder.write("layout.hpi", archive_of({{kLayout, &overlay_layout}}));
        const auto art = folder.write("background.hpi", archive_of({{kBackground, &background}}));
        auto assets = store_over(folder, {addon, art}, installed);
        check_case(assets, 82, 82, "an add-on background without a layout");
    }
}

} // namespace

int main() {
    oa::AssetStore assets = oa::test::require_game_assets("the installed main menu layout");
    try {
        check_base_layout(assets);
        check_layout_rule(assets);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    if (failures != 0)
        return 1;
    std::puts(
        "main menu layout: totala1.hpi 139/409, and with no overlay the layout of the archive "
        "that provides FrontendX, in five cases of add-on archives and loose files"
    );
    return 0;
}
