// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend_state/map_selection.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace oa::ui::frontend_state;
namespace map = oa::ui::frontend_state::map_selection;

namespace {
void check(bool v, const char* text) {
    if (!v)
        throw std::runtime_error(text);
}

struct TestHost final : map::Host {
    std::vector<std::string> calls, names{"Zulu", "alpha", "Beta"};
    std::array<uint32_t, 3> buttons{};
    int16_t selected{};
    int32_t select_result = 1;
    int32_t count_override = -1;
    bool have_title = true, load_success = true;
    map::PictureHandle image{7};

    int32_t map_count() override {
        calls.push_back("count");
        return count_override < 0 ? static_cast<int32_t>(names.size()) : count_override;
    }

    std::vector<std::string> copy_map_names() override {
        calls.push_back("copy");
        return names;
    }

    std::string translate_ui(std::string_view s) override {
        calls.push_back("translate:" + std::string(s));
        return std::string(s);
    }

    void
    show_frontend_message(std::string_view text, int32_t width, int32_t a, int32_t b) override {
        check(text == map::no_maps_message && width == 320 && a == 1 && b == 1, "no-maps message");
        calls.push_back("message");
    }

    map::MenuHandle load_modal(std::string_view name, uint32_t flags) override {
        check(name == "SELMAP.GUI" && flags == 0x880, "modal resource and flags");
        calls.push_back("load");
        return {11};
    }

    void install_map_event_callback(map::MenuHandle m) override {
        check(m.value == 11, "modal callback identity");
        calls.push_back("callback");
    }

    void load_background(std::string_view s) override {
        check(s == "DSELECTMAP2", "background");
        calls.push_back("background");
    }

    void bind_map_names(std::span<const std::string> values) override {
        calls.push_back("bind");
        names.assign(values.begin(), values.end());
    }

    void install_map_selection_callback() override { calls.push_back("list-callback"); }

    void set_selected_map_index(int16_t i) override {
        calls.push_back("select-index:" + std::to_string(i));
        selected = i;
    }

    int16_t selected_map_index() override {
        calls.push_back("index");
        return selected;
    }

    void set_input_enabled(int32_t i) override {
        check(i == 1, "input argument");
        calls.push_back("input");
    }

    void add_menu_flags(uint32_t i) override {
        check(i == 0x40, "input flags");
        calls.push_back("flags");
    }

    void select_cursor_animation(uint32_t i) override {
        check(i == 19, "active cursor");
        calls.push_back("cursor");
    }

    uint32_t button_result(map::MenuHandle m, map::Button b) override {
        check(m.value == 12, "event identity");
        calls.push_back("button:" + std::string(map::resource_name(b)));
        return buttons[static_cast<std::size_t>(b)];
    }

    void play_ui_sound(std::string_view s, uint32_t zero) override {
        check(zero == 0, "sound argument");
        calls.push_back("sound:" + std::string(s));
    }

    void clear_event_selection(map::MenuHandle m) override {
        check(m.value == 12, "clear event");
        calls.push_back("clear");
    }

    int32_t select_map(std::string_view s) override {
        calls.push_back("map:" + std::string(s));
        return select_result;
    }

    void set_parent_map_name(map::MenuHandle m, std::string_view s) override {
        check(m.value == 12, "parent text event identity");
        calls.push_back("parent:" + std::string(s));
    }

    bool has_map_name_widget() override {
        calls.push_back("has-title");
        return have_title;
    }

    std::string map_display_name() override { return "Display Name"; }

    std::string map_memory_requirement_text() override { return "16 mb"; }

    std::string permitted_player_counts_text() override { return "2,4"; }

    std::string map_description() override { return "Description"; }

    std::string terrain_resource_path() override { return "maps/map.tnt"; }

    void set_modal_text(std::string_view widget, std::string_view text, int32_t arg) override {
        check(arg == 0, "text zero");
        calls.push_back("text:" + std::string(widget) + ":" + std::string(text));
    }

    map::PictureHandle picture() override { return image; }

    void set_picture(map::PictureHandle h) override {
        image = h;
        calls.push_back("picture:" + std::to_string(h.value));
    }

    void release_picture(map::PictureHandle h) override {
        check(h.value == 7 || h.value == 8, "release identity");
        calls.push_back("release:" + std::to_string(h.value));
    }

    map::LoadedPicture load_picture(std::string_view path) override {
        check(path == "maps/map.tnt", "terrain path");
        calls.push_back("load-picture");
        return {{load_success ? 8U : 0U}, 64, 128};
    }

    map::PictureSize picture_size() override { return {128, 96}; }

    void fit_picture(
        map::PictureHandle h, int32_t w, int32_t hgt, int32_t worldw, int32_t worldh
    ) override {
        check(
            h.value == 8 && w == 128 && hgt == 96 && worldw == 1024 && worldh == 2048,
            "preview fit arguments"
        );
        calls.push_back("fit");
    }

    void invalidate_menu() override { calls.push_back("invalidate"); }

