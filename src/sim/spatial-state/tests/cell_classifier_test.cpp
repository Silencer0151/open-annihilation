// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/spatial_state/cell_classifier.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace spatial = oa::sim::spatial_state;

void require(bool value, std::string_view message) {
    if (!value)
        throw std::runtime_error(std::string(message));
}

int main() {
    try {
        std::vector<spatial::Unit> units(3);
        units[1].id = 1;
        units[2].id = 2;
        spatial::World world;
        world.terrain_width = 8;
        world.terrain_height = 8;
        world.plots.resize(64);
        world.units = units;
        world.sea_level = 20;
        for (auto& plot : world.plots) {
            plot.low_height = 20;
            plot.high_height = 20;
        }
        spatial::CellClassifier movement{2, 2, 10, -10, 6, 3, 8, 4, 70};
        require(
            spatial::classify_movement_cell(movement, 2, 2, world) == 3, "clear footprint/perimeter"
        );
        world.plots[2 * 8 + 2].high_height = 24;
        require(
            spatial::classify_movement_cell(movement, 2, 2, world) == 1, "preferred land slope band"
        );
        world.plots[2 * 8 + 2].high_height = 27;
        require(spatial::classify_movement_cell(movement, 2, 2, world) == 0, "maximum land slope");
        world.plots[2 * 8 + 2].high_height = 20;
        world.plots[1 * 8 + 2].high_height = 24;
        require(
            spatial::classify_movement_cell(movement, 2, 2, world) == 1, "perimeter degradation"
        );
        world.plots[1 * 8 + 2].high_height = 20;
        world.plots[2 * 8 + 2].ground = 1;
        units[1].object_present = true;
        units[1].object_tick = 69;
        require(spatial::classify_movement_cell(movement, 2, 2, world) == 0, "old occupant blocks");
        units[1].object_tick = 70;
        require(
            spatial::classify_movement_cell(movement, 2, 2, world) == 3,
            "current occupant is ignored"
        );
        units[1].object_present = false;
        require(
            spatial::classify_movement_cell(movement, 2, 2, world) == 0,
            "occupant without object blocks"
        );
        world.plots[2 * 8 + 2].ground = 0;
        world.plots[2 * 8 + 2].blocking_feature = true;
        require(spatial::classify_movement_cell(movement, 2, 2, world) == 0, "blocking feature");
        world.plots[2 * 8 + 2].blocking_feature = false;
        require(
            spatial::classify_movement_cell(movement, 0, 2, world) == 1,
            "missing perimeter is class one"
        );
        require(
            spatial::classify_movement_cell(movement, 7, 2, world) == 0, "footprint outside map"
        );
        world.plots[2 * 8 + 2].low_height = 19;
        world.plots[2 * 8 + 2].high_height = 24;
        require(
            spatial::classify_movement_cell(movement, 2, 2, world) == 1,
            "preferred underwater slope band"
        );
        world.plots[2 * 8 + 2].high_height = 28;
        require(
            spatial::classify_movement_cell(movement, 2, 2, world) == 0, "maximum underwater slope"
        );

        // Single-plot class used by the whole-map build.
        auto& plot = world.plots[2 * 8 + 2];
        require(
            spatial::classify_movement_plot(movement, 2, 2, world) == 0,
            "plot: maximum underwater slope"
        );
        plot.high_height = 26;
        require(
            spatial::classify_movement_plot(movement, 2, 2, world) == 1,
            "plot: underwater slope band"
        );
        plot.high_height = 23;
        require(
            spatial::classify_movement_plot(movement, 2, 2, world) == 3,
            "plot: preferred underwater slope"
        );
        plot.low_height = 20;
        plot.high_height = 24;
        require(
            spatial::classify_movement_plot(movement, 2, 2, world) == 1, "plot: land slope band"
        );
        plot.high_height = 27;
        require(
            spatial::classify_movement_plot(movement, 2, 2, world) == 0, "plot: maximum land slope"
        );
        plot.high_height = 20;
        require(spatial::classify_movement_plot(movement, 2, 2, world) == 3, "plot: clear");
        plot.low_height = 9;
        require(spatial::classify_movement_plot(movement, 2, 2, world) == 0, "plot: too deep");
        plot.low_height = 20;
        plot.high_height = 31;
        require(spatial::classify_movement_plot(movement, 2, 2, world) == 0, "plot: too shallow");
        plot.high_height = 20;
        plot.ground = 1;
        units[1].object_present = true;
        units[1].object_tick = 69;
        require(
            spatial::classify_movement_plot(movement, 2, 2, world) == 0, "plot: old occupant blocks"
        );
        units[1].object_tick = 70;
        require(
            spatial::classify_movement_plot(movement, 2, 2, world) == 3,
            "plot: recent occupant is ignored"
        );
        plot.ground = 2;
        require(
            spatial::classify_movement_plot(movement, 2, 2, world) == 0,
            "plot: occupant without object blocks"
        );
        plot.ground = 0;
        plot.blocking_feature = true;
        require(
            spatial::classify_movement_plot(movement, 2, 2, world) == 0, "plot: blocking feature"
        );
        plot.blocking_feature = false;
        require(spatial::classify_movement_plot(movement, 8, 2, world) == 0, "plot: outside map");
        require(
            spatial::classify_movement_plot(movement, -1, 2, world) == 0, "plot: negative cell"
        );
        std::cout << "cell classifier tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
