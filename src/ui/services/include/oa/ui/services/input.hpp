// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Keyboard code queue and pointer event queue kept in the display context,
// and the translation of host virtual keys into engine key codes.

#include <cstdint>

namespace oa::ui::services {

// Input boundary: modifier state and the pointer position in window pixels.
struct Input {
    void* context;
    // Whether the engine control key (see key_code) is held.
    bool (*key_down)(void* context, uint8_t key_code);
    void (*cursor_position)(void* context, int32_t* x, int32_t* y);
};

// Engine key codes above the ASCII range (the internal control codes).
namespace key_code {
inline constexpr uint8_t function_1 = 0xe2;              // F1..F12 are 0xe2..0xed
inline constexpr uint8_t control_function_offset = 0x14; // Ctrl+F1..F12 are 0xce..0xd9
inline constexpr uint8_t insert = 0xee;
inline constexpr uint8_t del = 0xef;
inline constexpr uint8_t home = 0xf0;
inline constexpr uint8_t end = 0xf1;
inline constexpr uint8_t page_up = 0xf2;
inline constexpr uint8_t page_down = 0xf3;
inline constexpr uint8_t left = 0xf4;
inline constexpr uint8_t up = 0xf5;
inline constexpr uint8_t right = 0xf6;
inline constexpr uint8_t down = 0xf7;
inline constexpr uint8_t pause = 0xf8;
inline constexpr uint8_t control = 0xfa;
inline constexpr uint8_t control_digit_offset = 0x94;  // Ctrl+'0'..'9' are 0xc4..0xcd
inline constexpr uint8_t control_letter_offset = 0x69; // Ctrl+'A'..'Z' are 0xaa..0xc3
} // namespace key_code

inline constexpr int32_t key_queue_max_capacity = 30;

// Ring of key codes kept in the display context.
struct KeyQueue {
    int32_t capacity; // at most 30
    uint32_t codes[key_queue_max_capacity];
    int32_t head; // next write
    int32_t tail; // next read
};

/// Empties the key queue.
///
/// @param[out] queue key queue whose head and tail are reset
void key_queue_reset(KeyQueue* queue) noexcept;
/// Sets the ring size, clamped to 30, and empties the queue.
///
/// The ring keeps one slot free, so it holds capacity - 1 codes.
///
/// @param[out] queue key queue
/// @param capacity requested ring size
void key_queue_set_capacity(KeyQueue* queue, int32_t capacity) noexcept;
/// Removes and returns the oldest key code.
///
/// @param[in,out] queue key queue
/// @return the code, or 0 when the queue is empty
uint32_t key_queue_pop(KeyQueue* queue) noexcept;
/// Returns the oldest key code without removing it.
///
/// @param queue key queue
/// @return the code, or 0 when the queue is empty
[[nodiscard]] uint32_t key_queue_peek(const KeyQueue* queue) noexcept;
/// Appends a key code unless the queue is full.
///
/// @param[in,out] queue key queue
/// @param code engine key code
/// @return whether the code was stored; a capacity below one stores nothing
bool key_queue_push(KeyQueue* queue, uint32_t code) noexcept;

/// Translates a host virtual-key press into an engine key code and queues it.
///
/// Navigation and function keys always translate. Digits, letters and
/// punctuation translate only for a system key press (Alt held) or with the
/// control key down, because plain character input arrives separately.
///
/// @param[in,out] queue key queue
/// @param input input boundary, asked whether the control key is held
/// @param virtual_key host virtual-key code
/// @param system_key true for a system key press (Alt held)
/// @return whether a code was queued
bool key_queue_push_virtual_key(
    KeyQueue* queue, const Input* input, int32_t virtual_key, bool system_key
) noexcept;

// One 0x18-byte pointer event.
struct PointerEvent {
    int32_t x;          // client x
    int32_t y;          // client y
    uint32_t buttons;   // button and modifier state reported with the message
    uint32_t tick;      // engine tick when received
    uint32_t message;   // host message identifier (0x200 = move)
    uint32_t is_button; // 0 for a move, 1 for a button transition
};

// Ring of pointer events kept in the display context.
struct PointerQueue {
    int32_t capacity;
    PointerEvent* events; // capacity entries
    int32_t head;         // next write
    int32_t tail;         // next read
    PointerEvent current; // latest move
};

/// Records the latest pointer move without queuing it.
///
/// @param[out] queue pointer queue whose current event is replaced
/// @param event the move
void pointer_queue_set_current(PointerQueue* queue, const PointerEvent* event) noexcept;
/// Empties the pointer queue; the latest move is kept.
///
/// @param[out] queue pointer queue whose head and tail are reset
void pointer_queue_reset(PointerQueue* queue) noexcept;
/// Removes the oldest queued pointer event.
///
/// @param[in,out] queue pointer queue
/// @param[out] out the event, or the latest move when the queue is empty
/// @return true when a queued event was taken, false when the latest move was copied
bool pointer_queue_pop(PointerQueue* queue, PointerEvent* out) noexcept;
/// Reads the oldest queued pointer event without removing it.
///
/// @param queue pointer queue
/// @param[out] out the event, or the latest move when the queue is empty
/// @return true when a queued event was copied, false when the latest move was copied
bool pointer_queue_peek(const PointerQueue* queue, PointerEvent* out) noexcept;
/// Appends a pointer event unless the queue is full or has no storage.
///
/// @param[in,out] queue pointer queue
/// @param event event to store
/// @return whether it was stored
bool pointer_queue_push(PointerQueue* queue, const PointerEvent* event) noexcept;

} // namespace oa::ui::services
