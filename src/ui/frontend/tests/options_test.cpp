// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "test_support.hpp"

#include "oa/ui/gui_layout/gui_gadget.hpp"

#include <cstdint>
#include <string>

namespace oa::ui::frontend::test {
namespace {

namespace flag = prefs::preference_flags;

struct Recorder {
    std::vector<std::string> sounds;
    int volumes = 0;
    int saves = 0;
    int restores = 0;
    int sound_undo = 0;
    int sound_reset = 0;
    int stops = 0;
    int voice_tests = 0;
    uint16_t speed = 0;
    std::vector<DisplayMode> modes;
};

OptionsContext make_context(prefs::Preferences& preferences, Recorder& recorder) {
    OptionsContext context;
    context.preferences = &preferences;
    context.host.context = &recorder;
    context.host.play_sound = [](void* c, const char* name) {
        static_cast<Recorder*>(c)->sounds.emplace_back(name);
    };
    context.host.apply_volumes = [](void* c) { ++static_cast<Recorder*>(c)->volumes; };
    context.host.save_options = [](void* c) { ++static_cast<Recorder*>(c)->saves; };
    context.host.restore_all = [](void* c) { ++static_cast<Recorder*>(c)->restores; };
    context.host.restore_sound = [](void* c) { ++static_cast<Recorder*>(c)->sound_undo; };
    context.host.reset_sound = [](void* c) { ++static_cast<Recorder*>(c)->sound_reset; };
    context.host.stop_sound = [](void* c) { ++static_cast<Recorder*>(c)->stops; };
    context.host.play_voice_test = [](void* c) { ++static_cast<Recorder*>(c)->voice_tests; };
    context.host.set_game_speed = [](void* c, uint16_t speed) {
        static_cast<Recorder*>(c)->speed = speed;
    };
    context.host.scan_display_modes = [](void* c, DisplayModeList& list) {
        const auto& modes = static_cast<Recorder*>(c)->modes;
        list.count = static_cast<int32_t>(modes.size());
        for (std::size_t i = 0; i < modes.size(); ++i)
            list.modes[i] = modes[i];
        return !modes.empty();
    };
    return context;
}

// STARTOPT.GUI with a sub-panel merged over it; false without the installed game's data.
bool options_panel(Panel& panel, const char* sub) {
    const auto base = load_gui("startopt.gui");
    const auto extra = load_gui(sub);
    if (!base || !extra)
        return false;
    panel_load_layout(panel, *base);
    merge_layout(panel, *extra);
    return true;
}

OA_TEST(slider_steps_round_up_and_values_truncate) {
    SliderState slider{11, 0x40, 0};
    slider_set_value(slider, 0);
    OA_CHECK(slider.knob == 0);
    slider_set_value(slider, 0x40);
    OA_CHECK(slider.knob == 10);
    slider_set_value(slider, 1); // 1/64*10 = 0.156 rounds up
    OA_CHECK(slider.knob == 1);
    slider_set_value(slider, 1000); // clamped to the maximum
    OA_CHECK(slider.knob == 10);
    slider.knob = 3;
    OA_CHECK(slider_value(slider) == 19); // 3/10*64 = 19.2
    slider.range = 1;
    OA_CHECK(slider_value(slider) == 0);
    // Negative fractional steps: -0.5 + 1 truncates to 0, not -1.
    SliderState negative{3, 4, 0};
    slider_set_value(negative, -1);
    OA_CHECK(negative.knob == 0);
    // A zero maximum divides 0 by 0: NaN skips the adjustment and converts to
    // INT64_MIN, whose low word is 0.
    SliderState empty{5, 0, 7};
    slider_set_value(empty, 0);
    OA_CHECK(empty.knob == 0);
}

// NEWCAMP.GUI's chosen Side0 as loaded: status 1, no stages.
OA_TEST(load_layout_takes_status_as_group_value) {
    ui::gui_layout::Layout layout;
    layout.gadgets.resize(3);
    layout.gadgets[0].common.type = ui::gui_layout::GadgetType::panel;
    for (std::size_t index = 1; index < layout.gadgets.size(); ++index) {
        auto& gadget = layout.gadgets[index];
        gadget.common.type = ui::gui_layout::GadgetType::button;
        gadget.common.association = 5;
        gadget.common.name = index == 1 ? "Side0" : "Side1";
        ui::gui_layout::ButtonFields fields;
        fields.status = index == 1 ? 1 : 0;
        gadget.fields = fields;
    }
    Panel panel;
    panel_load_layout(panel, layout);
    const auto* chosen = panel_control(panel, "Side0");
    const auto* other = panel_control(panel, "Side1");
    OA_CHECK(chosen != nullptr && chosen->group_value == 1 && chosen->stage == 0);
    OA_CHECK(other != nullptr && other->group_value == 0 && other->stage == 0);
}

OA_TEST(set_text_gives_a_button_the_first_free_quick_key) {
    ui::gui_layout::Layout layout;
    layout.gadgets.resize(5);
    layout.gadgets[0].common.type = ui::gui_layout::GadgetType::panel;
    const char* names[] = {"", "SAVE", "SEND", "CYCLE", "PLAIN"};
    for (std::size_t index = 1; index < layout.gadgets.size(); ++index) {
        auto& gadget = layout.gadgets[index];
        gadget.common.type = ui::gui_layout::GadgetType::button;
        gadget.common.name = names[index];
        ui::gui_layout::ButtonFields fields;
        fields.quick_key = index == 1 ? 'S' : 0;
        fields.stages = index == 3 ? 2 : 0;
        gadget.fields = fields;
    }
    layout.gadgets[4].common.attributes =
        static_cast<int32_t>(ui::gui_layout::attribute::no_quick_key);
    Panel panel;
    panel_load_layout(panel, layout);
    OA_CHECK(panel_control(panel, "SAVE")->quick_key == 'S');
    // SAVE holds 's' in either case, so "Send" takes its 'e'.
    panel_set_text(panel, "SEND", "Send");
    OA_CHECK(panel_control(panel, "SEND")->quick_key == 'e');
    // A caption set again gives up the key it had before it looks.
    panel_set_text(panel, "SAVE", "Save");
    OA_CHECK(panel_control(panel, "SAVE")->quick_key == 'S');
    // An empty caption keeps the key; stages and no_quick_key take none.
    panel_set_text(panel, "SEND", "");
    OA_CHECK(panel_control(panel, "SEND")->quick_key == 'e');
    panel_set_text(panel, "CYCLE", "One|Two");
    OA_CHECK(panel_control(panel, "CYCLE")->quick_key == 0);
    panel_set_text(panel, "PLAIN", "Plain");
    OA_CHECK(panel_control(panel, "PLAIN")->quick_key == 0);
}

OA_GAME_DATA_TEST(tab_panel_routes_and_closes) {
    Panel panel;
    if (!options_panel(panel, "sounds.gui"))
        return;
    prefs::Preferences preferences{};
    Recorder recorder;
    auto context = make_context(preferences, recorder);
    select(panel, "SPEEDS");
    OA_CHECK(options_on_tab_click(panel, context) == OptionsAction::open_speeds);
    select(panel, "VISUALS");
    OA_CHECK(options_on_tab_click(panel, context) == OptionsAction::open_visuals);
    // The clicked tab is its group's chosen member.
    OA_CHECK(panel_control(panel, "VISUALS")->group_value == 1);
    OA_CHECK(panel_control(panel, "SPEEDS")->group_value == 0);
    select(panel, "MUSIC");
    OA_CHECK(options_on_tab_click(panel, context) == OptionsAction::open_music);
    select(panel, "PREV");
    OA_CHECK(options_on_tab_click(panel, context) == OptionsAction::close_saved);
    OA_CHECK(recorder.saves == 1 && context.options_dirty == 1);
    select(panel, "CANCEL");
    const auto volumes_before_cancel = recorder.volumes;
    OA_CHECK(options_on_tab_click(panel, context) == OptionsAction::close_restored);
    OA_CHECK(recorder.restores == 1);
    OA_CHECK(recorder.volumes == volumes_before_cancel + 1);
    OA_CHECK(recorder.sounds.back() == "Previous");
    select(panel, "FXVOL");
    OA_CHECK(options_on_tab_click(panel, context) == OptionsAction::none);
    OA_CHECK(panel.selected == kNoSelection);
}

OA_GAME_DATA_TEST(sound_panel_setup_and_clicks) {
    Panel panel;
    if (!options_panel(panel, "sounds.gui"))
        return;
    prefs::Preferences preferences{};
    preferences.fx_volume = 27;
    preferences.sound_flags = static_cast<uint16_t>(flag::speech_fx | 1U);
    preferences.unit_chat = 10;
    Recorder recorder;
    auto context = make_context(preferences, recorder);
    options_enter_sound(panel, context);
    const auto* fx = panel_control(panel, "FXVOL");
    OA_CHECK(fx != nullptr && fx->slider.maximum == 0x40);
    OA_CHECK(fx != nullptr && fx->on_change == options_on_fx_volume_slider);
    // Entry runs the callback: the stored volume is the knob's quantized value.
    OA_CHECK(
        fx != nullptr && preferences.fx_volume == static_cast<uint32_t>(slider_value(fx->slider))
    );
    OA_CHECK(recorder.volumes >= 1);
    OA_CHECK(panel_stage(panel, "SPEECH") == 2);
    OA_CHECK(panel_stage(panel, "MODE") == 1);
    const auto* sound_tab = panel_control(panel, "SOUND");
    OA_CHECK(sound_tab != nullptr && sound_tab->group_value == 1);

    // MODE 0 turns sound off and grays the dependent controls.
    panel_set_stage(panel, "MODE", 0);
    select(panel, "MODE");
    OA_CHECK(options_on_sound_click(panel, context) == OptionsAction::none);
    OA_CHECK((preferences.sound_flags & 7U) == 0);
    OA_CHECK((preferences.sound_flags & flag::speech_fx) != 0);
    OA_CHECK(recorder.stops == 1);
    OA_CHECK(panel_control(panel, "FXVOL")->grayed == 1);
    OA_CHECK(panel_control(panel, "VOLTEXT")->active == 0);

    // SPEECH stores the flag and five times the stage.
    panel_set_stage(panel, "SPEECH", 0);
    select(panel, "SPEECH");
    options_on_sound_click(panel, context);
    OA_CHECK((preferences.sound_flags & flag::speech_fx) == 0);
    OA_CHECK(preferences.unit_chat == 0);

    // Both end by reapplying the saved volumes and gamma.
    const auto volumes_before_undo = recorder.volumes;
    select(panel, "UNDO");
    OA_CHECK(options_on_sound_click(panel, context) == OptionsAction::reload);
    OA_CHECK(recorder.sound_undo == 1 && recorder.volumes == volumes_before_undo + 1);
    select(panel, "RESTORE");
    OA_CHECK(options_on_sound_click(panel, context) == OptionsAction::reload);
    OA_CHECK(recorder.sound_reset == 1 && recorder.volumes == volumes_before_undo + 2);
    // Tab buttons fall through to the tab handler.
    select(panel, "VISUALS");
    OA_CHECK(options_on_sound_click(panel, context) == OptionsAction::open_visuals);
    panel.selected = kNoSelection;
    context.realtime_panels = true;
    options_on_sound_click(panel, context);
    OA_CHECK(!context.realtime_panels);
}

OA_GAME_DATA_TEST(visuals_flags_and_display_modes) {
    Panel panel;
    if (!options_panel(panel, "visuals.gui"))
        return;
    prefs::Preferences preferences{};
    preferences.graphics_flags = flag::shading;
    preferences.gamma = 12;
    preferences.display_width = 800;
    preferences.display_height = 600;
    Recorder recorder;
    recorder.modes = {{1024, 768, 0}, {800, 600, 0}, {512, 384, 0}, {640, 480, 0}};
    auto context = make_context(preferences, recorder);
    options_enter_visuals(panel, context, false);
    OA_CHECK(panel_stage(panel, "SHADING") == 1);
    OA_CHECK(panel_stage(panel, "ANTI") == 0);
    OA_CHECK(context.display_modes.count == 3);
    const auto* video = panel_control(panel, "VIDSLDR");
    OA_CHECK(video != nullptr && video->slider.maximum == 2);
    OA_CHECK(text_of(panel, "VIDVAL") == "800 X 600");
    const auto* gamma = panel_control(panel, "GAMMA");
    OA_CHECK(gamma != nullptr && gamma->slider.maximum == 0x14);

    // Moving VIDSLDR to the last step selects 1024x768.
    auto* slider = panel_control(panel, "VIDSLDR");
    slider->slider.knob = static_cast<int16_t>(slider->slider.range - 1);
    options_on_video_mode_slider(panel, context);
    OA_CHECK(preferences.display_width == 1024 && preferences.display_height == 768);
    OA_CHECK(text_of(panel, "VIDVAL") == "1024 X 768");

    panel_set_stage(panel, "BSHADOWS", 1);
    select(panel, "BSHADOWS");
    options_on_visuals_click(panel, context);
    OA_CHECK(
        (preferences.graphics_flags &
         (flag::feature_shadows | flag::vehicle_shadows | flag::shadows)) ==
        (flag::feature_shadows | flag::vehicle_shadows | flag::shadows)
    );
    panel_set_stage(panel, "ANTI", 1);
    select(panel, "ANTI");
    options_on_visuals_click(panel, context);
    OA_CHECK((preferences.graphics_flags & flag::anti_alias) != 0);
    OA_CHECK(panel.selected == kNoSelection);

    // The GAMMA slider stores its value and reapplies it; UNDO puts the entry
    // gamma back and RESTORE the default 12, each reapplied.
    oa::ui::frontend_state::State state{};
    context.state = &state;
    context.snapshot.gamma = 7;
    auto* gamma_slider = panel_control(panel, "GAMMA");
    gamma_slider->slider.knob = static_cast<int16_t>(gamma_slider->slider.range - 1);
    auto volumes = recorder.volumes;
    gamma_slider->on_change(panel, context);
    OA_CHECK(preferences.gamma == 0x14 && recorder.volumes == volumes + 1);
    select(panel, "UNDO");
    OA_CHECK(options_on_visuals_click(panel, context) == OptionsAction::reload);
    OA_CHECK(preferences.gamma == 7 && recorder.volumes == volumes + 2);
    select(panel, "RESTORE");
    OA_CHECK(options_on_visuals_click(panel, context) == OptionsAction::reload);
    OA_CHECK(preferences.gamma == 12 && recorder.volumes == volumes + 3);
}

OA_GAME_DATA_TEST(visuals_in_game_hides_mode_controls) {
    Panel panel;
    if (!options_panel(panel, "visuals.gui"))
        return;
    prefs::Preferences preferences{};
    Recorder recorder;
    auto context = make_context(preferences, recorder);
    context.in_game = true;
    options_enter_visuals(panel, context, false);
    OA_CHECK(context.realtime_panels);
    OA_CHECK(panel_control(panel, "VIDSLDR")->active == 0);
    OA_CHECK(panel_control(panel, "VIDVAL")->active == 0);
    OA_CHECK(panel_control(panel, "SHADING")->active == 1);
    // The widening belongs to PREFS.GUI's preparation, not to the sub-panel's set-up.
    OA_CHECK(panel_find(panel, "PANEL") == -1);
}

// PREFS.GUI with the tab's in-game sub-panel merged into it, as a tab in a
// match opens it; false without the installed game's data.
bool realtime_panel(Panel& panel, const char* sub, OptionsContext& context) {
    const auto base = load_gui("prefs.gui");
    const auto extra = load_gui(sub);
    if (!base || !extra)
        return false;
    panel_load_layout(panel, *base);
    Panel loaded;
    panel_load_layout(loaded, *extra);
    options_prepare_realtime_panel(panel, context);
    options_merge_realtime_panel(panel, loaded);
    return true;
}

// PREFS.GUI in a match: a tab widens it once, by 150 pixels, and adds the
// PANEL filler beside the tabs, into which SOUNDSRT.GUI is centred.
OA_GAME_DATA_TEST(realtime_sound_panel_merges_into_prefs) {
    const auto base = load_gui("prefs.gui");
    const auto extra = load_gui("soundsrt.gui");
    if (!base || !extra)
        return;
    Panel panel;
    panel_load_layout(panel, *base);
    Panel sub;
    panel_load_layout(sub, *extra);
    const auto tabs = panel.count;
    OA_CHECK(panel.controls[0].width == 128 && panel.controls[0].height == 354);
    OA_CHECK(sub.controls[0].width == 150 && sub.controls[0].height == 352);
    prefs::Preferences preferences{};
    preferences.fx_volume = 32;
    preferences.sound_flags = 1;
    Recorder recorder;
    auto context = make_context(preferences, recorder);
    context.in_game = true;
    options_prepare_realtime_panel(panel, context);
    OA_CHECK(context.realtime_panels && context.hold_game);
    OA_CHECK(panel.controls[0].width == 278 && panel.controls[0].height == 354);
    const auto* filler = panel_control(panel, "PANEL");
    OA_CHECK(
        filler != nullptr && filler->type == ControlType::filler && filler->x == 128 &&
        filler->y == 0 && filler->width == 150 && filler->height == 354 && filler->active == 1
    );
    OA_CHECK(panel.count == tabs + 1);

    options_merge_realtime_panel(panel, sub);
    OA_CHECK(panel.count == tabs + 1 + sub.count);
    OA_CHECK(panel_control(panel, "PANEL")->active == 0);
    // 128 + (150 - 150) / 2 across and 0 + (354 - 352) / 2 down.
    const auto* volume = panel_control(panel, "FXVOL");
    OA_CHECK(volume != nullptr && volume->x == 140 && volume->y == 69);
    const auto* image = panel_control(panel, "SOUNDSRT");
    OA_CHECK(
        image != nullptr && image->type == ControlType::frame && image->x == 128 && image->y == 1
    );
    // The tabs stay where PREFS.GUI has them.
    const auto* sound_tab = panel_control(panel, "SOUND");
    OA_CHECK(sound_tab != nullptr && sound_tab->x == 13 && sound_tab->y == 24);

    // The sub-panel's set-up widens nothing more and adds no second filler.
    options_enter_sound(panel, context);
    OA_CHECK(panel.controls[0].width == 278);
    OA_CHECK(panel.count == tabs + 1 + sub.count);
    OA_CHECK(panel_control(panel, "SOUND")->group_value == 1);
    OA_CHECK(panel_control(panel, "FXVOL")->slider.maximum == 0x40);
    OA_CHECK(panel_stage(panel, "MODE") == 1);
}

// Each tab's in-game sub-panel merges beside the tabs; VISUALRT.GUI offers no
// display mode, so none is scanned.
OA_GAME_DATA_TEST(realtime_sub_panels_merge_beside_the_tabs) {
    for (const char* sub : {"musicrt.gui", "speedsrt.gui", "visualrt.gui"}) {
        Panel panel;
        prefs::Preferences preferences{};
        preferences.game_speed = 10;
        Recorder recorder;
        recorder.modes = {{800, 600, 0}};
        auto context = make_context(preferences, recorder);
        context.in_game = true;
        if (!realtime_panel(panel, sub, context))
            return;
        OA_CHECK(panel.controls[0].width == 278);
        const auto* undo = panel_control(panel, "UNDO");
        OA_CHECK(undo != nullptr && undo->x == 128 + 13 && undo->y == 1 + 304);
    }
    Panel panel;
    prefs::Preferences preferences{};
    Recorder recorder;
    recorder.modes = {{800, 600, 0}};
    auto context = make_context(preferences, recorder);
    context.in_game = true;
    if (!realtime_panel(panel, "visualrt.gui", context))
        return;
    options_enter_visuals(panel, context, false);
    OA_CHECK(panel_find(panel, "VIDSLDR") == -1);
    OA_CHECK(context.display_modes.count == 0 && !context.display_modes_ready);
    OA_CHECK(panel_control(panel, "VISUALS")->group_value == 1);
    OA_CHECK(panel_control(panel, "GAMMA")->slider.maximum == 0x14);
    OA_CHECK(panel.controls[0].width == 278);
}

// Outside a match the sub-panels are the full-screen ones, merged by the
// application: their set-up neither widens the panel nor adds a filler.
OA_GAME_DATA_TEST(frontend_sub_panels_are_not_widened) {
    for (const char* sub : {"sounds.gui", "music.gui", "speeds.gui", "visuals.gui"}) {
        Panel panel;
        if (!options_panel(panel, sub))
            return;
        const auto width = panel.controls[0].width;
        const auto count = panel.count;
        prefs::Preferences preferences{};
        preferences.game_speed = 10;
        Recorder recorder;
        auto context = make_context(preferences, recorder);
        options_enter_sound(panel, context);
        options_enter_music(panel, context);
        options_enter_speeds(panel, context);
        options_enter_visuals(panel, context, false);
        OA_CHECK(!context.realtime_panels);
        OA_CHECK(panel.controls[0].width == width && panel.count == count);
        OA_CHECK(panel_find(panel, "PANEL") == -1);
    }
}

// Without a PANEL record the sub-panel's records move by its root's own
// position; the centring in PANEL halves the difference toward zero.
OA_TEST(realtime_merge_places_records_by_the_filler_or_the_root) {
    Panel sub;
    sub.controls[0].x = 128;
    sub.controls[0].y = 128;
    sub.controls[0].width = 150;
    sub.controls[0].height = 352;
    auto* button = panel_append(sub, ControlType::button, "RESTORE");
    OA_CHECK(button != nullptr);
    button->x = 13;
    button->y = 269;

    Panel bare;
    bare.controls[0].width = 128;
    bare.controls[0].height = 354;
    options_merge_realtime_panel(bare, sub);
    const auto* moved = panel_control(bare, "RESTORE");
    OA_CHECK(bare.count == 1 && moved != nullptr && moved->x == 141 && moved->y == 397);

    Panel narrow;
    auto* filler = panel_append(narrow, ControlType::filler, "PANEL");
    OA_CHECK(filler != nullptr);
    filler->x = 10;
    filler->y = 5;
    filler->width = 100;  // 50 narrower: moves 25 left
    filler->height = 351; // one row shorter: 0, not -1
    filler->active = 1;
    options_merge_realtime_panel(narrow, sub);
    const auto* centred = panel_control(narrow, "RESTORE");
    OA_CHECK(narrow.count == 2 && centred != nullptr && centred->x == -2 && centred->y == 274);
    OA_CHECK(panel_control(narrow, "PANEL")->active == 0);
}

OA_GAME_DATA_TEST(speeds_panel_setup_and_clicks) {
    Panel panel;
    if (!options_panel(panel, "speeds.gui"))
        return;
    prefs::Preferences preferences{};
    preferences.game_speed = 10;
    preferences.scroll_speed = 20;
    preferences.text_lines = 0;
    preferences.text_scroll = 8;
    preferences.unit_chat_text = 5;
    preferences.interface_type = 1;
    Recorder recorder;
    auto context = make_context(preferences, recorder);
    options_enter_speeds(panel, context);
    OA_CHECK(panel_stage(panel, "UNITCHAT") == 1);
    OA_CHECK(panel_stage(panel, "LEFTCLICK") == 1);
    OA_CHECK(recorder.speed == preferences.game_speed);
    OA_CHECK(preferences.game_speed >= 1);
    OA_CHECK(preferences.text_lines == 0);

    panel_set_stage(panel, "LEFTCLICK", 0);
    select(panel, "LEFTCLICK");
    OA_CHECK(options_on_speeds_click(panel, context) == OptionsAction::none);
    OA_CHECK(preferences.interface_type == 0);
    panel_set_stage(panel, "UNITCHAT", 2);
    select(panel, "UNITCHAT");
    options_on_speeds_click(panel, context);
    OA_CHECK(preferences.unit_chat_text == 10);
    select(panel, "RESTORE");
    OA_CHECK(options_on_speeds_click(panel, context) == OptionsAction::reload);

    // A locked game speed ignores the slider.
    context.game_speed_locked = true;
    recorder.speed = 0;
    options_on_game_speed_slider(panel, context);
    OA_CHECK(recorder.speed == 0);
}

// Every options slider stores a value that is a fixed point of the
// knob mapping, so saving and reopening the panel keeps the preference.
OA_GAME_DATA_TEST(every_slider_round_trips_preferences) {
    Panel sounds, speeds, visuals;
    if (!options_panel(sounds, "sounds.gui") || !options_panel(speeds, "speeds.gui") ||
        !options_panel(visuals, "visuals.gui"))
        return;
    const auto check_slider =
        [](Panel& panel, const char* name, int32_t maximum, auto&& read_back, auto&& enter) {
            for (int32_t value = 0; value <= maximum; ++value) {
                enter(value);
                const auto* control = panel_control(panel, name);
                OA_CHECK(control != nullptr);
                if (control == nullptr)
                    return;
                const auto stored = read_back();
                enter(stored);
                OA_CHECK(read_back() == stored);
                OA_CHECK(stored >= 0 && stored <= maximum);
            }
        };
    prefs::Preferences preferences{};
    Recorder recorder;
    auto context = make_context(preferences, recorder);
    check_slider(
        sounds,
        "FXVOL",
        0x40,
        [&] { return static_cast<int32_t>(preferences.fx_volume); },
        [&](int32_t value) {
            preferences.fx_volume = static_cast<uint32_t>(value);
            options_enter_sound(sounds, context);
        }
    );
    check_slider(
        speeds,
        "MAXLINES",
        0x1e,
        [&] { return static_cast<int32_t>(preferences.text_lines); },
        [&](int32_t value) {
            preferences.text_lines = static_cast<uint32_t>(value);
            options_enter_speeds(speeds, context);
        }
    );
    check_slider(
        speeds,
        "TXTSCROL",
        0x28,
        [&] { return static_cast<int32_t>(preferences.text_scroll); },
        [&](int32_t value) {
            preferences.text_scroll = static_cast<uint32_t>(value);
            options_enter_speeds(speeds, context);
        }
    );
    check_slider(
        speeds,
        "SCREEN",
        0x41,
        [&] { return static_cast<int32_t>(preferences.scroll_speed); },
        [&](int32_t value) {
            preferences.scroll_speed = static_cast<uint8_t>(value);
            options_enter_speeds(speeds, context);
        }
    );
    check_slider(
        speeds,
        "GAME",
        0x15,
        [&] { return static_cast<int32_t>(preferences.game_speed); },
        [&](int32_t value) {
            preferences.game_speed = static_cast<uint16_t>(value);
            options_enter_speeds(speeds, context);
        }
    );
    // GAMMA is not re-read on entry; its callback runs when the knob moves.
    check_slider(
        visuals,
        "GAMMA",
        0x14,
        [&] { return static_cast<int32_t>(preferences.gamma); },
        [&](int32_t value) {
            preferences.gamma = static_cast<uint32_t>(value);
            options_enter_visuals(visuals, context, false);
            options_on_gamma_slider(visuals, context);
        }
    );
}

OA_GAME_DATA_TEST(music_panel_binds_volume) {
    Panel panel;
    if (!options_panel(panel, "music.gui"))
        return;
    prefs::Preferences preferences{};
    preferences.music_volume = 32;
    preferences.music_flags = 1;
    preferences.cd_mode = 4;
    Recorder recorder;
    auto context = make_context(preferences, recorder);
    options_enter_music(panel, context);
    auto* music = panel_control(panel, "MUSICVOL");
    OA_CHECK(music != nullptr && music->slider.maximum == 0x40);
    if (music == nullptr)
        return;
    music->slider.knob = 0;
    music->on_change(panel, context);
    OA_CHECK(preferences.music_volume == 0 && recorder.volumes == 1);
    OA_CHECK(panel_control(panel, "MUSIC")->group_value == 1);
}

OA_TEST(cd_transport_follows_music_mode) {
    Panel panel;
    for (const auto* name : {"MUSICVOL", "CDPREV", "CDPLAY", "TRACKMODE", "TRACKTYPE", "NOTRAK"})
        panel_append(
            panel,
            std::string_view(name) == "MUSICVOL" ? ControlType::slider : ControlType::button,
            name
        );
    prefs::Preferences preferences{};
    OptionsContext context;
    context.preferences = &preferences;
    preferences.music_flags = 0;
    preferences.cd_mode = 4;
    options_update_cd_controls(panel, context);
    OA_CHECK(panel_control(panel, "CDPLAY")->grayed == 1);
    OA_CHECK(panel_control(panel, "TRACKTYPE")->grayed == 1);
    OA_CHECK(panel_stage(panel, "TRACKMODE") == 3);
    preferences.music_flags = 1;
    options_update_cd_controls(panel, context);
    OA_CHECK(panel_control(panel, "CDPLAY")->grayed == 0);
    OA_CHECK(panel_control(panel, "TRACKTYPE")->grayed == 0);
    OA_CHECK(panel_stage(panel, "NOTRAK") == 1);
}

// Opening OPTIONS: the lightbar takes the panel below before the tab panel
// loads; options4x only outside a match, the Panel sound only in one.
struct OpenCalls {
    std::vector<std::string> log;
};

OA_TEST(options_open_sets_up_the_lightbar_then_the_panel) {
    prefs::Preferences preferences{};
    Recorder recorder;
    auto context = make_context(preferences, recorder);
    static OpenCalls calls;
    calls = {};
    context.host.draw_current_frame = [](void*) { calls.log.emplace_back("frame"); };
    context.host.copy_top_panel =
        [](void*, int32_t* width, int32_t* height, int32_t* y) -> oa_ref32 {
        calls.log.emplace_back("flip");
        *width = 640;
        *height = 480;
        *y = 12;
        return 3;
    };
    context.host.create_surface =
        [](void*, const char* tag, int32_t width, int32_t height) -> oa_ref32 {
        calls.log.push_back(
            std::string(tag) + " " + std::to_string(width) + "x" + std::to_string(height)
        );
        return 4;
    };
    context.host.load_panel = [](void*, Panel&) { calls.log.emplace_back("panel"); };
    context.host.load_background = [](void*, const char* name) {
        calls.log.push_back(std::string("bg ") + name);
    };
    Panel panel;
    options_open(panel, context);
    OA_CHECK(
        (calls.log ==
         std::vector<std::string>{"frame", "flip", "BKUPSURFACE 300x480", "panel", "bg options4x"})
    );
    OA_CHECK(
        context.lightbar.flip == 3 && context.lightbar.backup == 4 &&
        context.lightbar.last_column == 639 && context.lightbar.last_row == 479 &&
        context.lightbar.panel_y == 12 && context.lightbar.active == 1 &&
        context.lightbar.scroll == 0
    );
    OA_CHECK(recorder.sounds.empty() && panel.dirty);

    calls = {};
    context.in_game = true;
    options_open(panel, context);
    OA_CHECK((calls.log == std::vector<std::string>{"flip", "BKUPSURFACE 300x480", "panel"}));
    OA_CHECK(
        recorder.sounds.size() == 1 && recorder.sounds[0] == "Panel" && context.realtime_panels
    );
}

// The lightbar over the in-game menu's 128x352 panel at y 128: six 21-pixel
// steps fold the picture onto column 127 while the lift grows, the seventh
// passes the last column, stamps LIGHTBAR and turns the picture over, and the
// sweep then gains a column a frame more until it stops at 277 with
// "Options".
OA_TEST(options_lightbar_sweeps_the_in_game_menu) {
    OptionsLightbar lightbar;
    const auto idle = options_lightbar_step(lightbar);
    OA_CHECK(!idle.drawn && !idle.stamp_lightbar && !idle.play_options_sound);
    OA_CHECK(lightbar.scroll == 0 && lightbar.velocity == 0);

    lightbar.active = 1;
    lightbar.last_column = 127;
    lightbar.last_row = 351;
    lightbar.panel_y = 128;
    const std::array<LightbarPoint, 4> source{
        LightbarPoint{1, 1}, LightbarPoint{127, 1}, LightbarPoint{127, 351}, LightbarPoint{1, 351}
    };
    for (int32_t step = 1; step <= 6; ++step) {
        const auto frame = options_lightbar_step(lightbar);
        OA_CHECK(frame.drawn && !frame.stamp_lightbar && !frame.play_options_sound);
        OA_CHECK(lightbar.scroll == 21 * step && lightbar.velocity == 6 * step);
        OA_CHECK(frame.source == source);
        const std::array<LightbarPoint, 4> folded{
            LightbarPoint{21 * step, 128 - 6 * step},
            LightbarPoint{127, 128},
            LightbarPoint{127, 479},
            LightbarPoint{21 * step, 479}
        };
        OA_CHECK(frame.destination == folded);
    }
    const auto turned = options_lightbar_step(lightbar);
    OA_CHECK(turned.stamp_lightbar && !turned.play_options_sound);
    OA_CHECK(lightbar.scroll == 148 && lightbar.velocity == 30);
    const std::array<LightbarPoint, 4> over{
        LightbarPoint{127, 128},
        LightbarPoint{148, 98},
        LightbarPoint{148, 479},
        LightbarPoint{127, 479}
    };
    OA_CHECK(turned.destination == over);
    const std::array<int32_t, 5> scrolls{170, 192, 214, 236, 258};
    const std::array<int32_t, 5> lifts{24, 18, 12, 6, 0};
    for (std::size_t index = 0; index < scrolls.size(); ++index) {
        const auto frame = options_lightbar_step(lightbar);
        OA_CHECK(!frame.stamp_lightbar && !frame.play_options_sound);
        OA_CHECK(lightbar.scroll == scrolls[index] && lightbar.velocity == lifts[index]);
        OA_CHECK(frame.destination[1] == (LightbarPoint{scrolls[index], 128 - lifts[index]}));
    }
    const auto end = options_lightbar_step(lightbar);
    OA_CHECK(end.play_options_sound && !end.stamp_lightbar);
    OA_CHECK(lightbar.scroll == 277 && lightbar.velocity == 0);
    const std::array<LightbarPoint, 4> open{
        LightbarPoint{127, 128},
        LightbarPoint{277, 128},
        LightbarPoint{277, 479},
        LightbarPoint{127, 479}
    };
    OA_CHECK(end.destination == open);
    for (int frame = 0; frame < 3; ++frame) {
        const auto held = options_lightbar_step(lightbar);
        OA_CHECK(held.drawn && !held.play_options_sound && !held.stamp_lightbar);
        OA_CHECK(lightbar.scroll == 277 && lightbar.velocity == 0 && held.destination == open);
    }
}

} // namespace
} // namespace oa::ui::frontend::test
