// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The language registry: its entries and their order, tags and 3.1c's words
// found without regard to case, fallback chains ending in English, the
// operating system's locales normalised and matched alone and in order, the
// settings' choice, and the interface catalogue read from small built
// files, malformed and oversized ones included.

#include "oa/data/languages.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/data/languages/unit_texts.hpp"
#include "oa/test/check.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace languages = oa::data::languages;

/// Returns the tag of a language a lookup found, or "none".
///
/// @param language the lookup's result
/// @return its tag
std::string_view tag_of(const languages::Language* language) {
    return language != nullptr ? language->tag : std::string_view{"none"};
}

/// The registry holds English, German, Spanish, French and Italian, English
/// first and the others in the order of their own names, each with its
/// name in itself and 3.1c's word for it.
void registry_lists_the_five_languages_in_menu_order() {
    const auto known = languages::known_languages();
    OA_CHECK(known.size() == 5);
    const std::array<std::string_view, 5> tags{"en", "de", "es", "fr", "it"};
    const std::array<std::string_view, 5> endonyms{
        "English",
        "Deutsch",
        "Espa\xC3\xB1"
        "ol",
        "Fran\xC3\xA7"
        "ais",
        "Italiano"
    };
    const std::array<std::string_view, 5> words{
        "English", "German", "Spanish", "French", "Italian"
    };
    for (std::size_t index = 0; index < known.size() && index < tags.size(); ++index) {
        OA_CHECK(known[index].tag == tags[index]);
        OA_CHECK(known[index].endonym == endonyms[index]);
        OA_CHECK(known[index].game_name == words[index]);
        OA_CHECK(known[index].needs == languages::TextNeeds::game_fonts);
        OA_CHECK(languages::drawable(known[index]));
    }
    OA_CHECK(&languages::english() == &known[0]);
    OA_CHECK(languages::english().english_name == "English");
    // After English, the endonyms are in order.
    for (std::size_t index = 2; index < known.size(); ++index)
        OA_CHECK(known[index - 1].endonym < known[index].endonym);
}

/// What drawing a language needs decides whether the build offers it.
void only_the_needs_this_build_meets_are_drawable() {
    languages::Language language{};
    language.needs = languages::TextNeeds::game_fonts;
    OA_CHECK(languages::drawable(language));
    language.needs = languages::TextNeeds::modern_fonts;
    OA_CHECK(languages::drawable(language));
    language.needs = languages::TextNeeds::more_font_faces;
    OA_CHECK(!languages::drawable(language));
    language.needs = languages::TextNeeds::text_shaping;
    OA_CHECK(!languages::drawable(language));
}

/// Tags and 3.1c's words are found without regard to case.
void tags_and_game_words_are_found_without_regard_to_case() {
    OA_CHECK(tag_of(languages::find_by_tag("de")) == "de");
    OA_CHECK(tag_of(languages::find_by_tag("DE")) == "de");
    OA_CHECK(tag_of(languages::find_by_tag("Fr")) == "fr");
    OA_CHECK(tag_of(languages::find_by_tag("en")) == "en");
    OA_CHECK(tag_of(languages::find_by_tag("pt")) == "none");
    OA_CHECK(tag_of(languages::find_by_tag("")) == "none");
    OA_CHECK(tag_of(languages::find_by_tag("de-AT")) == "none");
    // 3.1c's command line names a language by the data's word for it.
    OA_CHECK(tag_of(languages::find_by_game_name("german")) == "de");
    OA_CHECK(tag_of(languages::find_by_game_name("GERMAN")) == "de");
    OA_CHECK(tag_of(languages::find_by_game_name("Spanish")) == "es");
    OA_CHECK(tag_of(languages::find_by_game_name("french")) == "fr");
    OA_CHECK(tag_of(languages::find_by_game_name("italian")) == "it");
    OA_CHECK(tag_of(languages::find_by_game_name("english")) == "en");
    OA_CHECK(tag_of(languages::find_by_game_name("piglatin")) == "none");
    OA_CHECK(tag_of(languages::find_by_game_name("")) == "none");
}

