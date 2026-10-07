// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The automation endpoint's input (input.hpp).
#include "input.hpp"

#include "input_events.hpp"
#include "requests.hpp"

#include "oa/app/frame_coordinates.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace oa::app::automation {
namespace {

/// A modifier key and the modifier bit an event carries while it is down.
struct ModifierKey {
    SDL_Scancode scancode{}; ///< the key
    SDL_Keymod modifier{};   ///< its bit
};

/// The modifier keys, each with its bit.
constexpr ModifierKey kModifierKeys[] = {
    {SDL_SCANCODE_LSHIFT, SDL_KMOD_LSHIFT},
    {SDL_SCANCODE_RSHIFT, SDL_KMOD_RSHIFT},
    {SDL_SCANCODE_LCTRL, SDL_KMOD_LCTRL},
    {SDL_SCANCODE_RCTRL, SDL_KMOD_RCTRL},
    {SDL_SCANCODE_LALT, SDL_KMOD_LALT},
    {SDL_SCANCODE_RALT, SDL_KMOD_RALT},
    {SDL_SCANCODE_LGUI, SDL_KMOD_LGUI},
    {SDL_SCANCODE_RGUI, SDL_KMOD_RGUI},
};

/// A finger's pressure while it touches the screen.
constexpr float kFingerPressure = 1.0F;

/// What the endpoint holds down and where its pointer is: the devices the
/// client drives, kept between its requests.
struct HeldDevices {
    std::array<bool, scancode_limit> keys{};    ///< by scancode
    SDL_MouseButtonFlags buttons{};             ///< SDL_BUTTON_MASK bits
    bool pointer_known{};                       ///< an event has placed the pointer
    float pointer_x{};                          ///< the pointer's column, in window coordinates
    float pointer_y{};                          ///< the pointer's row, in window coordinates
    std::array<bool, max_finger + 1> fingers{}; ///< the fingers on the screen
    std::array<float, max_finger + 1>
        finger_x{}; ///< each finger's column, 0 to 1 across the window
    std::array<float, max_finger + 1> finger_y{}; ///< each finger's row, 0 to 1 down the window

    /// Returns the modifier bits of the modifier keys held.
    ///
    /// @return SDL_Keymod bits
    [[nodiscard]] SDL_Keymod modifiers() const noexcept {
        uint32_t bits = 0;
        for (const ModifierKey& key : kModifierKeys)
            if (keys[key.scancode])
                bits |= key.modifier;
        return static_cast<SDL_Keymod>(bits);
    }

    /// Tells whether anything is held down: a key, a button or a finger.
    ///
    /// @return true when something is
    [[nodiscard]] bool any() const noexcept {
        const auto down = [](bool held) { return held; };
        return buttons != 0 || std::any_of(keys.begin(), keys.end(), down) ||
               std::any_of(fingers.begin(), fingers.end(), down);
    }
};

/// The endpoint's input between frames. The endpoint is one for the process
/// (extension.cpp), and so is this.
struct InputState {
    HeldDevices held;
    /// The frame whose pump pushed the held input request's events; none
    /// while no input request is held.
    std::optional<uint64_t> pushed_frame;
    /// The text the pushed text events point at, kept until a later
    /// frame's pump, by which the game has taken them.
    std::deque<std::string> texts;
    uint64_t texts_frame{}; ///< the frame whose pump pushed them
};

/// Returns the endpoint's input state.
///
/// @return the one state, made on first use
InputState& input_state() {
    static InputState state;
    return state;
}

/// One SDL event an input request's event becomes, and what it holds down.
struct Step {
    SDL_Event event{};
    std::string text;     ///< a text event's characters, which the event points at once pushed
    bool pushed{true};    ///< false for text while the game takes none: it goes nowhere
    bool holds_key{};     ///< the step holds a key down or lets it go
    bool holds_buttons{}; ///< the step changes the buttons held
};

/// Returns SDL's number for a pointer button.
///
/// @param button the button
/// @return SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE or SDL_BUTTON_RIGHT
uint8_t sdl_button(PointerButton button) noexcept {
    switch (button) {
    case PointerButton::left:
        return SDL_BUTTON_LEFT;
    case PointerButton::middle:
        return SDL_BUTTON_MIDDLE;
    case PointerButton::right:
        return SDL_BUTTON_RIGHT;
    }
    return SDL_BUTTON_LEFT;
}

/// Returns the first touch screen SDL knows of.
///
/// @return its id, or 0 when the machine has none
SDL_TouchID touch_screen() {
    int count = 0;
    SDL_TouchID* devices = SDL_GetTouchDevices(&count);
    SDL_TouchID found = 0;
    for (int index = 0; devices != nullptr && index < count && found == 0; ++index)
        if (SDL_GetTouchDeviceType(devices[index]) == SDL_TOUCH_DEVICE_DIRECT)
            found = devices[index];
    SDL_free(devices);
    return found;
}

/// Makes the SDL events of an input request's events, without pushing them.
class StepMaker {
  public:

