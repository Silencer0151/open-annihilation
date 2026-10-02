// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Reading little-endian fields from a byte span without ever reading past
// it, and the one result type decoders return: a value, or what was wrong
// with the input and where. Nothing here allocates or throws.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace oa::base::bytes {

/// What was wrong with the bytes a decoder was given.
enum class DecodeCode : uint8_t {
    none,                ///< nothing; the decode succeeded
    truncated,           ///< the input ends before a field or record it needs
    out_of_range,        ///< an offset, index or value lies outside what it may name
    limit_exceeded,      ///< a count or size is over the decoder's named limit
    bad_signature,       ///< the input does not start with the format's marker
    unsupported_version, ///< the format's version is not one the decoder reads
    malformed,           ///< the input breaks the format's rules in another way
    cycle,               ///< links between records loop back on themselves
    not_found,           ///< a named entry the caller asked for is absent
};

/// What was wrong with a decoder's input, and where.
struct DecodeError {
    DecodeCode code{};
    uint64_t offset{};     ///< byte offset in the input at which the problem lies
    const char* message{}; ///< static text saying what was wrong, never freed
    uint16_t detail{};     ///< a decoder's own narrower code, 0 when it has none
};

/// A decoder's result: the decoded value, or the error that stopped it.
///
/// Converts implicitly from a value and from a DecodeError, so a decoder
/// returns either as it is.
template <class T>
struct Decoded {
    std::optional<T> value{};
    DecodeError error{};

    /// Holds a decoded value.
    ///
    /// @param decoded the value
    Decoded(T decoded) : value(std::move(decoded)) {}

    /// Holds the error that stopped the decode.
    ///
    /// @param failure the error; its code is not none
    Decoded(DecodeError failure) noexcept : error(failure) {}

    /// Returns whether the decode produced a value.
    ///
    /// @return true when value holds one
    [[nodiscard]] bool ok() const noexcept { return value.has_value(); }
};

/// Returns a short name of a decode code, for messages.
///
/// @param code the code
/// @return a static lower-case phrase, such as "truncated"
[[nodiscard]] std::string_view decode_code_name(DecodeCode code) noexcept;

/// Reads a 16-bit little-endian value.
///
/// For loops over spans already checked; the caller guarantees two bytes.
///
/// @param bytes the first of two bytes
/// @return the value
[[nodiscard]] constexpr uint16_t load_le16(const uint8_t* bytes) noexcept {
    return static_cast<uint16_t>(bytes[0] | (static_cast<uint32_t>(bytes[1]) << 8U));
}

/// Reads a 32-bit little-endian value.
///
/// For loops over spans already checked; the caller guarantees four bytes.
///
/// @param bytes the first of four bytes
/// @return the value
[[nodiscard]] constexpr uint32_t load_le32(const uint8_t* bytes) noexcept {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) | (static_cast<uint32_t>(bytes[3]) << 24U);
}

/// Writes a 16-bit value little-endian.
///
/// @param[out] bytes the first of two bytes written
/// @param value the value
constexpr void store_le16(uint8_t* bytes, uint16_t value) noexcept {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8U);
}

/// Writes a 32-bit value little-endian.
///
/// @param[out] bytes the first of four bytes written
/// @param value the value
constexpr void store_le32(uint8_t* bytes, uint32_t value) noexcept {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8U);
    bytes[2] = static_cast<uint8_t>(value >> 16U);
    bytes[3] = static_cast<uint8_t>(value >> 24U);
}

/// A cursor over a byte span that never reads outside it.
///
/// Reads are little-endian, at the cursor (which they advance) or at an
/// absolute offset (which leaves it). A read that does not fit fails: it
/// returns zero or an empty span, and the reader records a truncated error
/// at the offset of the read. The first failure is kept, and every later
/// read fails too, so a decoder reads a whole record and checks ok() once.
class ByteReader {
  public:

    /// Reads nothing; every read fails.
    constexpr ByteReader() noexcept = default;

    /// Reads `bytes`, with the cursor at its start.
    ///
    /// @param bytes the input; it must outlive the reader
    /// @param base_offset offset of bytes[0] in the whole input, added to
    ///        the offsets the reader reports
    constexpr explicit ByteReader(std::span<const uint8_t> bytes, uint64_t base_offset = 0) noexcept
        : data_(bytes.data()), size_(bytes.size()), base_(base_offset) {}

    /// Reads `bytes`, with the cursor at its start.
    ///
    /// @param bytes the input; it must outlive the reader
    /// @param base_offset offset of bytes[0] in the whole input
    explicit ByteReader(std::span<const std::byte> bytes, uint64_t base_offset = 0) noexcept
        : data_(reinterpret_cast<const uint8_t*>(bytes.data())), size_(bytes.size()),
          base_(base_offset) {}

