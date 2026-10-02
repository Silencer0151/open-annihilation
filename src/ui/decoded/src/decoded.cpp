// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/decoded.hpp"

namespace oa::ui::decoded {

std::string describe(const base::bytes::DecodeError& error, std::string_view what) {
    std::string text(what);
    text += ": ";
    text += error.message != nullptr ? error.message : "cannot be decoded";
    text += " (";
    text += base::bytes::decode_code_name(error.code);
    text += " at byte ";
    text += std::to_string(error.offset);
    text += ')';
    return text;
}

} // namespace oa::ui::decoded
