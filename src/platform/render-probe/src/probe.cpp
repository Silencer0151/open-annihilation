// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// describe: SDL's own properties of a renderer, then the adapter through
// the reader of the renderer's graphics interface, where this system has
// one.
#include "oa/platform/render_probe.hpp"

#include "readers.hpp"

#include <SDL3/SDL.h>

namespace oa::platform::render_probe {
namespace {

#if defined(_WIN32)
/// SDL's name for its Direct3D 9 renderer.
constexpr std::string_view kDirect3d9Renderer = "direct3d";
/// SDL's name for its Direct3D 11 renderer.
constexpr std::string_view kDirect3d11Renderer = "direct3d11";
/// SDL's name for its Direct3D 12 renderer.
constexpr std::string_view kDirect3d12Renderer = "direct3d12";
#endif
#if defined(__APPLE__)
/// SDL's name for its Metal renderer.
constexpr std::string_view kMetalRenderer = "metal";
#endif
/// SDL's name for its OpenGL renderer.
constexpr std::string_view kGlRenderer = "opengl";
/// SDL's name for its OpenGL ES 2 renderer.
constexpr std::string_view kGles2Renderer = "opengles2";

/// Returns a text SDL gave, or an empty one for none.
///
/// @param text the text; may be null
/// @return the text
std::string_view sdl_text(const char* text) noexcept {
    return text != nullptr ? std::string_view(text) : std::string_view();
}

/// Reads the adapter through the reader of the renderer's graphics
/// interface.
///
/// @param context the renderer
/// @param[in,out] facts the facts, renderer naming the driver
/// @return true when the adapter was read
bool read_adapter(void* context, AdapterFacts& facts) {
    auto* renderer = static_cast<SDL_Renderer*>(context);
    const std::string_view name = facts.renderer;
#if defined(_WIN32)
    if (name == kDirect3d9Renderer || name == kDirect3d11Renderer || name == kDirect3d12Renderer)
        return readers::read_direct3d_adapter(renderer, facts);
#endif
#if defined(__APPLE__)
    if (name == kMetalRenderer)
        return readers::read_metal_adapter(renderer, facts);
#endif
#if defined(_WIN32) || defined(__linux__)
    if (name == vulkan_renderer)
        return readers::read_vulkan_adapter(renderer, facts);
#endif
    if (name == kGlRenderer || name == kGles2Renderer)
        return readers::read_gl_adapter(facts);
    if (name == gpu_renderer)
        return readers::read_gpu_adapter(renderer, facts);
    return false;
}

} // namespace

AdapterFacts describe_reported(
    std::string_view renderer,
    std::string_view video_driver,
    int64_t reported_texture_limit,
    AdapterRead read,
    const AdapterReader& reader
) {
    AdapterFacts facts{};
    facts.renderer = clean_name(renderer);
    facts.video_driver = clean_name(video_driver);
    facts.reported_texture_limit = reported_texture_limit;
    if (facts.renderer == software_renderer) {
        facts.adapter_state = AdapterState::none;
    } else if (read == AdapterRead::skip) {
        facts.adapter_state = AdapterState::skipped;
    } else {
        const bool named = reader.read != nullptr && reader.read(reader.context, facts);
        facts.adapter = clean_name(facts.adapter);
        facts.vendor = clean_name(facts.vendor);
        facts.adapter_state =
            named && !facts.adapter.empty() ? AdapterState::read : AdapterState::unknown;
    }
    classify(facts);
    return facts;
}

AdapterFacts describe(SDL_Renderer* renderer, AdapterRead read) {
    if (renderer == nullptr)
        return {};
    AdapterFacts facts = describe_reported(
        sdl_text(SDL_GetRendererName(renderer)),
        sdl_text(SDL_GetCurrentVideoDriver()),
        SDL_GetNumberProperty(
            SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0
        ),
        read,
        AdapterReader{renderer, read_adapter}
    );
#if defined(_WIN32)
    if (read == AdapterRead::read)
        facts.wine = readers::running_under_wine();
#endif
    return facts;
}

} // namespace oa::platform::render_probe
