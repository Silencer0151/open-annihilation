// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The frames of the automation protocol, which the automation endpoint
// speaks. A frame is one ASCII header line, a JSON part and a payload:
//
//   AUTO/1 <route> <json-length> <payload-length> <crc32>\n
//   <json-length bytes of UTF-8 JSON><payload-length bytes>
//
// The header is at most max_header_bytes long, line feed included. The
// route is 1 to max_route_bytes characters of [a-z0-9._/-]; the endpoint's
// own frames use endpoint_route both ways. The lengths are decimal without
// leading zeros, at most max_json_bytes and max_payload_bytes. The CRC is
// CRC-32 (IEEE 802.3) over the JSON part and the payload together, as eight
// lowercase hexadecimal digits. The JSON part is one object, or empty; the
// payload is raw bytes the JSON describes.
//
// Frames follow each other without a separator. A reader that meets bytes
// that do not start a well-formed frame skips to the next "AUTO/1 " that
// does (its header parses, its lengths are within the limits, its JSON part
// starts with '{' and its CRC matches) and reports how many bytes it skipped
// and the first problem it met, just before the frame that ends the run, or
// when the stream ends. What it reports depends only on the bytes, never on
// how reads split them. However many bytes it holds, one call looks at no
// more than twice the largest frame its limits take: past that it pauses,
// and the next call goes on where it stopped.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app::automation {

/// The text that starts every frame: the framing's name and version, and a space.
inline constexpr std::string_view frame_magic = "AUTO/1 ";
/// The longest header line, its line feed included, in bytes.
inline constexpr size_t max_header_bytes = 96;
/// The longest route, in bytes.
inline constexpr size_t max_route_bytes = 48;
/// The largest JSON part a frame may announce, in bytes (1 MiB).
inline constexpr size_t max_json_bytes = size_t{1} << 20;
/// The largest payload a frame may announce, in bytes (64 MiB).
inline constexpr size_t max_payload_bytes = size_t{64} << 20;
/// The route of the endpoint's frames, to it and from it.
inline constexpr std::string_view endpoint_route = "-";

/// Why a reader skipped bytes: the first problem of a run of skipped bytes.
enum class SkipReason : uint8_t {
    noise,    ///< text where a header should be
    header,   ///< "AUTO/1 " followed by a malformed header, or a JSON part not starting with '{'
    crc,      ///< a well-formed frame whose CRC does not match its bytes
    too_long, ///< a header announcing a JSON part or payload above the reader's limits
    stalled,  ///< a frame cut off where the stream ended, or given up on
};

/// Returns the name a skip reason has in the protocol.
///
/// @param reason the reason
/// @return "noise", "header", "crc", "too_long" or "stalled"
[[nodiscard]] std::string_view skip_reason_name(SkipReason reason) noexcept;

/// Computes CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320), continuing from an earlier value.
///
/// @param bytes the bytes
/// @param crc the CRC of the bytes before them; 0 to start
/// @return the CRC of everything so far
[[nodiscard]] uint32_t crc32(std::span<const uint8_t> bytes, uint32_t crc = 0) noexcept;

/// Tells whether a text is a valid route: 1 to max_route_bytes characters of [a-z0-9._/-].
///
/// @param route the text
/// @return true when it is one
[[nodiscard]] bool route_valid(std::string_view route) noexcept;

/// Appends one frame to a buffer.
///
/// @param route the frame's route
/// @param json the JSON part, as text
/// @param payload the payload; empty for none
/// @param[in,out] out gains the frame's header, JSON part and payload
/// @return false, with `out` unchanged, when the route is not valid or a part
///         is above the protocol's limits
bool encode_frame(
    std::string_view route,
    std::string_view json,
    std::span<const uint8_t> payload,
    std::vector<uint8_t>& out
);

/// One frame a reader took from the stream.
struct Frame {
    std::string route;
    std::string json;             ///< the JSON part as it came, not yet parsed
    std::vector<uint8_t> payload; ///< empty when the frame has none
};

