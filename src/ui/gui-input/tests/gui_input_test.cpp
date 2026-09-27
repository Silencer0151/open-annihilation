// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/gui_input.hpp"

#include "oa/ui/gui_layout/gui_gadget.hpp"
#include "oa/test/game_assets.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <vector>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

oa::ui::gui_layout::Gadget gadget(
    oa::ui::gui_layout::GadgetType type,
    const char* name,
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t height
) {
    oa::ui::gui_layout::Gadget result;
    result.common.type = type;
    result.common.name = name;
    result.common.x = x;
    result.common.y = y;
    result.common.width = width;
    result.common.height = height;
    return result;
}

// SPEEDS.GUI as the speeds screen leaves it: the label pass ORs 8 into the attribs of
// the seven TEXT/TXTTEXT/GAMETEXT labels (18 in the file) and leaves the
// sliders, stage buttons and the root (attribs 52685) as loaded.
void check_speeds_panel_labels(const oa::AssetStore& assets) {
    const auto bytes = oa::test::read_game_file(assets, "guis/speeds.gui");
    require(!bytes.empty(), "the install holds guis/speeds.gui");
    auto parsed = oa::ui::gui_layout::parse(bytes);
    require(parsed.ok(), "speeds.gui parses");
    auto& gadgets = parsed.layout->gadgets;
    require(gadgets.size() == 14, "speeds.gui has the root and 13 records");
    oa::ui::gui_input::mark_label_shadows(gadgets);
    constexpr std::array<int32_t, 14> expected{
        52685,
        1,
        0x1A,
        1,
        0x1A,
        1,
        0x1A,
        1,
        0x1A,
        0x1A,
        0x1A,
        1,
        0x1A,
        1,
    };
    for (std::size_t index = 0; index < expected.size(); ++index)
        require(
            gadgets[index].common.attributes == expected[index],
            "speeds.gui attribs after the label walk"
        );
}

