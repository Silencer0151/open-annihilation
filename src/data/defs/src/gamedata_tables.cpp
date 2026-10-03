// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/defs/gamedata_tables.hpp"
#include "oa/data/defs/layout.hpp"

#include <cstdint>
#include <cstdio>

namespace oa::data::defs {

bool load_gamedata_table_lines(
    formats::tdf::Document* document, int16_t index, const GamedataTableSink& sink
) noexcept {
    char name[32];
    std::snprintf(name, sizeof name, "TABLE%d", index + 1);
    formats::tdf::reset_cursor(document);
    if (!formats::tdf::select_section(document, name))
        return true;
    const formats::tdf::Block* section = formats::tdf::cursor(document);
    const auto line_count = static_cast<int16_t>(formats::tdf::get_int(section, "numlines", 0));
    return sink.table(sink.context, index, section, line_count < 0 ? int16_t{0} : line_count);
}

bool load_gamedata_tables(
    const Files* files, const char* variant, const GamedataTableSink& sink
) noexcept {
    char path[path_capacity];
    build_variant_path(
        files, path, sizeof path, directory_name(DataDirectory::gamedata), "los", "TDF", variant
    );
    formats::tdf::Document document;
    formats::tdf::document_init(&document);
    if (!load_tdf_file(files, path, &document, nullptr)) {
        formats::tdf::document_free(&document);
        return false;
    }
    bool loaded = true;
    if (formats::tdf::select_section(&document, "TABLEINFO")) {
        const auto table_count = static_cast<int16_t>(
            formats::tdf::get_int(formats::tdf::cursor(&document), "numtables", 0)
        );
        loaded = sink.resize(sink.context, table_count < 0 ? int16_t{0} : table_count);
        for (int16_t index = 0; loaded && index < table_count; ++index)
            loaded = load_gamedata_table_lines(&document, index, sink);
    }
    formats::tdf::document_free(&document);
    return loaded;
}

} // namespace oa::data::defs
