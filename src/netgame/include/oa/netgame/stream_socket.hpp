// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// TCP streams on this machine's loopback address, through which a program
// on the same machine reaches the game: a listener and the connections it
// accepts, all non-blocking, read and written without waiting. They are
// served by socket_host.cpp, the one source that calls the system's socket
// functions, with the same sockets as network play's: Winsock 2 on Windows
// from Windows XP on, and the system's sockets elsewhere.
#pragma once

#include "oa/netgame/socket_host.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::netgame::sock {

/// What stream_read and stream_write return when the connection failed.
inline constexpr std::ptrdiff_t stream_failed = -1;
/// What stream_read returns once the other end has said it will send nothing
/// more; what it sent before has all been read, and writing may still work.
inline constexpr std::ptrdiff_t stream_ended = -2;

/// The loopback address a stream listener is bound to.
enum class Loopback : uint8_t {
    ipv4, ///< 127.0.0.1
    ipv6, ///< ::1
};

/// Opens a non-blocking TCP listener on a loopback address.
///
/// @param loopback the address
/// @param port the port; 0 lets the system choose one
/// @param[out] bound_port the port the listener is bound to; unchanged on failure
/// @param[out] error why it failed, ending in a zero byte
/// @param error_size the bytes `error` holds
/// @return the listener, or invalid_socket when the system refused it
[[nodiscard]] intptr_t stream_listen(
    Loopback loopback, uint16_t port, uint16_t& bound_port, char* error, std::size_t error_size
) noexcept;

/// Accepts a connection waiting at a listener, without waiting for one.
///
/// The connection is non-blocking and sends what it is given at once.
///
/// @param listener the listener
/// @return the connection, or invalid_socket when none waits
[[nodiscard]] intptr_t stream_accept(intptr_t listener) noexcept;

/// Reads what has arrived on a connection, without waiting.
///
/// @param stream the connection
/// @param[out] bytes receives what was read
/// @param size the bytes `bytes` holds
/// @return the bytes read; 0 when nothing has arrived; stream_ended once
///         the other end will send nothing more; stream_failed when the
///         connection failed
[[nodiscard]] std::ptrdiff_t
stream_read(intptr_t stream, uint8_t* bytes, std::size_t size) noexcept;

/// Writes what the system takes now to a connection, without waiting.
///
/// @param stream the connection
/// @param bytes the bytes to write
/// @param size how many
/// @return the bytes the system took, which may be fewer than `size` or 0;
///         stream_failed when the connection failed
[[nodiscard]] std::ptrdiff_t
stream_write(intptr_t stream, const uint8_t* bytes, std::size_t size) noexcept;

/// Tells the other end that nothing more will be sent, once what was written goes out.
///
/// @param stream the connection
void stream_finish(intptr_t stream) noexcept;

/// Closes a listener or a connection.
///
/// @param[in,out] stream the socket; invalid_socket afterwards
void stream_close(intptr_t* stream) noexcept;

} // namespace oa::netgame::sock
