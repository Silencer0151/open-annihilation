// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Exact numbers: reading their text, comparing them, and writing them as the
// JSON canonicalization scheme (RFC 8785) writes the double each stands for.

#include "oa/formats/oamod.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>

namespace oa::formats::oamod {

namespace {

/// The most digits an integer's significand has: 2^53 has 16.
constexpr uint32_t max_integer_digits = 16;
/// The exponent magnitude past which reading an exponent stops counting; any
/// number that far out is out of range anyway.
constexpr int64_t saturated_exponent = 1000000;
/// The largest power of ten a double holds exactly.
constexpr int32_t max_exact_power_of_ten = 22;
/// The exponent past which RFC 8785 writes a number with an exponent: a
/// number of 10^21 or more.
constexpr int32_t max_plain_point = 21;
/// The point position below which RFC 8785 writes an exponent: numbers
/// below 10^-6.
constexpr int32_t min_plain_point = -5;

/// Tells whether a character is a decimal digit.
///
/// @param c the character
/// @return true for '0' to '9'
constexpr bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

/// Returns ten to a power.
///
/// @param power 0 to 19
/// @return 10^power
constexpr uint64_t power_of_ten(uint32_t power) noexcept {
    uint64_t value = 1;
    for (uint32_t index = 0; index < power; ++index)
        value *= 10;
    return value;
}

/// Writes a significand's decimal digits.
///
/// @param significand the digits' value
/// @return the digits, without leading zeros; "0" for zero
std::string digits_of(uint64_t significand) {
    return std::to_string(significand);
}

} // namespace

NumberStatus parse_number(std::string_view text, Number& number, bool leading_zeros) noexcept {
    size_t at = 0;
    const bool negative = at < text.size() && text[at] == '-';
    if (negative)
        ++at;
    const size_t integer_start = at;
    while (at < text.size() && is_digit(text[at]))
        ++at;
    const std::string_view integer_digits = text.substr(integer_start, at - integer_start);
    if (integer_digits.empty())
        return NumberStatus::not_a_number;
    if (!leading_zeros && integer_digits.size() > 1 && integer_digits[0] == '0')
        return NumberStatus::not_a_number;
    std::string_view fraction_digits{};
    if (at < text.size() && text[at] == '.') {
        const size_t fraction_start = ++at;
        while (at < text.size() && is_digit(text[at]))
            ++at;
        fraction_digits = text.substr(fraction_start, at - fraction_start);
        if (fraction_digits.empty())
            return NumberStatus::not_a_number;
    }
    bool has_exponent = false;
    int64_t written_exponent = 0;
    if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
        has_exponent = true;
        ++at;
        bool exponent_negative = false;
        if (at < text.size() && (text[at] == '+' || text[at] == '-')) {
            exponent_negative = text[at] == '-';
            ++at;
        }
        const size_t exponent_start = at;
        while (at < text.size() && is_digit(text[at])) {
            if (written_exponent < saturated_exponent)
                written_exponent = written_exponent * 10 + (text[at] - '0');
            ++at;
        }
        if (at == exponent_start)
            return NumberStatus::not_a_number;
        if (exponent_negative)
            written_exponent = -written_exponent;
    }
    if (at != text.size())
        return NumberStatus::not_a_number;
    const bool integer = fraction_digits.empty() && !has_exponent;

    // The significant digits: every digit, leading and trailing zeros removed.
    std::string digits{integer_digits};
    digits += fraction_digits;
    int64_t exponent = written_exponent - static_cast<int64_t>(fraction_digits.size());
    const size_t first = digits.find_first_not_of('0');
    if (first == std::string::npos) {
        number = Number{false, 0, 0, integer};
        return NumberStatus::ok;
    }
    digits.erase(0, first);
    while (digits.back() == '0') {
        digits.pop_back();
        ++exponent;
    }
    const auto significant = static_cast<int64_t>(digits.size());
    if (integer) {
        if (significant + exponent > max_integer_digits)
            return NumberStatus::integer_too_large;
    } else {
        if (significant > max_significant_digits)
            return NumberStatus::too_many_digits;
        const int64_t leading = significant - 1 + exponent;
        if (leading > max_decimal_exponent || leading < -max_decimal_exponent)
            return NumberStatus::out_of_range;
    }
    uint64_t significand = 0;
    for (const char digit : digits)
        significand = significand * 10 + static_cast<uint64_t>(digit - '0');
    if (integer &&
        significand * power_of_ten(static_cast<uint32_t>(exponent)) > max_integer_magnitude)
        return NumberStatus::integer_too_large;
    number = Number{negative, significand, static_cast<int32_t>(exponent), integer};
    return NumberStatus::ok;
}

