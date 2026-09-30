// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The extensions a build registers, and the one extension table that
// combines theirs by the rules extension.hpp gives.
#pragma once

#include "oa/app/extension.hpp"

#include <memory>
#include <span>

namespace oa::app {

// An extension the build registered with oa_add_extension.
struct RegisteredExtension {
    const char* name{}; // its library's CMake target, which messages name
    /// Fills the extension's table.
    ///
    /// @param[out] table the extension's own table, zeroed on entry, which
    ///        the ExtensionList keeps while it lives
    void (*init)(Extension* table){};
};

/// Returns the extensions the build registered, in the order the game
/// combines them: an extension after every registered extension its library
/// links, the rest in the order they were registered.
///
/// Defined in the source the build generates from the registrations
/// (cmake/OaExtensions.cmake); empty when none is registered.
///
/// @return the extensions, which live as long as the process
[[nodiscard]] std::span<const RegisteredExtension> registered_extensions() noexcept;

// What the combined table's hooks keep: each extension's table, and what
// the combined answers need while the list lives.
struct ExtensionListState;

// The tables of several extensions and the one table that combines them.
// The combined table's context is state the list keeps, so the list must
// outlive every copy of that table and every call of its hooks.
class ExtensionList {
  public:

    /// Fills each extension's table with its init, in order, and combines
    /// the tables.
    ///
    /// Throws std::runtime_error naming both extensions when two of them
    /// fill frontend_game, or two fill frontend_states, which one extension
    /// at most may fill.
    ///
    /// @param extensions the extensions in list order; each name must live
    ///        as long as the list
    explicit ExtensionList(std::span<const RegisteredExtension> extensions);
    /// Frees the extensions' tables and what the combined hooks keep; the
    /// extensions' own contexts stay theirs.
    ~ExtensionList();
    ExtensionList(const ExtensionList&) = delete;
    ExtensionList& operator=(const ExtensionList&) = delete;
    ExtensionList(ExtensionList&&) = delete;
    ExtensionList& operator=(ExtensionList&&) = delete;

    /// Returns the table that combines the extensions' hooks.
    ///
    /// A hook no extension fills is null in it, so the engine keeps its own
    /// behaviour there; with no extension every hook is null.
    ///
    /// @return the combined table, whose context this list keeps
    [[nodiscard]] const Extension& combined() const noexcept;

  private:

    std::unique_ptr<ExtensionListState> state_;
};

} // namespace oa::app
