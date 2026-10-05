// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The game's own texts in the language shown: 3.1c's gamedata\translate.tdf,
// which the application loads for the language, reached through a hook it
// installs, so that every screen and panel the game loads translates its
// texts as 3.1c does: by the exact text, case included, a text without a
// translation shown as it is.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace oa::data::languages {

/// The hook the game's own texts are translated through.
struct TranslationHooks {
    void* context{}; ///< passed back to translate and word
    /// Returns a text's translation, valid until the language changes; null
    /// for a text without one. Null translates nothing.
    const char* (*translate)(void* context, const char* text){};
    /// Returns the word the game data knows the language shown by, as
    /// "German", which names its language folders (bitmaps-German); null
    /// for none. The word lives as long as the game runs. Null names none.
    const char* (*word)(void* context){};
    /// Returns a mission's text in the language shown: a mod's language
    /// pack's, else the game data's own in the language, else the player's
    /// or the engine's pack's (oa/data/languages/language_pack.hpp). The
    /// text is valid until the language changes. Null translates none.
    ///
    /// The mission file is as the campaign names it ("Lipar Pass.ota"), the
    /// key missionname, missiondescription or missionhint, and the data's
    /// text its value under the language's word, null when it has none;
    /// null is returned when no source has one.
    const char* (*mission_text)(
        void* context, const char* mission_file, const char* key, const char* data_text
    ){};
    /// Reads a file of the game data's language folders, as
    /// "camps/briefs-Chinese/arm01.txt", from the language packs: with
    /// before_data, those tried before the game data (a mod's); otherwise
    /// those tried after it (the player's, then the engine's). It returns
    /// the file's bytes, valid until the next call or until the language
    /// changes; null when no such pack holds it. Null reads none.
    const std::string* (*language_file)(void* context, const char* path, bool before_data){};
};

/// Installs the hook the game's own texts are translated through.
///
/// @param hooks kept until the next call; default hooks remove it
void set_translation_hooks(const TranslationHooks& hooks) noexcept;

/// Returns the installed hook.
///
/// @return the hook; translate is null when none is installed
[[nodiscard]] const TranslationHooks& translation_hooks() noexcept;

/// Returns where a file of the game data would be in the language's folder,
/// as 3.1c looks first: <directory>-<word>/<name> for <directory>/<name>.
/// Whether the file is there is for the caller to find out.
///
/// @param path the file's path, its directory and name parted by '/' or '\\'
/// @return the path in the language's folder; nothing without a word or a
///     directory
[[nodiscard]] std::optional<std::string> language_folder_path(std::string_view path);

/// Translates one of the game's own texts in the language shown.
///
/// @param text the text, as the game data and the game hold it
/// @return its translation; nothing for a text without one, or without the hook
[[nodiscard]] std::optional<std::string> translation_of(std::string_view text);

/// Translates one of the game's own texts through the installed hook, for a
/// service whose own context is another's, such as the campaign's files.
///
/// @param ignored the service's context, which the call does not use
/// @param text the text
/// @return its translation, valid until the language changes; null for a
///     text without one
[[nodiscard]] const char* installed_translation(void* ignored, const char* text);

/// Returns the word the game data knows the language shown by.
///
/// @return the installed hook's word, which lives as long as the game runs,
///     as "German"; null without one
[[nodiscard]] const char* installed_word();

/// Returns a mission's text in the language shown, through the installed
/// hook (TranslationHooks::mission_text).
///
/// @param mission_file the mission's file, as the campaign names it
/// @param key missionname, missiondescription or missionhint
/// @param data_text the game data's own text in the language; null for none
/// @return the text, valid until the language changes; null when no source
///     has one, or without the hook
[[nodiscard]] const char*
installed_mission_text(const char* mission_file, const char* key, const char* data_text);

/// Reads a file of the game data's language folders from the language
/// packs, through the installed hook (TranslationHooks::language_file).
///
/// @param path the file's path in the game data, as language_folder_path
///     gives it
/// @param before_data true for the packs tried before the game data (a
///     mod's); false for those tried after it
/// @return the file's bytes, valid until the next call or until the
///     language changes; a view without data when no such pack holds it,
///     or without the hook
[[nodiscard]] std::string_view installed_language_file(const char* path, bool before_data);

} // namespace oa::data::languages