    /// Returns the size of the input.
    ///
    /// @return the size in bytes
    [[nodiscard]] constexpr uint64_t size() const noexcept { return size_; }

    /// Returns the cursor's offset in the input.
    ///
    /// @return the offset, from the start of this reader's span
    [[nodiscard]] constexpr uint64_t position() const noexcept { return position_; }

    /// Returns how many bytes lie after the cursor.
    ///
    /// @return the count
    [[nodiscard]] constexpr uint64_t remaining() const noexcept { return size_ - position_; }

    /// Returns whether no read has failed.
    ///
    /// @return true until the first failure
    [[nodiscard]] constexpr bool ok() const noexcept { return error_.code == DecodeCode::none; }

    /// Returns the first failure.
    ///
    /// @return the error, with code none while every read has succeeded
    [[nodiscard]] constexpr const DecodeError& error() const noexcept { return error_; }

    /// Returns whether `length` bytes at `offset` lie inside the input.
    ///
    /// Records nothing.
    ///
    /// @param offset offset from the start of this reader's span
    /// @param length byte count
    /// @return true when the range fits
    [[nodiscard]] constexpr bool fits(uint64_t offset, uint64_t length) const noexcept {
        return offset <= size_ && length <= size_ - offset;
    }

    /// Records a failure, unless one is already recorded.
    ///
    /// @param code what was wrong
    /// @param offset offset from the start of this reader's span
    /// @param message static text saying what was wrong
    /// @param detail the decoder's own narrower code, or 0
    /// @return false, so a decoder can return it
    constexpr bool
    fail(DecodeCode code, uint64_t offset, const char* message, uint16_t detail = 0) noexcept {
        if (ok())
            error_ = {code, base_ + offset, message, detail};
        return false;
    }

    /// Moves the cursor to an offset.
    ///
    /// @param offset new cursor offset; the end of the input is allowed
    /// @return true when the offset lies inside the input
    constexpr bool seek(uint64_t offset) noexcept {
        if (!ok() || offset > size_)
            return fail(DecodeCode::truncated, offset, "offset lies past the end of the input");
        position_ = offset;
        return true;
    }

    /// Advances the cursor.
    ///
    /// @param count bytes skipped
    /// @return true when they lie inside the input
    constexpr bool skip(uint64_t count) noexcept {
        if (!ok() || count > remaining())
            return fail(DecodeCode::truncated, position_, "input ends inside a skipped range");
        position_ += count;
        return true;
    }

    /// Reads an 8-bit value at the cursor.
    ///
    /// @return the value, or 0 on failure
    constexpr uint8_t u8() noexcept { return static_cast<uint8_t>(advance(1)); }

    /// Reads a 16-bit little-endian value at the cursor.
    ///
    /// @return the value, or 0 on failure
    constexpr uint16_t u16() noexcept { return static_cast<uint16_t>(advance(2)); }

    /// Reads a 32-bit little-endian value at the cursor.
    ///
    /// @return the value, or 0 on failure
    constexpr uint32_t u32() noexcept { return static_cast<uint32_t>(advance(4)); }

    /// Reads a 16-bit little-endian two's-complement value at the cursor.
    ///
    /// @return the value, or 0 on failure
    constexpr int16_t i16() noexcept { return static_cast<int16_t>(u16()); }

    /// Reads a 32-bit little-endian two's-complement value at the cursor.
    ///
    /// @return the value, or 0 on failure
    constexpr int32_t i32() noexcept { return static_cast<int32_t>(u32()); }

    /// Reads an 8-bit value at an offset, leaving the cursor.
    ///
    /// @param offset offset from the start of this reader's span
    /// @return the value, or 0 on failure
    constexpr uint8_t u8_at(uint64_t offset) noexcept {
        return static_cast<uint8_t>(load(offset, 1));
    }

    /// Reads a 16-bit little-endian value at an offset, leaving the cursor.
    ///
    /// @param offset offset from the start of this reader's span
    /// @return the value, or 0 on failure
    constexpr uint16_t u16_at(uint64_t offset) noexcept {
        return static_cast<uint16_t>(load(offset, 2));
    }

    /// Reads a 32-bit little-endian value at an offset, leaving the cursor.
    ///
    /// @param offset offset from the start of this reader's span
    /// @return the value, or 0 on failure
    constexpr uint32_t u32_at(uint64_t offset) noexcept {
        return static_cast<uint32_t>(load(offset, 4));
    }

