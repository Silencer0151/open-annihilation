// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend_renderer.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    try {
        int first = 1;
        std::string screen = "main";
        const std::string_view option = argc > 1 ? argv[1] : "";
        if (option == "--single" || option == "--skirmish" || option == "--overlay-layout") {
            screen = option.substr(2);
            ++first;
        }
        if (argc - first < 3) {
            std::cerr << "usage: oa-render-main-menu [--single|--skirmish|--overlay-layout] "
                         "DATA_ROOT OUTPUT.ppm ARCHIVE...\n"
                         "The main menu is drawn without its overlay; "
                         "--overlay-layout takes the layout made for it anyway.\n";
            return 2;
        }
        oa::AssetStore assets(argv[first]);
        for (int index = first + 2; index < argc; ++index)
            assets.mount(argv[index]);
        oa::ui::frontend_renderer::ScreenResources resources;
        if (screen == "main" || screen == "overlay-layout") {
            resources = oa::ui::frontend_renderer::load_main_menu(
                assets,
                screen == "main" ? oa::ui::frontend_renderer::MainMenuLayout::base_game
                                 : oa::ui::frontend_renderer::MainMenuLayout::with_overlay
            );
        } else if (screen == "single") {
            resources = oa::ui::frontend_renderer::load_screen(
                assets,
                {"guis/single.gui",
                 "bitmaps/singlebg.pcx",
                 "palettes/guipal.pal",
                 "anims/single.gaf",
                 "anims/commongui.gaf"}
            );
        } else {
            resources = oa::ui::frontend_renderer::load_screen(
                assets,
                {"guis/skirmish.gui",
                 "bitmaps/skirmsetup4x.pcx",
                 "palettes/guipal.pal",
                 "anims/skirmish.gaf",
                 "anims/commongui.gaf"}
            );
        }
        const auto rendered = oa::ui::frontend_renderer::render_screen(resources);
        std::ofstream output(argv[first + 1], std::ios::binary);
        output << "P6\n" << rendered.width << ' ' << rendered.height << "\n255\n";
        output.write(
            reinterpret_cast<const char*>(rendered.rgb.data()),
            static_cast<std::streamsize>(rendered.rgb.size())
        );
        output.close();
        if (!output)
            throw std::runtime_error("cannot write output PPM");
        std::cout << rendered.width << 'x' << rendered.height << " -> " << argv[first + 1] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "oa-render-main-menu: " << error.what() << '\n';
        return 1;
    }
}
