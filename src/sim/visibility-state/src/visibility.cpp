// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/visibility_state.hpp"
#include <bit>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <charconv>
#include <cstdint>

namespace oa::sim::visibility_state {
const TerrainCell* TerrainGrid::at(int32_t x, int32_t z) const noexcept {
    if (x < 0 || z < 0 || x >= width || z >= height)
        return nullptr;
    const auto index =
        static_cast<std::size_t>(z) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    return index < cells.size() ? &cells[index] : nullptr;
}

SpeedResult
initialize_terrain_speed(SpeedUnit& unit, const TerrainGrid& terrain, SpeedHost* script) {
    if (!(unit.type_speed > 0.0F))
        return {};
    uint16_t sum = 0;
    const auto end_z = static_cast<int32_t>(unit.grid_z) + unit.footprint_z;
    for (int32_t z = unit.grid_z; z < end_z; ++z) {
        const auto end_x = static_cast<int32_t>(unit.grid_x) + unit.footprint_x;
        for (int32_t x = unit.grid_x; x < end_x; ++x)
            if (const auto* cell = terrain.at(x, z))
                sum = static_cast<uint16_t>(sum + cell->movement_cost + 1U);
    }
    const auto signed_sum = std::bit_cast<int16_t>(sum);
    unit.speed =
        static_cast<float>(static_cast<double>(unit.type_speed) * static_cast<double>(signed_sum));
    if (unit.script_present) {
        if (!script)
            throw std::invalid_argument("scripted unit requires SetSpeed host");
        script->set_speed(signed_sum);
    }
    return {sum, true};
}

std::vector<AltitudeCell> build_altitude_cells(
    std::span<const uint8_t> heights, int32_t width, int32_t height, uint8_t minimum_height
) {
    if (width < 0 || height < 0 ||
        heights.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
        throw std::invalid_argument("terrain height lattice dimensions are invalid");
    if (width > 32767 || height > 32767)
        throw std::invalid_argument("terrain height lattice exceeds signed-word bounds");
    const auto out_width = width / 2, out_height = height / 2;
    std::vector<AltitudeCell> result(
        static_cast<std::size_t>(out_width) * static_cast<std::size_t>(out_height),
        AltitudeCell{0, 255}
    );
    const auto apply = [&](std::optional<std::size_t> target, uint8_t value) {
        if (!target)
            return;
        auto& cell = result[*target];
        cell.high_height = std::max(cell.high_height, value);
        cell.low_height = std::min(cell.low_height, value);
    };
    for (int32_t x = 0; x < width; ++x) {
        std::optional<std::size_t> previous_left, previous_right;
        const auto left_x = x == 0 ? -1 : (x - 1) / 2, right_x = x / 2;
        for (int32_t z = 0; z < height; ++z) {
            const auto source = heights
                [static_cast<std::size_t>(z) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)];
            const auto projected = z * 16 - static_cast<int32_t>(source) / 2;
            if (projected >= 0) {
                const auto row = projected / 32;
                const auto projected_height = static_cast<uint8_t>(
                    ((row * 32 + 31) * static_cast<int32_t>(source)) / (projected + 31)
                );
                apply(previous_left, projected_height);
                apply(previous_right, projected_height);
                previous_left =
                    (left_x >= 0 && left_x < out_width && row < out_height)
                        ? std::optional<std::size_t>(
                              static_cast<std::size_t>(row) * static_cast<std::size_t>(out_width) +
                              static_cast<std::size_t>(left_x)
                          )
                        : std::nullopt;
                previous_right =
                    (right_x != left_x && right_x >= 0 && right_x < out_width && row < out_height)
                        ? std::optional<std::size_t>(
                              static_cast<std::size_t>(row) * static_cast<std::size_t>(out_width) +
                              static_cast<std::size_t>(right_x)
                          )
                        : std::nullopt;
                apply(previous_left, projected_height);
                apply(previous_right, projected_height);
            }
            apply(previous_left, source);
            apply(previous_right, source);
        }
    }
    for (auto& cell : result) {
        const auto high = (static_cast<uint32_t>(cell.low_height) +
                           static_cast<uint32_t>(cell.high_height) * 2U) /
                          3U;
        const auto low = (static_cast<uint32_t>(cell.high_height) +
                          static_cast<uint32_t>(cell.low_height) * 2U) /
                         3U;
        cell.high_height = static_cast<uint8_t>(std::max<uint32_t>(high, minimum_height));
        cell.low_height = static_cast<uint8_t>(std::max<uint32_t>(low, minimum_height));
    }
    return result;
}

AltitudeSightPattern build_altitude_pattern(std::span<const std::string_view> lines) {
    if (lines.size() > 8191)
        throw std::invalid_argument("LOS table line count exceeds signed-word storage");
    AltitudeSightPattern result;
    result.rays.resize(lines.size() * 4U);
    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        auto text = lines[line_index];
        std::vector<int16_t> values;
        if (text.empty())
            continue;
        while (true) {
            const auto comma = text.find(',');
            auto token = text.substr(0, comma);
            while (!token.empty() && (token.front() == ' ' || token.front() == '\t'))
                token.remove_prefix(1);
            if (!token.empty() && token.front() == '+')
                token.remove_prefix(1);
            int32_t value{};
            const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
            if (parsed.ptr == token.data() || parsed.ec == std::errc::result_out_of_range)
                throw std::invalid_argument("LOS line contains an invalid integer");
            values.push_back(static_cast<int16_t>(value));
            if (comma == std::string_view::npos)
                break;
            text.remove_prefix(comma + 1);
        }
        const auto count = values.empty() ? 0 : static_cast<int16_t>(values[0]);
        if (count < 0 || static_cast<std::size_t>(count) * 2U + 1U > values.size())
            throw std::invalid_argument("LOS line coordinate list is truncated");
        auto& north = result.rays[line_index];
        auto& east = result.rays[line_index + lines.size()];
        auto& south = result.rays[line_index + lines.size() * 2U];
        auto& west = result.rays[line_index + lines.size() * 3U];
        for (int32_t i = 0; i < count; ++i) {
            const auto x = values[static_cast<std::size_t>(i) * 2U + 1U],
                       y = values[static_cast<std::size_t>(i) * 2U + 2U];
            north.offsets.push_back({x, static_cast<int16_t>(-y)});
            east.offsets.push_back({y, x});
            south.offsets.push_back({static_cast<int16_t>(-x), y});
            west.offsets.push_back({static_cast<int16_t>(-y), static_cast<int16_t>(-x)});
        }
    }
    return result;
}

