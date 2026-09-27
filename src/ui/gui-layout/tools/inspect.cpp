// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/gui_layout.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: oa-gui-layout-inspect FILE.GUI...\n";
        return 2;
    }
    bool failed = false;
    for (int index = 1; index < argc; ++index) {
        const std::filesystem::path path(argv[index]);
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            std::cerr << path.string() << ": cannot open\n";
            failed = true;
            continue;
        }
        std::vector<uint8_t> bytes(
            (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>()
        );
        const auto result = oa::ui::gui_layout::parse(bytes);
        if (!result.ok()) {
            std::cerr << path.string() << ": " << result.error->message << " (offset "
                      << result.error->offset << ")\n";
            failed = true;
            continue;
        }
        const auto& root = result.layout->gadgets.front();
        std::cout << path.string() << '\t' << result.layout->gadgets.size() << '\t'
                  << root.common.name << '\n';
    }
    return failed ? 1 : 0;
}
