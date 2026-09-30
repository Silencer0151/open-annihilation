// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The director's frame, tick, sample and chunk clock (clock.hpp).
//
// Every quantity is an exact fraction of whole numbers, and each result is
// one product of whole numbers divided by another, rounded down (or up where
// clock.hpp says so). The products are formed at 128 bits, so they are exact
// for any rates, not only within the script's limits.
//
// Within those limits they stay far below that. A rate has at most
// max_rate_places decimal places, so its denominator is at most 1000; the
// tick rate is at most 3000 (numerator at most 3 000 000) and the frame rate
// at most 240 (numerator at most 240 000). frame_tick forms frame * tickrate
// numerator * framerate denominator: at most 2^31 * 3 000 000 * 1000, about
// 6.4e18, below 2^63 at max_frame_count frames. first_frame_of_tick forms
// ticks * tickrate denominator * framerate numerator: at most 2^32 * 1000 *
// 240 000, about 1.0e18. frame_samples forms frame * 48000 * framerate
// denominator: at most 2^31 * 4.8e7, about 1.0e17.

#include "oa/media/director/clock.hpp"

#include <cstdint>
#include <limits>

namespace oa::media::director {
namespace {

/// The largest value of 64 bits, the result of a quotient too large for them.
inline constexpr uint64_t saturated = std::numeric_limits<uint64_t>::max();
/// The largest tick: frame_tick saturates here.
inline constexpr uint64_t largest_tick = std::numeric_limits<uint32_t>::max();
/// The largest chunk index: chunk_of_frame and chunk_count saturate here.
inline constexpr uint64_t largest_chunk = std::numeric_limits<uint32_t>::max();
/// Bits in the low half of a 64-bit value.
inline constexpr uint32_t half_bits = 32;
/// The low 32 bits of a 64-bit value.
inline constexpr uint64_t low_half = (uint64_t{1} << half_bits) - 1;
/// Bits in a 64-bit value.
inline constexpr int32_t word_bits = 64;

/// An unsigned 128-bit value in two halves.
struct Wide {
    uint64_t high{};
    uint64_t low{};
};

/// A quotient and its remainder.
struct Quotient {
    uint64_t value{};     ///< saturated when it needs more than 64 bits
    uint64_t remainder{}; ///< meaningful only when the quotient is not saturated
    bool saturated{};     ///< the quotient needed more than 64 bits
};

/// Returns the exact 128-bit product of two 64-bit values.
///
/// @param a a factor
/// @param b the other factor
/// @return a * b
Wide multiply(uint64_t a, uint64_t b) noexcept {
    const uint64_t a_low{a & low_half};
    const uint64_t a_high{a >> half_bits};
    const uint64_t b_low{b & low_half};
    const uint64_t b_high{b >> half_bits};
    const uint64_t low_low{a_low * b_low};
    const uint64_t low_high{a_low * b_high};
    const uint64_t high_low{a_high * b_low};
    const uint64_t high_high{a_high * b_high};
    const uint64_t middle{(low_low >> half_bits) + (low_high & low_half) + (high_low & low_half)};
    return Wide{
        high_high + (low_high >> half_bits) + (high_low >> half_bits) + (middle >> half_bits),
        (low_low & low_half) | (middle << half_bits),
    };
}

/// Divides a 128-bit value by a 64-bit one, rounding down.
///
/// @param dividend the value divided
/// @param divisor the divisor, above zero
/// @return the quotient and remainder, or a saturated quotient when it needs
///         more than 64 bits
Quotient divide(Wide dividend, uint64_t divisor) noexcept {
    if (dividend.high >= divisor)
        return Quotient{saturated, 0, true};
    uint64_t remainder{dividend.high};
    uint64_t quotient{};
    for (int32_t bit{word_bits - 1}; bit >= 0; --bit) {
        const bool carried{(remainder >> (word_bits - 1)) != 0};
        remainder = (remainder << 1) | ((dividend.low >> bit) & 1);
        quotient <<= 1;
        if (carried || remainder >= divisor) {
            remainder -= divisor;
            quotient |= 1;
        }
    }
    return Quotient{quotient, remainder, false};
}

/// Returns floor(a * b / divisor), saturated at 64 bits.
///
/// @param a a factor
/// @param b the other factor
/// @param divisor the divisor; zero saturates
/// @return the quotient
uint64_t multiply_divide_down(uint64_t a, uint64_t b, uint64_t divisor) noexcept {
    if (divisor == 0)
        return saturated;
    return divide(multiply(a, b), divisor).value;
}

/// Returns ceil(a * b / divisor), saturated at 64 bits.
///
/// @param a a factor
/// @param b the other factor
/// @param divisor the divisor; zero saturates
/// @return the quotient
uint64_t multiply_divide_up(uint64_t a, uint64_t b, uint64_t divisor) noexcept {
    if (divisor == 0)
        return saturated;
    const Quotient quotient{divide(multiply(a, b), divisor)};
    if (quotient.saturated || (quotient.remainder != 0 && quotient.value == saturated))
        return saturated;
    return quotient.value + (quotient.remainder != 0 ? 1 : 0);
}

/// Returns a * b, saturated at 64 bits.
///
/// @param a a factor
/// @param b the other factor
/// @return the product
uint64_t multiply_saturated(uint64_t a, uint64_t b) noexcept {
    const Wide product{multiply(a, b)};
    return product.high != 0 ? saturated : product.low;
}

/// Returns a numerator as an unsigned value; below zero counts as zero.
///
/// @param value a fraction's numerator
/// @return it, or 0
uint64_t numerator_of(const Rational& value) noexcept {
    return value.numerator > 0 ? static_cast<uint64_t>(value.numerator) : 0;
}

/// Returns a denominator as an unsigned value; below one counts as one.
///
/// @param value a fraction
/// @return its denominator, at least 1
uint64_t denominator_of(const Rational& value) noexcept {
    return value.denominator > 1 ? static_cast<uint64_t>(value.denominator) : 1;
}

/// Returns a value capped at a largest one.
///
/// @param value the value
/// @param largest the cap
/// @return min(value, largest)
uint64_t capped(uint64_t value, uint64_t largest) noexcept {
    return value < largest ? value : largest;
}

/// Returns the whole game ticks a chunk of `ticks` mode holds.
///
/// @param rule the chunking
/// @return floor(length), at least 1
uint64_t chunk_ticks(const ChunkRule& rule) noexcept {
    const uint64_t ticks{numerator_of(rule.length) / denominator_of(rule.length)};
    return ticks > 0 ? ticks : 1;
}

} // namespace

Rational rational_of(oa::formats::oascript::Decimal value) noexcept {
    int64_t denominator{1};
    for (uint32_t place{};
         place < value.places && place < oa::formats::oascript::max_decimal_places;
         ++place)
        denominator *= 10;
    return Rational{value.mantissa, denominator};
}

uint32_t frame_tick(const FrameClock& clock, uint64_t frame) noexcept {
    const uint64_t ticks_numerator{
        multiply_saturated(numerator_of(clock.tickrate), denominator_of(clock.framerate))
    };
    const uint64_t ticks_denominator{
        multiply_saturated(denominator_of(clock.tickrate), numerator_of(clock.framerate))
    };
    const uint64_t elapsed{multiply_divide_down(frame, ticks_numerator, ticks_denominator)};
    return static_cast<uint32_t>(
        capped(uint64_t{clock.first_tick} + capped(elapsed, largest_tick), largest_tick)
    );
}

uint64_t first_frame_of_tick(const FrameClock& clock, uint32_t tick) noexcept {
    if (tick <= clock.first_tick)
        return 0;
    const uint64_t elapsed{uint64_t{tick} - clock.first_tick};
    // The smallest frame f with floor(f * A / B) >= elapsed is ceil(elapsed * B / A).
    const uint64_t ticks_numerator{
        multiply_saturated(numerator_of(clock.tickrate), denominator_of(clock.framerate))
    };
    const uint64_t ticks_denominator{
        multiply_saturated(denominator_of(clock.tickrate), numerator_of(clock.framerate))
    };
    return multiply_divide_up(elapsed, ticks_denominator, ticks_numerator);
}

SampleRange frame_samples(const FrameClock& clock, uint64_t frame) noexcept {
    const uint64_t samples_numerator{
        multiply_saturated(audio_sample_rate, denominator_of(clock.framerate))
    };
    const uint64_t samples_denominator{numerator_of(clock.framerate)};
    const uint64_t next{frame < saturated ? frame + 1 : saturated};
    return SampleRange{
        multiply_divide_down(frame, samples_numerator, samples_denominator),
        multiply_divide_down(next, samples_numerator, samples_denominator),
    };
}

uint64_t
frames_of_seconds(const FrameClock& clock, oa::formats::oascript::Decimal seconds) noexcept {
    const Rational duration{rational_of(seconds)};
    const uint64_t denominator{
        multiply_saturated(denominator_of(duration), denominator_of(clock.framerate))
    };
    const Quotient frames{
        divide(multiply(numerator_of(duration), numerator_of(clock.framerate)), denominator)
    };
    if (frames.saturated)
        return saturated;
    // Round half up: one more frame when the remainder is at least half the denominator.
    const bool round_up{frames.remainder >= denominator - frames.remainder};
    return frames.value + (round_up && frames.value < saturated ? 1 : 0);
}

uint64_t
chunk_first_frame(const FrameClock& clock, const ChunkRule& rule, uint32_t chunk) noexcept {
    if (rule.mode == oa::formats::oascript::ChunkMode::ticks) {
        const uint64_t tick{
            uint64_t{clock.first_tick} + multiply_saturated(chunk, chunk_ticks(rule))
        };
        return first_frame_of_tick(
            clock, static_cast<uint32_t>(capped(tick, oa::formats::oascript::max_tick))
        );
    }
    // ceil(chunk * length * framerate)
    const uint64_t frames_numerator{
        multiply_saturated(numerator_of(rule.length), numerator_of(clock.framerate))
    };
    const uint64_t frames_denominator{
        multiply_saturated(denominator_of(rule.length), denominator_of(clock.framerate))
    };
    return multiply_divide_up(chunk, frames_numerator, frames_denominator);
}

uint32_t chunk_of_frame(const FrameClock& clock, const ChunkRule& rule, uint64_t frame) noexcept {
    if (rule.mode == oa::formats::oascript::ChunkMode::ticks) {
        // A chunk starts at or before the frame exactly when the frame shows
        // its first tick or a later one.
        const uint64_t elapsed{uint64_t{frame_tick(clock, frame)} - clock.first_tick};
        return static_cast<uint32_t>(capped(elapsed / chunk_ticks(rule), largest_chunk));
    }
    // The largest chunk c with ceil(c * K) <= frame, K = length * framerate,
    // is floor(frame / K).
    const uint64_t frames_numerator{
        multiply_saturated(numerator_of(rule.length), numerator_of(clock.framerate))
    };
    const uint64_t frames_denominator{
        multiply_saturated(denominator_of(rule.length), denominator_of(clock.framerate))
    };
    return static_cast<uint32_t>(
        capped(multiply_divide_down(frame, frames_denominator, frames_numerator), largest_chunk)
    );
}

uint32_t
chunk_count(const FrameClock& clock, const ChunkRule& rule, uint64_t frame_count) noexcept {
    if (frame_count == 0)
        return 0;
    const uint64_t last{chunk_of_frame(clock, rule, frame_count - 1)};
    return static_cast<uint32_t>(capped(last + 1, largest_chunk));
}

} // namespace oa::media::director