    /// Reads a 16-bit little-endian two's-complement value at an offset.
    ///
    /// @param offset offset from the start of this reader's span
    /// @return the value, or 0 on failure
    constexpr int16_t i16_at(uint64_t offset) noexcept {
        return static_cast<int16_t>(u16_at(offset));
    }

    /// Reads a 32-bit little-endian two's-complement value at an offset.
    ///
    /// @param offset offset from the start of this reader's span
    /// @return the value, or 0 on failure
    constexpr int32_t i32_at(uint64_t offset) noexcept {
        return static_cast<int32_t>(u32_at(offset));
    }

    /// Takes `count` bytes at the cursor and advances past them.
    ///
    /// @param count byte count
    /// @return the bytes, or an empty span on failure
    constexpr std::span<const uint8_t> bytes(uint64_t count) noexcept {
        if (!ok() || count > remaining()) {
            fail(DecodeCode::truncated, position_, "input ends inside a byte range");
            return {};
        }
        const std::span<const uint8_t> taken(data_ + position_, static_cast<std::size_t>(count));
        position_ += count;
        return taken;
    }

    /// Takes `count` bytes at an offset, leaving the cursor.
    ///
    /// @param offset offset from the start of this reader's span
    /// @param count byte count
    /// @return the bytes, or an empty span on failure
    constexpr std::span<const uint8_t> bytes_at(uint64_t offset, uint64_t count) noexcept {
        if (!ok() || !fits(offset, count)) {
            fail(DecodeCode::truncated, offset, "byte range lies past the end of the input");
            return {};
        }
        return {data_ + offset, static_cast<std::size_t>(count)};
    }

    /// Returns a reader over `length` bytes at `offset`, its cursor at their start.
    ///
    /// The new reader reports offsets in this reader's whole input. When the
    /// range does not fit, this reader records the failure and the new one
    /// fails every read.
    ///
    /// @param offset offset from the start of this reader's span
    /// @param length byte count
    /// @return the reader
    constexpr ByteReader sub(uint64_t offset, uint64_t length) noexcept {
        const auto range = bytes_at(offset, length);
        if (!ok()) {
            ByteReader failed;
            failed.error_ = error_;
            return failed;
        }
        return ByteReader(range, base_ + offset);
    }

    /// Reads a NUL-terminated string at an offset, leaving the cursor.
    ///
    /// @param offset offset from the start of this reader's span
    /// @param max_bytes most bytes the string may hold, its NUL not counted
    /// @return the text without its NUL, or an empty view on failure: a
    ///         truncated error when the input ends first, limit_exceeded
    ///         when no NUL lies within max_bytes
    constexpr std::string_view c_string_at(uint64_t offset, uint64_t max_bytes) noexcept {
        if (!ok() || offset >= size_) {
            fail(DecodeCode::truncated, offset, "string lies past the end of the input");
            return {};
        }
        const uint64_t available = size_ - offset;
        const uint64_t scanned = available < max_bytes + 1 ? available : max_bytes + 1;
        const auto* text = reinterpret_cast<const char*>(data_ + offset);
        for (uint64_t i = 0; i < scanned; ++i) {
            if (text[i] == '\0')
                return {text, static_cast<std::size_t>(i)};
        }
        if (scanned == available)
            fail(DecodeCode::truncated, offset, "string is not NUL terminated");
        else
            fail(DecodeCode::limit_exceeded, offset, "string exceeds its length limit");
        return {};
    }

  private:

    /// Reads `width` bytes little-endian at the cursor and advances past them.
    ///
    /// @param width byte count, at most 8
    /// @return the value, or 0 on failure
    constexpr uint64_t advance(unsigned width) noexcept {
        const auto value = load(position_, width);
        if (ok())
            position_ += width;
        return value;
    }

    /// Reads `width` bytes little-endian at an offset.
    ///
    /// @param offset offset from the start of this reader's span
    /// @param width byte count, at most 8
    /// @return the value, or 0 on failure
    constexpr uint64_t load(uint64_t offset, unsigned width) noexcept {
        if (!ok() || !fits(offset, width)) {
            fail(DecodeCode::truncated, offset, "input ends inside a field");
            return 0;
        }
        uint64_t value = 0;
        for (unsigned byte = 0; byte < width; ++byte)
            value |= static_cast<uint64_t>(data_[offset + byte]) << (8U * byte);
        return value;
    }

    const uint8_t* data_{};
    uint64_t size_{};
    uint64_t position_{};
    uint64_t base_{};
    DecodeError error_{};
};

} // namespace oa::base::bytes
