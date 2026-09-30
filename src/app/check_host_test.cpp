// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The check host (check_host.hpp): its header stands alone, its table holds
// a context and eighteen entries, all null until check_host() sets them, and
// its screen id is the screen registry's; and the parts of it that need no
// running game: the left-button pointer events it makes at canvas points,
// with and without a renderer that shows the canvas scaled, the gadget it
// finds by name whatever the case, and the text it hands back only when it
// fits.
#include "oa/app/check_host.hpp"

#include "check_host_input.hpp"
#include "oa/ui/screen_registry.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace {

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

namespace input = oa::app::check_host_input;

// The canvas the game draws its frontend on, and a window twice its size.
constexpr int kCanvasWidth = 640;
constexpr int kCanvasHeight = 480;
constexpr int kScale = 2;

static_assert(std::is_same_v<oa::app::ScreenId, uint16_t>, "a screen id is 16 bits");
static_assert(
    sizeof(oa::app::CheckHost) == sizeof(void*) * 19,
    "the check host holds its context and eighteen entries"
);

/// Checks that an empty table holds no entry.
void test_empty_table() {
    const oa::app::CheckHost host{};
    void* words[19]{};
    std::memcpy(static_cast<void*>(words), &host, sizeof words);
    bool empty = true;
    for (const void* word : words)
        empty = empty && word == nullptr;
    CHECK(empty);
}

/// Checks the pointer events made without a renderer: the canvas point is
/// the event's, the window id 0.
void test_pointer_events_without_window() {
    const auto motion = input::pointer_event(nullptr, nullptr, SDL_EVENT_MOUSE_MOTION, 320, 240, 3);
    CHECK(motion.type == SDL_EVENT_MOUSE_MOTION);
    CHECK(motion.motion.windowID == 0);
    CHECK(motion.motion.x == 320.0F && motion.motion.y == 240.0F);

    const auto press =
        input::pointer_event(nullptr, nullptr, SDL_EVENT_MOUSE_BUTTON_DOWN, 187, 440, 2);
    CHECK(press.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
    CHECK(press.button.windowID == 0);
    CHECK(press.button.button == SDL_BUTTON_LEFT);
    CHECK(press.button.down);
    CHECK(press.button.clicks == 2);
    CHECK(press.button.x == 187.0F && press.button.y == 440.0F);

    const auto release =
        input::pointer_event(nullptr, nullptr, SDL_EVENT_MOUSE_BUTTON_UP, 187, 440, 1);
    CHECK(release.type == SDL_EVENT_MOUSE_BUTTON_UP);
    CHECK(release.button.button == SDL_BUTTON_LEFT);
    CHECK(!release.button.down);
    CHECK(release.button.clicks == 1);

    bool refused = false;
    try {
        (void)input::pointer_event(nullptr, nullptr, SDL_EVENT_KEY_DOWN, 0, 0, 1);
    } catch (const std::runtime_error&) {
        refused = true;
    }
    CHECK(refused);
}

/// Checks that a renderer showing the canvas at twice its size places the
/// point at twice its coordinates.
void test_pointer_events_through_renderer() {
    SDL_Surface* target =
        SDL_CreateSurface(kCanvasWidth * kScale, kCanvasHeight * kScale, SDL_PIXELFORMAT_RGBA32);
    CHECK(target != nullptr);
    if (target == nullptr)
        return;
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(target);
    CHECK(renderer != nullptr);
    if (renderer != nullptr) {
        CHECK(SDL_SetRenderLogicalPresentation(
            renderer, kCanvasWidth, kCanvasHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX
        ));
        const auto press =
            input::pointer_event(renderer, nullptr, SDL_EVENT_MOUSE_BUTTON_DOWN, 100, 50, 1);
        CHECK(press.button.x == 100.0F * kScale && press.button.y == 50.0F * kScale);
        CHECK(press.button.windowID == 0);
        SDL_DestroyRenderer(renderer);
    }
    SDL_DestroySurface(target);
}

/// Checks the gadget found by name, whatever the case.
void test_find_gadget() {
    oa::ui::gui_layout::Layout layout;
    for (const char* name : {"Mainmenu.GUI", "SINGLE", "MULTI"}) {
        oa::ui::gui_layout::Gadget gadget{};
        gadget.common.name = name;
        layout.gadgets.push_back(gadget);
    }
    const auto* multi = input::find_gadget(layout, "multi");
    CHECK(multi == &layout.gadgets[2]);
    CHECK(input::find_gadget(layout, "MULTI") == &layout.gadgets[2]);
    CHECK(input::find_gadget(layout, "MULT") == nullptr);
    CHECK(input::find_gadget(layout, "MULTIPLAYER") == nullptr);
    CHECK(input::find_gadget(oa::ui::gui_layout::Layout{}, "SINGLE") == nullptr);
}

/// Checks that text is handed back only with room for its zero byte.
void test_copy_text() {
    char out[8] = "unset";
    CHECK(input::copy_text("sounds", out, sizeof out));
    CHECK(std::strcmp(out, "sounds") == 0);
    CHECK(input::copy_text("1234567", out, sizeof out));
    CHECK(std::strcmp(out, "1234567") == 0);
    CHECK(!input::copy_text("12345678", out, sizeof out));
    CHECK(std::strcmp(out, "1234567") == 0);
    CHECK(!input::copy_text("", out, 0));
    CHECK(!input::copy_text("x", nullptr, 4));
}

} // namespace

int main() {
    test_empty_table();
    test_pointer_events_without_window();
    test_pointer_events_through_renderer();
    test_find_gadget();
    test_copy_text();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
