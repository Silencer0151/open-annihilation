// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Skirmish menu and map-selection modal host services.
#include "oa/app/runtime.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {

oa::ui::gui_layout::Gadget* Runtime::widget(std::string_view name) {
    const auto found = std::find_if(
        resources_.layout.gadgets.begin(),
        resources_.layout.gadgets.end(),
        [name](const auto& gadget) { return gadget.common.name == name; }
    );
    return found == resources_.layout.gadgets.end() ? nullptr : &*found;
}

skirmish::MenuHandle Runtime::frontend_menu() {
    return {kFrontendMenuHandle};
}

void Runtime::clear_backbuffer() {
}

skirmish::MenuHandle Runtime::load_menu(std::string_view name) {
    if (name != "SKIRMISH.GUI")
        throw std::runtime_error("unexpected skirmish layout");
    load(Screen::skirmish);
    return frontend_menu();
}

void Runtime::install_event_callback(skirmish::MenuHandle) {
}

void Runtime::load_background(std::string_view name) {
    (void)load_named_background(std::string(name).c_str(), false, false, false);
    rebuild_surface();
}

void Runtime::install_input_callback() {
    typed_key_hook_ = TypedKeyHook::skirmish_players;
}

void Runtime::set_input_enabled(int32_t enabled) {
    input_enabled_ = enabled != 0;
}

void Runtime::add_menu_flags(uint32_t flags) {
    menu_flags_ |= flags;
}

int16_t Runtime::widget_count() {
    return static_cast<int16_t>(std::min<std::size_t>(
        resources_.layout.gadgets.size(),
        static_cast<std::size_t>(std::numeric_limits<int16_t>::max())
    ));
}

void Runtime::set_widget_count(int16_t count) {
    if (count >= 0 && static_cast<std::size_t>(count) < resources_.layout.gadgets.size())
        resources_.layout.gadgets.resize(static_cast<std::size_t>(count));
}

void Runtime::link_slot_sprite(oa::ui::gui_layout::Gadget& gadget, std::string_view sequence) {
    widget_sprites_.erase(gadget.common.name);
    const auto* found = gaf_sequence(resources_.sprites, sequence);
    if (found == nullptr)
        return;
    widget_sprites_[gadget.common.name] = {renderer::SpriteArchive::screen, found->name};
    const auto stage = widget_gaf_frames_.find(gadget.common.name);
    const auto frame = stage == widget_gaf_frames_.end() ? 0U : stage->second;
    if (frame >= found->frames.size())
        return;
    gadget.common.width = static_cast<int16_t>(found->frames[frame].width);
    gadget.common.height = static_cast<int16_t>(found->frames[frame].height);
}

void Runtime::create_slot_widget(const skirmish::SlotWidget& source) {
    if (resources_.layout.gadgets.size() >= oa::ui::gui_layout::limit::gadgets)
        throw std::runtime_error("dynamic skirmish widgets exceed the GUI's gadget capacity");
    oa::ui::gui_layout::Gadget gadget;
    gadget.common.type = source.kind == skirmish::WidgetKind::button
                             ? oa::ui::gui_layout::GadgetType::button
                             : oa::ui::gui_layout::GadgetType::hot_surface;
    gadget.common.name = source.name;
    gadget.common.x = source.x;
    gadget.common.y = source.y;
    gadget.common.width = source.width;
    gadget.common.height = source.height;
    gadget.common.active = 1;
    gadget.common.attributes = static_cast<int32_t>(source.flags);
    gadget.common.runtime_help = source.tooltip;
    if (source.kind == skirmish::WidgetKind::button) {
        oa::ui::gui_layout::ButtonFields fields;
        fields.stages = static_cast<int8_t>(source.stages);
        gadget.fields = std::move(fields);
    }
    if (source.kind == skirmish::WidgetKind::button && !source.sprite_resource.empty())
        link_slot_sprite(gadget, source.sprite_resource);
    resources_.layout.gadgets.push_back(std::move(gadget));
}

std::string Runtime::translate_ui(std::string_view text) {
    return std::string(text);
}

