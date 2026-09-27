// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/persist/save_sections.hpp"

#include "bank_util.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace oa::data::persist {
namespace {

constexpr const char* camera_account = "Camera";
constexpr const char* meteor_account = "Meteor";
constexpr const char* metal_account = "Metal";
constexpr const char* player_features_account = "PlayerFeatures";
constexpr const char* mapping_account = "Mapping";
constexpr const char* plotmap_blob = "Plotmap";
constexpr const char* mapping_blob = "Data";

int32_t map_cells(const Game* game) {
    return game->map_height * game->map_width;
}

} // namespace

uint8_t* save_plot_at(const SaveContext* save, int32_t x, int32_t z) {
    const Game* game = (&save->world->game);
    if (save->plots == nullptr || x < 0 || z < 0 || x >= game->map_width || z >= game->map_height)
        return nullptr;
    return save->plots + (static_cast<std::size_t>(z) * static_cast<std::size_t>(game->map_width) +
                          static_cast<std::size_t>(x)) *
                             plot_bytes;
}

void save_write_camera(const SaveContext* save, Bank* bank) {
    bank_open_account(bank, camera_account);
    bank_set_int(bank, "X Position", static_cast<int32_t>(save->world->game.camera_x));
    bank_set_int(bank, "Z Position", static_cast<int32_t>(save->world->game.camera_y));
}

void save_read_camera(SaveContext* save, Bank* bank) {
    bank_open_account(bank, camera_account);
    Game* game = (&save->world->game);
    const int32_t z = bank_get_int(bank, "Z Position", static_cast<int32_t>(game->camera_y));
    const int32_t x = bank_get_int(bank, "X Position", static_cast<int32_t>(game->camera_x));
    const SaveHooks* hooks = save->hooks;
    if (hooks != nullptr && hooks->set_camera != nullptr) {
        hooks->set_camera(hooks->context, x, z);
    } else {
        game->camera_x = static_cast<uint32_t>(x);
        game->camera_y = static_cast<uint32_t>(z);
    }
}

void save_write_meteor(const sim::world_environment::MeteorState* meteor, Bank* bank) {
    bank_open_account(bank, meteor_account);
    bank_set_int(bank, "Enabled", static_cast<int32_t>(meteor->enabled));
    bank_set_int(bank, "Active", static_cast<int32_t>(meteor->active));
    bank_set_int(bank, "Next Strike Time", static_cast<int32_t>(meteor->next_strike_tick));
    bank_set_int(bank, "Time Strike Ends", static_cast<int32_t>(meteor->strike_end_tick));
    bank_set_int(bank, "Next Hit Time", static_cast<int32_t>(meteor->next_hit_tick));
    bank_set_int(bank, "Origin X", meteor->origin_x);
    bank_set_int(bank, "Origin Z", meteor->origin_z);
    bank_set_int(bank, "Target X", meteor->target_x);
    bank_set_int(bank, "Target Z", meteor->target_z);
}

void save_read_meteor(sim::world_environment::MeteorState* meteor, Bank* bank) {
    bank_open_account(bank, meteor_account);
    meteor->enabled = static_cast<uint32_t>(bank_get_int(bank, "Enabled", 0));
    meteor->active = static_cast<uint32_t>(bank_get_int(bank, "Active", 0));
    meteor->next_strike_tick = static_cast<uint32_t>(bank_get_int(bank, "Next Strike Time", 0));
    meteor->strike_end_tick = static_cast<uint32_t>(bank_get_int(bank, "Time Strike Ends", 0));
    meteor->next_hit_tick = static_cast<uint32_t>(bank_get_int(bank, "Next Hit Time", 0));
    meteor->origin_x = static_cast<int16_t>(bank_get_int(bank, "Origin X", 0));
    meteor->origin_z = static_cast<int16_t>(bank_get_int(bank, "Origin Z", 0));
    meteor->target_x = static_cast<int16_t>(bank_get_int(bank, "Target X", 0));
    meteor->target_z = static_cast<int16_t>(bank_get_int(bank, "Target Z", 0));
}

MeteorConfigResult
load_meteor_config(const MeteorTdf* section, sim::world_environment::MeteorSettings* out) {
    if (section == nullptr)
        return MeteorConfigResult::missing;
    if (!section->text(section->context, "MeteorWeapon", out->weapon, sizeof(out->weapon)))
        return MeteorConfigResult::bogus;
    out->radius = section->integer(section->context, "MeteorRadius", 0);
    out->density = static_cast<float>(section->real(section->context, "MeteorDensity", 0.0));
    out->duration = static_cast<float>(section->real(section->context, "MeteorDuration", 0.0));
    const double interval = section->real(section->context, "MeteorInterval", 0.0);
    out->interval = static_cast<float>(interval);
    // The interval test uses the unrounded double.
    if (out->radius != 0 && out->density != 0.0f && out->duration != 0.0f && interval != 0.0)
        return MeteorConfigResult::loaded;
    return MeteorConfigResult::bogus;
}