    /// Starts from what the endpoint holds now.
    ///
    /// @param placement where the window shows the canvas
    /// @param held what the endpoint holds now
    /// @param cursor_x the game's pointer, canvas column, for a pointer not yet placed
    /// @param cursor_y the game's pointer, canvas row
    StepMaker(
        const WindowPlacement& placement,
        const HeldDevices& held,
        int32_t cursor_x,
        int32_t cursor_y
    )
        : placement_(placement), held_(held) {
        if (!held_.pointer_known && canvas_to_window(
                                        placement_,
                                        static_cast<float>(cursor_x),
                                        static_cast<float>(cursor_y),
                                        held_.pointer_x,
                                        held_.pointer_y
                                    ))
            held_.pointer_known = true;
    }

    /// Makes one event's SDL event.
    ///
    /// @param event the request's event
    /// @param touch the touch screen the fingers are on
    /// @param text_taken the game takes typed text now
    /// @return false when its point cannot be placed in the window
    bool make(const InputEvent& event, SDL_TouchID touch, bool text_taken) {
        Step step;
        SDL_Event& made = step.event;
        const uint64_t now = SDL_GetTicksNS();
        switch (event.kind) {
        case InputKind::key_down:
        case InputKind::key_up: {
            const bool down = event.kind == InputKind::key_down;
            const auto scancode = static_cast<SDL_Scancode>(event.scancode);
            const bool repeat = down && held_.keys[event.scancode];
            held_.keys[event.scancode] = down;
            made.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            made.key.timestamp = now;
            made.key.windowID = placement_.window_id;
            made.key.scancode = scancode;
            made.key.key = event.keycode != 0
                               ? static_cast<SDL_Keycode>(event.keycode)
                               : SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
            // A modifier key's own event carries its bit, as the keyboard's do.
            made.key.mod = held_.modifiers();
            made.key.down = down;
            made.key.repeat = repeat;
            step.holds_key = true;
            break;
        }
        case InputKind::text:
            made.type = SDL_EVENT_TEXT_INPUT;
            made.text.timestamp = now;
            made.text.windowID = placement_.window_id;
            step.text = event.text;
            // SDL types text only while a field takes it; a key pressed on
            // a menu types nothing.
            step.pushed = text_taken;
            break;
        case InputKind::pointer_move: {
            const float before_x = held_.pointer_x;
            const float before_y = held_.pointer_y;
            if (!place_pointer(event))
                return false;
            made.type = SDL_EVENT_MOUSE_MOTION;
            made.motion.timestamp = now;
            made.motion.windowID = placement_.window_id;
            made.motion.state = held_.buttons;
            made.motion.x = held_.pointer_x;
            made.motion.y = held_.pointer_y;
            made.motion.xrel = held_.pointer_x - before_x;
            made.motion.yrel = held_.pointer_y - before_y;
            break;
        }
        case InputKind::button_down:
        case InputKind::button_up: {
            if (event.has_point && !place_pointer(event))
                return false;
            const bool down = event.kind == InputKind::button_down;
            const uint8_t button = sdl_button(event.button);
            if (down)
                held_.buttons |= SDL_BUTTON_MASK(button);
            else
                held_.buttons &= ~SDL_BUTTON_MASK(button);
            made.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            made.button.timestamp = now;
            made.button.windowID = placement_.window_id;
            made.button.button = button;
            made.button.down = down;
            made.button.clicks = event.clicks;
            made.button.x = held_.pointer_x;
            made.button.y = held_.pointer_y;
            step.holds_buttons = true;
            break;
        }
        case InputKind::wheel:
            if (event.has_point && !place_pointer(event))
                return false;
            made.type = SDL_EVENT_MOUSE_WHEEL;
            made.wheel.timestamp = now;
            made.wheel.windowID = placement_.window_id;
            made.wheel.x = static_cast<float>(event.wheel_x);
            made.wheel.y = static_cast<float>(event.wheel_y);
            made.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
            made.wheel.mouse_x = held_.pointer_x;
            made.wheel.mouse_y = held_.pointer_y;
            break;
        case InputKind::finger_down:
        case InputKind::finger_move:
        case InputKind::finger_up: {
            float x = 0.0F;
            float y = 0.0F;
            int width = 0;
            int height = 0;
            if (!window_point(event, x, y) || placement_.window == nullptr ||
                !SDL_GetWindowSize(placement_.window, &width, &height) || width <= 0 || height <= 0)
                return false;
            const float across = x / static_cast<float>(width);
            const float down = y / static_cast<float>(height);
            const bool touching = event.kind != InputKind::finger_up;
            made.type = event.kind == InputKind::finger_down ? SDL_EVENT_FINGER_DOWN
                        : event.kind == InputKind::finger_up ? SDL_EVENT_FINGER_UP
                                                             : SDL_EVENT_FINGER_MOTION;
            made.tfinger.timestamp = now;
            made.tfinger.touchID = touch;
            // SDL's finger ids are never 0.
            made.tfinger.fingerID = static_cast<SDL_FingerID>(event.finger) + 1;
            made.tfinger.x = across;
            made.tfinger.y = down;
            if (held_.fingers[event.finger]) {
                made.tfinger.dx = across - held_.finger_x[event.finger];
                made.tfinger.dy = down - held_.finger_y[event.finger];
            }
            made.tfinger.pressure = touching ? kFingerPressure : 0.0F;
            made.tfinger.windowID = placement_.window_id;
            held_.fingers[event.finger] = touching;
            held_.finger_x[event.finger] = across;
            held_.finger_y[event.finger] = down;
            break;
        }
        }
        steps_.push_back(std::move(step));
        return true;
    }

