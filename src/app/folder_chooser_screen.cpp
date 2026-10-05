// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The in-engine game folder chooser's loop, its listing of folders and its
// painting (folder_chooser_screen.hpp): events from fingers, the pointer,
// the wheel, the keys and gamepads mapped to the chooser's presses and keys
// (src/ui/folder-chooser), the folders found and the browsed folder checked
// with the engine's own inspection, the layout painted in the Game files
// screen's look and presented on the game's window.
#include "folder_chooser_screen.hpp"

#include "game_files_paint.hpp"
#include "game_files_screen.hpp"
#include "render_host.hpp"

#include "oa/app/game_files_hooks.hpp"
#include "oa/app/game_files_import.hpp"
#include "oa/platform/game_installs.hpp"
#include "oa/platform/text_font.hpp"
#include "oa/ui/folder_chooser.hpp"
#include "oa/ui/game_files.hpp"
#include "oa/ui/pad_controls.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace oa::app {

namespace {

namespace chooser = oa::ui::folder_chooser;
namespace view = oa::ui::game_files;
namespace installs = oa::platform::game_installs;
namespace text_font = oa::platform::text_font;

/// How long the loop waits for an event while a held direction or a stick needs its timer,
/// or while the renderer's start-up frames are counted, in milliseconds.
constexpr int32_t frame_wait_ms = 16;
/// How far a press may move over the rows before it scrolls them, in points.
constexpr float drag_threshold_points = 10.0F;
/// How far one notch of the wheel scrolls the rows, in points.
constexpr float wheel_notch_points = 40.0F;
/// The most entries of a folder the browser reads.
constexpr std::size_t browse_listing_limit = 8192;
/// The most folders the browser lists.
constexpr std::size_t most_browse_entries = 1000;
/// The most folders the browser looks into for the game's first archive, to mark them.
constexpr std::size_t most_marked_entries = 256;
/// The largest value of a gamepad's stick axis.
constexpr float stick_axis_range = 32767.0F;
/// The share of the left stick's travel past which it moves the focus.
constexpr float stick_on_share = 0.5F;
/// The share of the left stick's travel under which a held direction ends.
constexpr float stick_off_share = 0.3F;
/// The share of the right stick's travel it rests within without scrolling.
constexpr float scroll_dead_share = 0.2F;
/// How fast the right stick pushed fully scrolls the rows, in points a second.
constexpr float stick_scroll_points_per_second = 1200.0F;
/// Milliseconds in a second.
constexpr float ms_per_second = 1000.0F;
/// The folder under the home folder that the 1997 demo's installer is usually downloaded to.
constexpr std::string_view downloads_folder_name = "Downloads";

/// A direction the focus moves in.
enum class Direction : uint8_t {
    none,  ///< no direction
    up,    ///< up
    down,  ///< down
    left,  ///< left
    right, ///< right
};

/// What holds a direction down.
enum class HoldSource : uint8_t {
    dpad,  ///< the D-pad
    stick, ///< the left stick
};

/// Returns the chooser's key for a direction.
///
/// @param direction the direction, not none
/// @return the key
chooser::Key key_of(Direction direction) noexcept {
    switch (direction) {
    case Direction::up:
        return chooser::Key::up;
    case Direction::down:
        return chooser::Key::down;
    case Direction::left:
        return chooser::Key::left;
    case Direction::right:
    case Direction::none:
        break;
    }
    return chooser::Key::right;
}

/// Returns the direction a D-pad button stands for.
///
/// @param button the button
/// @return the direction; none for another button
Direction direction_of(uint8_t button) noexcept {
    switch (static_cast<SDL_GamepadButton>(button)) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return Direction::up;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return Direction::down;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return Direction::left;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return Direction::right;
    default:
        break;
    }
    return Direction::none;
}

/// Returns a word's ASCII letters lowered, for sorting names as people read them.
///
/// @param name the name
/// @return the name in lower case
std::string folded(std::string_view name) {
    std::string lowered(name);
    for (char& character : lowered)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    return lowered;
}

/// Returns the words a found folder's check gives when it can be played: the parts its
/// archives hold, as the Game files screen says them, or the 1997 demo.
///
/// @param directory the resolved folder
/// @return "Total Annihilation 3.1c with Core Contingency." and the like
std::string verdict_of(const GameDirectory& directory) {
    std::array<bool, 11> parts{};
    const bool demo = directory.demo.outcome == DemoOutcome::ready;
    for (const fs::path& archive : directory.archives) {
        const auto part = game_files::part_of(path_to_utf8(archive.filename()), 0, true, {});
        view::PartKind kind = view::PartKind::game_archives;
        switch (part) {
        case game_files::Part::game_archives:
            kind = view::PartKind::game_archives;
            break;
        case game_files::Part::update_31c:
            kind = view::PartKind::update_31c;
            break;
        case game_files::Part::core_contingency:
            kind = view::PartKind::core_contingency;
            break;
        case game_files::Part::battle_tactics:
            kind = view::PartKind::battle_tactics;
            break;
        case game_files::Part::extra:
            kind = view::PartKind::extra;
            break;
        case game_files::Part::music:
        case game_files::Part::movies:
        case game_files::Part::mods:
        case game_files::Part::other:
        case game_files::Part::demo:
            continue;
        }
        parts[static_cast<std::size_t>(kind)] = true;
    }
    return view::ready_text(parts, demo);
}