void Runtime::set_text(
    skirmish::MenuHandle, std::string_view name, std::string_view text, int32_t
) {
    auto* gadget = widget(name);
    if (gadget == nullptr)
        return;
    if (auto* fields = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget->fields))
        fields->text = text;
    else if (auto* fields = std::get_if<oa::ui::gui_layout::LabelFields>(&gadget->fields))
        fields->text = text;
}

void Runtime::set_enabled(std::string_view name, int32_t enabled) {
    if (auto* gadget = widget(name))
        gadget->common.active = enabled != 0 ? 1 : 0;
}

void Runtime::set_button_stage(std::string_view name, uint8_t stage) {
    widget_gaf_frames_[std::string(name)] = stage;
    widget_text_stages_[std::string(name)] = stage;
}

void Runtime::select_difficulty_label(std::string_view label, int32_t value) {
    set_button_status(label, static_cast<int16_t>(value));
}

void Runtime::set_button_status(std::string_view name, int16_t value) {
    auto* gadget = widget(name);
    auto* fields = gadget != nullptr
                       ? std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget->fields)
                       : nullptr;
    if (fields == nullptr)
        return;
    fields->status = value;
    const auto group = gadget->common.association;
    if (value == 0 || group == 0)
        return;
    for (auto& other : resources_.layout.gadgets) {
        auto* other_fields = std::get_if<oa::ui::gui_layout::ButtonFields>(&other.fields);
        if (&other != gadget && other_fields != nullptr && other.common.association == group)
            other_fields->status = 0;
    }
}

void Runtime::set_side_stage(std::string_view name, uint8_t stage) {
    widget_gaf_frames_[std::string(name)] = stage;
    widget_text_stages_[std::string(name)] = stage;
}

void Runtime::set_tooltip(std::string_view name, std::string_view text) {
    if (auto* gadget = widget(name))
        gadget->common.runtime_help = text;
}

void Runtime::set_image(std::string_view name, skirmish::Sprite sprite, uint16_t frame) {
    if (widget(name) == nullptr)
        return;
    widget_gaf_frames_[std::string(name)] = frame;
    widget_sprites_[std::string(name)] =
        sprite == skirmish::Sprite::player_colors
            ? renderer::SpriteOverride{renderer::SpriteArchive::global, "32xlogos"}
            : renderer::SpriteOverride{renderer::SpriteArchive::screen, "ally icons"};
}

void Runtime::set_image_frame(std::string_view name, uint16_t frame) {
    if (widget(name) != nullptr)
        widget_gaf_frames_[std::string(name)] = frame;
}

std::optional<uint16_t> Runtime::team_icon_frame_count() {
    const auto found = std::find_if(
        resources_.sprites.sequences.begin(),
        resources_.sprites.sequences.end(),
        [](const auto& sequence) { return sequence.name == "ally icons"; }
    );
    if (found == resources_.sprites.sequences.end())
        return std::nullopt;
    return static_cast<uint16_t>(
        std::min<std::size_t>(found->frames.size(), std::numeric_limits<uint16_t>::max())
    );
}

void Runtime::zero_team_icon_frame_origin(uint32_t) {
}

uint16_t Runtime::color_frame_count() {
    return logo_sequence_ != nullptr ? static_cast<uint16_t>(logo_sequence_->frames.size()) : 0;
}

void Runtime::load_logo_textures() {
    logo_sequence_ = nullptr;
    logo_textures_ = {};
    const auto bytes = read("textures/logos.gaf");
    if (!bytes)
        return;
    auto parsed = oa::formats::gaf::parse(*bytes);
    if (!parsed.ok())
        return;
    logo_textures_ = std::move(*parsed.archive);
    logo_sequence_ = gaf_sequence(logo_textures_, "32xlogos");
}

void Runtime::invalidate_menu() {
    rebuild_surface();
}

void Runtime::refresh_help_text() {
    if (widget("HELPTEXT") == nullptr)
        return;
    std::string help;
    if (hovered_ && *hovered_ < resources_.layout.gadgets.size())
        help = resources_.layout.gadgets[*hovered_].common.runtime_help;
    set_text(frontend_menu(), "HELPTEXT", translate_ui(help), 0);
}

