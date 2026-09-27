// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The engine's block allocator. Blocks carry their requested size and an
// optional static tag so callers can ask for a block's size and diagnostics
// can attribute live allocations. The host heap keeps no guard fills or page
// protection.

#include <cstddef>
#include <cstdint>

namespace oa::platform {

// Largest single request, 2 GiB; anything larger is refused.
inline constexpr std::size_t max_block_size = 0x80000000u;

struct HeapStats {
    uint64_t live_blocks{};
    uint64_t live_bytes{};
    uint64_t peak_bytes{};
    uint64_t total_allocations{};
};

/// Allocates an uninitialised block.
///
/// @param size bytes requested, at most max_block_size
/// @param tag diagnostic tag; must outlive the block (a string literal)
/// @return the block, or null when size exceeds max_block_size or the host is out of memory
[[nodiscard]] void* heap_alloc(std::size_t size, const char* tag = nullptr) noexcept;
/// Allocates a zero-filled block of count * size bytes.
///
/// @param count number of elements
/// @param size bytes per element
/// @return the block, or null on overflow or when out of memory
[[nodiscard]] void* heap_alloc_zeroed(std::size_t count, std::size_t size) noexcept;
/// Resizes a block, preserving the common prefix.
///
/// @param block block to resize, or null to allocate
/// @param size new size in bytes
/// @return the resized block, or null on failure with the old block left untouched
[[nodiscard]] void* heap_resize(void* block, std::size_t size) noexcept;
/// Frees a block.
///
/// @param block block from heap_alloc; null is ignored
void heap_free(void* block) noexcept;
/// Returns the requested size of a live block.
///
/// @param block live block, or null
/// @return its size in bytes; 0 for null
[[nodiscard]] std::size_t heap_block_size(const void* block) noexcept;
/// Returns the diagnostic tag of a live block.
///
/// @param block live block, or null
/// @return the tag given at allocation, or null
[[nodiscard]] const char* heap_block_tag(const void* block) noexcept;
/// Copies a NUL-terminated string onto the heap.
///
/// @param text string to copy, or null
/// @return the copy, or null for a null input or when out of memory
[[nodiscard]] char* heap_duplicate_string(const char* text) noexcept;
/// Returns the heap's live, peak and total counters.
///
/// @return a snapshot of the counters
[[nodiscard]] HeapStats heap_stats() noexcept;

} // namespace oa::platform