/// Tells whether nobody watches a run: --unattended and the scripted runs, a headless check,
/// CI, or a dummy or offscreen video driver.
///
/// @param options the parsed command line
/// @return true when nobody can answer the chooser
bool nobody_watches(const Options& options) {
    const char* ci = SDL_getenv("CI");
    const char* driver = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
    return options.unattended || options.headless_check ||
           unattended_environment(
               ci == nullptr ? std::string_view{} : std::string_view(ci),
               driver == nullptr ? std::string_view{} : std::string_view(driver)
           );
}

/// The renderer's state the chooser changes, kept to put back at the end.
class RendererState {
  public:

    /// Keeps the renderer's target, logical presentation, scale, viewport, clip, colour and
    /// blend mode.
    ///
    /// @param renderer the renderer
    explicit RendererState(SDL_Renderer* renderer) noexcept : renderer_(renderer) {
        target_ = SDL_GetRenderTarget(renderer_);
        SDL_GetRenderLogicalPresentation(
            renderer_, &logical_width_, &logical_height_, &logical_mode_
        );
        SDL_GetRenderScale(renderer_, &scale_x_, &scale_y_);
        viewport_set_ = SDL_RenderViewportSet(renderer_);
        SDL_GetRenderViewport(renderer_, &viewport_);
        clip_set_ = SDL_RenderClipEnabled(renderer_);
        SDL_GetRenderClipRect(renderer_, &clip_);
        SDL_GetRenderDrawColor(renderer_, &colour_[0], &colour_[1], &colour_[2], &colour_[3]);
        SDL_GetRenderDrawBlendMode(renderer_, &blend_);
    }

    /// Puts everything back as it was.
    ~RendererState() {
        SDL_SetRenderTarget(renderer_, target_);
        SDL_SetRenderLogicalPresentation(renderer_, logical_width_, logical_height_, logical_mode_);
        SDL_SetRenderScale(renderer_, scale_x_, scale_y_);
        SDL_SetRenderViewport(renderer_, viewport_set_ ? &viewport_ : nullptr);
        SDL_SetRenderClipRect(renderer_, clip_set_ ? &clip_ : nullptr);
        SDL_SetRenderDrawColor(renderer_, colour_[0], colour_[1], colour_[2], colour_[3]);
        SDL_SetRenderDrawBlendMode(renderer_, blend_);
    }

    RendererState(const RendererState&) = delete;
    RendererState& operator=(const RendererState&) = delete;

    /// Draws to the window directly, in its own pixels.
    void use_window_pixels() const noexcept {
        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        SDL_SetRenderScale(renderer_, 1.0F, 1.0F);
        SDL_SetRenderViewport(renderer_, nullptr);
        SDL_SetRenderClipRect(renderer_, nullptr);
    }

  private:

    SDL_Renderer* renderer_{}; ///< the renderer
    SDL_Texture* target_{};    ///< its target
    int logical_width_{};      ///< logical width
    int logical_height_{};     ///< logical height
    SDL_RendererLogicalPresentation logical_mode_{SDL_LOGICAL_PRESENTATION_DISABLED}; ///< its mode
    float scale_x_{1.0F};                      ///< scale across
    float scale_y_{1.0F};                      ///< scale down
    bool viewport_set_{};                      ///< a viewport was set
    SDL_Rect viewport_{};                      ///< the viewport
    bool clip_set_{};                          ///< a clip was set
    SDL_Rect clip_{};                          ///< the clip
    std::array<uint8_t, 4> colour_{};          ///< the draw colour
    SDL_BlendMode blend_{SDL_BLENDMODE_BLEND}; ///< the blend mode
};

/// The chooser on the game's window: its loop, its model and what its commands do.
class ChooserScreen {
  public:

    /// Makes the chooser for the folders resolution found.
    ///
    /// @param options the parsed command line
    /// @param host what made the renderer, whose start-up frames are counted; may be null
    /// @param window the game's window
    /// @param renderer its renderer
    /// @param needed why there is no folder, and the folders found
    ChooserScreen(
        const Options& options,
        RendererHost* host,
        SDL_Window* window,
        SDL_Renderer* renderer,
        const GameFilesNeeded& needed
    )
        : options_(options), host_(host), window_(window), renderer_(renderer), needed_(needed),
          render_state_(renderer) {}

    /// Closes the gamepads it opened and lets go of its texture.
    ~ChooserScreen() {
        for (const OpenPad& pad : pads_)
            SDL_CloseGamepad(pad.pad);
        if (texture_ != nullptr)
            SDL_DestroyTexture(texture_);
    }

    ChooserScreen(const ChooserScreen&) = delete;
    ChooserScreen& operator=(const ChooserScreen&) = delete;

    /// Runs the chooser until a folder is chosen, the player quits or the window closes.
    ///
    /// @param[out] game_directory the folder chosen
    /// @return false when the player quit or the window closed
    bool run(std::optional<GameDirectory>& game_directory) {
        start();
        while (!done_)
            pass();
        if (chosen_)
            game_directory = std::move(chosen_);
        return kept_;
    }

  private:

    /// A gamepad the chooser opened.
    struct OpenPad {
        SDL_JoystickID id{}; ///< its instance
        SDL_Gamepad* pad{};  ///< its handle
    };

