// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The raw deflate decoder both readers use: the in-memory reader in one pass
// over a whole entry, the streamed reader a piece at a time through fixed
// buffers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <zlib.h>

namespace oa::formats::zip::detail {

/// Inflates one raw deflate stream (no zlib header or trailer), in as many
/// passes as its caller likes.
class Inflater {
  public:

    Inflater() = default;
    Inflater(const Inflater&) = delete;
    Inflater& operator=(const Inflater&) = delete;

    /// Ends the stream when it was started.
    ~Inflater();

    /// What one pass did.
    struct Pass {
        std::size_t consumed{}; ///< input bytes used
        std::size_t produced{}; ///< output bytes written
        bool ended{};           ///< the stream's last block ended in this pass
        bool failed{};          ///< the data is not deflate, or the decoder failed
    };

    /// Starts a new stream, ending any stream started before.
    ///
    /// @return false when the decoder cannot be made
    [[nodiscard]] bool start() noexcept;

    /// Inflates as much of `input` as fits in `output`. A pass that can
    /// neither use input nor produce output does nothing, and is no failure:
    /// the caller decides whether input ran short or the output is full.
    ///
    /// @param input the deflated bytes not used yet
    /// @param output room for the inflated bytes
    /// @return what the pass did
    [[nodiscard]] Pass pass(std::span<const uint8_t> input, std::span<uint8_t> output) noexcept;

    /// Ends the stream, freeing the decoder's state.
    void finish() noexcept;

  private:

    z_stream stream_{};
    bool started_{};
};

} // namespace oa::formats::zip::detail
