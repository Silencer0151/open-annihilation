// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/languages/translation.hpp"

#include <string>
#include <string_view>

namespace oa::data::languages {

namespace {

/// The one hook the game's texts are translated through.
TranslationHooks& installed() noexcept {
    static TranslationHooks hooks{};
    return hooks;
}

} // namespace

void set_translation_hooks(const TranslationHooks& hooks) noexcept {
    installed() = hooks;
}

const TranslationHooks& translation_hooks() noexcept {
    return installed();
}

std::optional<std::string> language_folder_path(std::string_view path) {
    const char* word = installed_word();
    if (word == nullptr || word[0] == '\0')
        return std::nullopt;
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string_view::npos || slash == 0)
        return std::nullopt;
    std::string folder(path.substr(0, slash));
    folder += '-';
    folder += word;
    folder += path.substr(slash);
    return folder;
}

std::optional<std::string> translation_of(std::string_view text) {
    const TranslationHooks& hooks = installed();
    if (hooks.translate == nullptr)
        return std::nullopt;
    const std::string source(text);
    const char* translated = hooks.translate(hooks.context, source.c_str());
    if (translated == nullptr)
        return std::nullopt;
    return std::string(translated);
}

const char* installed_translation(void*, const char* text) {
    const TranslationHooks& hooks = installed();
    return hooks.translate != nullptr && text != nullptr ? hooks.translate(hooks.context, text)
                                                         : nullptr;
}

const char* installed_word() {
    const TranslationHooks& hooks = installed();
    return hooks.word != nullptr ? hooks.word(hooks.context) : nullptr;
}

const char*
installed_mission_text(const char* mission_file, const char* key, const char* data_text) {
    const TranslationHooks& hooks = installed();
    if (hooks.mission_text == nullptr || mission_file == nullptr || key == nullptr)
        return data_text;
    return hooks.mission_text(hooks.context, mission_file, key, data_text);
}

std::string_view installed_language_file(const char* path, bool before_data) {
    const TranslationHooks& hooks = installed();
    const std::string* bytes = hooks.language_file != nullptr && path != nullptr
                                   ? hooks.language_file(hooks.context, path, before_data)
                                   : nullptr;
    return bytes != nullptr ? std::string_view(bytes->data(), bytes->size()) : std::string_view{};
}

} // namespace oa::data::languages
