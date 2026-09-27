// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/gui_layout/gui_gadget.hpp"
#include "oa/test/game_assets.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gui = oa::ui::gui_layout;
namespace field = gui::field;

namespace {

void require(bool condition, std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

gui::Layout parse_text(std::string_view text) {
    auto result = gui::parse(std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
    require(result.ok(), "fixture parses");
    return std::move(*result.layout);
}

constexpr std::string_view fixture = R"GUI(
[GADGET0] { [COMMON] { id=0; name=Options; xpos=100; ypos=50; width=300; height=200; active=1; }
  totalgadgets=5; crdefault=DONE; escdefault=; defaultfocus=MUSIC;
  [VERSION] { major=3; minor=1; revision=2; } }
[GADGET1] { [COMMON] { id=1; assoc=4; name=DONE; xpos=10; ypos=20; width=60; height=20; attribs=64;
  colorf=5; active=1; gaffile=1; }
  status=1; text=Done|Again; quickkey=D; grayedout=1; stages=2; }
[GADGET2] { [COMMON] { id=2; assoc=7; name=MUSIC; xpos=10; ypos=50; width=100; height=80; active=1; }
  itemheight=12; }
[GADGET3] { [COMMON] { id=4; assoc=7; name=MUSICBAR; xpos=112; ypos=50; width=12; height=80; active=1; }
  range=40; thick=0; knobpos=3; knobsize=9; text=bar; }
[GADGET4] { [COMMON] { id=5; name=LABEL; xpos=5; ypos=5; width=50; height=10; active=1; }
  text=Hello; link=DONE; }
[GADGET5] { [COMMON] { id=3; name=NAME; xpos=5; ypos=150; width=120; height=14; active=1; }
  maxchars=300; text=Player; }
)GUI";

void loaders_write_record_offsets() {
    const auto layout = parse_text(fixture);
    auto table = std::make_unique<gui::GadgetTable>();
    const auto count = gui::load_gadget_records(layout, table->records);
    require(count == 6, "six records loaded");
    const auto& root = table->records[0];
    require(gui::record_i16(root, field::record_count) == 5, "root stores last index");
    require(
        gui::record_string(root, field::enter_default) == "DONE", "crdefault in the Enter default"
    );
    require(
        gui::record_string(root, field::default_focus) == "MUSIC",
        "defaultfocus in the default focus"
    );
    require(
        root.bytes[field::version_major] == 3 && root.bytes[field::version_revision] == 2, "version"
    );
    require(
        gui::record_i16(root, field::x) == 100 && gui::record_i16(root, field::height) == 200,
        "root geometry"
    );

    const auto& button = table->records[1];
    require(gui::gadget_type_of(button) == gui::gadget_type::button, "button type byte");
    require(button.bytes[field::group] == 4, "assoc byte");
    require(gui::record_i16(button, field::button_status) == 1, "button status");
    require(gui::record_string(button, field::text) == "Done|Again", "button caption");
    require(button.bytes[field::button_quick_key] == 'D', "alphabetic quick key");
    require((button.bytes[field::button_flags] & 1) == 1, "grayed bit");
    require(button.bytes[field::button_stages] == 2, "stage count");
    require((button.bytes[field::gaf_file] & 1) == 1, "gaffile bit");
    require(gui::gadget_attributes(button) == 64, "attributes word");
    require(gui::record_u32(button, field::color_foreground) == 5, "colorf word");

    const auto& list = table->records[2];
    require(gui::record_i16(list, field::list_item_height) == 12, "itemheight");

    const auto& bar = table->records[3];
    require(gui::record_i16(bar, field::scroll_range) == 40, "scroll range");
    require(gui::record_i16(bar, field::scroll_knob) == 3, "knob position");
    require(gui::record_i16(bar, field::scroll_knob_size) == 9, "knob size");

    const auto& label = table->records[4];
    require(gui::record_string(label, field::text) == "Hello", "label text");
    require(gui::record_string(label, field::label_link) == "DONE", "label link");
    require(label.bytes[field::label_quick_key] == 0, "label quick key cleared");

    const auto& text_box = table->records[5];
    require(gui::record_i16(text_box, field::text_max_chars) == 128, "maxchars clamped");
    require(gui::record_string(text_box, field::text) == "Player", "text box text");
}

void lookup_and_geometry() {
    const auto layout = parse_text(fixture);
    auto table = std::make_unique<gui::GadgetTable>();
    gui::load_gadget_records(layout, table->records);
    const std::span<const gui::GadgetRecord> records = table->records;
    require(gui::find_gadget(records, "MUSICBAR") == 3, "find by exact name");
    require(gui::find_gadget(records, "Options") == -1, "root is never matched");
    require(gui::find_gadget(records, "MUSICBARX") == -1, "longer query differs");
    require(gui::find_gadget_containing(records, "BAR") == 3, "substring lookup");
    std::array<char, 17> name{};
    gui::copy_gadget_name(records, 2, name.data());
    require(std::string_view(name.data()) == "MUSIC", "copied name");
    require(gui::gadget_name_matches(records, 1, "DONE"), "name match");
    require(!gui::gadget_name_matches(records, -1, "DONE"), "-1 never matches");

    const auto relative = gui::panel_relative_rect(records[1]);
    require(relative == gui::GadgetRect{10, 20, 69, 39}, "panel-relative rect");
    require(
        gui::panel_relative_rect(records[0]) == gui::GadgetRect{0, 0, 299, 199}, "root at origin"
    );
    require(
        gui::screen_rect(records, 1) == gui::GadgetRect{110, 70, 169, 89}, "screen rect adds root"
    );
    require(gui::screen_rect(records, 0) == gui::GadgetRect{100, 50, 399, 249}, "root screen rect");

    gui::GadgetRect outer;
    gui::GadgetRect knob;
    gui::scroll_bar_rects(records[3], outer, knob);
    require(outer == gui::GadgetRect{112, 50, 124, 130}, "scroll track uses exclusive edges");
    require(knob == gui::GadgetRect{113, 55, 123, 64}, "vertical knob rect");

    require(
        gui::find_group_member(records, 2, gui::gadget_type::scroll_bar) == 3, "list's scroll bar"
    );
    require(gui::find_group_member(records, 3, gui::gadget_type::list_box) == 2, "bar's list");
    require(
        gui::find_group_member(records, 1, gui::gadget_type::scroll_bar) == 0, "miss returns 0"
    );

    int reported = 0;
    static int* counter = nullptr;
    counter = &reported;
    auto* missing = gui::require_gadget(table->records, "NOPE", [](const char*) { ++*counter; });
    require(missing == nullptr && reported == 1, "fatal reporter called on a miss");
}

void record_construction() {
    const auto layout = parse_text(fixture);
    auto table = std::make_unique<gui::GadgetTable>();
    gui::load_gadget_records(layout, table->records);
    const auto added = gui::add_gadget(table->records, gui::gadget_type::hot_surface);
    require(added == 6, "added at next index");
    require(table->records[6].bytes[field::active] == 1, "added record active");
    const auto label = gui::add_label(table->records, "TEXT", "Line", 10, 30, -1, 2);
    require(label == 7, "label index");
    const auto& line = table->records[7];
    require(gui::record_i16(line, field::width) == 300 - 10 - 5, "width -1 spans the root");
    require(gui::record_i16(line, field::height) == 15, "fixed height");
    require(gui::record_string(line, field::text) == "Line", "label text");
    require(gui::gadget_attributes(line) == 2, "label attributes");

    auto source = table->records[1];
    source.refs.sprite = &source;
    require(gui::append_button_copy(table->records, source), "button appended");
    require(gui::gadget_type_of(table->records[8]) == gui::gadget_type::button, "copied type");
    require(table->records[8].refs.sprite == &source, "sprite pointer copied with the prefix");
    auto hot = table->records[1];
    hot.refs.hot_callback = [](oa::ui::gui_input::GadgetPanel&, int32_t) {};
    require(gui::append_hot_surface_copy(table->records, hot), "hot surface appended");
    require(table->records[9].refs.hot_callback == nullptr, "hot callback reset");
    require(
        gui::record_u32(table->records[9], field::hot_sequence) == 0, "hot sequence slot reset"
    );

    for (int index = 0; index < 400; ++index)
        static_cast<void>(gui::append_progress_copy(table->records, source));
    require(
        gui::record_i16(table->records[0], field::record_count) == 199, "appends stop at capacity"
    );
}

// Every GUI file the installed game provides.
void installed_gui_sweep(const oa::AssetStore& assets) {
    std::size_t files = 0;
    std::size_t records = 0;
    for (const auto& name : assets.list_effective("guis", "")) {
        const auto bytes = oa::test::read_game_file(assets, name);
        const auto parsed = gui::parse(bytes);
        if (!parsed.ok())
            continue;
        auto table = std::make_unique<gui::GadgetTable>();
        const auto count = gui::load_gadget_records(*parsed.layout, table->records);
        require(count == parsed.layout->gadgets.size(), "every installed gadget fits the table");
        require(
            gui::record_i16(table->records[0], field::record_count) ==
                static_cast<int16_t>(count - 1),
            "root count is the last index"
        );
        for (std::size_t index = 0; index < count; ++index) {
            const auto& gadget = parsed.layout->gadgets[index];
            const auto& record = table->records[index];
            require(
                gui::gadget_type_of(record) == static_cast<uint8_t>(gadget.common.type), "type byte"
            );
            require(gui::record_string(record, field::name) == gadget.common.name, "name field");
            require(gui::record_i16(record, field::x) == gadget.common.x, "x field");
            require(gui::record_i16(record, field::height) == gadget.common.height, "height field");
            if (const auto* button = std::get_if<gui::ButtonFields>(&gadget.fields)) {
                require(
                    gui::record_i16(record, field::button_status) == button->status, "button status"
                );
                require(
                    gui::record_string(record, field::text).substr(0, 0x80) ==
                        std::string_view(button->text).substr(0, 0x80),
                    "button caption"
                );
            }
            if (index != 0 && !gadget.common.name.empty() &&
                gui::find_gadget(table->records, gadget.common.name) < 0) {
                throw std::runtime_error("installed name not found: " + gadget.common.name);
            }
            ++records;
        }
        ++files;
    }
    require(files > 100, "the installed GUI files parsed");
    std::cout << "loaded " << records << " records from " << files << " installed GUI files\n";
}

} // namespace

int main(int argc, char** argv) {
    const bool installed = oa::test::game_data_requested(argc, argv);
    const auto assets = installed ? std::optional<oa::AssetStore>(
                                        oa::test::require_game_assets("the installed GUI sweep")
                                    )
                                  : std::nullopt;
    try {
        if (assets) {
            installed_gui_sweep(*assets);
        } else {
            loaders_write_record_offsets();
            lookup_and_geometry();
            record_construction();
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