void save_write_metal_plotmap(const SaveContext* save, Bank* bank) {
    bank_open_account(bank, metal_account);
    const int32_t cells = map_cells((&save->world->game));
    auto* metal =
        static_cast<uint8_t*>(std::malloc(cells > 0 ? static_cast<std::size_t>(cells) : 1));
    if (metal == nullptr)
        return;
    for (int32_t i = 0; i < cells; ++i)
        metal[i] = save->plots[static_cast<std::size_t>(i) * plot_bytes + plot::metal];
    bank_open_blob_name(bank, plotmap_blob);
    bank_blob_write(bank, metal, static_cast<uint32_t>(cells > 0 ? cells : 0));
    std::free(metal);
}

void save_read_metal_plotmap(SaveContext* save, Bank* bank) {
    if (!bank_open_account(bank, metal_account) || !bank_open_blob_name(bank, plotmap_blob))
        return;
    const auto cells = static_cast<uint32_t>(map_cells((&save->world->game)));
    if (static_cast<uint32_t>(bank_blob_size(bank)) != cells || save->plots == nullptr)
        return;
    auto* metal = static_cast<uint8_t*>(std::malloc(cells != 0 ? cells : 1));
    if (metal == nullptr)
        return;
    if (bank_blob_read(bank, metal, cells) >= cells) {
        for (uint32_t i = 0; i < cells; ++i)
            save->plots[static_cast<std::size_t>(i) * plot_bytes + plot::metal] = metal[i];
    }
    std::free(metal);
}

void save_write_player_features(const SaveContext* save, Bank* bank) {
    bank_open_account(bank, player_features_account);
    const int32_t pairs = map_cells((&save->world->game)) / 2;
    auto* packed =
        static_cast<uint8_t*>(std::malloc(pairs > 0 ? static_cast<std::size_t>(pairs) : 1));
    if (packed == nullptr)
        return;
    for (int32_t i = 0; i < pairs; ++i) {
        const uint8_t even =
            save->plots[static_cast<std::size_t>(2 * i) * plot_bytes + plot::flags];
        const uint8_t odd =
            save->plots[static_cast<std::size_t>(2 * i + 1) * plot_bytes + plot::flags];
        packed[i] = static_cast<uint8_t>(((even & 0xf8) << 1) | ((odd >> 3) & 0x0f));
    }
    bank_open_blob_name(bank, plotmap_blob);
    bank_blob_write(bank, packed, static_cast<uint32_t>(pairs > 0 ? pairs : 0));
    std::free(packed);
}

void save_read_player_features(SaveContext* save, Bank* bank) {
    if (!bank_open_account(bank, player_features_account) ||
        !bank_open_blob_name(bank, plotmap_blob))
        return;
    const int32_t pairs = map_cells((&save->world->game)) / 2;
    if (pairs < 0 || bank_blob_size(bank) != pairs || save->plots == nullptr)
        return;
    auto* packed =
        static_cast<uint8_t*>(std::malloc(pairs > 0 ? static_cast<std::size_t>(pairs) : 1));
    if (packed == nullptr)
        return;
    if (bank_blob_read(bank, packed, static_cast<uint32_t>(pairs)) >=
        static_cast<uint32_t>(pairs)) {
        constexpr uint8_t keep = static_cast<uint8_t>(~plot_flags_player_features);
        for (int32_t i = 0; i < pairs; ++i) {
            uint8_t& even = save->plots[static_cast<std::size_t>(2 * i) * plot_bytes + plot::flags];
            uint8_t& odd =
                save->plots[static_cast<std::size_t>(2 * i + 1) * plot_bytes + plot::flags];
            even = static_cast<uint8_t>(
                ((packed[i] >> 1) & plot_flags_player_features) | (even & keep)
            );
            odd = static_cast<uint8_t>(((packed[i] & 0x0f) << 3) | (odd & keep));
        }
    }
    std::free(packed);
}

void save_write_terrain_mapping(const SaveContext* save, Bank* bank) {
    bank_open_account(bank, mapping_account);
    const uint32_t bytes =
        (static_cast<uint32_t>(map_cells((&save->world->game))) & 0x7fffffffu) >> 1;
    bank_open_blob_name(bank, mapping_blob);
    if (save->mapping != nullptr)
        bank_blob_write(bank, save->mapping, bytes);
}

void save_read_terrain_mapping(SaveContext* save, Bank* bank) {
    if (!bank_open_account(bank, mapping_account) || !bank_open_blob_name(bank, mapping_blob))
        return;
    const uint32_t bytes =
        (static_cast<uint32_t>(map_cells((&save->world->game))) & 0x7fffffffu) >> 1;
    if (static_cast<uint32_t>(bank_blob_size(bank)) == bytes && save->mapping != nullptr)
        bank_blob_read(bank, save->mapping, bytes);
}

uint8_t save_cell_height(const SaveContext* save, int16_t x, int16_t z) {
    const uint8_t* cell = save_plot_at(save, x, z);
    return cell != nullptr ? cell[plot::height] : 0;
}