// SKIRMISH.GUI's Difficulty ("Easy|Medium|Hard", three stages, attribs 1)
// released over and over, then the attributes and states that keep a stage.
void check_released_button_stage() {
    namespace attribute = oa::ui::gui_layout::attribute;
    auto difficulty =
        gadget(oa::ui::gui_layout::GadgetType::button, "Difficulty", 461, 327, 108, 20);
    difficulty.common.attributes = 1;
    oa::ui::gui_layout::ButtonFields fields;
    fields.text = "Easy|Medium|Hard";
    fields.stages = 3;
    difficulty.fields = fields;
    require(oa::ui::gui_input::released_button_stage(difficulty, 0) == 1, "Easy steps to Medium");
    require(oa::ui::gui_input::released_button_stage(difficulty, 1) == 2, "Medium steps to Hard");
    require(oa::ui::gui_input::released_button_stage(difficulty, 2) == 0, "Hard wraps to Easy");
    require(
        oa::ui::gui_input::released_button_stage(difficulty, 7) == 0, "a stage past the last wraps"
    );
    for (const auto keeps :
         {attribute::text_list,
          attribute::toggle,
          attribute::checkbox,
          attribute::cycle_frames,
          attribute::scroll_step_back,
          attribute::scroll_step_forward}) {
        auto kept = difficulty;
        kept.common.attributes = static_cast<int32_t>(keeps | 1U);
        require(
            oa::ui::gui_input::released_button_stage(kept, 1) == 1,
            "hold, toggle, check-box, frame-cycling and scroll-step buttons keep their stage"
        );
    }
    auto grayed = difficulty;
    std::get<oa::ui::gui_layout::ButtonFields>(grayed.fields).grayed_out = true;
    require(
        oa::ui::gui_input::released_button_stage(grayed, 1) == 1, "a grayed button keeps its stage"
    );
    auto plain = difficulty;
    std::get<oa::ui::gui_layout::ButtonFields>(plain.fields).stages = 0;
    require(
        oa::ui::gui_input::released_button_stage(plain, 0) == 0, "a button without stages keeps 0"
    );
    auto single = difficulty;
    std::get<oa::ui::gui_layout::ButtonFields>(single.fields).stages = 1;
    require(oa::ui::gui_input::released_button_stage(single, 0) == 0, "one stage wraps to itself");
    const auto label = gadget(oa::ui::gui_layout::GadgetType::label, "MapName", 0, 0, 10, 10);
    require(oa::ui::gui_input::released_button_stage(label, 2) == 2, "a label keeps its stage");
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv)) {
        const auto assets = oa::test::require_game_assets("the installed SPEEDS.GUI labels");
        try {
            check_speeds_panel_labels(assets);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
        std::cout << "installed gui input vectors passed\n";
        return 0;
    }
    try {
        std::array<oa::ui::gui_layout::Gadget, 3> gadgets{
            gadget(oa::ui::gui_layout::GadgetType::panel, "ROOT", 99, 99, 10, 10),
            gadget(oa::ui::gui_layout::GadgetType::button, "SINGLE", 10, 20, 30, 12),
            gadget(oa::ui::gui_layout::GadgetType::button, "EXIT", 20, 25, 30, 12),
        };
        gadgets[1].common.active = 1;
        gadgets[2].common.active = 1;
        const auto geometry = oa::ui::gui_input::gadget_geometry(gadgets, 1);
        require(
            geometry && geometry->left == 10 && geometry->top == 20 && geometry->right == 39 &&
                geometry->bottom == 31,
            "inclusive geometry"
        );
        oa::ui::gui_input::MenuObject menu{gadgets, -1};
        require(oa::ui::gui_input::hit_test(menu, 10, 20) == 1, "root is not a hit target");
        require(oa::ui::gui_input::hit_test(menu, 39, 31) == 2, "inclusive button boundary hit");
        require(oa::ui::gui_input::hit_test(menu, 20, 25) == 2, "last matching active record");
        require(!oa::ui::gui_input::hit_test(menu, 9, 19), "outside hit");
        // MSNBRIEF.GUI's FONT record lies over PrevMenu; the button keeps the click.
        std::array<oa::ui::gui_layout::Gadget, 3> briefing{
            gadget(oa::ui::gui_layout::GadgetType::panel, "Msnbrief.GUI", 0, 0, 640, 480),
            gadget(oa::ui::gui_layout::GadgetType::button, "PrevMenu", 475, 394, 120, 20),
            gadget(oa::ui::gui_layout::GadgetType::font_resource, "FONT", 454, 293, 186, 136),
        };
        briefing[1].common.active = 1;
        briefing[2].common.active = 1;
        require(
            oa::ui::gui_input::hit_test({briefing, -1}, 535, 404) == 1,
            "a font record takes no click"
        );
        require(
            !oa::ui::gui_input::hit_test({briefing, -1}, 460, 300),
            "nothing to click under a font record alone"
        );
        menu.selected_index = 1;
        require(
            oa::ui::gui_input::hit_test(menu, 20, 25) == 1,
            "preselected scan stops after first active record"
        );

        require(oa::ui::gui_input::button_result(menu, "SINGLE"), "selected name match");
        require(!oa::ui::gui_input::button_result(menu, "single"), "case-sensitive name match");
        require(
            oa::ui::gui_input::button_result(menu, std::string_view("SINGLE\0suffix", 13)),
            "C-string query termination"
        );
        oa::ui::gui_input::clear_selection(menu);
        require(
            !oa::ui::gui_input::button_result(menu, "SINGLE") && menu.selected_index == -1,
            "clear selection"
        );
        require(
            oa::ui::gui_input::physical_key(oa::ui::gui_input::ControlKey::left) ==
                oa::ui::gui_input::VirtualKey::left,
            "left control key mapping"
        );
        require(
            oa::ui::gui_input::navigation_direction(oa::ui::gui_input::ControlKey::down) ==
                oa::ui::gui_input::NavigationDirection::down,
            "down navigation mapping"
        );
        require(
            !oa::ui::gui_input::navigation_direction(oa::ui::gui_input::ControlKey::space),
            "activation key is not directional"
        );
        require(
            oa::ui::gui_input::key_is_down(0x8000) && !oa::ui::gui_input::key_is_down(0x0001),
            "held bit counts, toggle bit alone does not"
        );
        {
            // The virtual key asked for each control code, and the
            // held test on the returned state word.
            struct KeyProbe {
                uint16_t word = 0;
                oa::ui::gui_input::VirtualKey held = oa::ui::gui_input::VirtualKey::shift;
                int queries = 0;
                uint8_t last = 0;
            } probe;

            const auto state = [](void* context, oa::ui::gui_input::VirtualKey key) -> uint16_t {
                auto& p = *static_cast<KeyProbe*>(context);
                ++p.queries;
                p.last = static_cast<uint8_t>(key);
                return key == p.held ? p.word : 0;
            };
            const std::array<std::pair<int32_t, uint8_t>, 8> mapping{{
                {0xF4, 0x25},
                {0xF5, 0x26},
                {0xF6, 0x27},
                {0xF7, 0x28},
                {0x20, 0x20},
                {0xF9, 0x10},
                {0xFA, 0x11},
                {0xFB, 0x12},
            }};
            for (const auto& [code, virtual_key] : mapping) {
                (void)oa::ui::gui_input::control_key_down(code, state, &probe);
                require(probe.last == virtual_key, "control code asks for its virtual key");
            }
            using oa::ui::gui_input::control_key_down;
            probe.word = oa::ui::gui_input::kAsyncKeyHeld;
            require(control_key_down(0xF9, state, &probe), "held shift is down");
            require(!control_key_down(0xFA, state, &probe), "released control is up");
            probe.word = 0x0001;
            require(!control_key_down(0xF9, state, &probe), "toggle bit alone is up");
            probe.word = 0x0002;
            require(control_key_down(0xF9, state, &probe), "any bit above the toggle is down");
            probe.queries = 0;
            require(
                !control_key_down(0xF8, state, &probe) && !control_key_down(0x41, state, &probe) &&
                    !control_key_down(0xFC, state, &probe) && probe.queries == 0,
                "codes without a key are never down and never reach the host"
            );
            require(!control_key_down(0xF9, nullptr, nullptr), "no keyboard, no key");
        }

        std::array<oa::ui::gui_layout::Gadget, 4> labeled{};
        labeled[0].common.type = oa::ui::gui_layout::GadgetType::panel;
        labeled[0].common.attributes = 1;
        oa::ui::gui_layout::PanelFields panel;
        panel.loaded_total_gadgets = 2;
        labeled[0].fields = panel;
        labeled[1].common.type = oa::ui::gui_layout::GadgetType::label;
        labeled[1].common.attributes = 0x11;
        labeled[2].common.type = oa::ui::gui_layout::GadgetType::button;
        labeled[2].common.attributes = 0x11;
        labeled[3].common.type = oa::ui::gui_layout::GadgetType::label;
        labeled[3].common.attributes = 4;
        oa::ui::gui_input::mark_label_shadows(labeled);
        require(labeled[0].common.attributes == 1, "the label pass does not visit the root");
        require(
            labeled[1].common.attributes == (0x11 | oa::ui::gui_input::kLabelShadowAttribute),
            "label within the loaded count gains the shadow bit"
        );
        require(labeled[2].common.attributes == 0x11, "non-label records stay unchanged");
        require(labeled[3].common.attributes == 4, "records past the loaded count stay unchanged");
        panel.loaded_total_gadgets = 3;
        labeled[0].fields = panel;
        oa::ui::gui_input::mark_label_shadows(labeled);
        require(
            labeled[3].common.attributes == (4 | oa::ui::gui_input::kLabelShadowAttribute),
            "loaded count is inclusive"
        );
        const auto marked = labeled[1].common.attributes;
        panel.loaded_total_gadgets = 0;
        labeled[0].fields = panel;
        oa::ui::gui_input::mark_label_shadows(labeled);
        panel.loaded_total_gadgets = -1;
        labeled[0].fields = panel;
        oa::ui::gui_input::mark_label_shadows(labeled);
        require(
            labeled[1].common.attributes == marked, "a non-positive loaded count leaves labels"
        );
        panel.loaded_total_gadgets = 99;
        labeled[0].fields = panel;
        oa::ui::gui_input::mark_label_shadows(labeled);
        require(labeled[1].common.attributes == marked, "count past the span adds no records");
        oa::ui::gui_input::mark_label_shadows(std::span<oa::ui::gui_layout::Gadget>{});
        std::array<oa::ui::gui_layout::Gadget, 1> unlabeled{};
        unlabeled[0].common.type = oa::ui::gui_layout::GadgetType::button;
        unlabeled[0].common.attributes = 3;
        oa::ui::gui_input::mark_label_shadows(unlabeled);
        require(unlabeled[0].common.attributes == 3, "a non-panel root has no loaded count");

        check_released_button_stage();
        std::cout << "gui input vectors passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
