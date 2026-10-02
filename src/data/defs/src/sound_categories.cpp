// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/defs/sound_categories.hpp"
#include "oa/base/text.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace oa::data::defs {

const char* const sound_event_keys[sound_event_count] = {
    "select",       "underattack",    "activate", "deactivate", "ok",     "arrived", "cant",
    "unitcomplete", "build",          "repair",   "working",    "load",   "unload",  "cloak",
    "uncloak",      "capture",        "count5",   "count4",     "count3", "count2",  "count1",
    "count0",       "canceldestruct",
};

namespace {

constexpr int32_t max_choices_per_event = 1024;

bool grow_choice_array(char (**array)[sound_choice_capacity], int32_t count) noexcept {
    auto* grown = static_cast<char (*)[sound_choice_capacity]>(
        std::realloc(*array, sizeof **array * static_cast<std::size_t>(count + 1))
    );
    if (grown == nullptr)
        return false;
    *array = grown;
    return true;
}

} // namespace

bool sound_choices_add(
    const formats::tdf::Block* section, const char* key, SoundChoices* choices
) noexcept {
    char sound[sound_choice_capacity];
    if (!formats::tdf::get_string(section, key, sound, sizeof sound, ""))
        return false;
    char text_key[256];
    std::snprintf(text_key, sizeof text_key, "%s%s", key, "text");
    char text[sound_choice_capacity];
    if (!formats::tdf::get_string(section, text_key, text, sizeof text, ""))
        text[0] = '\0';
    if (choices->count >= max_choices_per_event ||
        !grow_choice_array(&choices->sounds, choices->count) ||
        !grow_choice_array(&choices->texts, choices->count))
        return false;
    std::memcpy(choices->sounds[choices->count], sound, sizeof sound);
    std::memcpy(choices->texts[choices->count], text, sizeof text);
    ++choices->count;
    return true;
}

bool sound_category_table_load(formats::tdf::Document* sound, SoundCategoryTable* table) noexcept {
    table->categories = nullptr;
    table->count = 0;
    const uint32_t count = formats::tdf::child_count(sound->root);
    if (count == 0)
        return true;
    if (count > sound_category_limit)
        return false;
    table->categories = static_cast<SoundCategory*>(std::calloc(count, sizeof(SoundCategory)));
    if (table->categories == nullptr)
        return false;
    table->count = count;
    for (uint32_t index = 0; index < count; ++index) {
        SoundCategory* category = &table->categories[index];
        formats::tdf::reset_cursor(sound);
        if (!formats::tdf::step_entry(sound, index))
            continue;
        const formats::tdf::Block* section = formats::tdf::cursor(sound);
        oa::base::text::copy_padded(category->name, section->name, sizeof category->name - 1);
        for (uint32_t event = 0; event < sound_event_count; ++event) {
            SoundChoices* choices = &category->events[event + 1];
            sound_choices_add(section, sound_event_keys[event], choices);
            for (int32_t suffix = 1;; ++suffix) {
                char key[256];
                std::snprintf(key, sizeof key, "%s%i", sound_event_keys[event], suffix);
                if (!sound_choices_add(section, key, choices))
                    break;
            }
        }
    }
    return true;
}

bool load_sound_categories(
    const Files* files, SoundCategoryTable* table, const char* variant
) noexcept {
    table->categories = nullptr;
    table->count = 0;
    char path[path_capacity];
    build_variant_path(files, path, sizeof path, "gamedata", "sound", "tdf", variant);
    formats::tdf::Document document;
    formats::tdf::document_init(&document);
    bool ok = load_tdf_file(files, path, &document, nullptr);
    if (ok)
        ok = sound_category_table_load(&document, table);
    formats::tdf::document_free(&document);
    return ok;
}

void sound_category_table_free(SoundCategoryTable* table) noexcept {
    for (uint32_t index = 0; index < table->count; ++index) {
        for (SoundChoices& choices : table->categories[index].events) {
            std::free(choices.sounds);
            std::free(choices.texts);
        }
    }
    std::free(table->categories);
    table->categories = nullptr;
    table->count = 0;
}

void load_all_sounds(
    const Files* files, const char* variant, const SoundCache& cache, SoundCategoryTable* categories
) noexcept {
    cache.clear(cache.context);
    char path[path_capacity];
    build_variant_path(files, path, sizeof path, "gamedata", "allsound", "TDF", variant);
    formats::tdf::Document document;
    formats::tdf::document_init(&document);
    if (load_tdf_file(files, path, &document, nullptr)) {
        for (uint32_t entry = 0; formats::tdf::step_entry(&document, entry); ++entry) {
            const formats::tdf::Block* section = formats::tdf::cursor(&document);
            char name[allsound_name_capacity + 1] = {};
            oa::base::text::copy_padded(name, section->name, allsound_name_capacity);
            char sound[allsound_sound_capacity];
            if (formats::tdf::get_string(section, "sound", sound, sizeof sound, ""))
                cache.add(cache.context, name, sound);
            formats::tdf::reset_cursor(&document);
        }
    }
    formats::tdf::document_free(&document);
    load_sound_categories(files, categories, variant);
}

int sound_category_find(const SoundCategoryTable* table, const char* name) noexcept {
    for (uint32_t index = 0; index < table->count; ++index)
        if (formats::tdf::compare_nocase(table->categories[index].name, name) == 0)
            return static_cast<int>(index);
    return -1;
}

} // namespace oa::data::defs