namespace {
// World units per sight cell as a 16.16 value, and as a shift of whole units.
constexpr int32_t sight_cell_fixed = 0x200000;
constexpr int32_t sight_cell_shift = 5;
// Sight distance per band of the mask and ray tables; the mask table's first
// band is five steps in.
constexpr int32_t sight_band_distance = 32;
constexpr int32_t first_mask_band = 5;
// Y above the terrain per sight-cell row of Z, in whole units.
constexpr int32_t height_per_projected_row = 64;
// Altitude sight: stamped altitudes lie in a byte, and a stamp is only moved
// in place once its altitude changed by this much.
constexpr int32_t highest_altitude = 0xff;
constexpr int32_t altitude_restamp_distance = 6;
// Counts the game keeps in signed words.
constexpr std::size_t signed_word_limit = 0x7fff;
constexpr std::size_t word_limit = 0xffff;

int32_t high_word(int32_t value) noexcept {
    return std::bit_cast<int16_t>(static_cast<uint16_t>(static_cast<uint32_t>(value) >> 16U));
}

// The lowest stamp Y: one whole unit above the sea-level height.
int32_t sight_floor(uint8_t minimum_height_cell) noexcept {
    return static_cast<int32_t>((static_cast<uint32_t>(minimum_height_cell) + 1U) << 16U);
}

std::size_t sight_cells(const PlayerSightGrid& grid) noexcept {
    return static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height);
}

