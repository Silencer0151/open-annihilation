// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The table through which a check an extension runs drives the running game,
// as the engine's own checks drive it: it hands the game input events, runs
// frames and passes of the screen packages, holds the frontend clock,
// composes the frame and reads it back, and reads the screen shown, the
// frontend's state, the gadgets of the shown layout, the game's files and
// the files the packages' sounds play. An extension gets the table with
// check_host() from a hook that is given the runtime, such as run_mode or
// check_multiplayer_menu, and the command line's options with
// runtime_options(). It may keep the table and use it while the runtime
// lives, on the thread that runs main(). The entries that hand the game
// events, run a frame or a pass, compose the frame or write the preferences
// drive the game from a check's own hook; they are not called from a screen
// package's callback, from the frame hook or from inside another entry's
// call, where the game is in the middle of a step of its own.
//
// The header includes no other header of the engine's: an extension that
// includes it links oa::app::headers and needs no more of the engine's. The
// types it names are declared by SDL (SDL_Event), the asset store
// (oa/formats/hpi.hpp), the frontend renderer (oa/ui/frontend_renderer.hpp)
// and oa/app/app.hpp (Options), which an extension that reads them includes
// itself.
#pragma once

#include <cstddef>
#include <cstdint>

union SDL_Event;

namespace oa {
class AssetStore;
}

namespace oa::ui::frontend_renderer {
struct Surface;
}

