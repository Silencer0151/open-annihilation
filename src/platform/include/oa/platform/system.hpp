// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Host clock, sleep, thread start and fatal-message services.

#include <cstddef>
#include <cstdint>
#include <string>

namespace oa::platform {

/// Returns the host millisecond tick count.
///
/// @return milliseconds since an arbitrary process-local origin, wrapping at
///         32 bits
[[nodiscard]] uint32_t tick_ms() noexcept;

/// Suspends the calling thread.
///
/// @param milliseconds time to sleep
void sleep_ms(uint32_t milliseconds) noexcept;

using ThreadEntry = void (*)(void* argument);

/// Starts a detached thread.
///
/// @param entry function the thread runs
/// @param stack_size requested stack size in bytes; advisory
/// @param argument value passed to entry
/// @return false when the thread could not be created
[[nodiscard]] bool start_thread(ThreadEntry entry, std::size_t stack_size, void* argument) noexcept;

using ErrorSink = void (*)(const char* message);

/// Routes show_error_message to a front end, for example a message box.
///
/// @param sink receiver of error messages; null restores the default, which
///        writes to standard error
void set_error_sink(ErrorSink sink) noexcept;

/// Reports a fatal or user-visible error under the "Error" caption.
///
/// @param message NUL-terminated text passed to the error sink
void show_error_message(const char* message) noexcept;

// Out-of-memory report text, appended to the error log beside the application.
inline constexpr char out_of_memory_message[] = "Out of memory!\r\nYour hard disk may be full\r\n";
inline constexpr char error_log_file_name[] = "ErrorLog.txt";

/// Appends text to ErrorLog.txt.
///
/// @param directory path ending in a separator; null or empty for the working directory
/// @param text NUL-terminated text appended as is
/// @return false when the log cannot be opened
bool append_error_log(const char* directory, const char* text) noexcept;

/// Returns the folder beside the application, where ErrorLog.txt goes.
///
/// The application is the executable, or on macOS the application bundle
/// that holds it, whose own folders stay as they were installed.
///
/// @param base_path the folder SDL_GetBasePath() names, ending in a
///        separator: the executable's folder, or a bundle's
///        Contents/Resources/ folder; null for none
/// @return base_path, or the folder that holds the bundle when base_path is
///         the Contents/Resources/ folder of a bundle named *.app; empty
///         when base_path is null
[[nodiscard]] std::string error_log_directory(const char* base_path);

} // namespace oa::platform
