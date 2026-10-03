// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/unit_info.hpp"
#include "oa/data/defs/layout.hpp"

#include "oa/ui/hud/chat_panel.hpp"

#include "oa/data/languages/unit_texts.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace oa::ui::hud {
namespace {

constexpr float kFixedToFloat = 1.0F / 65536.0F;
/// Pixels per tick to the "m/s" the panel prints.
constexpr double kMetresPerPixel = 0.4;
/// Binary angle units to degrees.
constexpr double kDegreesPerAngle = 360.0 / 65536.0;
constexpr int32_t kUnitInfoPanelFlags = 0x1000;
constexpr int16_t kLabelColumn = 0x8c;
constexpr int16_t kHeadingColumn = 0x82;
constexpr int16_t kValueColumn = 0xf0;
constexpr int16_t kFirstRow = 0x20;
constexpr int16_t kRowHeight = 0xf;
constexpr size_t kNameBytes = 0x80;

const char* localized(Localize localize, void* user, const char* text) {
    const char* out = localize != nullptr ? localize(user, text) : nullptr;
    return out != nullptr ? out : text;
}

/// Appends one NUL-terminated line; lines that do not fit are dropped.
char* append(char* at, const char* end, const char* line) {
    const size_t length = std::strlen(line);
    if (at + length + 1 >= end)
        return at;
    std::memcpy(at, line, length + 1);
    return at + length + 1;
}

bool same_name(const char* a, const char* b) noexcept {
    for (;; ++a, ++b) {
        const auto ca = std::tolower(static_cast<unsigned char>(*a));
        if (ca != std::tolower(static_cast<unsigned char>(*b)))
            return false;
        if (ca == 0)
            return true;
    }
}

} // namespace

void format_unit_properties(
    char (&out)[kPropertyListBytes],
    const UnitDef& def,
    int32_t ticks_per_second,
    Localize localize,
    void* user
) {
    std::memset(out, 0, sizeof out);
    const char* end = out + sizeof out;
    char* at = out;
    char line[64];
    at = append(at, end, "\n");
    std::snprintf(line, sizeof line, "%d", static_cast<int>(def.build_cost_energy));
    at = append(at, end, line);
    std::snprintf(line, sizeof line, "%d", static_cast<int>(def.build_cost_metal));
    at = append(at, end, line);
    std::snprintf(line, sizeof line, "%d", def.build_time);
    at = append(at, end, line);
    at = append(at, end, "\n");
    if (def.bm_code == 0) {
        for (int i = 0; i < 3; ++i) {
            std::snprintf(line, sizeof line, "%s", localized(localize, user, "N/A"));
            at = append(at, end, line);
        }
        return;
    }
    const double rate = ticks_per_second;
    const float speed = static_cast<float>(def.max_velocity) * kFixedToFloat;
    std::snprintf(
        line,
        sizeof line,
        "%.1f %s ",
        rate * speed * kMetresPerPixel,
        localized(localize, user, "m/s")
    );
    at = append(at, end, line);
    const float acceleration = static_cast<float>(def.acceleration) * kFixedToFloat;
    std::snprintf(
        line,
        sizeof line,
        "%.2f %s",
        rate * acceleration * kMetresPerPixel,
        localized(localize, user, "m/s/s")
    );
    at = append(at, end, line);
    const auto turn = static_cast<uint16_t>(def.turn_rate);
    std::snprintf(
        line,
        sizeof line,
        "%.0f %s",
        rate * turn * kDegreesPerAngle,
        localized(localize, user, "deg/s")
    );
    append(at, end, line);
}

uint16_t unit_info_subject(
    World& world,
    const char* button_name,
    uint16_t (*type_for_name)(void* user, const char* name),
    bool (*can_see)(void* user, const Player& viewer, const Unit& unit),
    void* user
) {
    const Game& game = world.game;
    if (button_name != nullptr) {
        char name[kButtonUnitNameBytes + 1]{};
        std::snprintf(name, sizeof name, "%s", button_name);
        return type_for_name != nullptr ? type_for_name(user, name) : 0;
    }
    if (game.cursor_unit_id == 0)
        return 0;
    const Unit* unit = world_unit_at(&world, game.cursor_unit_id);
    const Player* viewer = world_player(&world, game.viewpoint_player);
    if (unit == nullptr || viewer == nullptr || can_see == nullptr ||
        !can_see(user, *viewer, *unit))
        return 0;
    return unit->type_index;
}

bool open_unit_info_panel(
    World& world,
    uint16_t type,
    int32_t ticks_per_second,
    const PanelLoader& loader,
    const PanelControls& controls,
    const UnitInfoHost& host
) {
    if ((world.game.frame_flags & kFrameUnitInfoOpen) != 0 || type == 0 ||
        type >= world.unit_def_count)
        return false;
    const UnitDef& def = world.unit_defs[type];
    if (loader.load == nullptr ||
        !loader.load(loader.user, "UNITINFOx.GUI", nullptr, kUnitInfoPanelFlags))
        return false;
    char path[256];
    std::snprintf(
        path,
        sizeof path,
        "%s\\%.32s.PCX",
        oa::data::defs::directory_name(oa::data::defs::DataDirectory::unitpics),
        def.unit_name
    );
    if (host.set_picture != nullptr)
        host.set_picture(host.user, path);
    char properties[kPropertyListBytes];
    format_unit_properties(properties, def, ticks_per_second, host.localize, host.user);

    struct Label {
        const char* text;
        int16_t x, y;
    };

    static constexpr Label labels[] = {
        {"Cost", kHeadingColumn, 0x20},
        {"Energy", kLabelColumn, 0x2f},
        {"Metal", kLabelColumn, 0x3e},
        {"Build Time", kLabelColumn, 0x4d},
        {"Statistics", kHeadingColumn, 0x5c},
        {"Max Velocity", kLabelColumn, 0x6b},
        {"Acceleration", kLabelColumn, 0x7a},
        {"Turn Rate", kLabelColumn, 0x89},
    };
    if (host.add_label != nullptr) {
        for (const auto& label : labels)
            host.add_label(
                host.user,
                localized(host.localize, host.user, label.text),
                label.x,
                label.y,
                kUnitInfoLabelAttributes
            );
        int16_t y = kFirstRow;
        for (const char* line = properties; *line != '\0'; line += std::strlen(line) + 1) {
            host.add_label(host.user, line, kValueColumn, y, kUnitInfoLabelAttributes);
            y = static_cast<int16_t>(y + kRowHeight);
        }
    }
    const auto name = find_control(controls, "NAME");
    if (name != -1 && controls.set_text != nullptr) {
        // The type's name in the player's language.
        const std::string_view shown = oa::data::languages::unit_display_name(def);
        char text[kNameBytes + 1]{};
        std::memcpy(text, shown.data(), std::min(shown.size(), kNameBytes));
        controls.set_text(controls.user, name, text);
    }
    return true;
}

UnitInfoClick unit_info_panel_click(
    Game& game, const char* name, const UnitInfoHost& host, const HudEvents& events
) {
    if (name == nullptr) {
        if (host.free_picture != nullptr)
            host.free_picture(host.user);
        game.frame_flags = static_cast<uint16_t>(game.frame_flags & ~kFrameUnitInfoOpen);
        return UnitInfoClick::closed;
    }
    if (same_name(name, "DONE")) {
        play_sound(events, "smlbutton");
        return UnitInfoClick::done;
    }
    return UnitInfoClick::clear_selection;
}

} // namespace oa::ui::hud
