// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The automation endpoint's token: the secret a client names in its hello,
// new for each run, and its comparison.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace oa::app::automation {

/// The random bits a token holds.
inline constexpr size_t token_bits = 128;

/// Makes a new token: token_bits random bits from the system's generator,
/// written as lowercase hexadecimal digits.
///
/// @return the token, or nothing when the system's generator cannot be read
[[nodiscard]] std::optional<std::string> make_token();

/// Compares a token a client gave with the run's, taking the same time
/// whichever of their characters differ.
///
/// @param given the token the client gave
/// @param expected the run's token
/// @return true when they are equal
[[nodiscard]] bool token_matches(std::string_view given, std::string_view expected) noexcept;

} // namespace oa::app::automation
