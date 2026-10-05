// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A package file opened for the streamed zip reader, read from any offset.
#pragma once

#include "oa/formats/zip/stream.hpp"
#include "oa/platform/files.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace oa::app::mod_install::detail {

/// A package file opened for reading, its size known.
struct PackageFile {
    std::FILE* stream{};         ///< the open file; null until open
    oa::platform::Files files{}; ///< the 64-bit seek and tell
    uint64_t bytes{};            ///< the file's size

    PackageFile() = default;
    PackageFile(const PackageFile&) = delete;
    PackageFile& operator=(const PackageFile&) = delete;

    /// Closes the file.
    ~PackageFile();

    /// Opens the file and measures it.
    ///
    /// @param file the package
    /// @return true when it opened and its size is known
    [[nodiscard]] bool open(const std::filesystem::path& file);

    /// Returns the hooks the streamed reader reads it through.
    ///
    /// @return the hooks; they read through this object, which must outlive them
    [[nodiscard]] oa::formats::zip::SourceHooks source();
};

} // namespace oa::app::mod_install::detail
