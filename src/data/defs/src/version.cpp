// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/defs/version.hpp"

#include "oa/formats/tdf.hpp"

namespace oa::data::defs {

bool revision_gpf_mismatch(const Files* files, const char* variant) noexcept {
    char path[path_capacity];
    build_variant_path(files, path, sizeof path, "gamedata", "version", "tdf", variant);
    formats::tdf::Document document;
    formats::tdf::document_init(&document);
    bool mismatch = false;
    if (load_tdf_file(files, path, &document, nullptr)) {
        char version[0x40];
        mismatch = !formats::tdf::select_section(&document, "Version") ||
                   !formats::tdf::get_string(
                       formats::tdf::cursor(&document), "GPFVersion", version, sizeof version, ""
                   ) ||
                   formats::tdf::compare_nocase(expected_gpf_version, version) != 0;
    }
    formats::tdf::document_free(&document);
    return mismatch;
}

} // namespace oa::data::defs