    bool saw(std::string_view text) const {
        return std::find(calls.begin(), calls.end(), text) != calls.end();
    }
};

void sort_tests() {
    std::vector<std::string> names{"z", "Alpha", "alpha", "B"};
    map::sort_map_names(names);
    check(
        names == std::vector<std::string>{"Alpha", "alpha", "B", "z"}, "stable case folded order"
    );
    names = {"z", "B", "\\folder", "d", "A"};
    map::sort_map_names(names);
    check(
        names == std::vector<std::string>{"\\folder", "B", "z", "A", "d"},
        "last backslash partition"
    );
    names.assign(3001, "x");
    bool caught = false;
    try {
        map::sort_map_names(names);
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    check(caught, "entry count bound");
    names.assign(500, std::string(200, 'a'));
    caught = false;
    try {
        map::sort_map_names(names);
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    check(caught, "byte count bound");
}

void modal_tests() {
    game_entry::SkirmishSettings settings;
    settings.map_name = "Beta";
    map::ModalState modal;
    TestHost h;
    check(map::open(modal, settings, h), "modal opens");
    check(
        modal.active && modal.names == std::vector<std::string>{"alpha", "Beta", "Zulu"} &&
            h.selected == 1,
        "sort and exact selection"
    );
    check(
        settings.map_name == "Beta" && h.saw("map:Beta") && h.calls.back() == "cursor",
        "preview preserves saved name"
    );
    check(h.saw("text:SIZE:16 mb  Players: 2,4") && h.saw("fit"), "metadata preview");
    auto release = std::find(h.calls.begin(), h.calls.end(), "release:7");
    check(
        release != h.calls.end() && *(release + 1) == "picture:0" &&
            *(release + 2) == "load-picture" && *(release + 3) == "picture:8",
        "replace preview ownership order"
    );
    h.calls.clear();
    h.selected = 2;
    map::preview_selection(modal, h);
    check(settings.map_name == "Beta" && h.saw("map:Zulu"), "browse does not commit");
    h.calls.clear();
    h.buttons[1] = 0x100;
    h.select_result = 0;
    map::handle_event(modal, settings, {{12}, 0}, h);
    check(
        settings.map_name == "Zulu" && h.calls ==
                                           std::vector<std::string>{
                                               "button:MAPNAMES",
                                               "button:LOAD",
                                               "button:LOAD",
                                               "sound:SmallButton",
                                               "index",
                                               "map:Zulu",
                                               "parent:Zulu"
                                           },
        "LOAD whole-word result/repeated check/ignored select result"
    );
    h.calls.clear();
    h.buttons = {1, 0, 0};
    h.selected = 0;
    map::handle_event(modal, settings, {{12}, 0}, h);
    check(
        settings.map_name == "alpha" &&
            h.calls ==
                std::vector<std::string>{
                    "button:MAPNAMES", "button:LOAD", "index", "map:alpha", "parent:alpha"
                },
        "list commit omits sound"
    );
    h.calls.clear();
    h.buttons = {0, 0, 1};
    map::handle_event(modal, settings, {{12}, 0}, h);
    check(
        settings.map_name == "alpha" && h.calls.back() == "sound:Previous" && !h.saw("clear"),
        "cancel policy"
    );
    h.calls.clear();
    h.buttons = {};
    map::handle_event(modal, settings, {{12}, 0}, h);
    check(h.calls.back() == "clear", "unknown event default");
    h.calls.clear();
    map::handle_event(modal, settings, {{12}, -1}, h);
    check(
        !modal.active && modal.names.empty() && h.image.value == 0 &&
            h.calls == std::vector<std::string>{"release:8", "picture:0"},
        "destroy cleanup"
    );
    TestHost empty;
    empty.names.clear();
    check(
        !map::open(modal, settings, empty) && !modal.active &&
            empty.calls ==
                std::vector<std::string>{
                    "count", "translate:There are no skirmish maps to choose from", "message"
                },
        "no maps branch"
    );
    TestHost no_preview;
    no_preview.select_result = 0;
    check(
        map::open(modal, settings, no_preview) && !no_preview.saw("has-title"),
        "failed preview select skips updates"
    );
    no_preview.calls.clear();
    no_preview.select_result = 1;
    no_preview.load_success = false;
    no_preview.have_title = false;
    map::preview_selection(modal, no_preview);
    check(
        no_preview.image.value == 0 && !no_preview.saw("fit") &&
            !no_preview.saw("text:MAPNAME:Display Name") &&
            no_preview.saw("text:DESCRIPTION:Description"),
        "null thumbnail still updates description"
    );
    no_preview.selected = -1;
    bool caught = false;
    try {
        map::preview_selection(modal, no_preview);
    } catch (const std::out_of_range&) {
        caught = true;
    }
    check(caught, "invalid selection rejected");
}
} // namespace

int main() {
    try {
        sort_tests();
        modal_tests();
        std::cout << "map selection passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
