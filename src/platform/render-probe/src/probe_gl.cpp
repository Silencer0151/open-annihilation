// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The OpenGL and OpenGL ES 2 renderers' adapter: the renderer and vendor
// strings, read through functions SDL looks up, and only while SDL's
// context is current. SDL makes its context current when it creates the
// renderer, so a call right after creation reads the renderer's own.
#include "readers.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

namespace oa::platform::render_probe::readers {
namespace {

/// The shape of glGetString.
using GetStringFunction = const GLubyte*(GLAPIENTRY*)(GLenum name);

/// Reads one string of the current context.
///
/// @param get_string glGetString
/// @param name the string's name
/// @return the string; empty when the context gave none
std::string_view context_string(GetStringFunction get_string, GLenum name) {
    const GLubyte* text = get_string(name);
    return text != nullptr ? std::string_view(reinterpret_cast<const char*>(text))
                           : std::string_view();
}

} // namespace

bool read_gl_adapter(AdapterFacts& facts) {
    if (SDL_GL_GetCurrentContext() == nullptr)
        return false;
    const auto get_string =
        reinterpret_cast<GetStringFunction>(SDL_GL_GetProcAddress("glGetString"));
    if (get_string == nullptr)
        return false;
    facts.adapter = std::string(context_string(get_string, GL_RENDERER));
    facts.vendor = std::string(context_string(get_string, GL_VENDOR));
    return !facts.adapter.empty();
}

} // namespace oa::platform::render_probe::readers
