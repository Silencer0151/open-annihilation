// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The adapter readers describe calls, one for each graphics interface. Each
// exists only on the systems whose sources define it (CMakeLists.txt): a
// caller names one only under the same #if as its definition.
#pragma once

#include "oa/platform/render_probe.hpp"

namespace oa::platform::render_probe::readers {

#if defined(_WIN32)
/// Reads the adapter behind a Direct3D renderer: a Direct3D 9 device's
/// adapter identifier, or a Direct3D 11 or 12 device's DXGI adapter
/// description with its software flag.
///
/// @param renderer the renderer
/// @param[in,out] facts the facts; renderer names the driver, and the
///     adapter's name, vendor, identifiers and flag are filled where read
/// @return true when the adapter was read
bool read_direct3d_adapter(SDL_Renderer* renderer, AdapterFacts& facts);

/// Asks SDL's Direct3D 9 device whether it can draw (TestCooperativeLevel).
///
/// @param renderer the renderer, SDL's Direct3D 9 renderer
/// @return the device's state; unknown when SDL gives no device
DeviceState direct3d9_device_state(SDL_Renderer* renderer) noexcept;

/// Says whether the game runs under Wine, which provides Windows'
/// interfaces on another system: ntdll.dll then exports wine_get_version.
///
/// @return true under Wine
bool running_under_wine();
#endif

#if defined(__APPLE__)
/// Reads the name of the Metal device behind SDL's Metal renderer.
///
/// @param renderer the renderer
/// @param[in,out] facts the facts; the adapter's name is filled where read
/// @return true when the device was named
bool read_metal_adapter(SDL_Renderer* renderer, AdapterFacts& facts);
#endif

#if defined(_WIN32) || defined(__linux__)
/// Reads the Vulkan physical device behind SDL's Vulkan renderer: its name,
/// identifiers, whether it is of CPU type, and its largest 2D image side.
///
/// @param renderer the renderer
/// @param[in,out] facts the facts; the adapter's name, identifiers,
///     software flag and device texture limit are filled where read
/// @return true when the device was read
bool read_vulkan_adapter(SDL_Renderer* renderer, AdapterFacts& facts);
#endif

/// Reads the OpenGL renderer and vendor strings while SDL's context is
/// current; never when no context is current.
///
/// @param[in,out] facts the facts; the adapter's name and vendor are filled
///     where read
/// @return true when the renderer string was read
bool read_gl_adapter(AdapterFacts& facts);

/// Reads the name of SDL's graphics device behind SDL's gpu renderer; on an
/// SDL before 3.4, which cannot name it, reads nothing.
///
/// @param renderer the renderer
/// @param[in,out] facts the facts; the adapter's name is filled where read
/// @return true when the device was named
bool read_gpu_adapter(SDL_Renderer* renderer, AdapterFacts& facts);

} // namespace oa::platform::render_probe::readers
