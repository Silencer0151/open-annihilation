// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <bit>
#include <cstdint>
#include <optional>

namespace oa::base::game_loop::detail {
// Finite binary arithmetic with a 53- or 64-bit significand, nearest-even rounding.
// This gives the timing expression the precision Timing::precision selects,
// even on hosts where long double has only 53 significant bits. No host FP
// rounding mode or nonstandard 128-bit integer type is required.
struct Wide {
    uint64_t hi{}, lo{};

    /// Multiplies two 64-bit values exactly.
    ///
    /// @param a one factor
    /// @param b the other factor
    /// @return the 128-bit product
    static Wide multiply(uint64_t a, uint64_t b) noexcept {
        constexpr auto mask = uint64_t{0xffffffff};
        const auto a0 = a & mask, a1 = a >> 32, b0 = b & mask, b1 = b >> 32;
        const auto w0 = a0 * b0;
        const auto t = a1 * b0 + (w0 >> 32);
        const auto w1 = (t & mask) + a0 * b1;
        return {a1 * b1 + (t >> 32) + (w1 >> 32), (w1 << 32) | (w0 & mask)};
    }

    /// Adds another 128-bit value, wrapping at 128 bits.
    ///
    /// @param b the value to add
    /// @return the sum
    Wide add(Wide b) const noexcept {
        const auto low = lo + b.lo;
        return {hi + b.hi + (low < lo), low};
    }

    /// Subtracts another 128-bit value, wrapping at 128 bits.
    ///
    /// @param b the value to subtract
    /// @return the difference
    Wide subtract(Wide b) const noexcept { return {hi - b.hi - (lo < b.lo), lo - b.lo}; }

    /// Shifts right, folding every bit shifted out into the lowest bit.
    ///
    /// The lowest bit of the result is set when it was set or when any bit
    /// shifted out was set, so a later rounding still sees an inexact value.
    ///
    /// @param n bits to shift by; 128 or more leaves only the folded bit
    /// @return the shifted value
    Wide right_jam(unsigned n) const noexcept {
        if (n == 0)
            return *this;
        if (n < 64)
            return {hi >> n, (lo >> n) | (hi << (64 - n)) | ((lo << (64 - n)) != 0)};
        if (n == 64)
            return {0, hi | (lo != 0)};
        if (n < 128)
            return {0, (hi >> (n - 64)) | (((hi << (128 - n)) | lo) != 0)};
        return {0, (hi | lo) != 0};
    }
};

// A finite value at the working precision. A nonfinite input or an
// unsupported precision gives a value that is not finite: arithmetic carries
// it, and the conversions to binary64 and binary32 refuse it.
class Extended {
    unsigned precision_ = 64;
    bool finite_ = true; // false for a nonfinite input or an unsupported precision
    bool negative_{};
    uint64_t mantissa_{}; // normalized: top bit set except zero
    int exponent_{};      // value = (+/-) mantissa * 2^exponent

    /// Builds a finite value, shifting the mantissa until its top bit is set.
    ///
    /// @param sign true for a negative value
    /// @param mantissa the significand, zero for a zero
    /// @param exponent power of two the mantissa is scaled by
    /// @param precision significand bits of the working precision
    /// @return the value
    static Extended
    normalized(bool sign, uint64_t mantissa, int exponent, unsigned precision = 64) noexcept {
        Extended result;
        result.precision_ = precision;
        result.negative_ = sign;
        if (mantissa) {
            const int shift = std::countl_zero(mantissa);
            result.mantissa_ = mantissa << shift;
            result.exponent_ = exponent - shift;
        }
        return result;
    }

    /// Shifts right, rounding to nearest with ties to even.
    ///
    /// @param value the value to shift
    /// @param shift bits to shift by; more than 64 gives zero
    /// @return the rounded quotient
    static uint64_t rounded_shift(uint64_t value, unsigned shift) noexcept {
        if (shift == 0)
            return value;
        if (shift > 64)
            return 0;
        if (shift == 64)
            return value > 0x8000000000000000ULL ? 1 : 0;
        const auto whole = value >> shift;
        const auto remainder = value & ((uint64_t{1} << shift) - 1);
        const auto half = uint64_t{1} << (shift - 1);
        return whole + (remainder > half || (remainder == half && (whole & 1)));
    }

    /// Rounds a 128-bit significand to the working precision, ties to even.
    ///
    /// @param sign true for a negative value
    /// @param value the significand, not zero
    /// @param exponent power of two the significand is scaled by
    /// @param precision significand bits to keep
    /// @return the rounded value
    static Extended from_wide(bool sign, Wide value, int exponent, unsigned precision) noexcept {
        const int top =
            value.hi ? 127 - std::countl_zero(value.hi) : 63 - std::countl_zero(value.lo);
        const int shift = top - static_cast<int>(precision) + 1;
        if (shift <= 0)
            return normalized(sign, value.lo, exponent, precision);
        uint64_t mantissa;
        bool increment;
        if (shift > 64) {
            const unsigned high_shift = static_cast<unsigned>(shift - 64);
            mantissa = value.hi >> high_shift;
            const auto remainder = value.hi & ((uint64_t{1} << high_shift) - 1);
            const auto half = uint64_t{1} << (high_shift - 1);
            increment = remainder > half || (remainder == half && (value.lo || (mantissa & 1)));
        } else if (shift == 64) {
            mantissa = value.hi;
            increment = value.lo > 0x8000000000000000ULL ||
                        (value.lo == 0x8000000000000000ULL && (mantissa & 1));
        } else {
            mantissa = (value.hi << (64 - shift)) | (value.lo >> shift);
            const auto remainder = value.lo & ((uint64_t{1} << shift) - 1);
            const auto half = uint64_t{1} << (shift - 1);
            increment = remainder > half || (remainder == half && (mantissa & 1));
        }
        exponent += shift;
        if (increment && ++mantissa == 0) {
            mantissa = 0x8000000000000000ULL;
            ++exponent;
        }
        return normalized(sign, mantissa, exponent, precision);
    }