Number number_from_integer(int64_t value) noexcept {
    Number number{};
    number.integer = true;
    if (value == 0)
        return number;
    number.negative = value < 0;
    uint64_t magnitude =
        number.negative ? uint64_t{0} - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
    while (magnitude % 10 == 0) {
        magnitude /= 10;
        ++number.exponent;
    }
    number.significand = magnitude;
    return number;
}

int compare_numbers(const Number& left, const Number& right) noexcept {
    const int left_sign = left.significand == 0 ? 0 : (left.negative ? -1 : 1);
    const int right_sign = right.significand == 0 ? 0 : (right.negative ? -1 : 1);
    if (left_sign != right_sign)
        return left_sign < right_sign ? -1 : 1;
    if (left_sign == 0)
        return 0;
    // Same sign: compare the magnitudes, then flip for negatives.
    const std::string left_digits = digits_of(left.significand);
    const std::string right_digits = digits_of(right.significand);
    const int64_t left_point = static_cast<int64_t>(left_digits.size()) + left.exponent;
    const int64_t right_point = static_cast<int64_t>(right_digits.size()) + right.exponent;
    int magnitude = 0;
    if (left_point != right_point) {
        magnitude = left_point < right_point ? -1 : 1;
    } else {
        const size_t width = std::max(left_digits.size(), right_digits.size());
        for (size_t index = 0; index < width && magnitude == 0; ++index) {
            const char a = index < left_digits.size() ? left_digits[index] : '0';
            const char b = index < right_digits.size() ? right_digits[index] : '0';
            if (a != b)
                magnitude = a < b ? -1 : 1;
        }
    }
    return left_sign > 0 ? magnitude : -magnitude;
}

bool integer_value(const Number& number, int64_t& value) noexcept {
    if (number.significand == 0) {
        value = 0;
        return true;
    }
    if (number.exponent < 0)
        return false;
    const auto digits = static_cast<int64_t>(digits_of(number.significand).size());
    if (digits + number.exponent > max_integer_digits)
        return false;
    const uint64_t magnitude =
        number.significand * power_of_ten(static_cast<uint32_t>(number.exponent));
    if (magnitude > max_integer_magnitude)
        return false;
    value = number.negative ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
    return true;
}

std::string canonical_number_text(const Number& number) {
    if (number.significand == 0)
        return "0";
    const std::string digits = digits_of(number.significand);
    const auto count = static_cast<int32_t>(digits.size());
    // The value is 0.digits x 10^point.
    const int32_t point = count + number.exponent;
    std::string text = number.negative ? "-" : "";
    if (count <= point && point <= max_plain_point) {
        text += digits;
        text.append(static_cast<size_t>(point - count), '0');
    } else if (0 < point && point <= max_plain_point) {
        text += digits.substr(0, static_cast<size_t>(point));
        text += '.';
        text += digits.substr(static_cast<size_t>(point));
    } else if (min_plain_point <= point && point <= 0) {
        text += "0.";
        text.append(static_cast<size_t>(-point), '0');
        text += digits;
    } else {
        const int32_t power = point - 1;
        text += digits[0];
        if (count > 1) {
            text += '.';
            text += digits.substr(1);
        }
        text += 'e';
        text += power >= 0 ? '+' : '-';
        text += std::to_string(power >= 0 ? power : -power);
    }
    return text;
}

double number_to_double(const Number& number) noexcept {
    if (number.significand == 0)
        return 0.0;
    static constexpr std::array<double, max_exact_power_of_ten + 1> powers{
        1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
        1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
    };
    double value = 0.0;
    if (number.significand <= max_integer_magnitude && number.exponent >= -max_exact_power_of_ten &&
        number.exponent <= max_exact_power_of_ten) {
        // Both operands are exact, so the one rounding gives the nearest double.
        const auto significand = static_cast<double>(number.significand);
        value = number.exponent >= 0 ? significand * powers[static_cast<size_t>(number.exponent)]
                                     : significand / powers[static_cast<size_t>(-number.exponent)];
    } else {
        const std::string text =
            digits_of(number.significand) + "e" + std::to_string(number.exponent);
        value = std::strtod(text.c_str(), nullptr);
    }
    return number.negative ? -value : value;
}

} // namespace oa::formats::oamod
