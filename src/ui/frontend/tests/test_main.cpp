// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "test_support.hpp"

#include <optional>

namespace oa::ui::frontend::test {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

int& failures() {
    static int count = 0;
    return count;
}

oa::AssetStore*& game_assets() {
    static oa::AssetStore* assets = nullptr;
    return assets;
}

} // namespace oa::ui::frontend::test

// Without arguments the self-contained cases; with --data the cases over the
// installed game.
int main(int argc, char** argv) {
    const bool installed = oa::test::game_data_requested(argc, argv);
    std::optional<oa::AssetStore> assets;
    if (installed) {
        assets.emplace(
            oa::test::require_game_assets("the frontend panels over the installed GUI files")
        );
        oa::ui::frontend::test::game_assets() = &*assets;
    }
    std::size_t ran = 0;
    for (const auto& test : oa::ui::frontend::test::registry()) {
        if (test.game_data != installed)
            continue;
        const int before = oa::ui::frontend::test::failures();
        test.fn();
        ++ran;
        std::printf(
            "%s %s\n", oa::ui::frontend::test::failures() == before ? "ok  " : "FAIL", test.name
        );
    }
    std::printf("%zu tests, %d failed checks\n", ran, oa::ui::frontend::test::failures());
    return oa::ui::frontend::test::failures() == 0 ? 0 : 1;
}