    /// Returns a value that is not finite at the given precision.
    ///
    /// @param precision significand bits of the working precision
    /// @return the value
    static Extended not_finite(unsigned precision) noexcept {
        Extended result;
        result.precision_ = precision;
        result.finite_ = false;
        return result;
    }

    /// Rounds the value to an IEEE binary format, ties to even, subnormals included.
    ///
    /// @param fraction stored fraction bits of the format
    /// @param bias exponent bias of the format
    /// @param sign_bit position of the sign bit
    /// @return the format's bit pattern; empty when the value is not finite
    ///         or lies outside the format's finite range
    std::optional<uint64_t> ieee(unsigned fraction, int bias, unsigned sign_bit) const noexcept {
        if (!finite_)
            return std::nullopt;
        const auto sign = uint64_t{negative_} << sign_bit;
        if (!mantissa_)
            return sign;
        int top = exponent_ + 63;
        const int minimum = 1 - bias;
        const int target = (top < minimum ? minimum : top) - static_cast<int>(fraction);
        const auto significand =
            rounded_shift(mantissa_, static_cast<unsigned>(target - exponent_));
        if (top < minimum)
            return sign | significand;
        auto rounded = significand;
        if (rounded == (uint64_t{1} << (fraction + 1))) {
            rounded >>= 1;
            ++top;
        }
        if (top > bias)
            return std::nullopt;
        return sign | (static_cast<uint64_t>(top + bias) << fraction) |
               (rounded & ((uint64_t{1} << fraction) - 1));
    }

  public:

    /// Widens a binary64 value to the working precision, exactly.
    ///
    /// @param value the value; an infinity or a NaN gives a value that is not finite
    /// @param precision significand bits, 53 or 64; any other gives a value
    ///        that is not finite
    explicit Extended(double value, unsigned precision = 64) noexcept {
        if (precision != 53 && precision != 64) {
            *this = not_finite(precision);
            return;
        }
        const auto bits = std::bit_cast<uint64_t>(value);
        const auto field = (bits >> 52) & 0x7ff;
        if (field == 0x7ff) {
            *this = not_finite(precision);
            return;
        }
        auto mantissa = bits & 0xfffffffffffffULL;
        int exponent = -1074;
        if (field) {
            mantissa |= uint64_t{1} << 52;
            exponent = static_cast<int>(field) - 1023 - 52;
        }
        *this = normalized((bits >> 63) != 0, mantissa, exponent, precision);
    }

    Extended() = default;

    /// Multiplies, rounding the exact product once to the working precision.
    ///
    /// @param b the other factor
    /// @return the product; not finite when either factor is not
    Extended operator*(const Extended& b) const noexcept {
        if (!finite_ || !b.finite_)
            return not_finite(precision_);
        if (!mantissa_ || !b.mantissa_)
            return normalized(negative_ != b.negative_, 0, 0, precision_);
        return from_wide(
            negative_ != b.negative_,
            Wide::multiply(mantissa_, b.mantissa_),
            exponent_ + b.exponent_,
            precision_
        );
    }

    /// Adds, rounding the exact sum once to the working precision.
    ///
    /// Two zeros add to a negative zero only when both are negative.
    ///
    /// @param b the other addend
    /// @return the sum; not finite when either addend is not
    Extended operator+(const Extended& b) const noexcept {
        if (!finite_ || !b.finite_)
            return not_finite(precision_);
        if (!mantissa_ && !b.mantissa_)
            return normalized(negative_ && b.negative_, 0, 0, precision_);
        if (!mantissa_)
            return b;
        if (!b.mantissa_)
            return *this;
        const Extended* large = this;
        const Extended* small = &b;
        if (exponent_ < b.exponent_ || (exponent_ == b.exponent_ && mantissa_ < b.mantissa_)) {
            large = &b;
            small = this;
        }
        const Wide x{large->mantissa_ >> 1, large->mantissa_ << 63};
        const auto y = Wide{small->mantissa_ >> 1, small->mantissa_ << 63}.right_jam(
            static_cast<unsigned>(large->exponent_ - small->exponent_)
        );
        const auto result = large->negative_ == small->negative_ ? x.add(y) : x.subtract(y);
        return from_wide(
            (result.hi | result.lo) != 0 && large->negative_,
            result,
            large->exponent_ - 63,
            precision_
        );
    }

    /// Subtracts, rounding the exact difference once to the working precision.
    ///
    /// @param b the value to subtract
    /// @return the difference; not finite when either operand is not
    Extended operator-(Extended b) const noexcept {
        b.negative_ = !b.negative_;
        return *this + b;
    }

    /// Rounds the value to a binary64, ties to even.
    ///
    /// @return the binary64; empty when the value is not finite or lies
    ///         outside the binary64 finite range
    std::optional<double> to_double() const noexcept {
        const auto bits = ieee(52, 1023, 63);
        if (!bits)
            return std::nullopt;
        return std::bit_cast<double>(*bits);
    }

    /// Rounds the value to a binary32, ties to even.
    ///
    /// @return the binary32; empty when the value is not finite or lies
    ///         outside the binary32 finite range
    std::optional<float> to_float() const noexcept {
        const auto bits = ieee(23, 127, 31);
        if (!bits)
            return std::nullopt;
        return std::bit_cast<float>(static_cast<uint32_t>(*bits));
    }
};
} // namespace oa::base::game_loop::detail