/// Every chain ends in English, which is its own whole chain.
void fallback_chains_end_in_english() {
    const auto english_chain = languages::fallback_chain(languages::english());
    OA_CHECK(english_chain.count == 1);
    OA_CHECK(english_chain.view()[0] == &languages::english());
    for (const languages::Language& language : languages::known_languages()) {
        const auto chain = languages::fallback_chain(language);
        OA_CHECK(chain.count >= 1);
        OA_CHECK(chain.view().front() == &language);
        OA_CHECK(chain.view().back() == &languages::english());
    }
    const auto french = languages::fallback_chain(*languages::find_by_tag("fr"));
    OA_CHECK(french.count == 2);
    // An entry's own fallbacks come between it and English, each once, and
    // English listed among them still comes last.
    const std::array<std::string_view, 4> fallbacks{"en", "fr", "fr", "xx"};
    const std::array<std::string_view, 1> locales{"wa"};
    const languages::Language walloon{"wa", "Walon", "Walloon", "Walloon", locales, fallbacks};
    const auto chain = languages::fallback_chain(walloon);
    OA_CHECK(chain.count == 3);
    OA_CHECK(chain.view()[0] == &walloon);
    OA_CHECK(tag_of(chain.view()[1]) == "fr");
    OA_CHECK(chain.view()[2] == &languages::english());
}

/// The operating system's locales are written as BCP-47 tags.
void locales_are_normalised() {
    OA_CHECK(languages::normalised_locale("de_DE.UTF-8@euro") == "de-DE");
    OA_CHECK(languages::normalised_locale("fr_CA") == "fr-CA");
    OA_CHECK(languages::normalised_locale("en-GB") == "en-GB");
    OA_CHECK(languages::normalised_locale(" es ") == "es");
    OA_CHECK(languages::normalised_locale("zh_Hant_TW") == "zh-Hant-TW");
    OA_CHECK(languages::normalised_locale("C").empty());
    OA_CHECK(languages::normalised_locale("C.UTF-8").empty());
    OA_CHECK(languages::normalised_locale("POSIX").empty());
    OA_CHECK(languages::normalised_locale("").empty());
    OA_CHECK(languages::normalised_locale("de DE").empty());
    OA_CHECK(languages::normalised_locale("-de").empty());
    OA_CHECK(languages::normalised_locale(std::string(65, 'a')).empty());
}

/// A locale chooses the language whose locale is its whole tag or its
/// leading subtags.
void locales_choose_languages_by_their_leading_subtags() {
    OA_CHECK(tag_of(languages::match_locale("de")) == "de");
    OA_CHECK(tag_of(languages::match_locale("de-AT")) == "de");
    OA_CHECK(tag_of(languages::match_locale("de_CH.UTF-8")) == "de");
    OA_CHECK(tag_of(languages::match_locale("DE-de")) == "de");
    OA_CHECK(tag_of(languages::match_locale("fr-CA")) == "fr");
    OA_CHECK(tag_of(languages::match_locale("it_IT")) == "it");
    OA_CHECK(tag_of(languages::match_locale("es-419")) == "es");
    OA_CHECK(tag_of(languages::match_locale("en-GB")) == "en");
    OA_CHECK(tag_of(languages::match_locale("pt-BR")) == "none");
    OA_CHECK(tag_of(languages::match_locale("zh-CN")) == "none");
    OA_CHECK(tag_of(languages::match_locale("deu")) == "none");
    OA_CHECK(tag_of(languages::match_locale("frx")) == "none");
    OA_CHECK(tag_of(languages::match_locale("C")) == "none");
}

/// The first preferred locale that chooses a known language wins; none
/// leaves English.
void preferred_locales_choose_the_first_known_language() {
    const auto pick = [](std::vector<std::string> locales) {
        return languages::preferred_language(locales).tag;
    };
    OA_CHECK(pick({"pt-BR", "fr-FR", "de"}) == "fr");
    OA_CHECK(pick({"de_DE.UTF-8", "fr"}) == "de");
    OA_CHECK(pick({"en-US", "de"}) == "en");
    OA_CHECK(pick({"ja-JP", "zh-Hant-TW", "ko"}) == "en");
    OA_CHECK(pick({"C", "es_ES"}) == "es");
    OA_CHECK(pick({}) == "en");
}

/// The settings' choice: a known tag, else the operating system's language.
void settings_choice_names_a_language_or_the_system_one() {
    const languages::Language& german = *languages::find_by_tag("de");
    const languages::Language& italian = *languages::find_by_tag("it");
    OA_CHECK(&languages::chosen_language(languages::system_choice, german) == &german);
    OA_CHECK(&languages::chosen_language("it", german) == &italian);
    OA_CHECK(&languages::chosen_language("IT", german) == &italian);
    OA_CHECK(&languages::chosen_language("en", german) == &languages::english());
    OA_CHECK(&languages::chosen_language("", german) == &german);
    OA_CHECK(&languages::chosen_language("xx-YY", german) == &german);
}

/// A small catalogue file: two texts, one translated into French and
/// German, one into French alone, with keys in mixed case.
constexpr std::string_view kCatalogue = "[Mouse wheel zoom]\n"
                                        "\t{\n"
                                        "\tFR=Zoom \xC3\xA0 la molette;\n"
                                        "\tde=Mausrad-Zoom;\n"
                                        "\t}\n"
                                        "[Font shadow]\n"
                                        "\t{\n"
                                        "\tfr=Ombre du texte;\n"
                                        "\tit=;\n"
                                        "\t}\n";