namespace oa::app {

class Runtime;
struct Options;

// The screen registry's screen id (oa/ui/screen_registry.hpp), declared
// again so that this header includes no other module's; the two must name
// the same type.
using ScreenId = uint16_t;

// What a check may do to the running game. check_host() sets every entry,
// and context, which every entry is passed back, is the runtime.
struct CheckHost {
    void* context{};
    /// Hands one SDL event to the game, as its main loop hands it each
    /// event it polls: the window's focus, then the overlays and the shown
    /// screen's package, else the game's own handling, then a screen change
    /// asked for. Runs no frame. Works with or without a window.
    ///
    /// @param context CheckHost::context
    /// @param[in,out] event the event; the game may change it as it handles it
    /// @return false when the event ended the run, as closing the game does;
    ///         true otherwise
    bool (*dispatch)(void* context, SDL_Event* event){};
    /// Hands the game a left-button pointer event at a point of the 640x480
    /// canvas, as dispatch does. With a window the point is placed in the
    /// window as the game shows the canvas there, and the event names the
    /// window; without one the canvas point is the event's point. Throws
    /// std::runtime_error for another type of event, or when the point
    /// cannot be placed in the window.
    ///
    /// @param context CheckHost::context
    /// @param type SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN or
    ///        SDL_EVENT_MOUSE_BUTTON_UP
    /// @param x canvas column
    /// @param y canvas row
    /// @param clicks the presses in a row a press or release counts, as SDL
    ///        counts them (1 for a single click); a motion ignores it
    /// @return as dispatch
    bool (*pointer)(void* context, uint32_t type, int32_t x, int32_t y, uint8_t clicks){};
    /// Reads where the game draws its cursor: the pointer's last position,
    /// as ScreenServices::pointer_position gives it.
    ///
    /// @param context CheckHost::context
    /// @param[out] x canvas column
    /// @param[out] y canvas row
    void (*cursor)(void* context, int32_t* x, int32_t* y){};
    /// Returns the game's window, for the events a check makes and for SDL's
    /// questions about the window (SDL_GetWindowFromID, then for example
    /// SDL_TextInputActive).
    ///
    /// @param context CheckHost::context
    /// @return the window's SDL id; 0 in a run without a window (headless)
    uint32_t (*window_id)(void* context){};
    /// Runs one frame of the main loop, as it runs one after handing the
    /// game the events it polled: the overlays and the shown screen's
    /// package tick, the music steps, the extensions' frame hooks and a
    /// running match run, and the frame is composed and shown in the
    /// window. Needs the window: throws std::runtime_error in a run without
    /// one.
    ///
    /// @param context CheckHost::context
    void (*frame)(void* context){};
    /// Runs one pass of the screen packages and composes the frame without
    /// showing it: the overlays and the shown screen's package tick, a
    /// screen change asked for follows, and the frame is composed as
    /// compose composes it. Works with or without a window.
    ///
    /// @param context CheckHost::context
    void (*package_pass)(void* context){};
    /// Composes the frame of the screen shown, packages and cursor included,
    /// without ticking anything or showing it.
    ///
    /// @param context CheckHost::context
    void (*compose)(void* context){};
    /// Returns the frame last composed, 640x480 RGB on the frontend's
    /// screens.
    ///
    /// @param context CheckHost::context
    /// @return the frame; the pointer stays valid while the runtime lives, and
    ///         what it points at changes with each frame composed
    const oa::ui::frontend_renderer::Surface* (*surface)(void* context){};
    /// Holds the frontend clock (ScreenServices::current_tick) at a tick
    /// until it is released or held at another.
    ///
    /// @param context CheckHost::context
    /// @param tick the clock's value, in game ticks (30 per second)
    void (*hold_clock)(void* context, uint32_t tick){};
    /// Lets the frontend clock follow real time again.
    ///
    /// @param context CheckHost::context
    void (*release_clock)(void* context){};
    /// Returns the frontend clock, held or not.
    ///
    /// @param context CheckHost::context
    /// @return the clock in game ticks (30 per second)
    uint32_t (*clock)(void* context){};
    /// Returns the screen shown.
    ///
    /// @param context CheckHost::context
    /// @return its id: a built-in screen's is oa::app::screen_id of its
    ///         Screen (app.hpp), a package screen's the id it registered
    ScreenId (*screen)(void* context){};
    /// Returns the frontend dispatcher's state.
    ///
    /// @param context CheckHost::context
    /// @return its state byte, a frontend_state::state_id value
    ///         (oa/ui/frontend_state/dispatcher.hpp)
    uint8_t (*frontend_state)(void* context){};
    /// Tells whether a screen package owns the main menu's frame and input:
    /// the main menu is shown and the frontend is in state_id::pump_only, in
    /// which it only takes input and shows frames.
    ///
    /// @param context CheckHost::context
    /// @return true while a package owns them
    bool (*frame_owned_by_package)(void* context){};
    /// Finds a gadget of the layout shown (the main menu's MAINMENU.GUI, or
    /// the frontend screen's panel), by its name compared ignoring ASCII
    /// case.
    ///
    /// @param context CheckHost::context
    /// @param name the gadget's name
    /// @param[out] x its left column on the canvas; left as it is when none
    ///        is found
    /// @param[out] y its top row on the canvas; likewise
    /// @param[out] width its width in pixels; likewise
    /// @param[out] height its height in pixels; likewise
    /// @return true when the layout holds such a gadget
    bool (*gadget)(
        void* context, const char* name, int32_t* x, int32_t* y, int32_t* width, int32_t* height
    ){};
    /// Returns the game's files: the loose files of the installation, then
    /// its archives, in the game's lookup order.
    ///
    /// @param context CheckHost::context
    /// @return the store; valid while the runtime lives
    const oa::AssetStore* (*assets)(void* context){};
    /// Finds the file a sound name plays, as ScreenServices::play_sound
    /// finds it: the file registered under the name (allsound.tdf or
    /// ScreenServices::register_sound), else sounds/<name>.wav.
    ///
    /// @param context CheckHost::context
    /// @param name the sound name
    /// @param[out] out receives the file's path in the store, ending in a
    ///        zero byte
    /// @param size the bytes `out` holds
    /// @return false, with `out` left as it is, when the path and its zero
    ///         byte do not fit or `name` is null
    bool (*sound_resource)(void* context, const char* name, char* out, std::size_t size){};
    /// Writes the preferences file now, as the game writes it when it ends.
    /// A run_mode run that ends the game writes it only through this.
    ///
    /// @param context CheckHost::context
    void (*write_preferences)(void* context){};
};

/// Returns the table through which a check drives a running game.
///
/// @param[in,out] runtime the running game, which the table's entries change
/// @return the table, every entry set and bound to `runtime`
[[nodiscard]] CheckHost check_host(Runtime& runtime);

/// Returns the options the game's command line gave: the game folder,
/// --snapshot, --preferences-file and the rest.
///
/// @param runtime the running game
/// @return its options; valid while the runtime lives
[[nodiscard]] const Options& runtime_options(const Runtime& runtime);

} // namespace oa::app
