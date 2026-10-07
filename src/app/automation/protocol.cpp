// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The automation protocol's frames: the header line, encoding, and the
// reader that takes frames from a stream (protocol.hpp).
#include "oa/app/automation/protocol.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace oa::app::automation {
namespace {

// Hexadecimal digits of a CRC in a header.
constexpr size_t crc_digits = 8;
// The most decimal digits a length may have: those of the largest 64-bit value.
constexpr size_t max_length_digits = 20;
// Buffered bytes already taken that are dropped before more are fed.
constexpr size_t compact_after_bytes = 64 * 1024;

/// Tells whether a character may appear in a route.
///
/// @param c the character
/// @return true for [a-z0-9._/-]
bool route_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '/' ||
           c == '-';
}

/// Reads a length of a header: decimal digits without a leading zero, or "0".
///
/// @param text the digits
/// @param[out] value the length; unchanged on failure
/// @return false when the text is no such number or does not fit in 64 bits
bool parse_length(std::string_view text, uint64_t& value) {
    if (text.empty() || text.size() > max_length_digits || (text.size() > 1 && text[0] == '0'))
        return false;
    uint64_t result = 0;
    for (const char c : text) {
        if (c < '0' || c > '9')
            return false;
        const auto digit = static_cast<uint64_t>(c - '0');
        if (result > (std::numeric_limits<uint64_t>::max() - digit) / 10)
            return false;
        result = result * 10 + digit;
    }
    value = result;
    return true;
}

// The fields of a header line.
struct Header {
    std::string_view route;
    uint64_t json_length{};    ///< bytes
    uint64_t payload_length{}; ///< bytes
    uint32_t crc{};
};

/// Reads a header line, magic included and line feed left out.
///
/// @param line the line
/// @param[out] header its fields; partly written on failure
/// @return true when the line is a well-formed header
bool parse_header(std::string_view line, Header& header) {
    std::string_view rest = line.substr(frame_magic.size());
    const auto field = [&rest](std::string_view& out) {
        const size_t space = rest.find(' ');
        if (space == std::string_view::npos)
            return false;
        out = rest.substr(0, space);
        rest.remove_prefix(space + 1);
        return true;
    };
    std::string_view json_text;
    std::string_view payload_text;
    if (!field(header.route) || !route_valid(header.route) || !field(json_text) ||
        !parse_length(json_text, header.json_length) || !field(payload_text) ||
        !parse_length(payload_text, header.payload_length) || rest.size() != crc_digits)
        return false;
    uint32_t crc = 0;
    for (const char c : rest) {
        uint32_t digit = 0;
        if (c >= '0' && c <= '9')
            digit = static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<uint32_t>(c - 'a' + 10);
        else
            return false;
        crc = crc << 4 | digit;
    }
    header.crc = crc;
    return true;
}

/// Finds where the magic starts, or could start before the bytes end, after the first byte.
///
/// @param bytes the bytes
/// @return the offset, from 1 on; 0 when there is none
size_t find_magic(std::span<const uint8_t> bytes) {
    for (size_t at = 1; at < bytes.size(); ++at) {
        const auto* found = static_cast<const uint8_t*>(
            std::memchr(bytes.data() + at, frame_magic[0], bytes.size() - at)
        );
        if (found == nullptr)
            return 0;
        at = static_cast<size_t>(found - bytes.data());
        const size_t compared = std::min(bytes.size() - at, frame_magic.size());
        if (std::memcmp(bytes.data() + at, frame_magic.data(), compared) == 0)
            return at;
    }
    return 0;
}

} // namespace

std::string_view skip_reason_name(SkipReason reason) noexcept {
    switch (reason) {
    case SkipReason::noise:
        return "noise";
    case SkipReason::header:
        return "header";
    case SkipReason::crc:
        return "crc";
    case SkipReason::too_long:
        return "too_long";
    case SkipReason::stalled:
        return "stalled";
    }
    return "noise";
}

uint32_t crc32(std::span<const uint8_t> bytes, uint32_t crc) noexcept {
    // zlib takes a 32-bit length; a frame's parts are far shorter, but the
    // span is taken in pieces all the same.
    constexpr size_t max_piece = std::numeric_limits<uInt>::max();
    uLong value = crc;
    while (!bytes.empty()) {
        const size_t piece = std::min(bytes.size(), max_piece);
        value = ::crc32(value, bytes.data(), static_cast<uInt>(piece));
        bytes = bytes.subspan(piece);
    }
    return static_cast<uint32_t>(value);
}

bool route_valid(std::string_view route) noexcept {
    return !route.empty() && route.size() <= max_route_bytes &&
           std::all_of(route.begin(), route.end(), route_char);
}

bool encode_frame(
    std::string_view route,
    std::string_view json,
    std::span<const uint8_t> payload,
    std::vector<uint8_t>& out
) {
    if (!route_valid(route) || json.size() > max_json_bytes || payload.size() > max_payload_bytes)
        return false;
    const std::span<const uint8_t> json_bytes{
        reinterpret_cast<const uint8_t*>(json.data()), json.size()
    };
    const uint32_t crc = crc32(payload, crc32(json_bytes));
    static constexpr char hex[] = "0123456789abcdef";
    std::string header(frame_magic);
    header += route;
    header += ' ';
    header += std::to_string(json.size());
    header += ' ';
    header += std::to_string(payload.size());
    header += ' ';
    for (size_t digit = 0; digit < crc_digits; ++digit)
        header += hex[(crc >> (4 * (crc_digits - 1 - digit))) & 0xFU];
    header += '\n';
    out.reserve(out.size() + header.size() + json.size() + payload.size());
    out.insert(out.end(), header.begin(), header.end());
    out.insert(out.end(), json_bytes.begin(), json_bytes.end());
    out.insert(out.end(), payload.begin(), payload.end());
    return true;
}