/// Texts are looked up by their English, in the language's chain.
void catalogue_translates_by_english_text() {
    languages::InterfaceText catalogue;
    std::string error;
    OA_CHECK(catalogue.add(kCatalogue, &error));
    OA_CHECK(error.empty());
    OA_CHECK(catalogue.size() == 3);
    const auto& french = *languages::find_by_tag("fr");
    const auto& german = *languages::find_by_tag("de");
    const auto& italian = *languages::find_by_tag("it");
    OA_CHECK(catalogue.text("Mouse wheel zoom", french) == "Zoom \xC3\xA0 la molette");
    OA_CHECK(catalogue.text("Mouse wheel zoom", german) == "Mausrad-Zoom");
    OA_CHECK(catalogue.text("Font shadow", german) == "Font shadow");
    // An empty value adds nothing, so Italian falls back to English.
    OA_CHECK(catalogue.text("Font shadow", italian) == "Font shadow");
    OA_CHECK(catalogue.text("Mouse wheel zoom", languages::english()) == "Mouse wheel zoom");
    // The lookup is by the exact text.
    OA_CHECK(catalogue.text("mouse wheel zoom", french) == "mouse wheel zoom");
    OA_CHECK(catalogue.text("Not in the catalogue", french) == "Not in the catalogue");
    // A later file replaces a translation and adds to the others.
    OA_CHECK(catalogue.add("[Font shadow]\n{\nfr=Ombre;\nes=Sombra;\n}\n"));
    OA_CHECK(catalogue.text("Font shadow", french) == "Ombre");
    OA_CHECK(catalogue.text("Font shadow", *languages::find_by_tag("es")) == "Sombra");
    OA_CHECK(catalogue.text("Mouse wheel zoom", german) == "Mausrad-Zoom");
    OA_CHECK(catalogue.size() == 4);
}

/// A file that does not parse, or is too large, adds nothing.
void malformed_catalogues_add_nothing() {
    languages::InterfaceText catalogue;
    std::string error;
    OA_CHECK(!catalogue.add("[Mouse wheel zoom]\n{\nfr=Zoom\n}\n", &error));
    OA_CHECK(!error.empty());
    OA_CHECK(catalogue.size() == 0);
    error.clear();
    OA_CHECK(!catalogue.add("[Mouse wheel zoom\n{\nfr=Zoom;\n}\n", &error));
    OA_CHECK(!error.empty());
    error.clear();
    const std::string oversized(languages::most_catalogue_bytes + 1, ' ');
    OA_CHECK(!catalogue.add(oversized, &error));
    OA_CHECK(!error.empty());
    OA_CHECK(catalogue.size() == 0);
    // An empty file is a catalogue with nothing in it.
    OA_CHECK(catalogue.add(""));
    OA_CHECK(catalogue.size() == 0);
}

/// The installed catalogue and language answer interface_text; without a
/// catalogue every word is English.
void installed_language_answers_interface_text() {
    OA_CHECK(&languages::interface_language() == &languages::english());
    OA_CHECK(languages::interface_text("Mouse wheel zoom") == "Mouse wheel zoom");
    languages::InterfaceText catalogue;
    OA_CHECK(catalogue.add(kCatalogue));
    const auto& french = *languages::find_by_tag("fr");
    languages::set_interface_language(&catalogue, french);
    OA_CHECK(&languages::interface_language() == &french);
    OA_CHECK(languages::interface_text("Mouse wheel zoom") == "Zoom \xC3\xA0 la molette");
    OA_CHECK(languages::interface_text("Cancel") == "Cancel");
    languages::set_interface_language(nullptr, french);
    OA_CHECK(languages::interface_text("Mouse wheel zoom") == "Mouse wheel zoom");
    languages::set_interface_language(nullptr, languages::english());
}

/// Copies text into a zero-filled character field, keeping its last byte zero.
///
/// @param field the field
/// @param text the text; the characters that fit are copied
template <std::size_t Size>
void copy_text(char (&field)[Size], std::string_view text) {
    std::memcpy(field, text.data(), std::min(text.size(), Size - 1));
}

/// Returns a unit type with its own name and description, as a unit file's
/// Name and Description give them.
///
/// @param unit_name the unit's name
/// @param name its name
/// @param description its description
/// @return the type
oa::UnitDef unit_type(const char* unit_name, const char* name, const char* description) {
    oa::UnitDef def{};
    copy_text(def.unit_name, unit_name);
    copy_text(def.name, name);
    copy_text(def.description, description);
    return def;
}