// The viewpoint player's sight changed: the fog edge mask is stale and the
// mapped radar image is rebuilt when next drawn.
void mark_viewpoint_sight_changed(PlayerSightGrid& grid) noexcept {
    if (grid.game == nullptr)
        return;
    grid.game->visibility_flags =
        static_cast<uint8_t>(grid.game->visibility_flags & ~OA_VISIBILITY_FOG_MASK_CURRENT);
    grid.game->radar_blink_flags =
        static_cast<uint16_t>(grid.game->radar_blink_flags | OA_RADAR_MAPPED_DIRTY);
}

// The mask band a sight distance selects: distance/32 - 5 within the table.
int32_t distance_band(int16_t sight_distance, std::size_t mask_count) noexcept {
    const auto band = sight_distance / sight_band_distance - first_mask_band;
    if (band < 0)
        return 0;
    if (static_cast<int32_t>(mask_count) <= band)
        return static_cast<int32_t>(mask_count) - 1;
    return band;
}

// Visits the grid cell under each opaque mask pixel, clipped to the grid.
template <class Visit>
void visit_standard_stamp(
    int32_t center_x,
    int32_t center_z,
    const SightMask& mask,
    const PlayerSightGrid& grid,
    Visit visit
) noexcept {
    auto end_x = static_cast<int32_t>(mask.width);
    if (grid.width <= end_x + center_x)
        end_x = grid.width - center_x;
    auto end_z = static_cast<int32_t>(mask.height);
    if (grid.height <= end_z + center_z)
        end_z = grid.height - center_z;
    const auto begin_x = center_x < 0 ? -center_x : 0;
    const auto begin_z = center_z < 0 ? -center_z : 0;
    for (auto z = begin_z; z < end_z; ++z)
        for (auto x = begin_x; x < end_x; ++x)
            if (mask.pixels
                    [static_cast<std::size_t>(z) * mask.width + static_cast<std::size_t>(x)] !=
                mask.transparent)
                visit(
                    static_cast<std::size_t>(center_z + z) * static_cast<std::size_t>(grid.width) +
                    static_cast<std::size_t>(center_x + x)
                );
}

bool inside_altitude_grid(int32_t x, int32_t z, const AltitudeSightData& altitude) noexcept {
    return static_cast<uint32_t>(x) < static_cast<uint32_t>(altitude.width) &&
           static_cast<uint32_t>(z) < static_cast<uint32_t>(altitude.height);
}

// Visits the stamp's cell, then along each ray every cell whose high corner
// clears the steepest low corner seen so far. The ray table is one-based:
// sight/32 selects entry band - 1 and the band stops at count - 1, so the
// last entry is never read. A sight under 32 visits only the cell.
template <class Visit>
void visit_altitude_stamp(
    const SightStamp& stamp, const AltitudeSightData& altitude, int32_t grid_width, Visit visit
) noexcept {
    visit(
        static_cast<std::size_t>(stamp.center_z) * static_cast<std::size_t>(grid_width) +
        static_cast<std::size_t>(stamp.center_x)
    );
    auto band = std::max(stamp.sight_distance / sight_band_distance, 0);
    const auto last = static_cast<int32_t>(altitude.patterns.size()) - 1;
    if (band >= last)
        band = last;
    if (band <= 0)
        return;
    for (const auto& ray : altitude.patterns[static_cast<std::size_t>(band - 1)].rays) {
        int32_t seen_step = 0, seen_slope = -1, step = 1;
        for (const auto& offset : ray.offsets) {
            const auto x = static_cast<int32_t>(static_cast<int16_t>(
                static_cast<uint16_t>(stamp.center_x) + static_cast<uint16_t>(offset[0])
            ));
            const auto z = static_cast<int32_t>(static_cast<int16_t>(
                static_cast<uint16_t>(stamp.center_z) + static_cast<uint16_t>(offset[1])
            ));
            if (inside_altitude_grid(x, z, altitude)) {
                const auto& cell =
                    altitude.cells
                        [static_cast<std::size_t>(z) * static_cast<std::size_t>(altitude.width) +
                         static_cast<std::size_t>(x)];
                const auto horizon = step * seen_slope;
                const auto top = static_cast<int32_t>(cell.high_height) - stamp.band;
                if (top * seen_step > horizon) {
                    visit(
                        static_cast<std::size_t>(z) * static_cast<std::size_t>(grid_width) +
                        static_cast<std::size_t>(x)
                    );
                    const auto bottom = static_cast<int32_t>(cell.low_height) - stamp.band;
                    if (bottom * seen_step > horizon) {
                        seen_step = step;
                        seen_slope = bottom;
                    }
                }
            }
            ++step;
        }
    }
}

