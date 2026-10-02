// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"

#include "oa/ui/hud/build_page_fit.hpp"
#include "oa/ui/hud/order_panel.hpp"

#include <cstring>
#include <string_view>
#include <vector>

using namespace oa::ui::hud;

namespace {

// The side panel's top edge, under the radar picture; gadget positions below
// include it, as a loaded page's do.
constexpr int32_t kPanelTop = 128;

PanelGadget button(const char* name, int32_t x, int32_t y, int32_t width, int32_t height) {
    return {name, x, kPanelTop + y, width, height, kGadgetTypeButton, 0, true};
}

PanelGadget unit(const char* name, int32_t x, int32_t y) {
    auto gadget = button(name, x, y, kBuildButtonSize, kBuildButtonSize);
    gadget.common_attributes = kCommonUnitButton;
    return gadget;
}

PanelGadget empty_slot(int32_t x, int32_t y) {
    return button("IGPATCH", x, y, kBuildButtonSize, kBuildButtonSize);
}

// A 3.1c commander page: six build buttons under ORDERS and BUILD tabs, the
// order buttons below them, all inside the 480-row column.
std::vector<PanelGadget> stock_page() {
    return {
        {"HEADER", 0, kPanelTop, 128, 352, 0, 0, true},
        {"ARMFONT", 0, 0, 0, 0, 7, 0, true},
        button("ARMPREV", 8, 222, 45, 17),
        button("ARMNEXT", 72, 222, 45, 17),
        unit("ARMSOLAR", 0, 27),
        unit("ARMWIN", 64, 27),
        unit("ARMESTOR", 0, 91),
        unit("ARMMSTOR", 64, 91),
        unit("ARMMEX", 0, 155),
        unit("ARMMAKR", 64, 155),
        button("ARMORDERS", 3, 4, 58, 18),
        button("ARMBUILD", 65, 4, 58, 18),
        button("ARMMOVE", 5, 247, 54, 30),
        button("ARMSTOP", 64, 247, 54, 30),
        button("ARMATTACK", 5, 317, 54, 30),
        button("ARMBLAST", 64, 317, 54, 30),
    };
}

// A commander page of twelve build buttons with the order buttons on the
// same page under them, reaching row 768.
std::vector<PanelGadget> tall_page(int32_t units = 12) {
    static constexpr const char* names[] = {
        "ARMSOLAR",
        "ARMWIN",
        "ARMESTOR",
        "ARMMSTOR",
        "ARMMEX",
        "ARMMAKR",
        "ARMLAB",
        "ARMVP",
        "ARMAP",
        "ARMSY",
        "ARMLLT",
        "ARMRAD",
    };
    std::vector<PanelGadget> page{
        {"HEADER", 0, kPanelTop, 128, 640, 0, 0, true},
        {"ARMFONT", 0, 0, 0, 0, 7, 0, true},
        button("ARMBUILD", 3, 4, 58, 18),
        button("ARMPREV", 8, 414, 45, 17),
        button("ARMNEXT", 72, 414, 45, 17),
    };
    for (int32_t slot = 0; slot < 12; ++slot) {
        const int32_t x = (slot % 2) * kBuildButtonSize;
        const int32_t y = 27 + (slot / 2) * kBuildButtonSize;
        page.push_back(slot < units ? unit(names[slot], x, y) : empty_slot(x, y));
    }
    for (const auto& gadget :
         {button("ARMFIREORD", 6, 434, 113, 21),
          button("ARMMOVEORD", 6, 456, 113, 21),
          button("ARMCLOAK", 6, 478, 113, 21),
          button("ARMMOVE", 5, 502, 54, 30),
          button("ARMSTOP", 64, 502, 54, 30),
          button("ARMDEFEND", 5, 529, 54, 30),
          button("ARMPATROL", 64, 529, 54, 30),
          button("ARMATTACK", 5, 556, 54, 30),
          button("ARMBLAST", 64, 556, 54, 30),
          button("ARMRECLAIM", 5, 583, 54, 30),
          button("ARMREPAIR", 64, 583, 54, 30),
          button("ARMCAPTURE", 5, 610, 54, 30)})
        page.push_back(gadget);
    return page;
}

const PanelGadget& named(const std::vector<PanelGadget>& page, std::string_view name) {
    for (const auto& gadget : page)
        if (name == gadget.name)
            return gadget;
    std::fprintf(stderr, "no gadget %.*s\n", static_cast<int>(name.size()), name.data());
    std::exit(1);
}

bool same(const std::vector<PanelGadget>& a, const std::vector<PanelGadget>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t index = 0; index < a.size(); ++index)
        if (std::strcmp(a[index].name, b[index].name) != 0 || a[index].x != b[index].x ||
            a[index].y != b[index].y || a[index].width != b[index].width ||
            a[index].height != b[index].height || a[index].shown != b[index].shown)
            return false;
    return true;
}

std::vector<std::string_view> shown_units(const std::vector<PanelGadget>& page) {
    std::vector<std::string_view> names;
    for (const auto& gadget : page)
        if (gadget.shown && (gadget.common_attributes & kCommonUnitButton) != 0)
            names.emplace_back(gadget.name);
    return names;
}

