// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Exact decimals: assembly from a number's text, digit-by-digit writing and
// comparison, and the core-schema resolution of YAML plain scalars. No
// binary floating point is used anywhere here.

#include "document_internal.hpp"

#include <array>

namespace oa::formats::oascript {

namespace {

/// The most decimal digits an unsigned 64-bit magnitude has.
constexpr size_t max_magnitude_digits = 20;

/// The decimal digits of a magnitude, most significant first, without
/// leading zeros; zero has none.
struct Digits {
    std::array<char, max_magnitude_digits> digits{};
    size_t count{};
};

/// Splits a magnitude into its decimal digits.
///
/// @param magnitude the value
/// @return its digits
Digits digits_of(uint64_t magnitude) noexcept {
    std::array<char, max_magnitude_digits> reversed{};
    size_t count{};
    while (magnitude != 0) {
        reversed[count++] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    }
    Digits result{};
    result.count = count;
    for (size_t index{}; index < count; ++index)
        result.digits[index] = reversed[count - 1 - index];
    return result;
}

/// The magnitude of a mantissa, exact for the most negative value too.
///
/// @param mantissa the signed value
/// @return its absolute value
uint64_t magnitude_of(int64_t mantissa) noexcept {
    return mantissa < 0 ? uint64_t{0} - static_cast<uint64_t>(mantissa)
                        : static_cast<uint64_t>(mantissa);
}

/// Compares two non-negative decimals given by their digits and places.
///
/// @param left the first value's digits
/// @param left_places the first value's places
/// @param right the second value's digits
/// @param right_places the second value's places
/// @return negative, zero or positive as left is less than, equal to or greater than right
int compare_magnitudes(
    const Digits& left, uint32_t left_places, const Digits& right, uint32_t right_places
) noexcept {
    if (left.count == 0 || right.count == 0)
        return left.count == right.count ? 0 : (left.count == 0 ? -1 : 1);
    // The power of ten of each value's leading digit.
    const int64_t left_exponent{static_cast<int64_t>(left.count) - int64_t{left_places}};
    const int64_t right_exponent{static_cast<int64_t>(right.count) - int64_t{right_places}};
    if (left_exponent != right_exponent)
        return left_exponent < right_exponent ? -1 : 1;
    const size_t longest{left.count > right.count ? left.count : right.count};
    for (size_t index{}; index < longest; ++index) {
        const char left_digit{index < left.count ? left.digits[index] : '0'};
        const char right_digit{index < right.count ? right.digits[index] : '0'};
        if (left_digit != right_digit)
            return left_digit < right_digit ? -1 : 1;
    }
    return 0;
}

/// Tells whether a text is one of the core schema's forms the reader
/// refuses: 0x followed by hexadecimal digits, 0o followed by octal digits,
/// an optionally signed .inf, or .nan, in their three spellings each.
///
/// @param raw the plain scalar's text
/// @return true for a refused form
bool refused_number_form(std::string_view raw) noexcept {
    if (raw.size() > 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'o')) {
        const bool hexadecimal{raw[1] == 'x'};
        for (size_t index{2}; index < raw.size(); ++index) {
            const uint8_t c{static_cast<uint8_t>(raw[index])};
            if (hexadecimal ? detail::hex_digit_value(c) < 0 : (c < '0' || c > '7'))
                return false;
        }
        return true;
    }
    std::string_view unsigned_text{raw};
    if (!unsigned_text.empty() && (unsigned_text[0] == '+' || unsigned_text[0] == '-'))
        unsigned_text.remove_prefix(1);
    if (unsigned_text == ".inf" || unsigned_text == ".Inf" || unsigned_text == ".INF")
        return true;
    return raw == ".nan" || raw == ".NaN" || raw == ".NAN";
}

/// Splits a plain scalar into number parts when it matches
/// [-+]?(digits(.digits?)?|.digits)([eE][-+]?digits)?.
///
/// @param raw the plain scalar's text
/// @param[out] parts the parts, when it matches
/// @return true when the text is a decimal number
bool split_plain_number(std::string_view raw, detail::NumberParts& parts) noexcept {
    size_t index{};
    const auto digits_from = [&raw, &index]() {
        const size_t first{index};
        while (index < raw.size() && detail::is_digit(static_cast<uint8_t>(raw[index])))
            ++index;
        return raw.substr(first, index - first);
    };
    parts = detail::NumberParts{};
    if (index < raw.size() && (raw[index] == '+' || raw[index] == '-')) {
        parts.negative = raw[index] == '-';
        ++index;
    }
    parts.integer_digits = digits_from();
    bool has_point{};
    if (index < raw.size() && raw[index] == '.') {
        has_point = true;
        ++index;
        parts.fraction_digits = digits_from();
    }
    if (parts.integer_digits.empty() && parts.fraction_digits.empty())
        return false;
    if (parts.integer_digits.empty() && !has_point)
        return false;
    if (index < raw.size() && (raw[index] == 'e' || raw[index] == 'E')) {
        ++index;
        if (index < raw.size() && (raw[index] == '+' || raw[index] == '-')) {
            parts.exponent_negative = raw[index] == '-';
            ++index;
        }
        parts.exponent_digits = digits_from();
        if (parts.exponent_digits.empty())
            return false;
    }
    return index == raw.size();
}

} // namespace