    /// A press of a finger or the pointer's button under way.
    struct Press {
        bool active{};       ///< a press is under way
        bool finger{};       ///< it is a finger's
        SDL_TouchID touch{}; ///< the finger's device
        SDL_FingerID id{};   ///< the finger
        float start_x{};     ///< where it went down, canvas pixels
        float start_y{};     ///< where it went down, canvas pixels
        float last_y{};      ///< where it was last, canvas pixels
        bool scrolling{};    ///< it scrolls the rows
    };

    /// Opens the fonts, the gamepads already there, the list and the first frame.
    void start() {
        render_state_.use_window_pixels();
        install_game_files_language(options_.preferences_file);
        try {
            fonts_ = text_font::FontStack::open(text_font::bundled_font_directory());
        } catch (const std::exception& error) {
            std::cerr << "open-annihilation: the folder chooser's fonts could not be opened: "
                      << error.what() << '\n';
        }
        if (!fonts_)
            std::cerr << "open-annihilation: the folder chooser shows no text: the bundled fonts "
                         "are missing\n";
        int count = 0;
        if (SDL_JoystickID* ids = SDL_GetGamepads(&count); ids != nullptr) {
            for (int index = 0; index < count; ++index)
                open_pad(ids[index]);
            SDL_free(ids);
        }
        model_.version = game_files_version_text();
        model_.offers_dialog = needed_.dialog_offered;
        std::string notice;
        if (!needed_.stored_folder.empty())
            notice = "The Total Annihilation folder chosen earlier can no longer be used:\n" +
                     path_to_utf8(needed_.stored_folder) + "\n" + needed_.stored_problem;
        if (!needed_.dialog_problem.empty()) {
            if (!notice.empty())
                notice += "\n\n";
            notice += "The system's folder dialog did not open: " + needed_.dialog_problem;
        }
        model_.notice = std::move(notice);
        for (const FoundInstall& install : needed_.found) {
            chooser::InstallRow row;
            row.folder = path_to_utf8(install.folder);
            row.source = std::string(installs::source_words(install.source, install.removable));
            std::string problem;
            if (const auto checked = take_chosen_folder(options_, install.folder, &problem)) {
                row.usable = true;
                row.verdict = verdict_of(*checked);
            } else {
                row.verdict = problem;
            }
            model_.installs.push_back(std::move(row));
        }
        home_ = installs::home_folder();
        refresh_viewport();
        dirty_ = true;
    }

    /// Runs one pass of the loop: events, the gamepads' timers, the layout, the frame.
    void pass() {
        SDL_Event event{};
        if (SDL_WaitEventTimeout(&event, dirty_ ? 0 : wait_ms())) {
            handle_event(event);
            while (!done_ && SDL_PollEvent(&event))
                handle_event(event);
        }
        if (done_)
            return;
        tick_pads();
        refresh_viewport();
        repaint();
        present();
    }

    /// Returns how long the loop may wait for an event.
    ///
    /// @return milliseconds; -1 waits for an event alone
    [[nodiscard]] int32_t wait_ms() const noexcept {
        const bool timers =
            held_ != Direction::none || std::fabs(scroll_stick_) > scroll_dead_share;
        if (timers || (host_ != nullptr && host_->start_stage_open()))
            return frame_wait_ms;
        return -1;
    }