// Adding and removing coverage differ only in the step added to each count.
void adjust_area_coverage(const SightStamp& stamp, SightContext& context, uint8_t step) noexcept {
    auto& grid = *context.grid;
    if (stamp.owner == grid.viewpoint_player)
        mark_viewpoint_sight_changed(grid);
    if (stamp.owner >= OA_PLAYER_COUNT || context.coverage[stamp.owner].empty())
        return;
    const auto coverage = context.coverage[stamp.owner];
    const auto count = [&](std::size_t cell) {
        coverage[cell] = static_cast<uint8_t>(coverage[cell] + step);
    };
    if ((context.visibility_flags & altitude_sight_algorithm) != 0) {
        const auto& altitude = *context.altitude;
        if (inside_altitude_grid(stamp.center_x, stamp.center_z, altitude))
            visit_altitude_stamp(stamp, altitude, grid.width, count);
        return;
    }
    // A band outside the mask table stamps nothing.
    if (stamp.band >= context.masks.size())
        return;
    visit_standard_stamp(stamp.center_x, stamp.center_z, context.masks[stamp.band], grid, count);
}

SightStamp unit_sight_stamp(
    const oa::Unit& unit,
    const oa::UnitDef& def,
    const oa::Player& owner,
    uint8_t minimum_height_cell
) noexcept {
    SightStamp stamp{};
    stamp.owner = owner.index;
    stamp.center_x = std::bit_cast<int16_t>(unit.sight_center_x);
    stamp.center_z = std::bit_cast<int16_t>(unit.sight_center_z);
    stamp.sight_distance = def.sight_distance;
    stamp.model_height = static_cast<uint8_t>(static_cast<uint32_t>(def.model_height) >> 16U);
    stamp.band = unit.sight_band;
    stamp.position_x = unit.position.x;
    stamp.position_y = std::max(unit.position.y, sight_floor(minimum_height_cell));
    stamp.position_z = unit.position.z;
    return stamp;
}

void store_unit_stamp(oa::Unit& unit, const SightStamp& stamp) noexcept {
    unit.sight_center_x = std::bit_cast<uint16_t>(stamp.center_x);
    unit.sight_center_z = std::bit_cast<uint16_t>(stamp.center_z);
    unit.sight_band = stamp.band;
}

const char*
altitude_data_error(const AltitudeSightData& altitude, const PlayerSightGrid& grid) noexcept {
    if (altitude.width != grid.width || altitude.height != grid.height)
        return "altitude and sight grid dimensions differ";
    if (altitude.cells.size() < sight_cells(grid))
        return "altitude sight grid dimensions are invalid";
    if (altitude.patterns.size() < 2)
        return "altitude sight requires at least two table entries";
    if (altitude.patterns.size() > signed_word_limit)
        return "altitude sight pattern count exceeds the signed-word limit";
    for (const auto& pattern : altitude.patterns) {
        if (pattern.rays.size() > signed_word_limit)
            return "altitude sight ray count exceeds the signed-word limit";
        for (const auto& ray : pattern.rays)
            if (ray.offsets.size() > signed_word_limit)
                return "altitude sight ray exceeds the signed-word limit";
    }
    return nullptr;
}
} // namespace

