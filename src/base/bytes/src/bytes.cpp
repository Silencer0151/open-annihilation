// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/base/bytes.hpp"

namespace oa::base::bytes {

std::string_view decode_code_name(DecodeCode code) noexcept {
    switch (code) {
    case DecodeCode::none:
        return "no error";
    case DecodeCode::truncated:
        return "truncated";
    case DecodeCode::out_of_range:
        return "out of range";
    case DecodeCode::limit_exceeded:
        return "over its limit";
    case DecodeCode::bad_signature:
        return "bad signature";
    case DecodeCode::unsupported_version:
        return "unsupported version";
    case DecodeCode::malformed:
        return "malformed";
    case DecodeCode::cycle:
        return "cyclic";
    case DecodeCode::not_found:
        return "not found";
    }
    return "unknown error";
}

} // namespace oa::base::bytes