std::string Runtime::selected_widget_name(const entry::Event&) {
    if (selected_ < 0 || static_cast<std::size_t>(selected_) >= resources_.layout.gadgets.size())
        return {};
    return resources_.layout.gadgets[static_cast<std::size_t>(selected_)].common.name;
}

uint32_t Runtime::button_result(skirmish::MenuHandle, skirmish::Button button) {
    return oa::ui::gui_input::button_result(input_menu(), skirmish::resource_name(button));
}

int32_t Runtime::event_button(skirmish::MenuHandle) {
    return event_button_;
}

void Runtime::capture_input() {
}

void Runtime::play_ui_sound(std::string_view name, uint32_t) {
    if (options_.mute)
        return;
    const auto selection =
        oa::audio::game_audio::select(audio_registry_, name, false, sound_playback_state());
    std::string error;
    if (selection.status == oa::audio::game_audio::SelectionStatus::selected)
        (void)audio_player_.play(selection, error);
}

void Runtime::open_map_selection() {
    (void)map_modal::open(map_modal_, skirmish_settings_, *this);
}

int32_t Runtime::map_count() {
    return static_cast<int32_t>(eligible_map_names_.size());
}

std::vector<std::string> Runtime::copy_map_names() {
    return eligible_map_names_;
}

map_modal::MenuHandle Runtime::load_modal(std::string_view resource, uint32_t flags) {
    if (resource != "SELMAP.GUI" || flags != map_modal::modal_flags)
        throw std::runtime_error("unexpected map modal setup");
    modal_parent_surface_ = surface_;
    load(Screen::map_selection);
    return {kFrontendMenuHandle};
}

void Runtime::install_map_event_callback(map_modal::MenuHandle) {
}

void Runtime::bind_map_names(std::span<const std::string> names) {
    bound_map_names_.assign(names.begin(), names.end());
}

void Runtime::install_map_selection_callback() {
}

void Runtime::set_selected_map_index(int16_t index) {
    modal_map_index_ = index;
}

int16_t Runtime::selected_map_index() {
    return modal_map_index_;
}

uint32_t Runtime::button_result(map_modal::MenuHandle, map_modal::Button button) {
    return oa::ui::gui_input::button_result(input_menu(), map_modal::resource_name(button));
}

void Runtime::set_parent_map_name(map_modal::MenuHandle, std::string_view text) {
    pending_parent_map_name_ = text;
}

bool Runtime::has_map_name_widget() {
    return widget("MAPNAME") != nullptr;
}

std::string Runtime::map_display_name() {
    return selected_map_metadata_ ? selected_map_metadata_->mission_name : std::string{};
}

std::string Runtime::map_memory_requirement_text() {
    return selected_map_metadata_ ? selected_map_metadata_->memory_requirement : std::string{};
}

std::string Runtime::permitted_player_counts_text() {
    return selected_map_metadata_ ? selected_map_metadata_->permitted_player_counts : std::string{};
}

std::string Runtime::map_description() {
    return selected_map_metadata_ ? selected_map_metadata_->mission_description : std::string{};
}

std::string Runtime::terrain_resource_path() {
    return "maps/" + selected_map_name_runtime_ + ".tnt";
}

void Runtime::set_modal_text(std::string_view name, std::string_view text, int32_t argument) {
    set_text(frontend_menu(), name, text, argument);
}

map_modal::PictureHandle Runtime::picture() {
    return map_picture_;
}

void Runtime::set_picture(map_modal::PictureHandle picture_value) {
    map_picture_ = picture_value;
}

void Runtime::release_picture(map_modal::PictureHandle) {
    preview_rgb_.clear();
    preview_width_ = preview_height_ = 0;
    preview_source_width_ = preview_source_height_ = 0;
    preview_destination_x_ = preview_destination_y_ = 0;
    preview_destination_width_ = preview_destination_height_ = 0;
}

