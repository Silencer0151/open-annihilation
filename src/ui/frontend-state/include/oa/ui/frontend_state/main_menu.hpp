// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/ui/frontend_state/dispatcher.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace oa::ui::frontend_state {
namespace main_menu {
inline constexpr int32_t destroy_event = -1; // Event.code when the panel is destroyed
inline constexpr uint32_t menu_cursor_animation_index = 20; // cursor animation table index
inline constexpr uint32_t sound_argument = 0;
enum class Button { single_player, multiplayer, intro, exit, credits };
enum class Sound { big_button, small_button, exit };
enum class Disc : uint32_t { campaign = 0, multiplayer = 1 };
enum class Message { multiplayer_disc_required, fullscreen_required, game_disc_required };

/// Returns the gadget name of a main-menu button.
///
/// @param button Button.
/// @return Its MAINMENU.GUI record name.
constexpr std::string_view resource_name(Button button) noexcept {
    switch (button) {
    case Button::single_player:
        return "SINGLE";
    case Button::multiplayer:
        return "MULTI";
    case Button::intro:
        return "INTRO";
    case Button::exit:
        return "EXIT";
    case Button::credits:
        return "Credits";
    }
    return {};
}

/// Returns the ALLSOUND name of a main-menu sound.
///
/// @param sound Sound.
/// @return Its registered sound name.
constexpr std::string_view resource_name(Sound sound) noexcept {
    switch (sound) {
    case Sound::big_button:
        return "BigButton";
    case Sound::small_button:
        return "smlButton";
    case Sound::exit:
        return "exit";
    }
    return {};
}

/// Returns the text of a main-menu message.
///
/// @param message Message.
/// @return The untranslated message text.
constexpr std::string_view message_text(Message message) noexcept {
    switch (message) {
    case Message::multiplayer_disc_required:
        return "Please insert the Multiplayer CD (Disc 1) and try again";
    case Message::fullscreen_required:
        return "Debug:  You must be in full-screen mode to play a movie";
    case Message::game_disc_required:
        return "Please insert a Total Annihilation CD and try again";
    }
    return {};
}

// Portable identities supplied by the host, never interpreted as pointers.
struct MenuHandle {
    uintptr_t value{};
};

struct MessageTarget {
    uintptr_t value{};
};

struct DocumentHandle {
    uintptr_t value{};
};

struct Event {
    MenuHandle menu;
    int32_t code{}; // gadget event code, or destroy_event
};

struct Environment {                // the menu environment: where notices go
    uint8_t messages_enabled{};     // notices are shown only while nonzero
    MessageTarget message_target{}; // window the notices go to; none when 0
};

struct ResourceRequest {
    std::string_view directory = "maps";
    std::string_view name = "multiplay";
    std::string_view extension = "tdf";
};

// The routines the main-menu handler calls.
class Host {
  public:

    virtual ~Host() = default;

    /// Returns the menu environment object.
    ///
    /// References must remain valid for the event: intro/credits keep the
    /// entry object's identity, while a failed multiplayer document load asks
    /// for the object again.
    ///
    /// @return The environment.
    virtual Environment& environment() = 0;

    /// Releases the main-menu spark animation.
    virtual void release_sparks() = 0;

    /// Reports whether a button was activated by the event.
    ///
    /// @param menu Menu the event came from.
    /// @param button Button to test.
    /// @return Nonzero when it was; the whole word is tested.
    virtual uint32_t button_result(MenuHandle menu, Button button) = 0;

    /// Plays a menu sound.
    ///
    /// @param sound Sound to play.
    /// @param argument Second argument, always sound_argument (0).
    virtual void play_sound(Sound sound, uint32_t argument) = 0;

    /// Selects a cursor animation.
    ///
    /// @param index Cursor animation table index.
    virtual void select_cursor_animation(uint32_t index) = 0;

    /// Prepares the multiplayer menus.
    virtual void prepare_multiplayer() = 0;

    /// Resolves a resource path.
    ///
    /// @param request Directory, name and extension (maps/multiplay.tdf by default).
    /// @return The resolved path.
    virtual std::string resolve_resource(ResourceRequest request) = 0;

    /// Constructs a document object.
    ///
    /// @return Its handle.
    virtual DocumentHandle construct_document() = 0;

    /// Loads a document from a path.
    ///
    /// @param document Document to load into.
    /// @param path File path.
    /// @return Nonzero on success; the whole word is tested.
    virtual uint32_t load_document(DocumentHandle document, std::string_view path) = 0;

    /// Destroys a document object.
    ///
    /// @param document Document to destroy.
    virtual void destroy_document(DocumentHandle document) noexcept = 0;

    /// Resets the frontend after the multiplayer selection.
    virtual void reset_after_multiplayer_selection() = 0;

    /// Returns the display's mode flags byte.
    ///
    /// @return The flags; flags::fullscreen_mode is tested.
    virtual uint8_t application_flags() = 0;

    /// Looks for a game disc.
    ///
    /// @param disc Disc wanted.
    /// @return Nonzero in the low byte when found; only that byte is tested.
    virtual uint32_t find_disc(Disc disc) = 0;

    /// Returns the Shift key's state word.
    ///
    /// @return The key state; negative while Shift is held.
    virtual int16_t shift_key_state() = 0;

    /// Drains pending input.
    virtual void drain_input() = 0;

    /// Checks the frontend state checksum.
    virtual void check_frontend_integrity() = 0;

    /// Shows a message.
    ///
    /// @param target Where the message is shown.
    /// @param message Message to show.
    virtual void show_message(MessageTarget target, Message message) = 0;

    /// Runs the default handling of an event no button took.
    ///
    /// @param menu Menu the event came from.
    virtual void default_event(MenuHandle menu) = 0;
};

/// Handles a MAINMENU.GUI event (installed by the main-menu setup).
///
/// Sets the pending signal for SINGLE, MULTI, EXIT and Credits; INTRO
/// restarts the intro follow-up movie state. Hit testing, documents, audio
/// and dialogs stay behind the host.
///
/// @param[in,out] state Dispatcher state.
/// @param event Menu event.
/// @param[in,out] host Routines the handler calls.
void handle_event(State& state, const Event& event, Host& host);
} // namespace main_menu
} // namespace oa::ui::frontend_state