void save_write_image_rows(const ImageRows* image, Bank* bank) {
    bank_blob_seek(bank, 0);
    uint8_t header[8];
    detail::store_le32(header, image->width);
    detail::store_le32(header + 4, image->height);
    bank_blob_write(bank, header, sizeof(header));
    for (int32_t row = 0; row < static_cast<int32_t>(image->height); ++row)
        bank_blob_write(
            bank,
            image->pixels + static_cast<std::size_t>(image->stride) * static_cast<std::size_t>(row),
            image->width
        );
}

bool save_write_game(
    SaveContext* save,
    const SummaryHooks* summary,
    const char* path,
    const char* description,
    int32_t game_id,
    const FileSink* files
) {
    Bank bank;
    bank_init(&bank);
    if (!bank_reset(&bank))
        return false;
    const Game* game = (&save->world->game);
    const bool in_match = game->mode == game_mode_in_match;
    void* context = summary->context;
    char key[256];

    bank_open_account(&bank, save_key::summary);
    std::snprintf(
        key,
        sizeof(key),
        "BUILD DATE: %s",
        summary->build_date != nullptr ? summary->build_date : ""
    );
    bank_set_int(&bank, key, 0);
    std::snprintf(
        key,
        sizeof(key),
        "BUILD TIME: %s",
        summary->build_time != nullptr ? summary->build_time : ""
    );
    bank_set_int(&bank, key, 0);
    bank_set_int(&bank, save_key::max_units, game->max_units_setting);
    bank_set_text(&bank, save_key::campaign, summary->campaign_name(context));
    if (!in_match)
        summary->advance_next_mission(context);
    bank_set_text(&bank, save_key::mission, summary->mission_name(context));
    bank_set_text(&bank, save_key::map, summary->mission_name(context));
    bank_set_int(&bank, save_key::difficulty, game->difficulty);

    const SaveHooks* hooks = save->hooks;
    const Player& local = game->players[game->local_player_index % OA_PLAYER_COUNT];
    const auto* info = static_cast<const PlayerSetupInfo*>(
        hooks->resolve(hooks->context, SaveRef::player_info, local.info)
    );
    bank_set_int(&bank, save_key::side, info != nullptr ? info->side : 0);
    bank_set_int(&bank, save_key::players, game->player_count);
    const int32_t game_type = summary->game_type(context);
    bank_set_int(&bank, save_key::game_type, game_type);
    char thumbs[sizeof(game->mission_results) + 1] = {};
    std::memcpy(thumbs, game->mission_results, sizeof(game->mission_results));
    bank_set_text(&bank, save_key::thumbs, thumbs);
    if (summary->game_type(context) == game_type_skirmish) {
        const auto* rules = static_cast<const uint8_t*>(
            hooks->resolve(hooks->context, SaveRef::mission_rules, game->skirmish_info)
        );
        if (rules != nullptr) {
            const auto rule = [&](std::size_t offset) {
                return static_cast<int32_t>(detail::load_le32(rules + offset));
            };
            bank_set_int(&bank, save_key::commander_death, rule(skirmish_rules::commander_death));
            bank_set_int(&bank, save_key::location, rule(skirmish_rules::location));
            bank_set_int(&bank, save_key::mapping, rule(skirmish_rules::mapping));
            bank_set_int(&bank, save_key::line_of_sight, rule(skirmish_rules::line_of_sight));
            bank_set_int(
                &bank, save_key::line_of_sight_type, rule(skirmish_rules::line_of_sight_type)
            );
        }
    }
    if (!in_match) {
        bank_set_int(&bank, save_key::between_missions, 1);
        summary->bind_mission_info(context);
    }
    if (description != nullptr)
        bank_set_text(&bank, save_key::description, description);
    bank_set_int(&bank, save_key::game_id, game_id);
    bank_set_int(&bank, save_key::game_time, static_cast<int32_t>(game->tick));
    if (in_match) {
        bank_open_blob_name(&bank, save_key::radar_image);
        const ImageRows* radar = summary->radar_image(context);
        if (radar != nullptr)
            save_write_image_rows(radar, &bank);
        save_write_camera(save, &bank);
        summary->write_stats_panel(context, &bank);
        save_write_units(save, &bank);
        save_write_terrain_mapping(save, &bank);
        save_write_features(save, &bank);
        save_write_player_features(save, &bank);
        save_write_metal_plotmap(save, &bank);
        save_write_meteor(save->meteor, &bank);
        summary->save_conditions(context, &bank);
    }
    const bool written = bank_write_file(&bank, path, savegame_description, true, false, files);
    bank_destroy(&bank);
    return written;
}

bool save_command_line_game(
    SaveContext* save,
    const SummaryHooks* summary,
    const char* const* tokens,
    int32_t token_count,
    const FileSink* files,
    char* path_out,
    std::size_t path_bytes
) {
    if (token_count <= 1 || path_out == nullptr || path_bytes == 0)
        return false;
    // The path is savegame/<name>.sav; creating the directory belongs to the
    // file sink.
    const int length = std::snprintf(
        path_out, path_bytes, "savegame/%s.sav", tokens[1] != nullptr ? tokens[1] : ""
    );
    if (length < 0 || static_cast<std::size_t>(length) >= path_bytes)
        return false;
    return save_write_game(
        save, summary, path_out, command_line_description, command_line_game_id, files
    );
}

} // namespace oa::data::persist