/// The words a language's text is looked up by: its chain's, English's
/// only for English itself, or the command line's word alone.
void data_words_follow_the_chain() {
    OA_CHECK(
        languages::data_words(languages::english(), "") == std::vector<std::string>({"English"})
    );
    OA_CHECK(
        languages::data_words(*languages::find_by_tag("de"), "") ==
        std::vector<std::string>({"German"})
    );
    OA_CHECK(
        languages::data_words(languages::english(), "piglatin") ==
        std::vector<std::string>({"piglatin"})
    );
}

/// A unit's name and description in a language come from its file's keys,
/// found without regard to case, and fall back to its own.
void unit_texts_fall_back_to_the_types_own() {
    languages::UnitTexts texts;
    texts.add("ARMCOM", "French", "Commandeur", "Commandant");
    texts.add("armcom", "german", nullptr, "Kommandant");
    texts.add("ARMCOM", "Spanish", "Comandante", nullptr);
    texts.add("", "French", "x", "y");
    texts.add("ARMSOLAR", "French", nullptr, nullptr);
    OA_CHECK(texts.size() == 1);
    const oa::UnitDef commander = unit_type("ARMCOM", "Commander", "Commander");
    const oa::UnitDef solar = unit_type("ARMSOLAR", "Solar Collector", "Produces Energy");
    const std::vector<std::string> french{"French"};
    const std::vector<std::string> german{"GERMAN"};
    const std::vector<std::string> spanish{"spanish"};
    OA_CHECK(texts.name(commander, french) == "Commandeur");
    OA_CHECK(texts.description(commander, french) == "Commandant");
    OA_CHECK(texts.name(commander, german) == "Commander");
    OA_CHECK(texts.description(commander, german) == "Kommandant");
    OA_CHECK(texts.name(commander, spanish) == "Comandante");
    OA_CHECK(texts.description(commander, spanish) == "Commander");
    OA_CHECK(texts.name(commander, {}) == "Commander");
    OA_CHECK(texts.name(solar, french) == "Solar Collector");
    OA_CHECK(texts.description(solar, french) == "Produces Energy");
    // A later word is tried after an earlier one that has no text.
    const std::vector<std::string> chain{"Italian", "French"};
    OA_CHECK(texts.name(commander, chain) == "Commandeur");
    // A text added again replaces the first.
    texts.add("ARMCOM", "FRENCH", "Chef", nullptr);
    OA_CHECK(texts.name(commander, french) == "Chef");
    OA_CHECK(texts.description(commander, french) == "Commandant");
    // A field filled to its end without a NUL is read to its end.
    oa::UnitDef full = commander;
    std::memset(full.name, 'N', sizeof full.name);
    OA_CHECK(texts.name(full, {}).size() == sizeof full.name);
    texts.clear();
    OA_CHECK(texts.size() == 0);
    OA_CHECK(texts.name(commander, french) == "Commander");
}

/// The installed table and words answer the interface's lookups; without
/// a table every type shows its own name and description.
void installed_unit_texts_answer_the_interface() {
    const oa::UnitDef commander = unit_type("ARMCOM", "Commander", "Commander");
    OA_CHECK(languages::unit_display_name(commander) == "Commander");
    languages::UnitTexts texts;
    texts.add("ARMCOM", "Italian", "Comandante", "Comandante");
    const auto words = languages::data_words(*languages::find_by_tag("it"), "");
    languages::set_unit_texts(&texts, words);
    OA_CHECK(languages::unit_display_name(commander) == "Comandante");
    OA_CHECK(languages::unit_display_description(commander) == "Comandante");
    languages::set_unit_texts(&texts, languages::data_words(languages::english(), ""));
    OA_CHECK(languages::unit_display_name(commander) == "Commander");
    languages::set_unit_texts(nullptr, words);
    OA_CHECK(languages::unit_display_description(commander) == "Commander");
}

} // namespace

int main() {
    registry_lists_the_five_languages_in_menu_order();
    only_the_needs_this_build_meets_are_drawable();
    tags_and_game_words_are_found_without_regard_to_case();
    fallback_chains_end_in_english();
    locales_are_normalised();
    locales_choose_languages_by_their_leading_subtags();
    preferred_locales_choose_the_first_known_language();
    settings_choice_names_a_language_or_the_system_one();
    catalogue_translates_by_english_text();
    malformed_catalogues_add_nothing();
    installed_language_answers_interface_text();
    data_words_follow_the_chain();
    unit_texts_fall_back_to_the_types_own();
    installed_unit_texts_answer_the_interface();
    return oa::test::check_exit_status();
}
