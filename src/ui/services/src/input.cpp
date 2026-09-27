// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/services/input.hpp"

#include <cstdint>

namespace oa::ui::services {
namespace {

// Host virtual-key codes the translation distinguishes.
namespace vk {
constexpr int32_t pause = 0x13;
constexpr int32_t page_up = 0x21;
constexpr int32_t page_down = 0x22;
constexpr int32_t end = 0x23;
constexpr int32_t home = 0x24;
constexpr int32_t left = 0x25;
constexpr int32_t up = 0x26;
constexpr int32_t right = 0x27;
constexpr int32_t down = 0x28;
constexpr int32_t insert = 0x2d;
constexpr int32_t del = 0x2e;
constexpr int32_t digit_0 = 0x30;
constexpr int32_t digit_9 = 0x39;
constexpr int32_t letter_a = 0x41;
constexpr int32_t letter_z = 0x5a;
constexpr int32_t f1 = 0x70;
constexpr int32_t f12 = 0x7b;
constexpr int32_t oem_first = 0xba; // ; = , - . / `
constexpr int32_t oem_last = 0xc0;
constexpr int32_t bracket_first = 0xdb; // [ \ ] '
constexpr int32_t bracket_last = 0xde;
} // namespace vk

constexpr int32_t lowercase_offset = 0x20;
constexpr char oem_characters[] = {';', '=', ',', '-', '.', '/', '`'};
constexpr char bracket_characters[] = {'[', '\\', ']', '\''};

int32_t navigation_code(int32_t virtual_key) noexcept {
    switch (virtual_key) {
    case vk::pause:
        return key_code::pause;
    case vk::page_up:
        return key_code::page_up;
    case vk::page_down:
        return key_code::page_down;
    case vk::end:
        return key_code::end;
    case vk::home:
        return key_code::home;
    case vk::left:
        return key_code::left;
    case vk::up:
        return key_code::up;
    case vk::right:
        return key_code::right;
    case vk::down:
        return key_code::down;
    case vk::insert:
        return key_code::insert;
    case vk::del:
        return key_code::del;
    default:
        return -1;
    }
}

} // namespace

void key_queue_reset(KeyQueue* queue) noexcept {
    queue->head = 0;
    queue->tail = 0;
}

void key_queue_set_capacity(KeyQueue* queue, int32_t capacity) noexcept {
    queue->capacity = capacity < key_queue_max_capacity + 1 ? capacity : key_queue_max_capacity;
    key_queue_reset(queue);
}

uint32_t key_queue_pop(KeyQueue* queue) noexcept {
    const int32_t tail = queue->tail;
    if (queue->head == tail) {
        return 0;
    }
    const uint32_t code = queue->codes[tail];
    queue->tail = tail + 1;
    if (tail + 1 == queue->capacity) {
        queue->tail = 0;
    }
    return code;
}

uint32_t key_queue_peek(const KeyQueue* queue) noexcept {
    if (queue->head == queue->tail) {
        return 0;
    }
    return queue->codes[queue->tail];
}

bool key_queue_push(KeyQueue* queue, uint32_t code) noexcept {
    if (queue->capacity < 1) {
        return false;
    }
    if ((queue->head + 1) % queue->capacity == queue->tail) {
        return false;
    }
    queue->codes[queue->head] = code;
    queue->head += 1;
    if (queue->head == queue->capacity) {
        queue->head = 0;
    }
    return true;
}

bool key_queue_push_virtual_key(
    KeyQueue* queue, const Input* input, int32_t virtual_key, bool system_key
) noexcept {
    const bool control = input->key_down(input->context, key_code::control);
    if (virtual_key >= vk::f1 && virtual_key <= vk::f12) {
        const int32_t code = key_code::function_1 + (virtual_key - vk::f1);
        return key_queue_push(
            queue, static_cast<uint32_t>(control ? code - key_code::control_function_offset : code)
        );
    }
    const int32_t navigation = navigation_code(virtual_key);
    if (navigation >= 0) {
        return key_queue_push(queue, static_cast<uint32_t>(navigation));
    }
    if (!system_key && !control) {
        return false;
    }
    int32_t code;
    if (virtual_key >= vk::digit_0 && virtual_key <= vk::digit_9) {
        code = control ? virtual_key + key_code::control_digit_offset : virtual_key;
    } else if (virtual_key >= vk::letter_a && virtual_key <= vk::letter_z) {
        code = control ? virtual_key + key_code::control_letter_offset
                       : virtual_key + lowercase_offset;
    } else if (virtual_key >= vk::oem_first && virtual_key <= vk::oem_last) {
        code = oem_characters[virtual_key - vk::oem_first];
    } else if (virtual_key >= vk::bracket_first && virtual_key <= vk::bracket_last) {
        code = bracket_characters[virtual_key - vk::bracket_first];
    } else {
        return false;
    }
    return key_queue_push(queue, static_cast<uint32_t>(code));
}

void pointer_queue_set_current(PointerQueue* queue, const PointerEvent* event) noexcept {
    queue->current = *event;
}

void pointer_queue_reset(PointerQueue* queue) noexcept {
    queue->head = 0;
    queue->tail = 0;
}

bool pointer_queue_pop(PointerQueue* queue, PointerEvent* out) noexcept {
    if (queue->head == queue->tail) {
        *out = queue->current;
        return false;
    }
    *out = queue->events[queue->tail];
    queue->tail += 1;
    if (queue->tail == queue->capacity) {
        queue->tail = 0;
    }
    return true;
}

bool pointer_queue_peek(const PointerQueue* queue, PointerEvent* out) noexcept {
    if (queue->head == queue->tail) {
        *out = queue->current;
        return false;
    }
    *out = queue->events[queue->tail];
    return true;
}

bool pointer_queue_push(PointerQueue* queue, const PointerEvent* event) noexcept {
    if (queue->capacity < 1 || queue->events == nullptr) {
        return false;
    }
    if ((queue->head + 1) % queue->capacity == queue->tail) {
        return false;
    }
    queue->events[queue->head] = *event;
    queue->head += 1;
    if (queue->head == queue->capacity) {
        queue->head = 0;
    }
    return true;
}

} // namespace oa::ui::services
