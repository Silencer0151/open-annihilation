// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Turning a decoder's error into an exception, for the screens and the
// application, which treat a file they cannot decode as fatal or skip it.
// Decoders return their errors as values; the exception stays in the layer
// that throws it.

#include "oa/base/bytes.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace oa::ui::decoded {

/// Describes a decode error for a message.
///
/// @param error the error
/// @param what what was being decoded, such as a file name
/// @return "<what>: <message> (<code> at byte <offset>)"
[[nodiscard]] std::string describe(const base::bytes::DecodeError& error, std::string_view what);

/// Returns the value a decoder produced, or throws its error.
///
/// @param decoded the decoder's result
/// @param what what was decoded, named in the error
/// @return the value
/// @throws std::runtime_error with describe()'s text when the decode failed
template <class T>
[[nodiscard]] T require(base::bytes::Decoded<T>&& decoded, std::string_view what) {
    if (!decoded.ok())
        throw std::runtime_error(describe(decoded.error, what));
    return std::move(*decoded.value);
}

} // namespace oa::ui::decoded