map_modal::LoadedPicture Runtime::load_picture(std::string_view terrain_path) {
    const auto bytes = assets_.read(terrain_path).bytes;
    const auto parsed = oa::formats::tnt::parse(bytes);
    if (!parsed.ok())
        throw std::runtime_error("cannot parse map terrain: " + parsed.error->message);
    selected_tnt_ = std::move(*parsed.map);
    if (selected_tnt_->minimap) {
        const auto palette_data = assets_.read("palettes/palette.pal").bytes;
        if (palette_data.size() != oa::PaletteBytes{}.size())
            throw std::runtime_error("game palette has invalid size");
        std::copy_n(palette_data.begin(), preview_clear_rgb_.size(), preview_clear_rgb_.begin());
        preview_rgb_.resize(selected_tnt_->minimap->palette_indices.size() * 3U);
        preview_width_ = selected_tnt_->minimap->width;
        preview_height_ = selected_tnt_->minimap->height;
        preview_source_width_ = preview_width_;
        preview_source_height_ = preview_height_;
        for (std::size_t i = 0; i < selected_tnt_->minimap->palette_indices.size(); ++i) {
            const auto source =
                static_cast<std::size_t>(selected_tnt_->minimap->palette_indices[i]) * 4U;
            std::copy_n(
                palette_data.begin() + static_cast<std::ptrdiff_t>(source),
                3,
                preview_rgb_.begin() + static_cast<std::ptrdiff_t>(i * 3U)
            );
        }
    }
    return {
        {++next_picture_handle_},
        static_cast<int32_t>(selected_tnt_->attribute_width),
        static_cast<int32_t>(selected_tnt_->attribute_height)
    };
}

map_modal::PictureSize Runtime::picture_size() {
    if (const auto* gadget = widget("MAPPIC"))
        return {gadget->common.width, gadget->common.height};
    return {};
}

void Runtime::fit_picture(
    map_modal::PictureHandle,
    int32_t widget_width,
    int32_t widget_height,
    int32_t world_width,
    int32_t world_height
) {
    const auto effective_width = world_width - 32;
    const auto effective_height = world_height - 128;
    if (effective_width <= 0 || effective_height <= 0 || widget_width <= 0 || widget_height <= 0)
        throw std::runtime_error("map preview dimensions are invalid");
    if (effective_width < effective_height) {
        preview_destination_width_ = static_cast<int32_t>(
            static_cast<int64_t>(effective_width) * widget_width / effective_height
        );
        preview_destination_height_ = widget_height;
        preview_destination_x_ = (widget_width - preview_destination_width_) / 2;
        preview_destination_y_ = 0;
        preview_source_width_ = preview_width_ * static_cast<std::size_t>(effective_width) /
                                static_cast<std::size_t>(effective_height);
    } else {
        preview_destination_width_ = widget_width;
        preview_destination_height_ = static_cast<int32_t>(
            static_cast<int64_t>(effective_height) * widget_height / effective_width
        );
        preview_destination_x_ = 0;
        preview_destination_y_ = (widget_height - preview_destination_height_) / 2;
        preview_source_height_ = preview_height_ * static_cast<std::size_t>(effective_height) /
                                 static_cast<std::size_t>(effective_width);
    }
}

int32_t Runtime::select_map(std::string_view name) {
    const auto base = "maps/" + std::string(name);
    const auto tnt_data = read(base + ".tnt");
    const auto ota_data = read(base + ".ota");
    if (!tnt_data || !ota_data) {
        selected_map_metadata_.reset();
        selected_start_markers_.clear();
        return 0;
    }
    const std::string_view ota_text(
        reinterpret_cast<const char*>(ota_data->data()), ota_data->size()
    );
    auto parsed = oa::formats::ota::parse(ota_text);
    if (!parsed.ok())
        throw std::runtime_error("cannot parse selected map metadata: " + parsed.error->message);
    if (oa::formats::ota::select_multiplayer_schema(*parsed.metadata, 2) == nullptr) {
        selected_map_metadata_.reset();
        selected_start_markers_.clear();
        return 0;
    }
    const auto terrain = oa::formats::tnt::parse(*tnt_data);
    if (!terrain.ok())
        throw std::runtime_error("cannot parse selected map terrain: " + terrain.error->message);
    selected_map_metadata_ = std::move(*parsed.metadata);
    auto scenario_document = oa::data::unit_definitions::parse_tdf(ota_text);
    if (!scenario_document)
        throw std::runtime_error(
            "cannot parse selected scenario definitions: " + scenario_document.error.message
        );
    selected_ota_document_ = std::move(scenario_document.value);
    selected_tnt_ = std::move(*terrain.map);
    selected_map_name_runtime_ = name;
    selected_start_markers_.clear();
    return 1;
}