const char* sight_context_error(const SightContext& context) noexcept {
    if (context.grid == nullptr)
        return "sight context has no grid";
    const auto& grid = *context.grid;
    if (grid.width < 0 || grid.height < 0)
        return "sight grid dimensions are invalid";
    const auto cells = sight_cells(grid);
    if (grid.player_bits.size() < cells)
        return "player sight grid dimensions are invalid";
    for (const auto coverage : context.coverage)
        if (!coverage.empty() && coverage.size() < cells)
            return "coverage grid dimensions are invalid";
    if ((context.visibility_flags & altitude_sight_algorithm) != 0)
        return context.altitude == nullptr ? "altitude sight data is required"
                                           : altitude_data_error(*context.altitude, grid);
    if (context.masks.empty())
        return "sight mask table is empty";
    if (context.masks.size() > word_limit)
        return "sight mask table exceeds the word count";
    for (const auto& mask : context.masks)
        if (mask.pixels.size() < static_cast<std::size_t>(mask.width) * mask.height)
            return "sight mask pixels are truncated";
    return nullptr;
}

SightProjection project_sight_cell(const SightStamp& stamp, const SightContext& context) noexcept {
    if (context.masks.empty())
        return {stamp.center_x, stamp.center_z, stamp.band};
    const auto band = distance_band(stamp.sight_distance, context.masks.size());
    const auto& mask = context.masks[static_cast<std::size_t>(band)];
    return {
        stamp.position_x / sight_cell_fixed - mask.offset_x,
        stamp.position_z / sight_cell_fixed -
            high_word(stamp.position_y) / height_per_projected_row - mask.offset_z,
        band
    };
}

void add_area_coverage(const SightStamp& stamp, SightContext& context) noexcept {
    adjust_area_coverage(stamp, context, 1);
}

void remove_area_coverage(const SightStamp& stamp, SightContext& context) noexcept {
    adjust_area_coverage(stamp, context, 0xff);
}

void map_area(const SightStamp& stamp, SightContext& context) noexcept {
    auto& grid = *context.grid;
    const auto bit = static_cast<uint16_t>(1U << (stamp.owner & 0x1fU));
    bool changed = false;
    const auto mark = [&](std::size_t cell) {
        if ((grid.player_bits[cell] & bit) == 0) {
            grid.player_bits[cell] ^= bit;
            changed = true;
        }
    };
    if ((context.visibility_flags & altitude_sight_algorithm) != 0) {
        const auto& altitude = *context.altitude;
        if (!inside_altitude_grid(stamp.center_x, stamp.center_z, altitude))
            return;
        visit_altitude_stamp(stamp, altitude, grid.width, mark);
    } else if (!context.masks.empty()) {
        const auto band = distance_band(stamp.sight_distance, context.masks.size());
        visit_standard_stamp(
            stamp.center_x,
            stamp.center_z,
            context.masks[static_cast<std::size_t>(band)],
            grid,
            mark
        );
    }
    if (changed && stamp.owner == grid.viewpoint_player)
        mark_viewpoint_sight_changed(grid);
}

void update_area_coverage(SightStamp& stamp, SightContext& context) noexcept {
    const auto rules = context.visibility_flags;
    if ((rules & altitude_sight_algorithm) != 0) {
        const auto x = high_word(stamp.position_x) >> sight_cell_shift;
        const auto altitude =
            std::clamp(high_word(stamp.position_y) + stamp.model_height, 0, highest_altitude);
        const auto z = (high_word(stamp.position_z) - (altitude >> 1)) >> sight_cell_shift;
        if (stamp.center_x == x && stamp.center_z == z &&
            std::abs(static_cast<int32_t>(stamp.band) - altitude) < altitude_restamp_distance)
            return;
        if (stamp.band != 0 && (rules & update_sight_grid) != 0)
            remove_area_coverage(stamp, context);
        stamp.center_x = static_cast<int16_t>(x);
        stamp.center_z = static_cast<int16_t>(z);
        if (!inside_altitude_grid(x, z, *context.altitude)) {
            stamp.band = 0;
            return;
        }
        stamp.band = static_cast<uint8_t>(altitude);
        if ((rules & update_sight_grid) != 0)
            add_area_coverage(stamp, context);
    } else {
        const auto cell = project_sight_cell(stamp, context);
        if (stamp.center_x == cell.center_x && stamp.center_z == cell.center_z &&
            stamp.band == cell.band)
            return;
        if ((rules & update_sight_grid) != 0)
            remove_area_coverage(stamp, context);
        stamp.center_x = static_cast<int16_t>(cell.center_x);
        stamp.center_z = static_cast<int16_t>(cell.center_z);
        stamp.band = static_cast<uint8_t>(cell.band);
        if ((rules & update_sight_grid) != 0)
            add_area_coverage(stamp, context);
    }
    if ((rules & terrain_mapping) != 0)
        map_area(stamp, context);
}