    /// Returns the events made.
    ///
    /// @return the steps, in order
    [[nodiscard]] std::vector<Step>& steps() noexcept { return steps_; }

    /// Returns what the endpoint holds once the events have been taken.
    ///
    /// @return the devices
    [[nodiscard]] const HeldDevices& held() const noexcept { return held_; }

  private:

    /// Finds an event's point in window coordinates.
    ///
    /// @param event the event, with its point
    /// @param[out] x the column
    /// @param[out] y the row
    /// @return false when the point cannot be placed
    bool window_point(const InputEvent& event, float& x, float& y) const {
        if (event.space == PointSpace::window) {
            // The window's own pixels, as hello gives its size.
            x = static_cast<float>(event.x) / placement_.density;
            y = static_cast<float>(event.y) / placement_.density;
            return true;
        }
        return canvas_to_window(
            placement_, static_cast<float>(event.x), static_cast<float>(event.y), x, y
        );
    }

    /// Moves the endpoint's pointer to an event's point.
    ///
    /// @param event the event, with its point
    /// @return false when the point cannot be placed
    bool place_pointer(const InputEvent& event) {
        float x = 0.0F;
        float y = 0.0F;
        if (!window_point(event, x, y))
            return false;
        held_.pointer_x = x;
        held_.pointer_y = y;
        held_.pointer_known = true;
        return true;
    }