    /// Takes one event.
    ///
    /// @param event the event
    void handle_event(const SDL_Event& event) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            finish(false);
            return;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_RESTORED:
            dirty_ = true;
            return;
        case SDL_EVENT_RENDER_DEVICE_RESET:
            if (texture_ != nullptr)
                SDL_DestroyTexture(texture_);
            texture_ = nullptr;
            render_state_.use_window_pixels();
            dirty_ = true;
            return;
        case SDL_EVENT_RENDER_TARGETS_RESET:
            render_state_.use_window_pixels();
            dirty_ = true;
            return;
        case SDL_EVENT_FINGER_DOWN:
            if (accepted_touch(event.tfinger.touchID))
                press_at(
                    event.tfinger.x * static_cast<float>(viewport_.width),
                    event.tfinger.y * static_cast<float>(viewport_.height),
                    true,
                    event.tfinger.touchID,
                    event.tfinger.fingerID
                );
            return;
        case SDL_EVENT_FINGER_MOTION:
            if (press_.active && press_.finger && event.tfinger.touchID == press_.touch &&
                event.tfinger.fingerID == press_.id)
                move_to(event.tfinger.y * static_cast<float>(viewport_.height));
            return;
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            if (press_.active && press_.finger && event.tfinger.touchID == press_.touch &&
                event.tfinger.fingerID == press_.id) {
                if (event.type == SDL_EVENT_FINGER_CANCELED) {
                    ui_.pressed = -1;
                    press_ = {};
                    dirty_ = true;
                } else {
                    release_at(
                        event.tfinger.x * static_cast<float>(viewport_.width),
                        event.tfinger.y * static_cast<float>(viewport_.height)
                    );
                }
            }
            return;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            // SDL's mouse events made from a finger are left out: the finger itself is taken.
            if (event.button.which != SDL_TOUCH_MOUSEID && event.button.button == SDL_BUTTON_LEFT)
                press_at(
                    event.button.x * viewport_.px_per_point,
                    event.button.y * viewport_.px_per_point,
                    false,
                    0,
                    0
                );
            return;
        case SDL_EVENT_MOUSE_MOTION:
            if (event.motion.which != SDL_TOUCH_MOUSEID && press_.active && !press_.finger)
                move_to(event.motion.y * viewport_.px_per_point);
            return;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.which != SDL_TOUCH_MOUSEID && event.button.button == SDL_BUTTON_LEFT &&
                press_.active && !press_.finger)
                release_at(
                    event.button.x * viewport_.px_per_point, event.button.y * viewport_.px_per_point
                );
            return;
        case SDL_EVENT_MOUSE_WHEEL: {
            if (event.wheel.which == SDL_TOUCH_MOUSEID)
                return;
            float notches = event.wheel.y;
            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                notches = -notches;
            chooser::scroll(
                layout_, ui_, static_cast<int32_t>(std::lround(-notches * wheel_notch_points))
            );
            dirty_ = true;
            return;
        }
        case SDL_EVENT_KEY_DOWN:
            take_key(event.key);
            return;
        case SDL_EVENT_GAMEPAD_ADDED:
            open_pad(event.gdevice.which);
            return;
        case SDL_EVENT_GAMEPAD_REMOVED:
            close_pad(event.gdevice.which);
            return;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            take_pad_button(event.gbutton.button, true);
            return;
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            take_pad_button(event.gbutton.button, false);
            return;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            take_pad_axis(event.gaxis.axis, event.gaxis.value);
            return;
        default:
            return;
        }
    }

    /// Tells whether a touch device's fingers reach the chooser: a touch screen's, never the
    /// pointer's or a pen's made into fingers.
    ///
    /// @param device the device
    /// @return true for a touch screen
    [[nodiscard]] static bool accepted_touch(SDL_TouchID device) noexcept {
        if (device == SDL_MOUSE_TOUCHID || device == SDL_PEN_TOUCHID)
            return false;
        return SDL_GetTouchDeviceType(device) == SDL_TOUCH_DEVICE_DIRECT;
    }

    /// Starts a press.
    ///
    /// @param x where, canvas pixels
    /// @param y where, canvas pixels
    /// @param finger it is a finger's
    /// @param touch the finger's device
    /// @param id the finger
    void press_at(float x, float y, bool finger, SDL_TouchID touch, SDL_FingerID id) {
        if (press_.active)
            return;
        lay_out_if_stale();
        press_ = Press{true, finger, touch, id, x, y, y, false};
        chooser::press_down(layout_, ui_, point_of(x, y));
        dirty_ = true;
    }

    /// Moves a press: over the rows, past drag_threshold_points it scrolls them instead.
    ///
    /// @param y where it is, canvas pixels
    void move_to(float y) {
        if (!press_.active)
            return;
        const float scale = std::max(viewport_.px_per_point, 0.01F);
        if (!press_.scrolling) {
            const view::Rect rows = layout_.paint.rows;
            const bool over_rows = rows.width > 0 && rows.height > 0 &&
                                   press_.start_x >= static_cast<float>(rows.x) &&
                                   press_.start_x < static_cast<float>(rows.x + rows.width) &&
                                   press_.start_y >= static_cast<float>(rows.y) &&
                                   press_.start_y < static_cast<float>(rows.y + rows.height);
            if (!over_rows || std::fabs(y - press_.start_y) <= drag_threshold_points * scale)
                return;
            // The press becomes a scroll: what it held is let go.
            press_.scrolling = true;
            ui_.pressed = -1;
            press_.last_y = press_.start_y;
        }
        const float points = (press_.last_y - y) / scale;
        const auto whole = static_cast<int32_t>(points);
        if (whole == 0)
            return;
        press_.last_y -= static_cast<float>(whole) * scale;
        chooser::scroll(layout_, ui_, whole);
        dirty_ = true;
    }

    /// Ends a press: on the target it went down on, the target's command is carried out.
    ///
    /// @param x where, canvas pixels
    /// @param y where, canvas pixels
    void release_at(float x, float y) {
        if (!press_.active)
            return;
        const Press finished = press_;
        press_ = {};
        dirty_ = true;
        if (finished.scrolling) {
            ui_.pressed = -1;
            return;
        }
        act(chooser::press_up(layout_, ui_, point_of(x, y)));
    }

    /// Converts canvas pixels to a whole point.
    ///
    /// @param x across, canvas pixels
    /// @param y down, canvas pixels
    /// @return the point
    [[nodiscard]] static chooser::Point point_of(float x, float y) noexcept {
        return {static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))};
    }

    /// Takes a key.
    ///
    /// @param key the key's event
    void take_key(const SDL_KeyboardEvent& key) {
        const bool shift = (key.mod & SDL_KMOD_SHIFT) != 0;
        switch (key.key) {
        case SDLK_TAB:
            press_key(shift ? chooser::Key::back_tab : chooser::Key::tab);
            return;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
            press_key(chooser::Key::enter);
            return;
        case SDLK_ESCAPE:
            press_key(chooser::Key::escape);
            return;
        case SDLK_BACKSPACE:
            // In the browser Backspace goes up a folder, as file managers do.
            if (model_.view == chooser::View::browse && model_.has_parent)
                act({chooser::Command::parent_folder, 0});
            return;
        case SDLK_UP:
            press_key(chooser::Key::up);
            return;
        case SDLK_DOWN:
            press_key(chooser::Key::down);
            return;
        case SDLK_LEFT:
            press_key(chooser::Key::left);
            return;
        case SDLK_RIGHT:
            press_key(chooser::Key::right);
            return;
        case SDLK_PAGEUP:
            press_key(chooser::Key::page_up);
            return;
        case SDLK_PAGEDOWN:
            press_key(chooser::Key::page_down);
            return;
        default:
            return;
        }
    }

    /// Gives the chooser a key and carries out what it asks.
    ///
    /// @param key the key
    void press_key(chooser::Key key) {
        lay_out_if_stale();
        const chooser::Outcome outcome = chooser::key(layout_, ui_, key);
        dirty_ = true;
        stale_ = true;
        act(outcome);
    }

    /// Opens a gamepad, once.
    ///
    /// @param id its instance
    void open_pad(SDL_JoystickID id) {
        if (std::any_of(pads_.begin(), pads_.end(), [id](const OpenPad& pad) {
                return pad.id == id;
            }))
            return;
        if (SDL_Gamepad* pad = SDL_OpenGamepad(id))
            pads_.push_back({id, pad});
    }

    /// Closes a gamepad that went away.
    ///
    /// @param id its instance
    void close_pad(SDL_JoystickID id) {
        const auto found = std::find_if(pads_.begin(), pads_.end(), [id](const OpenPad& pad) {
            return pad.id == id;
        });
        if (found == pads_.end())
            return;
        SDL_CloseGamepad(found->pad);
        pads_.erase(found);
        release_direction();
        stick_direction_ = Direction::none;
        scroll_stick_ = 0.0F;
    }

    /// Takes a gamepad's button: the D-pad moves the focus (held, it repeats), A presses, B
    /// goes back, the shoulder buttons turn a page.
    ///
    /// @param button the button
    /// @param down it went down
    void take_pad_button(uint8_t button, bool down) {
        const Direction direction = direction_of(button);
        if (direction != Direction::none) {
            if (down)
                hold_direction(direction, HoldSource::dpad);
            else if (held_ == direction && hold_source_ == HoldSource::dpad)
                release_direction();
            return;
        }
        if (!down)
            return;
        switch (static_cast<SDL_GamepadButton>(button)) {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            press_key(chooser::Key::enter);
            return;
        case SDL_GAMEPAD_BUTTON_EAST:
            press_key(chooser::Key::escape);
            return;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            press_key(chooser::Key::page_up);
            return;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            press_key(chooser::Key::page_down);
            return;
        default:
            return;
        }
    }

    /// Takes a gamepad's axis: the left stick moves the focus like the D-pad, the right stick
    /// scrolls the rows.
    ///
    /// @param axis the axis
    /// @param value its value
    void take_pad_axis(uint8_t axis, int16_t value) {
        const float share = std::clamp(static_cast<float>(value) / stick_axis_range, -1.0F, 1.0F);
        switch (static_cast<SDL_GamepadAxis>(axis)) {
        case SDL_GAMEPAD_AXIS_LEFTX:
            stick_x_ = share;
            break;
        case SDL_GAMEPAD_AXIS_LEFTY:
            stick_y_ = share;
            break;
        case SDL_GAMEPAD_AXIS_RIGHTY:
            scroll_stick_ = share;
            last_tick_ms_ = SDL_GetTicks();
            return;
        default:
            return;
        }
        // A held direction lasts while the stick stays out past stick_off_share along it; a new
        // one needs stick_on_share along the axis the stick leans on most.
        Direction next = Direction::none;
        const float along_y = std::fabs(stick_y_);
        const float along_x = std::fabs(stick_x_);
        const bool keeps = (stick_direction_ == Direction::up && stick_y_ <= -stick_off_share) ||
                           (stick_direction_ == Direction::down && stick_y_ >= stick_off_share) ||
                           (stick_direction_ == Direction::left && stick_x_ <= -stick_off_share) ||
                           (stick_direction_ == Direction::right && stick_x_ >= stick_off_share);
        if (keeps)
            next = stick_direction_;
        else if (std::max(along_x, along_y) >= stick_on_share)
            next = along_y >= along_x ? (stick_y_ < 0.0F ? Direction::up : Direction::down)
                                      : (stick_x_ < 0.0F ? Direction::left : Direction::right);
        if (next == stick_direction_)
            return;
        stick_direction_ = next;
        if (next != Direction::none)
            hold_direction(next, HoldSource::stick);
        else if (hold_source_ == HoldSource::stick)
            release_direction();
    }

    /// Starts a held direction: one step at once, then the menu repeat.
    ///
    /// @param direction the direction
    /// @param source what holds it
    void hold_direction(Direction direction, HoldSource source) {
        held_ = direction;
        hold_source_ = source;
        const uint32_t steps = repeat_.press(SDL_GetTicks());
        for (uint32_t step = 0; step < steps; ++step)
            press_key(key_of(direction));
    }

    /// Ends the held direction.
    void release_direction() noexcept {
        held_ = Direction::none;
        repeat_.release();
    }

    /// Moves the gamepads' timers on: a held direction's repeats and the right stick's scroll.
    void tick_pads() {
        const uint64_t now = SDL_GetTicks();
        if (held_ != Direction::none) {
            const uint32_t steps = repeat_.advance(now);
            for (uint32_t step = 0; step < steps; ++step)
                press_key(key_of(held_));
        }
        const float lean = std::fabs(scroll_stick_);
        if (lean > scroll_dead_share && now > last_tick_ms_) {
            const float push = (lean - scroll_dead_share) / (1.0F - scroll_dead_share);
            const float seconds = static_cast<float>(now - last_tick_ms_) / ms_per_second;
            scroll_carry_ += (scroll_stick_ < 0.0F ? -1.0F : 1.0F) * push * push *
                             stick_scroll_points_per_second * seconds;
            const auto whole = static_cast<int32_t>(scroll_carry_);
            if (whole != 0) {
                scroll_carry_ -= static_cast<float>(whole);
                chooser::scroll(layout_, ui_, whole);
                dirty_ = true;
            }
        } else {
            scroll_carry_ = 0.0F;
        }
        last_tick_ms_ = now;
    }

    /// Carries out what a press or a key asks.
    ///
    /// @param outcome the command
    void act(chooser::Outcome outcome) {
        switch (outcome.command) {
        case chooser::Command::none:
            return;
        case chooser::Command::play_install:
            if (outcome.index < needed_.found.size())
                pick(needed_.found[outcome.index].folder, outcome.index);
            return;
        case chooser::Command::open_browser:
            model_.hint.clear();
            browse_to(browsed_.empty() ? start_folder() : browsed_);
            return;
        case chooser::Command::find_demo: {
            const fs::path downloads = home_ / downloads_folder_name;
            std::error_code error;
            model_.hint = "Open the folder that holds the installer of the Total Annihilation demo "
                          "(1997), then Play this folder.";
            browse_to(
                !home_.empty() && fs::is_directory(downloads, error) ? downloads : start_folder()
            );
            return;
        }
        case chooser::Command::use_dialog:
            use_dialog();
            return;
        case chooser::Command::quit:
            finish(false);
            return;
        case chooser::Command::open_place:
            if (outcome.index < places_.size())
                browse_to(places_[outcome.index]);
            return;
        case chooser::Command::parent_folder:
            if (model_.has_parent)
                browse_to(browsed_.parent_path());
            return;
        case chooser::Command::enter_folder:
            if (outcome.index < entries_.size())
                browse_to(entries_[outcome.index]);
            return;
        case chooser::Command::choose_folder:
            if (model_.folder_usable)
                pick(browsed_, std::nullopt);
            return;
        case chooser::Command::back_to_list:
            model_.view = chooser::View::list;
            model_.notice = list_notice_;
            ui_.scroll_points = 0;
            ui_.focus = 0;
            stale_ = true;
            dirty_ = true;
            return;
        }
    }

    /// Returns the folder the browser opens at first: the home folder, else the folder the game
    /// runs in.
    ///
    /// @return the folder
    [[nodiscard]] fs::path start_folder() const {
        std::error_code error;
        if (!home_.empty() && fs::is_directory(home_, error))
            return home_;
        fs::path current = fs::current_path(error);
        return error ? fs::path("/") : current;
    }

    /// Plays a folder when its check passes, else says why it cannot be played.
    ///
    /// @param folder the folder
    /// @param row the found folder's row it came from; none for the browser
    void pick(const fs::path& folder, std::optional<uint16_t> row) {
        std::string problem;
        if (auto taken = take_chosen_folder(options_, folder, &problem)) {
            chosen_ = std::move(taken);
            finish(true);
            return;
        }
        model_.notice = "This folder cannot be played:\n" + path_to_utf8(folder) + "\n" + problem;
        if (row && *row < model_.installs.size()) {
            model_.installs[*row].usable = false;
            model_.installs[*row].verdict = problem;
            list_notice_ = model_.notice;
        }
        if (model_.view == chooser::View::browse) {
            model_.folder_usable = false;
            model_.folder_verdict = problem;
        }
        stale_ = true;
        dirty_ = true;
    }

    /// Opens the system's folder dialog and plays the folder picked; a dialog that cannot
    /// open is said in the notice and no longer offered.
    void use_dialog() {
        fs::path chosen;
        std::string error;
        const fs::path start = model_.view == chooser::View::browse ? browsed_ : start_folder();
        const FolderPick picked = pick_game_folder_with_dialog(start, &chosen, &error);
        dirty_ = true;
        stale_ = true;
        switch (picked) {
        case FolderPick::chosen:
            pick(chosen, std::nullopt);
            return;
        case FolderPick::cancelled:
            return;
        case FolderPick::unavailable:
            model_.notice = "The system's folder dialog did not open: " + error;
            list_notice_ = model_.notice;
            model_.offers_dialog = false;
            return;
        }
    }

    /// Shows a folder in the browser: its places, its folders (hidden ones left out, in name
    /// order, game folders marked) and what the check finds there.
    ///
    /// @param folder the folder
    void browse_to(const fs::path& folder) {
        std::error_code error;
        fs::path shown = fs::absolute(folder, error).lexically_normal();
        if (error)
            shown = folder;
        // A folder named "a/b/" keeps no empty last part.
        if (!shown.has_filename() && shown.has_relative_path())
            shown = shown.parent_path();
        if (model_.view == chooser::View::list)
            list_notice_ = model_.notice;
        model_.view = chooser::View::browse;
        model_.notice.clear();
        browsed_ = shown;
        model_.folder = path_to_utf8(shown);
        model_.has_parent = shown.has_relative_path() && shown.parent_path() != shown;
        fill_places();
        fill_entries();
        std::string problem;
        if (const auto checked = take_chosen_folder(options_, shown, &problem)) {
            model_.folder_usable = true;
            model_.folder_verdict = verdict_of(*checked);
        } else {
            model_.folder_usable = false;
            model_.folder_verdict = problem;
        }
        ui_.scroll_points = 0;
        ui_.pressed = -1;
        // The keys and a pad start on the first folder, else the first place.
        lay_out_now();
        ui_.focus = 0;
        for (std::size_t index = 0; index < layout_.focus_order.size(); ++index)
            if (layout_.focus_order[index].kind == chooser::TargetKind::entry) {
                ui_.focus = static_cast<int32_t>(index);
                break;
            }
        stale_ = true;
        dirty_ = true;
    }

    /// Fills the browser's places: Home, Downloads and each drive.
    void fill_places() {
        places_.clear();
        model_.place_labels.clear();
        std::error_code error;
        if (!home_.empty() && fs::is_directory(home_, error)) {
            places_.push_back(home_);
            model_.place_labels.push_back("HOME");
            const fs::path downloads = home_ / downloads_folder_name;
            if (fs::is_directory(downloads, error)) {
                places_.push_back(downloads);
                model_.place_labels.push_back("DOWNLOADS");
            }
        }
        for (const fs::path& drive : installs::drive_folders()) {
            places_.push_back(drive);
            const std::string name = path_to_utf8(drive.filename());
            model_.place_labels.push_back(name.empty() ? path_to_utf8(drive) : name);
        }
    }

    /// Fills the browser's folders: at most most_browse_entries of the first
    /// browse_listing_limit entries, hidden ones left out, in name order, the first
    /// most_marked_entries marked when they hold the game's first archive.
    void fill_entries() {
        entries_.clear();
        model_.entries.clear();
        std::vector<std::pair<std::string, fs::path>> found;
        std::error_code error;
        fs::directory_iterator entry(
            browsed_, fs::directory_options::skip_permission_denied, error
        );
        std::size_t seen = 0;
        for (; !error && entry != fs::directory_iterator(); entry.increment(error)) {
            if (++seen > browse_listing_limit || found.size() >= most_browse_entries)
                break;
            std::error_code kind_error;
            if (!entry->is_directory(kind_error) || kind_error)
                continue;
            std::string name = path_to_utf8(entry->path().filename());
            if (name.empty() || name.front() == '.')
                continue;
            found.emplace_back(std::move(name), entry->path());
        }
        std::sort(found.begin(), found.end(), [](const auto& left, const auto& right) {
            const std::string a = folded(left.first);
            const std::string b = folded(right.first);
            return a != b ? a < b : left.first < right.first;
        });
        for (std::size_t index = 0; index < found.size(); ++index) {
            chooser::FolderEntry shown;
            shown.name = found[index].first;
            shown.game_folder =
                index < most_marked_entries && installs::holds_game_archive(found[index].second);
            model_.entries.push_back(std::move(shown));
            entries_.push_back(std::move(found[index].second));
        }
    }

    /// Ends the chooser.
    ///
    /// @param kept true when a folder was chosen; false when the player quit
    void finish(bool kept) noexcept {
        done_ = true;
        kept_ = kept;
    }

    /// Reads the window's size, density and safe area into the viewport.
    void refresh_viewport() {
        int window_width = 0;
        int window_height = 0;
        int output_width = 0;
        int output_height = 0;
        if (!SDL_GetWindowSize(window_, &window_width, &window_height) ||
            !SDL_GetRenderOutputSize(renderer_, &output_width, &output_height) ||
            window_width <= 0 || window_height <= 0 || output_width <= 0 || output_height <= 0)
            return;
        view::Viewport next;
        next.width = output_width;
        next.height = output_height;
        next.px_per_point = static_cast<float>(output_width) / static_cast<float>(window_width);
        SDL_Rect safe{};
        if (SDL_GetWindowSafeArea(window_, &safe) && safe.w > 0 && safe.h > 0) {
            const float scale = next.px_per_point;
            next.safe.left = static_cast<int>(std::lround(static_cast<float>(safe.x) * scale));
            next.safe.top = static_cast<int>(std::lround(static_cast<float>(safe.y) * scale));
            next.safe.right = static_cast<int>(
                std::lround(static_cast<float>(window_width - safe.x - safe.w) * scale)
            );
            next.safe.bottom = static_cast<int>(
                std::lround(static_cast<float>(window_height - safe.y - safe.h) * scale)
            );
        }
        const bool same =
            next.width == viewport_.width && next.height == viewport_.height &&
            next.px_per_point == viewport_.px_per_point && next.safe.left == viewport_.safe.left &&
            next.safe.top == viewport_.safe.top && next.safe.right == viewport_.safe.right &&
            next.safe.bottom == viewport_.safe.bottom;
        if (!same) {
            viewport_ = next;
            dirty_ = true;
            stale_ = true;
        }
    }

    /// Lays the chooser out from the model and the state now.
    void lay_out_now() {
        layout_ = chooser::lay_out(model_, ui_, viewport_, game_files_measure(fonts_.get()));
        // A focus past the end of a new layout moves to its first target.
        if (ui_.focus >= static_cast<int32_t>(layout_.focus_order.size()))
            ui_.focus = layout_.focus_order.empty() ? -1 : 0;
        ui_.scroll_points =
            std::clamp(ui_.scroll_points, 0, std::max(0, layout_.scroll_max_points));
        stale_ = false;
    }

    /// Lays the chooser out again when the model, the state or the scroll changed since, so
    /// that input reads the boxes as they now are.
    void lay_out_if_stale() {
        if (stale_ || dirty_)
            lay_out_now();
    }

    /// Lays out and paints the frame when anything changed.
    void repaint() {
        painted_ = false;
        if (!dirty_ || viewport_.width <= 0 || viewport_.height <= 0)
            return;
        lay_out_now();
        if (canvas_.width != viewport_.width || canvas_.height != viewport_.height)
            canvas_ = touch_paint::make_canvas(viewport_.width, viewport_.height);
        paint_game_files(
            canvas_, layout_.paint, fonts_.get(), viewport_.px_per_point, game_files_icon()
        );
        dirty_ = false;
        painted_ = true;
    }

    /// Shows the frame on the window.
    void present() {
        // While the renderer's start-up stage stands, the last frame is shown again each pass.
        const bool counting = host_ != nullptr && host_->start_stage_open();
        if ((!painted_ && !counting) || canvas_.width <= 0 || canvas_.height <= 0)
            return;
        if (texture_ == nullptr || texture_width_ != canvas_.width ||
            texture_height_ != canvas_.height) {
            if (texture_ != nullptr)
                SDL_DestroyTexture(texture_);
            texture_ = SDL_CreateTexture(
                renderer_,
                SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_STREAMING,
                canvas_.width,
                canvas_.height
            );
            if (texture_ == nullptr) {
                std::cerr << "open-annihilation: the folder chooser cannot make its texture: "
                          << SDL_GetError() << '\n';
                return;
            }
            SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_NONE);
            SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
            texture_width_ = canvas_.width;
            texture_height_ = canvas_.height;
        }
        SDL_UpdateTexture(texture_, nullptr, canvas_.rgba.data(), canvas_.width * 4);
        render_state_.use_window_pixels();
        const view::Colour back = view::background_colour;
        SDL_SetRenderDrawColor(renderer_, back.r, back.g, back.b, SDL_ALPHA_OPAQUE);
        SDL_RenderClear(renderer_);
        SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
        if (SDL_RenderPresent(renderer_) && host_ != nullptr)
            host_->note_presented_frame(0);
    }

    const Options& options_;     ///< the parsed command line
    RendererHost* host_{};       ///< what made the renderer; may be null
    SDL_Window* window_{};       ///< the game's window
    SDL_Renderer* renderer_{};   ///< its renderer
    GameFilesNeeded needed_{};   ///< why there is no folder, and the folders found
    RendererState render_state_; ///< the renderer's state to put back
    std::unique_ptr<text_font::FontStack> fonts_{}; ///< the bundled fonts, when open
    chooser::Model model_{};                        ///< what the chooser shows
    chooser::UiState ui_{};                         ///< what the chooser keeps between frames
    chooser::Layout layout_{};                      ///< the layout last made
    view::Viewport viewport_{};                     ///< the window's canvas
    touch_paint::Canvas canvas_{};                  ///< the frame last painted
    SDL_Texture* texture_{};                        ///< the frame's texture
    int texture_width_{};                           ///< its width
    int texture_height_{};                          ///< its height
    bool dirty_{true};                              ///< the frame must be painted again
    bool stale_{true};                      ///< the layout must be made again before input reads it
    bool painted_{};                        ///< this pass painted a frame
    bool done_{};                           ///< the chooser has ended
    bool kept_{};                           ///< it ended with a folder chosen
    std::optional<GameDirectory> chosen_{}; ///< the folder chosen
    Press press_{};                         ///< the press under way
    std::vector<OpenPad> pads_{};           ///< the gamepads opened
    Direction held_{Direction::none};       ///< the direction held down
    HoldSource hold_source_{HoldSource::dpad};   ///< what holds it
    oa::ui::pad_controls::MenuRepeat repeat_{};  ///< its repeats
    Direction stick_direction_{Direction::none}; ///< the left stick's direction
    float stick_x_{};                            ///< the left stick across, -1 to 1
    float stick_y_{};                            ///< the left stick down, -1 to 1
    float scroll_stick_{};                       ///< the right stick down, -1 to 1
    float scroll_carry_{};            ///< the part of a point the right stick has scrolled
    uint64_t last_tick_ms_{};         ///< when the gamepads' timers last moved on
    fs::path home_{};                 ///< the player's home folder
    fs::path browsed_{};              ///< the folder the browser shows
    std::vector<fs::path> places_{};  ///< the browser's places, by Model::place_labels
    std::vector<fs::path> entries_{}; ///< the browser's folders, by Model::entries
    std::string list_notice_{};       ///< the list's notice, kept while the browser shows
};

} // namespace

bool folder_chooser_offered(const Options& options) {
    return !nobody_watches(options) && !game_files_import_offered(game_files_hooks());
}

bool run_folder_chooser_until_resolved(
    const Options& options,
    const FolderChooserDisplayHooks& display,
    GameFilesNeeded& needed,
    std::optional<GameDirectory>& game_directory
) {
    if (game_directory || !needed.needed || !needed.chooser)
        return true;
    SDL_Window* window = display.window != nullptr ? display.window(display.context) : nullptr;
    SDL_Renderer* renderer = window != nullptr && display.renderer != nullptr
                                 ? display.renderer(display.context)
                                 : nullptr;
    if (window == nullptr || renderer == nullptr)
        throw std::runtime_error(
            "the game's window cannot open to choose the Total Annihilation folder; name it with "
            "--game-dir PATH"
        );
    RendererHost* host = display.host != nullptr ? display.host(display.context) : nullptr;
    bool kept = false;
    {
        ChooserScreen screen(options, host, window, renderer, needed);
        kept = screen.run(game_directory);
    }
    if (game_directory)
        needed = GameFilesNeeded{};
    return kept;
}

} // namespace oa::app