void refresh_area_coverage(SightStamp& stamp, SightContext& context) noexcept {
    if ((context.visibility_flags & update_sight_grid) == 0)
        return;
    stamp.band = 0;
    if ((context.visibility_flags & altitude_sight_algorithm) != 0) {
        update_area_coverage(stamp, context);
        return;
    }
    const auto cell = project_sight_cell(stamp, context);
    stamp.center_x = static_cast<int16_t>(cell.center_x);
    stamp.center_z = static_cast<int16_t>(cell.center_z);
    stamp.band = static_cast<uint8_t>(cell.band);
    add_area_coverage(stamp, context);
    map_area(stamp, context);
}

void stamp_unit_sight(
    oa::Unit& unit, const oa::UnitDef& def, const oa::Player& owner, SightContext& context
) noexcept {
    auto stamp = unit_sight_stamp(unit, def, owner, context.minimum_height_cell);
    refresh_area_coverage(stamp, context);
    store_unit_stamp(unit, stamp);
}

void move_unit_sight(
    oa::Unit& unit, const oa::UnitDef& def, const oa::Player& owner, SightContext& context
) noexcept {
    auto stamp = unit_sight_stamp(unit, def, owner, context.minimum_height_cell);
    update_area_coverage(stamp, context);
    store_unit_stamp(unit, stamp);
}

void clear_unit_sight(
    const oa::Unit& unit, const oa::UnitDef& def, const oa::Player& owner, SightContext& context
) noexcept {
    remove_area_coverage(unit_sight_stamp(unit, def, owner, context.minimum_height_cell), context);
}

void remember_sight(
    EyeballMemory& memory,
    const oa::FixedVec3& position,
    int16_t sight_distance,
    uint8_t model_height,
    int32_t duration,
    uint32_t tick,
    SightContext& context
) noexcept {
    // A negative or full count remembers nothing.
    if ((context.visibility_flags & update_sight_grid) == 0 || memory.count >= eyeball_capacity ||
        memory.count < 0)
        return;
    auto& slot = memory.slots[memory.count];
    auto& stamp = slot.stamp;
    stamp.owner = context.grid->viewpoint_player;
    stamp.sight_distance = sight_distance;
    stamp.model_height = model_height;
    stamp.position_x = position.x;
    stamp.position_y = std::max(position.y, sight_floor(context.minimum_height_cell));
    stamp.position_z = position.z;
    slot.expiry = tick + static_cast<uint32_t>(duration);
    refresh_area_coverage(stamp, context);
    ++memory.count;
}

bool remembered_sight_expired(const EyeballSlot& slot, uint32_t tick) noexcept {
    return slot.expiry < tick;
}

int32_t compact_remembered_sight(EyeballMemory& memory, uint32_t tick) noexcept {
    const auto end = std::clamp(memory.count, 0, eyeball_capacity);
    auto kept = 0;
    while (kept != end && !remembered_sight_expired(memory.slots[kept], tick))
        ++kept;
    if (kept == end)
        return end;
    for (auto next = kept + 1; next != end; ++next)
        if (!remembered_sight_expired(memory.slots[next], tick))
            memory.slots[kept++] = memory.slots[next];
    return kept;
}

void expire_remembered_sight(
    EyeballMemory& memory, uint32_t tick, bool always_compact, SightContext& context
) noexcept {
    auto expired = always_compact;
    const auto end = std::min(memory.count, eyeball_capacity);
    for (auto index = 0; index < end; ++index)
        if (remembered_sight_expired(memory.slots[index], tick)) {
            remove_area_coverage(memory.slots[index].stamp, context);
            expired = true;
        }
    if (expired)
        memory.count = compact_remembered_sight(memory, tick);
}
} // namespace oa::sim::visibility_state