    const WindowPlacement& placement_;
    HeldDevices held_;
    std::vector<Step> steps_;
};

/// Pushes made events onto SDL's event queue, holding what each holds down
/// just before it, as SDL's devices change their state before their event
/// is queued.
///
/// @param endpoint the endpoint
/// @param[in,out] state the input state, whose texts keep the text events' characters
/// @param[in,out] steps the events
/// @param held what the endpoint holds once they are taken
/// @return the events SDL's queue took; fewer than were pushed when it is full
size_t push_steps(
    const Endpoint& endpoint, InputState& state, std::vector<Step>& steps, const HeldDevices& held
) {
    const AutomationHost& host = endpoint.automation_host();
    if (!state.texts.empty() && state.texts_frame != endpoint.frame())
        state.texts.clear();
    state.texts_frame = endpoint.frame();
    size_t taken = 0;
    for (Step& step : steps) {
        if (step.holds_key)
            host.hold_key(
                host.context,
                static_cast<uint32_t>(step.event.key.scancode),
                step.event.type == SDL_EVENT_KEY_DOWN
            );
        if (step.holds_buttons) {
            const SDL_MouseButtonFlags mask = SDL_BUTTON_MASK(step.event.button.button);
            const bool down = step.event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            state.held.buttons = down ? state.held.buttons | mask : state.held.buttons & ~mask;
            host.hold_buttons(host.context, state.held.buttons);
        }
        if (!step.pushed) {
            ++taken;
            continue;
        }
        if (step.event.type == SDL_EVENT_TEXT_INPUT)
            step.event.text.text = state.texts.emplace_back(std::move(step.text)).c_str();
        if (SDL_PushEvent(&step.event))
            ++taken;
    }
    state.held = held;
    return taken;
}

/// Lets go of every key, button and finger the endpoint holds, with the
/// events that say so, as a keyboard and mouse unplugged would.
///
/// @param endpoint the endpoint, served
/// @param[in,out] state the input state
void let_go(const Endpoint& endpoint, InputState& state) {
    HeldDevices& held = state.held;
    if (!held.any())
        return;
    const WindowPlacement placement = window_placement(endpoint);
    const AutomationHost& host = endpoint.automation_host();
    const uint64_t now = SDL_GetTicksNS();
    for (size_t scancode = 0; scancode < held.keys.size(); ++scancode) {
        if (!held.keys[scancode])
            continue;
        held.keys[scancode] = false;
        host.hold_key(host.context, static_cast<uint32_t>(scancode), false);
        SDL_Event up{};
        up.type = SDL_EVENT_KEY_UP;
        up.key.timestamp = now;
        up.key.windowID = placement.window_id;
        up.key.scancode = static_cast<SDL_Scancode>(scancode);
        up.key.key = SDL_GetKeyFromScancode(up.key.scancode, SDL_KMOD_NONE, false);
        up.key.mod = held.modifiers();
        (void)SDL_PushEvent(&up);
    }
    for (const uint8_t button : {SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE, SDL_BUTTON_RIGHT}) {
        if ((held.buttons & SDL_BUTTON_MASK(button)) == 0)
            continue;
        held.buttons &= ~SDL_BUTTON_MASK(button);
        host.hold_buttons(host.context, held.buttons);
        SDL_Event up{};
        up.type = SDL_EVENT_MOUSE_BUTTON_UP;
        up.button.timestamp = now;
        up.button.windowID = placement.window_id;
        up.button.button = button;
        up.button.clicks = 1;
        up.button.x = held.pointer_x;
        up.button.y = held.pointer_y;
        (void)SDL_PushEvent(&up);
    }
    std::optional<SDL_TouchID> touch;
    for (size_t finger = 0; finger < held.fingers.size(); ++finger) {
        if (!held.fingers[finger])
            continue;
        held.fingers[finger] = false;
        if (!touch)
            touch = touch_screen();
        if (*touch == 0)
            continue;
        SDL_Event up{};
        up.type = SDL_EVENT_FINGER_UP;
        up.tfinger.timestamp = now;
        up.tfinger.touchID = *touch;
        up.tfinger.fingerID = static_cast<SDL_FingerID>(finger) + 1;
        up.tfinger.x = held.finger_x[finger];
        up.tfinger.y = held.finger_y[finger];
        up.tfinger.windowID = placement.window_id;
        (void)SDL_PushEvent(&up);
    }
}

} // namespace

