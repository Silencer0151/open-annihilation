// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Options tab panel, SOUNDS/VISUALS/SPEEDS sub-panels and slider callbacks.
#include "oa/ui/frontend/options.hpp"
#include "oa/base/game_math.hpp"

#include "oa/data/languages/translation.hpp"
#include "oa/ui/gui_input/gadget_panel.hpp"
#include "oa/ui/gui_layout/gui_gadget.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <variant>

namespace oa::ui::frontend {
using base::game_math::truncate_to_int64;

namespace {

namespace flag = prefs::preference_flags;

constexpr std::string_view kOptionsSound = "Options";
constexpr std::string_view kPreviousSound = "Previous";
constexpr std::string_view kTestWave = "sounds\\explode.wav";
constexpr std::string_view kNoLinesText = "None";

constexpr int32_t kFxVolumeMaximum = 0x40;
constexpr int32_t kGammaMaximum = 0x14;
constexpr int32_t kGameSpeedMaximum = 0x15;
constexpr int32_t kScrollSpeedMaximum = 0x41;
constexpr int32_t kMaxLinesMaximum = 0x1e;
constexpr int32_t kTextScrollMaximum = 0x28;
constexpr uint8_t kChatVolumeStep = 5; // chat bytes are stage * 5
constexpr int16_t kGamePanelExtraWidth = 0x96;
constexpr int16_t kGamePanelFillerX = 0x80;
constexpr std::string_view kFillerName = "PANEL";
/// The bytes, NUL included, of the longest text panel_set_text looks up in
/// the game's translation; a longer text shows as it is.
constexpr std::size_t kTranslationKeyBytes = 0x200;
constexpr uint8_t kSoundModeMask = 7;
constexpr uint8_t kSoundModeOff = 0;
constexpr uint8_t kSoundModeTest = 1;
constexpr uint8_t kSoundModeSpatial = 2;
constexpr uint8_t kCdModeTrackType = 4;

void play(OptionsContext& context, std::string_view name) {
    if (context.host.play_sound != nullptr)
        context.host.play_sound(context.host.context, name.data());
}

void call(OptionsContext& context, void (*fn)(void*)) {
    if (fn != nullptr)
        fn(context.host.context);
}

bool selected_is_button(const Panel& panel) noexcept {
    return panel.selected >= 0 && panel.selected < static_cast<int32_t>(kPanelControls) &&
           panel.controls[static_cast<std::size_t>(panel.selected)].type == ControlType::button;
}

void set_number_text(Panel& panel, std::string_view name, int64_t value, const char* format) {
    char text[32];
    std::snprintf(text, sizeof text, format, static_cast<long long>(value));
    panel_set_text(panel, name, text);
}

void set_lines_text(Panel& panel, uint32_t lines) {
    if (lines == 0)
        panel_set_text(panel, "MAXLINESTEXT", kNoLinesText);
    else
        set_number_text(panel, "MAXLINESTEXT", static_cast<int32_t>(lines), "%lld");
}

void set_mode_text(Panel& panel, const DisplayMode& mode) {
    char text[40];
    std::snprintf(text, sizeof text, "%d X %d", mode.width, mode.height);
    panel_set_text(panel, "VIDVAL", text);
}

// Sets a slider's maximum, knob and callback when the record exists.
Control* bind_slider(
    Panel& panel, std::string_view name, int32_t maximum, int32_t value, SliderHandler handler
) {
    auto* slider = panel_control(panel, name);
    if (slider == nullptr)
        return nullptr;
    slider->slider.maximum = maximum;
    slider->on_change = handler;
    slider_set_value(slider->slider, value);
    return slider;
}

uint8_t sound_mode(const prefs::Preferences& preferences) noexcept {
    return static_cast<uint8_t>(preferences.sound_flags & kSoundModeMask);
}

// Radio highlight of the chosen tab: stores 1 and clears the rest of its group.
void select_tab(Panel& panel) noexcept {
    if (panel.selected < 0 || panel.selected >= static_cast<int32_t>(kPanelControls))
        return;
    auto& chosen = panel.controls[static_cast<std::size_t>(panel.selected)];
    chosen.group_value = 1;
    if (chosen.group == 0)
        return;
    for (int32_t index = 1; index <= panel.count; ++index) {
        auto& other = panel.controls[static_cast<std::size_t>(index)];
        if (index != panel.selected && other.type == ControlType::button &&
            other.group == chosen.group)
            other.group_value = 0;
    }
}

// Panel-stack lookup by name: the root record carries the GUI's panel name.
bool root_named(const Panel& panel, std::string_view name) noexcept {
    const auto root = control_name(panel.controls[0]);
    if (root.size() != name.size())
        return false;
    for (std::size_t index = 0; index < root.size(); ++index) {
        const auto a = static_cast<unsigned char>(root[index]);
        const auto b = static_cast<unsigned char>(name[index]);
        if (std::toupper(a) != std::toupper(b))
            return false;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Panel model

std::string_view control_name(const Control& control) noexcept {
    return {control.name.data(), ::strnlen(control.name.data(), control.name.size())};
}

void set_control_name(Control& control, std::string_view name) noexcept {
    control.name.fill('\0');
    std::memcpy(control.name.data(), name.data(), std::min(name.size(), control.name.size()));
}

std::string_view control_text(const Control& control) noexcept {
    return {control.text.data(), ::strnlen(control.text.data(), control.text.size())};
}

void set_control_text(Control& control, std::string_view text) noexcept {
    control.text.fill('\0');
    std::memcpy(control.text.data(), text.data(), std::min(text.size(), control.text.size() - 1U));
}

void panel_load_layout(Panel& panel, const ui::gui_layout::Layout& layout) noexcept {
    panel = Panel{};
    const auto count = std::min(layout.gadgets.size(), kPanelControls);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& gadget = layout.gadgets[index];
        auto& control = panel.controls[index];
        const auto& common = gadget.common;
        control.type = static_cast<ControlType>(common.type);
        control.group = common.association;
        set_control_name(control, common.name);
        control.x = common.x;
        control.y = common.y;
        control.width = common.width;
        control.height = common.height;
        control.active = static_cast<uint8_t>(common.active);
        control.attributes = static_cast<uint32_t>(common.attributes);
        if (const auto* button = std::get_if<ui::gui_layout::ButtonFields>(&gadget.fields)) {
            control.group_value = button->status;
            control.grayed = button->grayed_out ? 1 : 0;
            control.stages = button->stages;
            control.quick_key = button->quick_key;
            set_control_text(control, button->text);
        } else if (
            const auto* slider = std::get_if<ui::gui_layout::ScrollBarFields>(&gadget.fields)
        ) {
            control.slider.range = slider->range;
            control.slider.maximum = slider->thickness;
            control.slider.knob = slider->knob_position;
            control.slider.knob_size = slider->knob_size;
            control.grayed = slider->locked ? 1 : 0;
            set_control_text(control, slider->text);
        } else if (const auto* label = std::get_if<ui::gui_layout::LabelFields>(&gadget.fields)) {
            set_control_text(control, label->text);
        } else if (const auto* box = std::get_if<ui::gui_layout::TextBoxFields>(&gadget.fields)) {
            set_control_text(control, box->text);
        }
    }
    panel.count = static_cast<int16_t>(count == 0 ? 0 : count - 1U);
}

Control* panel_append(Panel& panel, ControlType type, std::string_view name) noexcept {
    const auto index = static_cast<std::size_t>(panel.count) + 1U;
    if (panel.count < 0 || index >= kPanelControls)
        return nullptr;
    auto& control = panel.controls[index];
    control = Control{};
    control.type = type;
    set_control_name(control, name);
    panel.count = static_cast<int16_t>(index);
    return &control;
}

int32_t panel_find(const Panel& panel, std::string_view name) noexcept {
    const auto wanted = name.substr(0, std::min(name.size(), kControlNameBytes));
    for (int32_t index = 1; index < panel.count + 1; ++index) {
        if (index >= static_cast<int32_t>(kPanelControls))
            break;
        if (control_name(panel.controls[static_cast<std::size_t>(index)]) == wanted)
            return index;
    }
    return -1;
}

Control* panel_control(Panel& panel, std::string_view name) noexcept {
    const auto index = panel_find(panel, name);
    return index < 0 ? nullptr : &panel.controls[static_cast<std::size_t>(index)];
}

const Control* panel_control(const Panel& panel, std::string_view name) noexcept {
    const auto index = panel_find(panel, name);
    return index < 0 ? nullptr : &panel.controls[static_cast<std::size_t>(index)];
}

bool panel_selected_is(const Panel& panel, std::string_view name) noexcept {
    if (panel.selected < 0 || panel.selected >= static_cast<int32_t>(kPanelControls))
        return false;
    return control_name(panel.controls[static_cast<std::size_t>(panel.selected)]) == name;
}

void panel_clear_selection(Panel& panel) noexcept {
    panel.selected = kNoSelection;
}

uint32_t panel_stage(const Panel& panel, std::string_view name) noexcept {
    const auto* control = panel_control(panel, name);
    if (control == nullptr || control->type != ControlType::button)
        return kNoStage;
    return control->stage;
}

bool panel_set_stage(Panel& panel, std::string_view name, uint8_t stage) noexcept {
    auto* control = panel_control(panel, name);
    if (control == nullptr)
        return false;
    control->stage = stage;
    return true;
}

bool panel_set_group_value(Panel& panel, std::string_view name, int16_t value) noexcept {
    auto* control = panel_control(panel, name);
    if (control == nullptr)
        return false;
    control->group_value = value;
    panel.dirty = true;
    return true;
}

void panel_set_active(Panel& panel, std::string_view name, uint8_t active) noexcept {
    auto* control = panel_control(panel, name);
    if (control == nullptr)
        return;
    control->active = active;
    if (control->type == ControlType::slider) {
        for (int32_t index = 0; index <= panel.count; ++index) {
            auto& other = panel.controls[static_cast<std::size_t>(index)];
            if (other.type == ControlType::button && other.group == control->group)
                other.active = active;
        }
    }
    panel.dirty = true;
}

void panel_set_grayed(Panel& panel, std::string_view name, bool grayed) noexcept {
    if (auto* control = panel_control(panel, name))
        control->grayed = grayed ? 1 : 0;
}

void panel_set_disabled(Panel& panel, std::string_view name, bool disabled) noexcept {
    auto* control = panel_control(panel, name);
    if (control == nullptr)
        return;
    control->grayed = disabled ? 1 : 0;
    if (control->type != ControlType::slider)
        return;
    // Only the stepping buttons of the slider's group follow: buttons with
    // attribute bit 0x08 or 0x10 set.
    for (int32_t index = 1; index < panel.count + 1; ++index) {
        auto& other = panel.controls[static_cast<std::size_t>(index)];
        if (other.type == ControlType::button && other.group == control->group &&
            (other.attributes & 0x18U) != 0)
            other.grayed = disabled ? 1 : 0;
    }
}

void panel_set_text(Panel& panel, std::string_view name, std::string_view text) noexcept {
    auto* control = panel_control(panel, name);
    if (control == nullptr)
        return;
    // The text shows in the language shown, as the texts a GUI file holds do.
    std::array<char, kTranslationKeyBytes> key{};
    const char* translated = nullptr;
    if (text.size() < key.size()) {
        std::memcpy(key.data(), text.data(), text.size());
        translated = oa::data::languages::installed_translation(nullptr, key.data());
    }
    set_control_text(*control, translated != nullptr ? std::string_view(translated) : text);
    panel.dirty = true;
    if (control->type != ControlType::button)
        return;
    const auto caption = control_text(*control);
    const auto rule =
        ui::gui_input::caption_quick_key(control->attributes, control->stages, caption);
    if (rule == ui::gui_input::CaptionQuickKey::keep)
        return;
    control->quick_key = 0;
    if (rule == ui::gui_input::CaptionQuickKey::none)
        return;
    // The keys the panel's buttons hold are taken; its labels hold none, as
    // a label takes one only when a caption is set on it with a link.
    std::array<int8_t, kPanelControls> taken{};
    const auto last = std::min<std::size_t>(
        static_cast<std::size_t>(std::max<int16_t>(panel.count, 0)), kPanelControls - 1
    );
    for (std::size_t index = 0; index <= last; ++index)
        if (panel.controls[index].type == ControlType::button)
            taken[index] = panel.controls[index].quick_key;
    control->quick_key =
        static_cast<int8_t>(ui::gui_input::free_quick_key(caption, {taken.data(), last + 1}));
}

// ---------------------------------------------------------------------------
// Sliders

void slider_set_value(SliderState& slider, int32_t value) noexcept {
    const int32_t maximum = slider.maximum;
    if (value > maximum)
        value = maximum;
    // At a 53-bit significand: value / maximum * (range - 1).
    double step = static_cast<double>(value) / static_cast<double>(maximum);
    step *= static_cast<double>(static_cast<int32_t>(slider.range) - 1);
    // The fraction test uses the low 32 bits of the truncated step.
    const auto whole = static_cast<int32_t>(static_cast<uint32_t>(truncate_to_int64(step)));
    if (step - static_cast<double>(whole) != 0.0) // unordered (NaN) compares equal here
        step += 1.0;
    slider.knob = static_cast<int16_t>(truncate_to_int64(step));
}

int32_t slider_value(const SliderState& slider) noexcept {
    if (slider.range < 2)
        return 0;
    double value = static_cast<double>(slider.knob) / static_cast<double>(slider.range - 1);
    value *= static_cast<double>(slider.maximum);
    return static_cast<int32_t>(static_cast<uint32_t>(truncate_to_int64(value)));
}

// ---------------------------------------------------------------------------
// Tab panel

std::string_view options_panel_file(OptionsPanel panel, bool realtime) noexcept {
    switch (panel) {
    case OptionsPanel::tabs:
        return realtime ? "PREFS.GUI" : "STARTOPT.GUI";
    case OptionsPanel::sound:
        return realtime ? "SOUNDSRT.GUI" : "SOUNDS.GUI";
    case OptionsPanel::visuals:
        return realtime ? "VISUALRT.GUI" : "VISUALS.GUI";
    case OptionsPanel::select_video_mode:
        return "SELVMODE.GUI";
    case OptionsPanel::speeds:
        return realtime ? "SPEEDSRT.GUI" : "SPEEDS.GUI";
    case OptionsPanel::music:
        return realtime ? "MUSICRT.GUI" : "MUSIC.GUI";
    }
    return {};
}

std::string_view options_panel_background(OptionsPanel panel) noexcept {
    switch (panel) {
    case OptionsPanel::tabs:
        return "options4x";
    case OptionsPanel::sound:
        return "optsound4x";
    case OptionsPanel::visuals:
    case OptionsPanel::select_video_mode:
        return "optvisual4x";
    case OptionsPanel::speeds:
        return "optinterface4x";
    case OptionsPanel::music:
        return "optmusic4x";
    }
    return {};
}

void options_open(Panel& panel, OptionsContext& context) noexcept {
    auto& host = context.host;
    if (!context.in_game)
        call(context, host.draw_current_frame);
    auto& lightbar = context.lightbar;
    int32_t width = 0;
    int32_t height = 0;
    int32_t y = 0;
    lightbar.flip =
        host.copy_top_panel != nullptr ? host.copy_top_panel(host.context, &width, &height, &y) : 0;
    lightbar.scroll = 0;
    lightbar.last_column = width - 1;
    lightbar.last_row = height - 1;
    lightbar.panel_y = y;
    lightbar.active = 1;
    lightbar.backup =
        host.create_surface != nullptr
            ? host.create_surface(
                  host.context, "BKUPSURFACE", kLightbarBackupWidth, kLightbarBackupHeight
              )
            : 0;
    lightbar.velocity = 0;
    if (host.load_panel != nullptr)
        host.load_panel(host.context, panel);
    options_enter_tabs(panel, context);
    if (!context.in_game && host.load_background != nullptr)
        host.load_background(host.context, "options4x");
    options_capture_entry(context);
    panel.dirty = true;
    if (context.in_game)
        play(context, "Panel");
}

OptionsLightbarStep options_lightbar_step(OptionsLightbar& lightbar) noexcept {
    OptionsLightbarStep step;
    if (lightbar.active == 0)
        return step;
    step.drawn = true;
    const auto limit = lightbar.last_column;
    if (lightbar.scroll < kLightbarScrollEnd) {
        const auto before = lightbar.scroll;
        lightbar.scroll += kLightbarScrollStep;
        if (lightbar.scroll >= kLightbarScrollEnd) {
            step.play_options_sound = true;
            lightbar.scroll = kLightbarScrollEnd;
        }
        step.stamp_lightbar = lightbar.scroll > limit && before < limit;
    }
    if (lightbar.scroll < limit)
        lightbar.velocity += kLightbarVelocityStep;
    else {
        lightbar.velocity -= kLightbarVelocityStep;
        if (lightbar.velocity < 0)
            lightbar.velocity = 0;
    }
    const auto top = lightbar.panel_y;
    const auto lifted = top - lightbar.velocity;
    if (lightbar.scroll > limit) {
        if (lightbar.scroll < kLightbarScrollEnd)
            ++lightbar.scroll;
        const auto edge = lightbar.scroll;
        step.destination = {
            LightbarPoint{limit, top},
            LightbarPoint{edge, lifted},
            LightbarPoint{edge, kLightbarBottomRow},
            LightbarPoint{limit, kLightbarBottomRow}
        };
    } else {
        const auto edge = lightbar.scroll;
        step.destination = {
            LightbarPoint{edge, lifted},
            LightbarPoint{kLightbarFoldColumn, top},
            LightbarPoint{kLightbarFoldColumn, kLightbarBottomRow},
            LightbarPoint{edge, kLightbarBottomRow}
        };
    }
    step.source = {
        LightbarPoint{1, 1},
        LightbarPoint{lightbar.last_column, 1},
        LightbarPoint{lightbar.last_column, lightbar.last_row},
        LightbarPoint{1, lightbar.last_row}
    };
    return step;
}

void options_capture_entry(OptionsContext& context) noexcept {
    if (context.preferences != nullptr)
        prefs::capture_options_entry(*context.preferences, context.snapshot);
    context.screen_size_restored = false;
}

void options_enter_tabs(Panel& panel, OptionsContext& context) noexcept {
    if (context.in_game)
        context.realtime_panels = true;
    panel_set_grayed(panel, "MUSIC", context.audio_device_missing);
    if (context.in_game && context.session_kind != oa::data::campaign::SessionKind::multiplayer)
        context.hold_game = true;
}

void options_extend_panel_for_game(Panel& panel, const OptionsContext& context) noexcept {
    const auto existing = panel_find(panel, kFillerName);
    if (!context.realtime_panels)
        return;
    auto& root = panel.controls[0];
    root.width = static_cast<int16_t>(root.width + kGamePanelExtraWidth);
    if (existing != -1)
        return;
    auto* filler = panel_append(panel, ControlType::filler, kFillerName);
    if (filler == nullptr)
        return;
    filler->x = kGamePanelFillerX;
    filler->y = 0;
    filler->width = static_cast<int16_t>(root.width - filler->x);
    filler->height = root.height;
    filler->active = 1;
}

void options_prepare_realtime_panel(Panel& panel, OptionsContext& context) noexcept {
    options_enter_tabs(panel, context);
    options_extend_panel_for_game(panel, context);
}

void options_merge_realtime_panel(Panel& panel, const Panel& sub) noexcept {
    const auto& sub_root = sub.controls[0];
    int32_t dx = sub_root.x;
    int32_t dy = sub_root.y;
    if (auto* filler = panel_control(panel, kFillerName)) {
        // Halved toward zero, so a sub-panel wider than PANEL moves left.
        dx = filler->x + (filler->width - sub_root.width) / 2;
        dy = filler->y + (filler->height - sub_root.height) / 2;
        filler->active = 0;
    }
    for (int32_t index = 1; index <= sub.count && index < static_cast<int32_t>(kPanelControls);
         ++index) {
        const auto& record = sub.controls[static_cast<std::size_t>(index)];
        auto* merged = panel_append(panel, record.type, control_name(record));
        if (merged == nullptr)
            break;
        *merged = record;
        merged->x = static_cast<int16_t>(record.x + dx);
        merged->y = static_cast<int16_t>(record.y + dy);
    }
    panel.dirty = true;
}

OptionsAction options_on_tab_click(Panel& panel, OptionsContext& context) noexcept {
    auto action = OptionsAction::none;
    if (panel.selected != kNoSelection) {
        select_tab(panel);
        if (panel_selected_is(panel, "SPEEDS"))
            action = OptionsAction::open_speeds;
        else if (panel_selected_is(panel, "VISUALS"))
            action = OptionsAction::open_visuals;
        else if (panel_selected_is(panel, "MUSIC"))
            action = OptionsAction::open_music;
        else if (panel_selected_is(panel, "PREV")) {
            play(context, kOptionsSound);
            call(context, context.host.save_options);
            context.options_dirty = 1;
            return OptionsAction::close_saved;
        } else if (panel_selected_is(panel, "CANCEL")) {
            play(context, kPreviousSound);
            call(context, context.host.restore_all);
            // The sound and visual UNDOs it runs end by reapplying the volumes
            // and gamma.
            call(context, context.host.apply_volumes);
            context.options_dirty = 1;
            return OptionsAction::close_restored;
        } else if (panel_selected_is(panel, "SOUND"))
            action = OptionsAction::open_sound;
        else {
            panel_clear_selection(panel);
            return OptionsAction::none;
        }
        play(context, kOptionsSound);
        context.options_dirty = 0;
    }
    call(context, context.host.release_lightbar);
    return action;
}

void options_leave_subpanel(OptionsContext& context) noexcept {
    context.realtime_panels = false;
}

// ---------------------------------------------------------------------------
// SOUNDS

void options_update_sound_state(Panel& panel, const OptionsContext& context) noexcept {
    const auto mode = sound_mode(*context.preferences);
    panel_set_stage(panel, "MODE", mode);
    panel_set_active(panel, "VOLTEXT", mode != kSoundModeOff ? 1 : 0);
    panel_set_disabled(panel, "FXVOL", mode == kSoundModeOff);
    panel_set_disabled(panel, "TEST", mode == kSoundModeOff);
    panel_set_disabled(panel, "SPEECH", mode == kSoundModeOff);
}

void options_enter_sound(Panel& panel, OptionsContext& context) noexcept {
    options_enter_tabs(panel, context);
    panel_set_group_value(panel, "SOUND", 1);
    if (auto* fx = panel_control(panel, "FXVOL")) {
        fx->slider.maximum = kFxVolumeMaximum;
        fx->on_change = options_on_fx_volume_slider;
        // The knob word is primed with the volume's low word before the step
        // is computed from its sign-extended value.
        const auto low = static_cast<int16_t>(context.preferences->fx_volume & 0xffffU);
        fx->slider.knob = low;
        slider_set_value(fx->slider, low);
    }
    options_run_slider_callbacks(panel, context);
    const auto& preferences = *context.preferences;
    const auto speech = (preferences.sound_flags & flag::speech_fx) == 0
                            ? uint8_t{0}
                            : static_cast<uint8_t>(preferences.unit_chat / kChatVolumeStep);
    panel_set_stage(panel, "SPEECH", speech);
    options_update_sound_state(panel, context);
    panel.dirty = true;
}

OptionsAction options_on_sound_click(Panel& panel, OptionsContext& context) noexcept {
    auto& preferences = *context.preferences;
    if (panel.selected == kNoSelection) {
        options_leave_subpanel(context);
        return OptionsAction::none;
    }
    panel.dirty = true;
    if (panel_selected_is(panel, "SPEECH")) {
        play(context, kOptionsSound);
        const auto stage = panel.controls[static_cast<std::size_t>(panel.selected)].stage;
        preferences.sound_flags = static_cast<uint16_t>(
            (stage != 0 ? flag::speech_fx : 0U) | (preferences.sound_flags & ~flag::speech_fx)
        );
        preferences.unit_chat = static_cast<uint8_t>(stage * kChatVolumeStep);
        panel_clear_selection(panel);
    } else if (panel_selected_is(panel, "MODE")) {
        const auto stage = panel_stage(panel, "MODE");
        const auto flags = preferences.sound_flags;
        preferences.sound_flags = static_cast<uint16_t>(((flags ^ stage) & kSoundModeMask) ^ flags);
        const auto mode = sound_mode(preferences);
        if (mode == kSoundModeOff)
            call(context, context.host.stop_sound);
        if (context.host.set_spatial_sound != nullptr)
            context.host.set_spatial_sound(context.host.context, mode == kSoundModeSpatial);
        if (mode == kSoundModeTest && !context.in_game)
            call(context, context.host.play_voice_test);
        options_update_sound_state(panel, context);
        panel_clear_selection(panel);
        play(context, kOptionsSound);
        return OptionsAction::none;
    }
    // Both end by reapplying the volumes and gamma.
    if (panel_selected_is(panel, "UNDO")) {
        call(context, context.host.restore_sound);
        call(context, context.host.apply_volumes);
    } else if (panel_selected_is(panel, "RESTORE")) {
        call(context, context.host.reset_sound);
        call(context, context.host.apply_volumes);
    } else if (panel_selected_is(panel, "TEST")) {
        if (context.host.play_wave != nullptr)
            context.host.play_wave(context.host.context, kTestWave.data());
        panel_clear_selection(panel);
        return OptionsAction::none;
    } else {
        if (panel.selected == kNoSelection)
            return OptionsAction::none;
        if (selected_is_button(panel))
            return options_on_tab_click(panel, context);
        panel_clear_selection(panel);
        return OptionsAction::none;
    }
    play(context, kOptionsSound);
    return OptionsAction::reload;
}

// ---------------------------------------------------------------------------
// VISUALS

void options_hide_controls_with_prefix(Panel& panel, std::string_view prefix) noexcept {
    for (int32_t index = 0; index <= panel.count; ++index) {
        const auto name = control_name(panel.controls[static_cast<std::size_t>(index)]);
        if (name.size() >= prefix.size() && name.substr(0, prefix.size()) == prefix)
            panel_set_active(panel, name, 0);
    }
}

void options_sync_video_mode(Panel& panel, OptionsContext& context) noexcept {
    auto* slider = panel_control(panel, "VIDSLDR");
    if (slider == nullptr)
        return;
    const auto& preferences = *context.preferences;
    const auto& list = context.display_modes;
    for (int32_t index = 0; index < list.count; ++index) {
        const auto& mode = list.modes[static_cast<std::size_t>(index) % kDisplayModeCapacity];
        if (preferences.display_width != static_cast<uint32_t>(mode.width) ||
            preferences.display_height != static_cast<uint32_t>(mode.height))
            continue;
        slider_set_value(slider->slider, index);
        if (panel_control(panel, "VIDVAL") != nullptr)
            set_mode_text(panel, mode);
        return;
    }
}

void options_on_video_mode_slider(Panel& panel, OptionsContext& context) noexcept {
    if (auto* slider = panel_control(panel, "VIDSLDR")) {
        const auto index = slider_value(slider->slider);
        if (index >= 0 && index < static_cast<int32_t>(kDisplayModeCapacity)) {
            const auto mode = context.display_modes.modes[static_cast<std::size_t>(index)];
            set_mode_text(panel, mode);
            context.preferences->display_width = static_cast<uint32_t>(mode.width);
            context.preferences->display_height = static_cast<uint32_t>(mode.height);
            context.screen_size_restored = false;
        }
    }
    panel.dirty = true;
}

void options_on_gamma_slider(Panel& panel, OptionsContext& context) noexcept {
    if (const auto* slider = panel_control(panel, "GAMMA")) {
        context.preferences->gamma = static_cast<uint32_t>(slider_value(slider->slider));
        call(context, context.host.apply_volumes);
    }
}

void options_enter_visuals(Panel& panel, OptionsContext& context, bool select_mode) noexcept {
    if (!select_mode)
        options_enter_tabs(panel, context);
    if (!context.realtime_panels) {
        context.display_modes = DisplayModeList{};
        context.display_modes_ready = true;
        const bool scanned =
            context.host.scan_display_modes != nullptr &&
            context.host.scan_display_modes(context.host.context, context.display_modes);
        if (scanned) {
            auto& list = context.display_modes;
            list.count = oa::present::world_renderer::sort_display_modes(
                list.modes.data(),
                std::clamp(list.count, 0, static_cast<int32_t>(kDisplayModeCapacity)),
                context.minimum_mode_height
            );
            if (auto* slider = panel_control(panel, "VIDSLDR")) {
                slider->on_change = options_on_video_mode_slider;
                slider->slider.maximum = context.display_modes.count - 1;
                options_sync_video_mode(panel, context);
            }
        }
    } else {
        context.display_modes_ready = false;
        options_hide_controls_with_prefix(panel, "MAP");
        options_hide_controls_with_prefix(panel, "VID");
    }
    if (select_mode)
        return;
    panel_set_group_value(panel, "VISUALS", 1);
    const auto flags = context.preferences->graphics_flags;
    const auto set_flag_stage = [&](std::string_view name, uint16_t mask, int shift) {
        auto* control = panel_control(panel, name);
        if (control != nullptr && control->type == ControlType::button)
            control->stage = static_cast<uint8_t>((flags & mask) >> shift);
    };
    set_flag_stage("ANTI", flag::anti_alias, 1);
    set_flag_stage("BSHADOWS", flag::feature_shadows, 4);
    set_flag_stage("SHADING", flag::shading, 5);
    bind_slider(
        panel,
        "GAMMA",
        kGammaMaximum,
        static_cast<int32_t>(context.preferences->gamma),
        options_on_gamma_slider
    );
}

OptionsAction options_on_visuals_click(Panel& panel, OptionsContext& context) noexcept {
    auto& flags = context.preferences->graphics_flags;
    if (panel.selected == kNoSelection) {
        context.display_modes_ready = false;
        options_leave_subpanel(context);
        return OptionsAction::none;
    }
    if (panel_selected_is(panel, "ANTI")) {
        play(context, kOptionsSound);
        const auto stage = panel_stage(panel, "ANTI");
        flags = static_cast<uint16_t>(((stage & 1U) << 1) | (flags & ~flag::anti_alias));
    } else if (panel_selected_is(panel, "BSHADOWS")) {
        play(context, kOptionsSound);
        const auto stage = panel_stage(panel, "BSHADOWS");
        flags = static_cast<uint16_t>(((stage & 1U) << 4) | (flags & ~flag::feature_shadows));
        // Unit and vehicle shadows follow the feature-shadow bit.
        flags = static_cast<uint16_t>(
            ((flags >> 1) & flag::vehicle_shadows) | (flags & ~flag::vehicle_shadows)
        );
        flags = static_cast<uint16_t>(((flags >> 1) & flag::shadows) | (flags & ~flag::shadows));
    } else if (panel_selected_is(panel, "SHADING")) {
        play(context, kOptionsSound);
        const auto stage = panel_stage(panel, "SHADING");
        flags = static_cast<uint16_t>(((stage & 1U) << 5) | (flags & ~flag::shading));
    } else if (panel_selected_is(panel, "UNDO")) {
        // Both arms end by reapplying the volumes and gamma: the gamma they set shows.
        play(context, kOptionsSound);
        if (context.state != nullptr) {
            prefs::restore_visual_options(*context.preferences, *context.state, context.snapshot);
            context.screen_size_restored = false;
            call(context, context.host.apply_volumes);
        }
        return OptionsAction::reload;
    } else if (panel_selected_is(panel, "RESTORE")) {
        play(context, kOptionsSound);
        if (context.state != nullptr) {
            prefs::reset_visual_options(*context.preferences, *context.state);
            // The display stores are reset only while no game loads or runs.
            if ((context.state->session_flags & oa::ui::frontend_state::flags::loading) == 0) {
                call(context, context.host.reset_screen_size);
                context.screen_size_restored = true;
            }
            call(context, context.host.apply_volumes);
        }
        return OptionsAction::reload;
    } else if (panel_selected_is(panel, "OK") && root_named(panel, "SELVMODE.GUI")) {
        play(context, kOptionsSound);
        return OptionsAction::none;
    } else {
        if (panel.selected == kNoSelection)
            return OptionsAction::none;
        if (selected_is_button(panel))
            return options_on_tab_click(panel, context);
        panel_clear_selection(panel);
        return OptionsAction::none;
    }
    panel.dirty = true;
    panel_clear_selection(panel);
    return OptionsAction::none;
}

// ---------------------------------------------------------------------------
// SPEEDS

void options_on_game_speed_slider(Panel& panel, OptionsContext& context) noexcept {
    if (context.game_speed_locked)
        return;
    const auto* slider = panel_control(panel, "GAME");
    if (slider == nullptr)
        return;
    const auto value = slider_value(slider->slider);
    auto& speed = context.preferences->game_speed;
    speed = value < 1 ? uint16_t{1} : static_cast<uint16_t>(value);
    if (context.host.set_game_speed != nullptr)
        context.host.set_game_speed(context.host.context, speed);
    panel.dirty = true;
}

void options_on_scroll_speed_slider(Panel& panel, OptionsContext& context) noexcept {
    const auto* slider = panel_control(panel, "SCREEN");
    if (slider == nullptr)
        return;
    const auto value = slider_value(slider->slider);
    context.preferences->scroll_speed = value < 2 ? uint8_t{1} : static_cast<uint8_t>(value);
    panel.dirty = true;
}

void options_on_max_lines_slider(Panel& panel, OptionsContext& context) noexcept {
    auto& lines = context.preferences->text_lines;
    if (const auto* slider = panel_control(panel, "MAXLINES")) {
        const auto value = slider_value(slider->slider);
        lines = value < 0 ? 0U : static_cast<uint32_t>(value);
        panel.dirty = true;
    }
    set_lines_text(panel, lines);
}

void options_on_text_scroll_slider(Panel& panel, OptionsContext& context) noexcept {
    const auto* slider = panel_control(panel, "TXTSCROL");
    if (slider == nullptr)
        return;
    const auto value = slider_value(slider->slider);
    context.preferences->text_scroll = static_cast<uint32_t>(value);
    set_number_text(panel, "TEXTSCROLLTEXT", value, "%lld secs");
    panel.dirty = true;
}

void options_on_fx_volume_slider(Panel& panel, OptionsContext& context) noexcept {
    if (const auto* slider = panel_control(panel, "FXVOL")) {
        context.preferences->fx_volume = static_cast<uint32_t>(slider_value(slider->slider));
        call(context, context.host.apply_volumes);
    }
}

void options_on_music_volume_slider(Panel& panel, OptionsContext& context) noexcept {
    if (const auto* slider = panel_control(panel, "MUSICVOL")) {
        context.preferences->music_volume = static_cast<uint32_t>(slider_value(slider->slider));
        call(context, context.host.apply_volumes);
    }
}

void options_run_slider_callbacks(Panel& panel, OptionsContext& context) noexcept {
    for (int32_t index = 1; index <= panel.count; ++index) {
        const auto& control = panel.controls[static_cast<std::size_t>(index)];
        if (control.type == ControlType::slider && control.on_change != nullptr)
            control.on_change(panel, context);
    }
}

void options_enter_speeds(Panel& panel, OptionsContext& context) noexcept {
    options_enter_tabs(panel, context);
    auto& preferences = *context.preferences;
    const bool has_game = panel_find(panel, "GAME") != -1;
    panel_set_group_value(panel, "SPEEDS", 1);
    if (has_game)
        bind_slider(
            panel, "GAME", kGameSpeedMaximum, preferences.game_speed, options_on_game_speed_slider
        );
    bind_slider(
        panel,
        "SCREEN",
        kScrollSpeedMaximum,
        preferences.scroll_speed,
        options_on_scroll_speed_slider
    );
    panel_set_stage(
        panel, "UNITCHAT", static_cast<uint8_t>(preferences.unit_chat_text / kChatVolumeStep)
    );
    panel_set_stage(panel, "LEFTCLICK", static_cast<uint8_t>(preferences.interface_type));
    set_lines_text(panel, preferences.text_lines);
    bind_slider(
        panel,
        "MAXLINES",
        kMaxLinesMaximum,
        static_cast<int32_t>(preferences.text_lines),
        options_on_max_lines_slider
    );
    bind_slider(
        panel,
        "TXTSCROL",
        kTextScrollMaximum,
        static_cast<int32_t>(preferences.text_scroll),
        options_on_text_scroll_slider
    );
    options_run_slider_callbacks(panel, context);
}

OptionsAction options_on_speeds_click(Panel& panel, OptionsContext& context) noexcept {
    auto& preferences = *context.preferences;
    if (panel.selected == kNoSelection) {
        options_leave_subpanel(context);
        return OptionsAction::none;
    }
    if (panel_selected_is(panel, "LEFTCLICK")) {
        play(context, kOptionsSound);
        preferences.interface_type = static_cast<int32_t>(panel_stage(panel, "LEFTCLICK"));
        panel_clear_selection(panel);
        return OptionsAction::none;
    }
    if (panel_selected_is(panel, "UNITCHAT")) {
        play(context, kOptionsSound);
        panel_clear_selection(panel);
        preferences.unit_chat_text =
            static_cast<uint8_t>(panel_stage(panel, "UNITCHAT") * kChatVolumeStep);
        return OptionsAction::none;
    }
    if (panel_selected_is(panel, "UNDO")) {
        play(context, kOptionsSound);
        prefs::restore_speed_options(preferences, context.snapshot);
        return OptionsAction::reload;
    }
    if (panel_selected_is(panel, "RESTORE")) {
        play(context, kOptionsSound);
        prefs::reset_speed_options(preferences);
        return OptionsAction::reload;
    }
    if (selected_is_button(panel))
        return options_on_tab_click(panel, context);
    panel_clear_selection(panel);
    return OptionsAction::none;
}

// ---------------------------------------------------------------------------
// MUSIC transport

void options_enter_music(Panel& panel, OptionsContext& context) noexcept {
    options_enter_tabs(panel, context);
    panel_set_group_value(panel, "MUSIC", 1);
    if (auto* music = panel_control(panel, "MUSICVOL")) {
        music->slider.maximum = kFxVolumeMaximum;
        music->on_change = options_on_music_volume_slider;
        const auto low = static_cast<int16_t>(context.preferences->music_volume & 0xffffU);
        music->slider.knob = low;
        slider_set_value(music->slider, low);
    }
    options_update_cd_controls(panel, context);
    panel.dirty = true;
}

void options_update_cd_controls(Panel& panel, const OptionsContext& context) noexcept {
    const auto& preferences = *context.preferences;
    const bool music_on = (preferences.music_flags & flag::music_mode) != 0;
    panel_set_stage(panel, "NOTRAK", music_on ? 1 : 0);
    panel_set_stage(panel, "TRACKMODE", static_cast<uint8_t>(preferences.cd_mode - 1U));
    panel_set_disabled(panel, "MUSICVOL", !music_on);
    for (const auto name : {"CDPREV", "CDSTOP", "CDPLAY", "CDNEXT", "TRACKMODE"})
        panel_set_grayed(panel, name, !music_on);
    panel_set_grayed(panel, "TRACKTYPE", !(music_on && preferences.cd_mode == kCdModeTrackType));
}

} // namespace oa::ui::frontend