FrameReader::FrameReader(size_t json_limit, size_t payload_limit) noexcept
    : json_limit_(std::min(json_limit, max_json_bytes)),
      payload_limit_(std::min(payload_limit, max_payload_bytes)) {
}

void FrameReader::set_limits(size_t json_limit, size_t payload_limit) noexcept {
    json_limit_ = std::min(json_limit, max_json_bytes);
    payload_limit_ = std::min(payload_limit, max_payload_bytes);
}

void FrameReader::feed(std::span<const uint8_t> bytes) {
    if (position_ == buffer_.size()) {
        buffer_.clear();
        position_ = 0;
    } else if (position_ >= compact_after_bytes) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(position_));
        position_ = 0;
    }
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

void FrameReader::skip(size_t count, SkipReason reason) noexcept {
    if (skipped_ == 0)
        skip_reason_ = reason;
    skipped_ += count;
    position_ += count;
}

ReadResult FrameReader::next(Frame& frame, SkippedBytes& skipped) {
    if (pending_) {
        frame = std::move(*pending_);
        pending_.reset();
        return ReadResult::frame;
    }
    // The bytes this call has looked at. Each step looks at no more than
    // the largest frame, so a call that pauses once it has looked at that
    // many looks at no more than twice as many, whatever the bytes hold:
    // skipping a damaged frame retries from its next byte, and a stream of
    // headers that each announce the same bytes would otherwise have them
    // checked again for every one.
    size_t looked_at = 0;
    const size_t looked_at_most = largest_frame_bytes();
    for (;;) {
        const std::span<const uint8_t> bytes{
            buffer_.data() + position_, buffer_.size() - position_
        };
        if (bytes.empty())
            return ReadResult::need_more;
        if (looked_at >= looked_at_most)
            return ReadResult::paused;
        const size_t compared = std::min(bytes.size(), frame_magic.size());
        if (std::memcmp(bytes.data(), frame_magic.data(), compared) != 0) {
            const size_t magic = find_magic(bytes.first(std::min(bytes.size(), looked_at_most)));
            const size_t noise = magic != 0 ? magic : std::min(bytes.size(), looked_at_most);
            looked_at += noise;
            skip(noise, SkipReason::noise);
            continue;
        }
        if (compared < frame_magic.size())
            return ReadResult::need_more;
        const size_t searched = std::min(bytes.size(), max_header_bytes);
        looked_at += searched;
        const auto* line_end =
            static_cast<const uint8_t*>(std::memchr(bytes.data(), '\n', searched));
        if (line_end == nullptr) {
            if (bytes.size() >= max_header_bytes) {
                skip(1, SkipReason::header);
                continue;
            }
            return ReadResult::need_more;
        }
        const auto header_length = static_cast<size_t>(line_end - bytes.data()) + 1;
        Header header;
        if (!parse_header(
                {reinterpret_cast<const char*>(bytes.data()), header_length - 1}, header
            )) {
            skip(1, SkipReason::header);
            continue;
        }
        if (header.json_length > json_limit_ || header.payload_length > payload_limit_) {
            skip(1, SkipReason::too_long);
            continue;
        }
        const auto json_length = static_cast<size_t>(header.json_length);
        const auto payload_length = static_cast<size_t>(header.payload_length);
        // The JSON part is not where the header says: a damaged length.
        if (json_length > 0 && bytes.size() > header_length && bytes[header_length] != '{') {
            skip(1, SkipReason::header);
            continue;
        }
        const size_t total = header_length + json_length + payload_length;
        if (bytes.size() < total)
            return ReadResult::need_more;
        const auto body = bytes.subspan(header_length, json_length + payload_length);
        looked_at += body.size();
        if (crc32(body) != header.crc) {
            skip(1, SkipReason::crc);
            continue;
        }
        Frame found;
        found.route = std::string(header.route);
        found.json.assign(reinterpret_cast<const char*>(body.data()), json_length);
        found.payload.assign(body.begin() + static_cast<std::ptrdiff_t>(json_length), body.end());
        position_ += total;
        if (skipped_ != 0) {
            skipped = {skip_reason_, skipped_};
            skipped_ = 0;
            pending_ = std::move(found);
            return ReadResult::skipped;
        }
        frame = std::move(found);
        return ReadResult::frame;
    }
}

ReadResult FrameReader::finish(SkippedBytes& skipped) {
    const size_t left = buffer_.size() - position_;
    if (left != 0)
        skip(left, SkipReason::stalled);
    if (skipped_ == 0)
        return ReadResult::need_more;
    skipped = {skip_reason_, skipped_};
    skipped_ = 0;
    return ReadResult::skipped;
}

void FrameReader::abandon() {
    if (!pending_ && position_ < buffer_.size())
        skip(1, SkipReason::stalled);
}

size_t FrameReader::buffered() const noexcept {
    return buffer_.size() - position_;
}

size_t FrameReader::largest_frame_bytes() const noexcept {
    return max_header_bytes + json_limit_ + payload_limit_;
}

bool FrameReader::skipping() const noexcept {
    return skipped_ != 0;
}

} // namespace oa::app::automation