int32_t Runtime::map_player_capacity() {
    if (!selected_map_metadata_)
        return 0;
    const auto* schema = oa::formats::ota::select_multiplayer_schema(
        *selected_map_metadata_, static_cast<int32_t>(state_.player_count)
    );
    if (schema == nullptr)
        return 0;
    selected_start_markers_.clear();
    selected_start_markers_.reserve(schema->start_positions.size());
    for (const auto& position : schema->start_positions)
        selected_start_markers_.push_back({1, position.index, position.x, position.z});
    return oa::sim::unit_spawn::count_start_positions(selected_start_markers_);
}

std::optional<std::vector<uint8_t>> Runtime::read(std::string_view path) {
    try {
        return assets_.read(path).bytes;
    } catch (const std::runtime_error& error) {
        if (std::string_view(error.what()).starts_with("asset not found: "))
            return std::nullopt;
        throw;
    }
}

namespace {

const oa::data::unit_definitions::TdfSection*
global_header(const std::optional<oa::data::unit_definitions::TdfDocument>& document) {
    if (!document)
        return nullptr;
    const auto header =
        std::find_if(document->sections.begin(), document->sections.end(), [](const auto& section) {
            return section.name == "GlobalHeader";
        });
    return header != document->sections.end() ? &*header : nullptr;
}

std::optional<int32_t> parse_tdf_int(const std::string* value) {
    if (value == nullptr || value->empty())
        return std::nullopt;
    char* end = nullptr;
    errno = 0;
    const auto parsed = std::strtol(value->c_str(), &end, 10);
    if (end == value->c_str() || errno == ERANGE)
        return std::nullopt;
    return static_cast<int32_t>(parsed);
}

std::optional<std::string>
tdf_text(const oa::data::unit_definitions::TdfSection* section, std::string_view key) {
    const auto* value = section != nullptr ? section->find(key) : nullptr;
    if (value == nullptr)
        return std::nullopt;
    if (value->size() >= 256)
        throw std::runtime_error("OTA scenario text exceeds 255 bytes");
    return *value;
}

} // namespace

int32_t Runtime::integer(std::string_view key, int32_t fallback) {
    const auto* header = global_header(selected_ota_document_);
    if (header == nullptr)
        return fallback;
    return parse_tdf_int(header->find(key)).value_or(fallback);
}

std::optional<std::string> Runtime::text(std::string_view key) {
    return tdf_text(global_header(selected_ota_document_), key);
}

const oa::data::unit_definitions::TdfSection* Runtime::session_schema_section() const {
    const auto* header = global_header(selected_ota_document_);
    if (header == nullptr || session_schema_.empty())
        return nullptr;
    for (const auto& child : header->children)
        if (tdf_names_equal(child.name, session_schema_))
            return &child;
    return nullptr;
}

int32_t Runtime::schema_integer(std::string_view key, int32_t fallback) {
    const auto* schema = session_schema_section();
    if (schema == nullptr)
        return fallback;
    return parse_tdf_int(schema->find(key)).value_or(fallback);
}

std::optional<std::string> Runtime::schema_text(std::string_view key) {
    return tdf_text(session_schema_section(), key);
}

uint16_t Runtime::commander_type_for_side(uint8_t side) {
    if (side >= side_table_.count)
        throw std::out_of_range("player side has no commander definition");
    const auto type =
        oa::sim::unit_spawn::find_type_index(spawn_type_names_, side_table_.sides[side].commander);
    if (type == 0)
        throw std::runtime_error("side commander is absent from unit catalog");
    return type;
}

void Runtime::report_missing_start_position(int32_t index) {
    throw std::runtime_error("selected map lacks start position " + std::to_string(index));
}

void Runtime::set_camera_position(int32_t x, int32_t z, uint32_t flags) {
    match_camera_x_ = x;
    match_camera_z_ = z;
    match_camera_flags_ = flags;
}

} // namespace oa::app
