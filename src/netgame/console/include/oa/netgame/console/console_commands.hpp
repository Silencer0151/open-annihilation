// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The console's network and service commands: the options NetStats, Drop,
// Compression, BPS, Page and P, and the developer command Senderror.
// register_console_commands is a ConsoleHost::extend; the host's
// extension_context must be the CommandHost, which the handlers find again
// through the dispatching console's ConsoleHost.
#pragma once

#include "oa/ui/console/console.hpp"

#include <cstdint>

namespace oa::netgame::console {

// Systems the commands reach outside the console. Every pointer may be null;
// the command then only performs its own state changes.
struct CommandHost {
    void* context{};
    void (*reset_traffic_stats)(void* context){};
    // Whether a launch is active; Page sends only then. Null answers false.
    bool (*launch_active)(void* context){};
    void (*send_page)(void* context, const char* user, const char* text){};
};

/// Resets the traffic statistics, as every console setup does, and registers the commands in console->commands.
///
/// @param extension_context The CommandHost (ConsoleHost::extension_context); null
///                          skips the reset.
/// @param[in,out] console Console whose command table gains NetStats, Drop,
///                        Compression, BPS, Page, P and Senderror.
void register_console_commands(void* extension_context, oa::ui::console::Console* console) noexcept;

/// Validates and sends a page from a whole "page <user> <text>" line.
///
/// The line is tokenised and handled as the Page command is, outside a
/// dispatch; the outcome is posted to `console` as a service message.
///
/// @param console Console whose ConsoleHost leads to the CommandHost.
/// @param text Command line, the command word first.
void console_page_user(oa::ui::console::Console* console, const char* text) noexcept;

} // namespace oa::netgame::console