// A page that ends inside the column is left exactly as authored, at the
// smallest column a match draws.
void leaves_a_page_that_fits() {
    const auto authored = stock_page();
    auto page = authored;
    const auto fit = fit_build_page(page, 480, 0);
    CHECK(fit.layout == BuildPageLayout::as_authored);
    CHECK(fit.part_count == 1 && fit.part == 0 && fit.build_tab == -1);
    CHECK(fit.bottom == kPanelTop + 347);
    CHECK(same(page, authored));

    auto tall = tall_page();
    const auto tall_authored = tall;
    CHECK(fit_build_page(tall, 768, 0).layout == BuildPageLayout::as_authored);
    CHECK(same(tall, tall_authored));
}

// With room for three rows beside the order buttons, the page keeps them
// and shows its build buttons three rows at a time.
void shares_the_page_when_three_rows_fit() {
    auto page = tall_page();
    const auto fit = fit_build_page(page, 600, 0);
    CHECK(fit.layout == BuildPageLayout::shared);
    CHECK(fit.rows == 3 && fit.part_count == 2 && fit.part == 0);
    CHECK(fit.bottom <= 600);
    CHECK(
        (shown_units(page) == std::vector<std::string_view>{
                                  "ARMSOLAR", "ARMWIN", "ARMESTOR", "ARMMSTOR", "ARMMEX", "ARMMAKR"
                              })
    );
    // The order buttons move up by the three rows given up.
    CHECK(named(page, "ARMFIREORD").y == kPanelTop + 434 - 3 * kBuildButtonSize);
    CHECK(named(page, "ARMCAPTURE").shown);
    CHECK(named(page, "ARMNEXT").y == kPanelTop + 414 - 3 * kBuildButtonSize);

    page = tall_page();
    const auto second = fit_build_page(page, 600, 1);
    CHECK(second.part == 1);
    CHECK(
        (shown_units(page) ==
         std::vector<std::string_view>{"ARMLAB", "ARMVP", "ARMAP", "ARMSY", "ARMLLT", "ARMRAD"})
    );
    CHECK(named(page, "ARMLAB").x == 0 && named(page, "ARMLAB").y == kPanelTop + 27);
    CHECK(named(page, "ARMRAD").x == 64 && named(page, "ARMRAD").y == kPanelTop + 27 + 128);
}

// A 1920x1080 window's column of 540 rows splits the page under ORDERS and
// BUILD tabs: ten build buttons, then the last two.
void splits_into_tabs_when_three_rows_do_not_fit() {
    auto page = tall_page();
    const auto fit = fit_build_page(page, 540, 0);
    CHECK(fit.layout == BuildPageLayout::build_tab);
    CHECK(fit.rows == 5 && fit.part_count == 2);
    CHECK(fit.bottom <= 540);
    CHECK(shown_units(page).size() == 10);
    CHECK(!named(page, "ARMFIREORD").shown && !named(page, "ARMCAPTURE").shown);
    const auto& build = named(page, "ARMBUILD");
    CHECK(fit.build_tab == static_cast<int32_t>(&build - page.data()));
    CHECK(build.x == kBuildTabX && build.y == kPanelTop + 4);
    CHECK(fit.orders_tab_x == kOrdersTabX && fit.orders_tab_y == kPanelTop + 4);
    CHECK(named(page, "ARMPREV").y == kPanelTop + 414 - kBuildButtonSize);

    page = tall_page();
    CHECK(fit_build_page(page, 540, kLastBuildPart).part == 1);
    CHECK((shown_units(page) == std::vector<std::string_view>{"ARMLLT", "ARMRAD"}));

    // The 640x480 column has room for four rows.
    page = tall_page();
    const auto small = fit_build_page(page, 480, 0);
    CHECK(small.layout == BuildPageLayout::build_tab);
    CHECK(small.rows == 4 && small.part_count == 2);
    CHECK(small.bottom <= 480);
}

// Empty slots after the last build button are padding and make no part of
// their own.
void drops_trailing_empty_slots() {
    auto page = tall_page(7);
    const auto fit = fit_build_page(page, 540, 0);
    CHECK(fit.rows == 4 && fit.part_count == 1);
    CHECK(shown_units(page).size() == 7);
    for (const auto& gadget : page)
        if (std::string_view(gadget.name) == "IGPATCH")
            CHECK(!gadget.shown);
}

// A page without square build buttons, such as the general orders page, is
// never moved, however short the column.
void leaves_pages_without_build_buttons() {
    std::vector<PanelGadget> page{
        {"HEADER", 0, kPanelTop, 128, 640, 0, 0, true},
        button("ARMMOVE", 5, 600, 54, 30),
    };
    const auto authored = page;
    CHECK(fit_build_page(page, 480, 0).layout == BuildPageLayout::as_authored);
    CHECK(same(page, authored));
}

} // namespace

int main() {
    leaves_a_page_that_fits();
    shares_the_page_when_three_rows_fit();
    splits_into_tabs_when_three_rows_do_not_fit();
    drops_trailing_empty_slots();
    leaves_pages_without_build_buttons();
    return 0;
}
