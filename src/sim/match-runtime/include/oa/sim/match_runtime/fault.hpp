// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace oa::sim::match_runtime {

// The first broken invariant or refused input a match operation met. The
// operation that meets one notes it here and stops; what it had done stays
// done, and later operations run as before. The first note is kept until the
// owner clears it.
class MatchFault {
  public:

    /// Notes a fault, unless one is already noted.
    ///
    /// @param what what went wrong; cut to the held length
    void note(std::string_view what) noexcept { note(what, {}); }

    /// Notes a fault with its detail as "what: detail", unless one is already noted.
    ///
    /// @param what what went wrong
    /// @param detail the name or message that goes with it; none leaves out the colon
    void note(std::string_view what, std::string_view detail) noexcept {
        if (noted_)
            return;
        noted_ = true;
        std::size_t length = 0;
        const auto append = [&](std::string_view part) {
            const auto count = std::min(part.size(), text_.size() - 1 - length);
            std::copy_n(part.data(), count, text_.data() + length);
            length += count;
        };
        append(what);
        if (!detail.empty()) {
            append(": ");
            append(detail);
        }
        text_[length] = '\0';
    }

    /// Returns the noted fault.
    ///
    /// @return its text, or null while none is noted
    [[nodiscard]] const char* text() const noexcept { return noted_ ? text_.data() : nullptr; }

    /// Forgets the noted fault, so the next one is noted.
    void clear() noexcept {
        noted_ = false;
        text_[0] = '\0';
    }

  private:

    std::array<char, 192> text_{};
    bool noted_{};
};

} // namespace oa::sim::match_runtime
