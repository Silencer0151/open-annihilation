// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <bit>
#include <cstdint>
#include <stdexcept>

namespace oa::base::game_loop::detail {
// Finite binary arithmetic with a 53- or 64-bit significand, nearest-even rounding.
// This gives the timing expression the precision Timing::precision selects,
// even on hosts where long double has only 53 significant bits. No host FP
// rounding mode or nonstandard 128-bit integer type is required.
struct Wide {
    uint64_t hi{}, lo{};

    static Wide multiply(uint64_t a, uint64_t b) noexcept {
        constexpr auto mask = uint64_t{0xffffffff};
        const auto a0 = a & mask, a1 = a >> 32, b0 = b & mask, b1 = b >> 32;
        const auto w0 = a0 * b0;
        const auto t = a1 * b0 + (w0 >> 32);
        const auto w1 = (t & mask) + a0 * b1;
        return {a1 * b1 + (t >> 32) + (w1 >> 32), (w1 << 32) | (w0 & mask)};
    }

    Wide add(Wide b) const noexcept {
        const auto low = lo + b.lo;
        return {hi + b.hi + (low < lo), low};
    }

    Wide subtract(Wide b) const noexcept { return {hi - b.hi - (lo < b.lo), lo - b.lo}; }

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

class Extended {
    unsigned precision_ = 64;
    bool negative_{};
    uint64_t mantissa_{}; // normalized: top bit set except zero
    int exponent_{};      // value = (+/-) mantissa * 2^exponent

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

    uint64_t ieee(unsigned fraction, int bias, unsigned sign_bit) const {
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
            throw std::overflow_error("extended timing value outside finite IEEE range");
        return sign | (static_cast<uint64_t>(top + bias) << fraction) |
               (rounded & ((uint64_t{1} << fraction) - 1));
    }

  public:

    explicit Extended(double value, unsigned precision = 64) {
        if (precision != 53 && precision != 64)
            throw std::invalid_argument("unsupported precision");
        const auto bits = std::bit_cast<uint64_t>(value);
        const auto field = (bits >> 52) & 0x7ff;
        if (field == 0x7ff)
            throw std::invalid_argument("nonfinite extended timing input");
        auto mantissa = bits & 0xfffffffffffffULL;
        int exponent = -1074;
        if (field) {
            mantissa |= uint64_t{1} << 52;
            exponent = static_cast<int>(field) - 1023 - 52;
        }
        *this = normalized((bits >> 63) != 0, mantissa, exponent, precision);
    }

    Extended() = default;

    Extended operator*(const Extended& b) const noexcept {
        if (!mantissa_ || !b.mantissa_)
            return normalized(negative_ != b.negative_, 0, 0, precision_);
        return from_wide(
            negative_ != b.negative_,
            Wide::multiply(mantissa_, b.mantissa_),
            exponent_ + b.exponent_,
            precision_
        );
    }

    Extended operator+(const Extended& b) const noexcept {
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

    Extended operator-(Extended b) const noexcept {
        b.negative_ = !b.negative_;
        return *this + b;
    }

    double to_double() const { return std::bit_cast<double>(ieee(52, 1023, 63)); }

    float to_float() const {
        return std::bit_cast<float>(static_cast<uint32_t>(ieee(23, 127, 31)));
    }
};
} // namespace oa::base::game_loop::detail