WindowPlacement window_placement(const Endpoint& endpoint) {
    const CheckHost& host = endpoint.check_host();
    WindowPlacement placement;
    placement.window_id = host.window_id(host.context);
    placement.window =
        placement.window_id != 0 ? SDL_GetWindowFromID(placement.window_id) : nullptr;
    placement.renderer = placement.window != nullptr ? SDL_GetRenderer(placement.window) : nullptr;
    if (placement.window != nullptr) {
        const float density = SDL_GetWindowPixelDensity(placement.window);
        if (density > 0.0F)
            placement.density = density;
    }
    return placement;
}

bool canvas_to_window(
    const WindowPlacement& placement, float x, float y, float& window_x, float& window_y
) {
    if (placement.renderer == nullptr) {
        window_x = x;
        window_y = y;
        return true;
    }
    return frame_to_window(placement.renderer, x, y, &window_x, &window_y);
}

void answer_input(Endpoint& endpoint, const Request& request, Answer& answer) {
    InputError error;
    const std::optional<std::vector<InputEvent>> events =
        decode_input_events(request.fields.find("events"), error);
    if (!events) {
        answer.refuse("bad_request", error.message, error.field);
        return;
    }
    const CheckHost& host = endpoint.check_host();
    if (const Json* expected = request.fields.find("expect_screen");
        expected != nullptr && expected->type() != JsonType::null) {
        if (expected->string() == nullptr) {
            answer.refuse("bad_request", "expect_screen names a screen", "expect_screen");
            return;
        }
        const std::string shown = screen_name(host.screen(host.context));
        if (*expected->string() != shown) {
            answer.refuse(
                "screen_changed", "the screen is " + shown + ", not " + *expected->string()
            );
            return;
        }
    }
    SDL_TouchID touch = 0;
    for (const InputEvent& event : *events)
        if (event.kind == InputKind::finger_down || event.kind == InputKind::finger_move ||
            event.kind == InputKind::finger_up) {
            touch = touch_screen();
            if (touch == 0) {
                answer.refuse("unsupported", "this machine has no touch screen to touch");
                return;
            }
            break;
        }
    const WindowPlacement placement = window_placement(endpoint);
    const bool text_taken = placement.window != nullptr && SDL_TextInputActive(placement.window);
    int32_t cursor_x = 0;
    int32_t cursor_y = 0;
    host.cursor(host.context, &cursor_x, &cursor_y);
    auto& state = input_state();
    StepMaker maker(placement, state.held, cursor_x, cursor_y);
    for (const InputEvent& event : *events) {
        if (!maker.make(event, touch, text_taken)) {
            answer.refuse(
                "internal", std::string("a point cannot be placed in the window: ") + SDL_GetError()
            );
            return;
        }
    }
    const size_t taken = push_steps(endpoint, state, maker.steps(), maker.held());
    if (taken < maker.steps().size()) {
        answer.refuse(
            "internal",
            "SDL's event queue took " + std::to_string(taken) + " of " +
                std::to_string(maker.steps().size()) + " events: " + SDL_GetError()
        );
        return;
    }
    state.pushed_frame = endpoint.frame();
    answer.held = true;
}

void answer_taken_input(Endpoint& endpoint, FrameStage stage) {
    auto& state = input_state();
    if (stage != FrameStage::pump)
        return;
    // By a later frame's pump the main loop's poll has handed the game
    // every event pushed before it.
    if (!state.texts.empty() && endpoint.frame() > state.texts_frame)
        state.texts.clear();
    if (!endpoint.has_client()) {
        state.pushed_frame.reset();
        let_go(endpoint, state);
        state.held.pointer_known = false;
        return;
    }
    const Request* held = endpoint.held_request();
    if (!state.pushed_frame || held == nullptr || held->op != "input" ||
        endpoint.frame() <= *state.pushed_frame)
        return;
    state.pushed_frame.reset();
    Answer answer = endpoint.begin_answer(held->id);
    JsonWriter& json = answer.json;
    json.key("consumed");
    json.begin_object();
    json.key("frame");
    json.integer(static_cast<int64_t>(endpoint.frame()));
    json.key("tick");
    json.integer(endpoint.tick());
    json.end_object();
    endpoint.answer_held(answer);
}

} // namespace oa::app::automation
