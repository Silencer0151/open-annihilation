// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The gpu renderer's adapter: the name of SDL's graphics device behind it.
// SDL can name that device only from SDL 3.4; on SDL 3.2 the adapter reads
// as unknown. SDL gives no texture limit for the device, so the device's
// own limit is left unread (0).
#include "readers.hpp"

#include <SDL3/SDL.h>

namespace oa::platform::render_probe::readers {

bool read_gpu_adapter(
    [[maybe_unused]] SDL_Renderer* renderer, [[maybe_unused]] AdapterFacts& facts
) {
#if SDL_VERSION_ATLEAST(3, 4, 0)
    SDL_GPUDevice* device = SDL_GetGPURendererDevice(renderer);
    if (device == nullptr)
        return false;
    const char* name = SDL_GetStringProperty(
        SDL_GetGPUDeviceProperties(device), SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr
    );
    if (name == nullptr)
        return false;
    facts.adapter = name;
    return !facts.adapter.empty();
#else
    return false;
#endif
}

} // namespace oa::platform::render_probe::readers
