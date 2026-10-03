// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The interface catalogue: the Open Annihilation interface's own words (the
// settings dialog, the engine's own notices and messages) in the languages
// translations are given for. The game's own texts are 3.1c's and come from
// its data (Translate.tdf and the unit files, oa/data/defs/locale.hpp);
// these are the words the engine adds. Each is written in English in the
// source and looked up by that text, and a catalogue file has the shape of
// 3.1c's Translate.tdf: a section names the English text, and each of its
// keys, a language's tag, holds the text in that language, in UTF-8:
//
//     [Mouse wheel zoom]
//         {
//         fr = ...;
//         de = ...;
//         }
//
// No translation ships yet, so every language shows the interface in
// English; adding one is adding a catalogue file, with no change to the
// code.
#pragma once

#include "oa/data/languages.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace oa::data::languages {

/// The most bytes of a catalogue file read: a larger file is refused.
inline constexpr std::size_t most_catalogue_bytes = std::size_t{4} * 1024 * 1024;

/// The interface's own words in other languages, by their English text.
class InterfaceText {
  public:

    /// Adds the translations a catalogue file holds. Each top-level section
    /// names an English text, and each of its keys a language's tag, read
    /// without regard to case, with the text in that language; an empty
    /// value adds nothing. A later translation of the same text into the
    /// same language replaces the earlier one.
    ///
    /// @param file the file's bytes, at most most_catalogue_bytes
    /// @param[out] error why the file was refused; may be null
    /// @return false when the file is too large or does not parse; nothing
    ///     is added then
    bool add(std::string_view file, std::string* error = nullptr);

    /// Returns a text as a language shows it: its translation into the
    /// first language of the language's fallback chain that has one, else
    /// the English text itself.
    ///
    /// @param english the English text
    /// @param language the language
    /// @return the text; a view of the catalogue's copy, valid while the
    ///     catalogue lives and nothing is added, or of `english`
    [[nodiscard]] std::string_view text(std::string_view english, const Language& language) const;

    /// Returns how many translations the catalogue holds.
    ///
    /// @return the texts times the languages each is translated into
    [[nodiscard]] std::size_t size() const noexcept;

  private:

    /// The translations: English text, then lower-case tag, then the text.
    std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>> texts_{};
};

/// Installs the catalogue and the language the interface's own words show
/// in. The view interface_text gives stays valid until the next call. Only
/// the thread that draws the interface calls it and interface_text.
///
/// @param catalogue the catalogue; null shows every word in English
/// @param language the language
void set_interface_language(const InterfaceText* catalogue, const Language& language) noexcept;

/// Returns the language the interface's own words show in.
///
/// @return the installed language; English before any is installed
[[nodiscard]] const Language& interface_language() noexcept;

/// Returns one of the interface's own words in the installed language.
///
/// @param english the word, in English as the source writes it
/// @return its translation, or `english` itself without one
[[nodiscard]] std::string_view interface_text(std::string_view english);

} // namespace oa::data::languages
