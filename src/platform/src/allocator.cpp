// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/allocator.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace oa::platform {
namespace {

struct alignas(std::max_align_t) BlockHeader {
    std::size_t size{};
    const char* tag{};
};

std::atomic<uint64_t> live_blocks{0};
std::atomic<uint64_t> live_bytes{0};
std::atomic<uint64_t> peak_bytes{0};
std::atomic<uint64_t> total_allocations{0};

BlockHeader* header_of(const void* block) noexcept {
    return const_cast<BlockHeader*>(static_cast<const BlockHeader*>(block) - 1);
}

void note_alloc(std::size_t size) noexcept {
    live_blocks.fetch_add(1);
    total_allocations.fetch_add(1);
    const uint64_t now = live_bytes.fetch_add(size) + size;
    uint64_t peak = peak_bytes.load();
    while (now > peak && !peak_bytes.compare_exchange_weak(peak, now)) {
    }
}

void note_free(std::size_t size) noexcept {
    live_blocks.fetch_sub(1);
    live_bytes.fetch_sub(size);
}

} // namespace

void* heap_alloc(std::size_t size, const char* tag) noexcept {
    if (size > max_block_size) {
        return nullptr;
    }
    auto* header = static_cast<BlockHeader*>(std::malloc(sizeof(BlockHeader) + size));
    if (!header) {
        return nullptr;
    }
    header->size = size;
    header->tag = tag;
    note_alloc(size);
    return header + 1;
}

void* heap_alloc_zeroed(std::size_t count, std::size_t size) noexcept {
    if (size != 0 && count > max_block_size / size) {
        return nullptr;
    }
    void* block = heap_alloc(count * size);
    if (block) {
        std::memset(block, 0, count * size);
    }
    return block;
}

void* heap_resize(void* block, std::size_t size) noexcept {
    if (!block) {
        return heap_alloc(size);
    }
    const BlockHeader* old_header = header_of(block);
    void* resized = heap_alloc(size, old_header->tag);
    if (!resized) {
        return nullptr;
    }
    std::memcpy(resized, block, old_header->size < size ? old_header->size : size);
    heap_free(block);
    return resized;
}

void heap_free(void* block) noexcept {
    if (!block) {
        return;
    }
    BlockHeader* header = header_of(block);
    note_free(header->size);
    std::free(header);
}

std::size_t heap_block_size(const void* block) noexcept {
    return block ? header_of(block)->size : 0;
}

const char* heap_block_tag(const void* block) noexcept {
    return block ? header_of(block)->tag : nullptr;
}

char* heap_duplicate_string(const char* text) noexcept {
    if (!text) {
        return nullptr;
    }
    const std::size_t length = std::strlen(text) + 1;
    auto* copy = static_cast<char*>(heap_alloc(length));
    if (copy) {
        std::memcpy(copy, text, length);
    }
    return copy;
}

HeapStats heap_stats() noexcept {
    return {live_blocks.load(), live_bytes.load(), peak_bytes.load(), total_allocations.load()};
}

} // namespace oa::platform