/// A run of bytes a reader skipped.
struct SkippedBytes {
    SkipReason reason{}; ///< the first problem the run met
    uint64_t count{};    ///< bytes skipped
};

/// What FrameReader::next found.
enum class ReadResult : uint8_t {
    need_more, ///< nothing more until more bytes are fed
    frame,     ///< a frame
    skipped,   ///< a run of skipped bytes, reported before the frame that ended it
    paused,    ///< the call looked at as many bytes as one may; call next again
};

/// Reads frames from a stream of bytes as they arrive.
///
/// Feed it bytes as they come and call next until it answers need_more. It
/// buffers at most a header, the JSON part and the payload its limits allow,
/// and whatever was fed beyond the frame it waits for: a caller that feeds it
/// only while buffered is below largest_frame_bytes, and a read more, keeps
/// it within those bounds.
class FrameReader {
  public:

    /// Starts a reader with the protocol's limits.
    FrameReader() = default;

    /// Starts a reader with limits of its own.
    ///
    /// @param json_limit the largest JSON part to take, at most max_json_bytes
    /// @param payload_limit the largest payload to take, at most max_payload_bytes
    FrameReader(size_t json_limit, size_t payload_limit) noexcept;

    /// Changes the reader's limits for the frames it has not yet taken; the
    /// bytes fed and not yet taken are kept.
    ///
    /// @param json_limit the largest JSON part to take, at most max_json_bytes
    /// @param payload_limit the largest payload to take, at most max_payload_bytes
    void set_limits(size_t json_limit, size_t payload_limit) noexcept;

    /// Takes bytes from the stream.
    ///
    /// @param bytes the bytes, in the order they arrived
    void feed(std::span<const uint8_t> bytes);

    /// Takes the next frame, or the run of skipped bytes before it.
    ///
    /// Looks at no more than twice largest_frame_bytes of the bytes fed,
    /// however many they are: once it has looked at largest_frame_bytes it
    /// answers paused, and the next call goes on from there.
    ///
    /// @param[out] frame the frame, when the result is frame
    /// @param[out] skipped the run, when the result is skipped
    /// @return what was found
    ReadResult next(Frame& frame, SkippedBytes& skipped);

    /// Reports what is left at the end of the stream, once next answers need_more.
    ///
    /// @param[out] skipped the run of bytes that never made a frame, a frame
    ///        cut off among them as stalled
    /// @return skipped when bytes were left over or skipped; need_more otherwise
    ReadResult finish(SkippedBytes& skipped);

    /// Gives up on the frame now being read, as damaged, and looks for the next one.
    void abandon();

    /// Returns the bytes fed and not yet taken as a frame or skipped.
    ///
    /// @return the count
    [[nodiscard]] size_t buffered() const noexcept;

    /// Returns the bytes of the largest frame the reader takes: a header of
    /// max_header_bytes, and a JSON part and a payload at its limits.
    ///
    /// @return the count
    [[nodiscard]] size_t largest_frame_bytes() const noexcept;

    /// Tells whether the reader is in a run of skipped bytes it has not reported yet.
    ///
    /// @return true once it skipped a byte since the last frame or run it gave
    [[nodiscard]] bool skipping() const noexcept;

  private:

    /// Skips bytes from the front of the buffered ones.
    ///
    /// @param count bytes to skip
    /// @param reason why; kept when the run has none yet
    void skip(size_t count, SkipReason reason) noexcept;

    std::vector<uint8_t> buffer_;
    size_t position_{};                       ///< the first buffered byte not yet taken or skipped
    uint64_t skipped_{};                      ///< the current run of skipped bytes
    SkipReason skip_reason_{};                ///< the run's first problem
    size_t json_limit_{max_json_bytes};       ///< bytes
    size_t payload_limit_{max_payload_bytes}; ///< bytes
    std::optional<Frame> pending_; ///< a frame found behind a reported run of skipped bytes
};

} // namespace oa::app::automation
