// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Registration tables for frontend screens, overlays and dispatcher steps.
//
// Packages describe their screens with plain descriptor structs and register
// them from one function listed in screens.inc, or from the extension's
// register_screens. The application owns the registry and hands packages a
// ScreenContext; packages never see Runtime.
#pragma once

#include <cstddef>
#include <cstdint>

namespace oa {
class AssetStore;
}

namespace oa::ui::frontend_renderer {
struct Surface;
}

namespace oa::sim::match_runtime {
class Match;
}

namespace oa::ui::frontend_state {
enum class Step : uint32_t;
}

namespace oa::app {

// Built-in screens use the values of app::Screen; package screens take ids
// from kFirstPackageScreen upward. Duplicate ids are rejected at startup.
using ScreenId = uint16_t;
constexpr ScreenId kFirstPackageScreen = 0x100;
constexpr ScreenId kScreenAny = 0xffff;

constexpr uint32_t kMaxScreens = 64;
constexpr uint32_t kMaxOverlays = 32;
constexpr uint32_t kMaxSteps = 64;

enum class ScreenInputKind : uint8_t {
    key_down,
    key_up,
    text,
    pointer_move,
    pointer_down,
    pointer_up,
    wheel
};

// One platform input event. Pointer coordinates are in render space (the
// 640x480 canvas on frontend screens).
struct ScreenInput {
    ScreenInputKind kind;
    uint8_t button;     // pointer button, 1 = left
    uint8_t clicks;     // pointer_down: presses in a row (SDL's click count)
    uint16_t modifiers; // SDL_Keymod bits
    uint32_t key;       // SDL_Keycode for key events
    float x;
    float y;
    float wheel_y;
    const char* text; // UTF-8 for text events, otherwise null
};

// Application services available to packages. `host` is opaque.
struct ScreenServices {
    void (*request_screen)(void* host, ScreenId id);
    // Plays the sound registered under `name` (allsound.tdf or
    // register_sound), else the WAV sounds/<name>.wav.
    void (*play_sound)(void* host, const char* name);
    int (*read_number)(void* host, const char* section, const char* key, uint32_t* value);
    void (*write_number)(void* host, const char* section, const char* key, uint32_t value);
    int (*read_string)(
        void* host, const char* section, const char* key, char* out, std::size_t capacity
    );
    void (*write_string)(void* host, const char* section, const char* key, const char* value);
    void (*set_status)(void* host, const char* text);
    // Writes both frontend signal bytes (the current and the pending signal).
    void (*set_frontend_signal)(void* host, uint8_t signal);
    // Writes the frontend dispatcher's state byte.
    void (*set_frontend_state)(void* host, uint8_t state);
    // Takes the main-menu panel (MAINMENU.GUI) off the screen.
    void (*close_main_menu_panel)(void* host);
    // Loads MAINMENU.GUI again and runs its setup, sparks included.
    void (*reload_main_menu_panel)(void* host);
    // Starts (nonzero) or stops platform text input; typed characters then
    // arrive as text events.
    void (*set_text_input)(void* host, int enabled);
    // The frontend clock in game ticks, 30 per second.
    uint32_t (*current_tick)(void* host);
    // Registers the sounds/ WAV `file` under the sound name `category`
    // unless the name is taken; play_sound then plays it by that name.
    void (*register_sound)(void* host, const char* category, const char* file);
    // The pointer in frame pixels, where the software cursor is drawn.
    void (*pointer_position)(void* host, int32_t* x, int32_t* y);
    // Reports (nonzero) that an overlay with buttons of its own over
    // MAINMENU.GUI's stands on the main menu; the main menu then takes the
    // layout made for that overlay instead of TA's own.
    void (*set_main_menu_overlay)(void* host, int present);
};

struct ScreenContext {
    void* host;
    const ScreenServices* services;
    oa::AssetStore* assets;
    oa::ui::frontend_renderer::Surface* surface; // current frame
    oa::sim::match_runtime::Match* world;        // null outside a match
    const ScreenInput* input;                    // set only during event callbacks
    ScreenId screen;                             // screen currently shown
};

using ScreenFn = void (*)(ScreenContext* ctx, void* state);
using ScreenEventFn = int (*)(ScreenContext* ctx, void* state); // nonzero = consumed
using ScreenBackgroundFn = const char* (*)(ScreenContext* ctx, void* state);

// GUI resources loaded before `enter`. A null layout means `enter` supplies
// its own resources.
struct ScreenAssets {
    const char* layout;
    const char* background; // named background (bitmaps\<name>.pcx); null for none
    const char* palette;
    const char* sprites;
    const char* shared_sprites;
};

struct ScreenDesc {
    ScreenId id;
    const char* name;
    ScreenAssets assets;
    ScreenBackgroundFn background; // optional; overrides assets.background
    ScreenFn enter;                // after resources load
    ScreenFn leave;                // before the next screen loads
    ScreenEventFn event;           // after overlays, before built-in handling
    ScreenFn tick;                 // once per frame
    ScreenFn draw;                 // after the GUI is composed, before the cursor
    void* state;
};

// Drawn over every screen matching `screen` (or kScreenAny) in ascending z;
// input reaches overlays in descending z, before the screen itself.
struct OverlayDesc {
    const char* name;
    ScreenId screen;
    int16_t z;
    ScreenFn create; // once, after all registrations
    ScreenEventFn event;
    ScreenFn tick;
    ScreenFn draw;
    void* state;
};

struct StepDesc {
    oa::ui::frontend_state::Step step;
    ScreenFn run;
    void* state;
};

struct ScreenRegistry {
    ScreenDesc screens[kMaxScreens];
    uint32_t screen_count;
    OverlayDesc overlays[kMaxOverlays]; // kept sorted by z
    uint32_t overlay_count;
    StepDesc steps[kMaxSteps];
    uint32_t step_count;
    const char* rejected; // name of the first rejected registration
};

/// Adds a screen to the registry.
///
/// Returns false and records the name in `rejected` for kScreenAny, a
/// duplicate id or a full table.
///
/// @param[in,out] registry registry to add to
/// @param desc screen description, copied
/// @return true when registered
bool screen_register(ScreenRegistry* registry, const ScreenDesc* desc);

/// Adds an overlay to the registry, keeping the overlays sorted by z.
///
/// An overlay goes after those of equal z. Returns false and records the name
/// in `rejected` when the table is full.
///
/// @param[in,out] registry registry to add to
/// @param desc overlay description, copied
/// @return true when registered
bool overlay_register(ScreenRegistry* registry, const OverlayDesc* desc);

/// Binds a handler to a dispatcher step.
///
/// Returns false and records "dispatcher step" in `rejected` for a null
/// handler, a step already bound or a full table.
///
/// @param[in,out] registry registry to add to
/// @param step dispatcher step
/// @param run handler
/// @param state handler state passed back to `run`
/// @return true when registered
bool step_register(
    ScreenRegistry* registry, oa::ui::frontend_state::Step step, ScreenFn run, void* state
);

/// Finds a registered screen.
///
/// @param registry registry to search
/// @param id screen id
/// @return the screen's description, or null when none has the id
[[nodiscard]] const ScreenDesc* screen_find(const ScreenRegistry* registry, ScreenId id);

/// Finds the handler bound to a dispatcher step.
///
/// @param registry registry to search
/// @param step dispatcher step
/// @return the binding, or null when the step has none
[[nodiscard]] const StepDesc*
step_find(const ScreenRegistry* registry, oa::ui::frontend_state::Step step);

/// Asks the host to show a screen.
///
/// @param ctx package context
/// @param id screen to show
inline void screen_request(ScreenContext* ctx, ScreenId id) {
    ctx->services->request_screen(ctx->host, id);
}

/// Plays a named interface sound through the host.
///
/// @param ctx package context
/// @param name sound name
inline void screen_play_sound(ScreenContext* ctx, const char* name) {
    ctx->services->play_sound(ctx->host, name);
}

/// Sets the host's status line.
///
/// @param ctx package context
/// @param text status text
inline void screen_status(ScreenContext* ctx, const char* text) {
    ctx->services->set_status(ctx->host, text);
}

// Declares every package registration function listed in screens.inc.
#define OA_REGISTER(fn) void fn(ScreenRegistry* registry);
#include "oa/ui/screen_registry/screens.inc"
#undef OA_REGISTER

} // namespace oa::app