namespace detail {

ReadStatus assemble_decimal(const NumberParts& parts, Decimal& value) noexcept {
    // The significant digits: the integer and fraction digits read as one
    // sequence, less its leading zeros.
    const std::string_view integer_digits{parts.integer_digits};
    const std::string_view fraction_digits{parts.fraction_digits};
    const size_t total_digits{integer_digits.size() + fraction_digits.size()};
    const auto digit_at = [&integer_digits, &fraction_digits](size_t index) {
        return index < integer_digits.size() ? integer_digits[index]
                                             : fraction_digits[index - integer_digits.size()];
    };
    size_t first_significant{};
    while (first_significant < total_digits && digit_at(first_significant) == '0')
        ++first_significant;

    int64_t exponent{};
    for (const char c : parts.exponent_digits) {
        exponent = exponent * 10 + (c - '0');
        if (exponent > saturated_exponent)
            exponent = saturated_exponent;
    }
    if (parts.exponent_negative)
        exponent = -exponent;
    // The fraction's length is below max_input_bytes, so this cannot overflow.
    int64_t places{static_cast<int64_t>(fraction_digits.size()) - exponent};

    if (first_significant == total_digits) {
        if (places < 0)
            places = 0;
        if (places > int64_t{max_decimal_places})
            return ReadStatus::number_out_of_range;
        value = Decimal{0, static_cast<uint32_t>(places)};
        return ReadStatus::ok;
    }
    int64_t significant_digits{static_cast<int64_t>(total_digits - first_significant)};
    int64_t appended_zeros{};
    if (places < 0) {
        appended_zeros = -places;
        places = 0;
    }
    significant_digits += appended_zeros;
    if (significant_digits > int64_t{max_significant_digits} ||
        places > int64_t{max_decimal_places})
        return ReadStatus::number_out_of_range;
    int64_t mantissa{};
    for (size_t index{first_significant}; index < total_digits; ++index)
        mantissa = mantissa * 10 + (digit_at(index) - '0');
    for (int64_t zero{}; zero < appended_zeros; ++zero)
        mantissa *= 10;
    value = Decimal{parts.negative ? -mantissa : mantissa, static_cast<uint32_t>(places)};
    return ReadStatus::ok;
}

ReadStatus resolve_plain_scalar(std::string_view raw, Node& node) {
    if (raw.empty() || raw == "~" || raw == "null" || raw == "Null" || raw == "NULL") {
        node.kind = NodeKind::null_value;
        return ReadStatus::ok;
    }
    if (raw == "true" || raw == "True" || raw == "TRUE" || raw == "false" || raw == "False" ||
        raw == "FALSE") {
        node.kind = NodeKind::boolean;
        node.boolean = raw[0] == 't' || raw[0] == 'T';
        return ReadStatus::ok;
    }
    if (refused_number_form(raw))
        return ReadStatus::unsupported_number_form;
    NumberParts parts{};
    if (split_plain_number(raw, parts)) {
        Decimal number{};
        const ReadStatus status{assemble_decimal(parts, number)};
        if (status != ReadStatus::ok)
            return status;
        node.kind = NodeKind::number;
        node.number = number;
        return ReadStatus::ok;
    }
    if (raw.size() > max_string_bytes)
        return ReadStatus::string_too_long;
    node.kind = NodeKind::string;
    node.text.assign(raw);
    return ReadStatus::ok;
}

} // namespace detail

std::string decimal_text(Decimal value) {
    const Digits digits{digits_of(magnitude_of(value.mantissa))};
    std::string text{};
    if (value.mantissa < 0)
        text.push_back('-');
    const size_t places{value.places};
    if (digits.count <= places) {
        // The value is below one: a zero, the point, then zeros up to the digits.
        text.push_back('0');
        if (places != 0) {
            text.push_back('.');
            text.append(places - digits.count, '0');
            text.append(digits.digits.data(), digits.count);
        }
        return text;
    }
    const size_t integer_count{digits.count - places};
    text.append(digits.digits.data(), integer_count);
    if (places != 0) {
        text.push_back('.');
        text.append(digits.digits.data() + integer_count, places);
    }
    return text;
}

int compare_decimals(Decimal left, Decimal right) noexcept {
    const int left_sign{left.mantissa < 0 ? -1 : (left.mantissa > 0 ? 1 : 0)};
    const int right_sign{right.mantissa < 0 ? -1 : (right.mantissa > 0 ? 1 : 0)};
    if (left_sign != right_sign)
        return left_sign < right_sign ? -1 : 1;
    if (left_sign == 0)
        return 0;
    const int magnitude_order{compare_magnitudes(
        digits_of(magnitude_of(left.mantissa)),
        left.places,
        digits_of(magnitude_of(right.mantissa)),
        right.places
    )};
    return left_sign < 0 ? -magnitude_order : magnitude_order;
}

} // namespace oa::formats::oascript
