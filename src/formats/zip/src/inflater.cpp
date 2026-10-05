// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The raw deflate decoder both readers use.

#include "inflater.hpp"

#include <algorithm>
#include <climits>

namespace oa::formats::zip::detail {

namespace {

/// The most bytes handed to the decoder at once, whose counts are unsigned
/// ints.
constexpr std::size_t most_pass_bytes = std::size_t{1} << 30;
static_assert(most_pass_bytes <= UINT_MAX);

} // namespace

Inflater::~Inflater() {
    finish();
}

bool Inflater::start() noexcept {
    finish();
    stream_ = z_stream{};
    if (inflateInit2(&stream_, -MAX_WBITS) != Z_OK)
        return false;
    started_ = true;
    return true;
}

Inflater::Pass Inflater::pass(std::span<const uint8_t> input, std::span<uint8_t> output) noexcept {
    Pass done{};
    if (!started_) {
        done.failed = true;
        return done;
    }
    // zlib refuses a null output pointer even when there is no room to fill.
    uint8_t no_output{};
    const std::size_t in_bytes = std::min(input.size(), most_pass_bytes);
    const std::size_t out_bytes = std::min(output.size(), most_pass_bytes);
    stream_.next_in = const_cast<Bytef*>(input.data());
    stream_.avail_in = static_cast<uInt>(in_bytes);
    stream_.next_out = output.empty() ? &no_output : output.data();
    stream_.avail_out = static_cast<uInt>(out_bytes);
    const int result = inflate(&stream_, Z_NO_FLUSH);
    done.consumed = in_bytes - stream_.avail_in;
    done.produced = out_bytes - stream_.avail_out;
    done.ended = result == Z_STREAM_END;
    done.failed = result != Z_OK && result != Z_STREAM_END && result != Z_BUF_ERROR;
    return done;
}

void Inflater::finish() noexcept {
    if (started_)
        inflateEnd(&stream_);
    started_ = false;
}

} // namespace oa::formats::zip::detail
