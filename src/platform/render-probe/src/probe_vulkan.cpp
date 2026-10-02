// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Vulkan renderer's adapter, on Windows and Linux: the physical device
// SDL chose, read through the vkGetInstanceProcAddr of the Vulkan library
// SDL loaded and the instance SDL made. No Vulkan header is used: the head
// of the device's properties is declared here, in a buffer larger than the
// whole structure, which the device fills.
#include "readers.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

namespace oa::platform::render_probe::readers {
namespace {

#if defined(_WIN32)
#define OA_VULKAN_CALL __stdcall
#else
#define OA_VULKAN_CALL
#endif

/// Bytes of a Vulkan device name, its terminating zero included.
constexpr std::size_t kDeviceNameBytes = 256;
/// Bytes of a Vulkan pipeline cache identifier.
constexpr std::size_t kPipelineCacheIdBytes = 16;
/// Bytes kept for the device's properties: more than the whole structure
/// takes on any system (824 bytes on a 64-bit one).
constexpr std::size_t kDevicePropertiesBytes = 1024;
/// The device type of a device that draws on the processor.
constexpr int32_t kCpuDeviceType = 4;

/// The head of the device limits: the two image limits read, and a 64-bit
/// field that gives the head the alignment the whole limits structure has
/// on this system, whose own 64-bit fields follow.
struct LimitsHead {
    uint32_t max_image_dimension_1d{}; ///< the largest 1D image, texels
    uint32_t max_image_dimension_2d{}; ///< the largest side of a 2D image, texels
    uint64_t alignment_of_the_rest{};  ///< not read: sets the alignment
};

/// The head of a Vulkan physical device's properties, up to its limits,
/// laid out as the whole structure lays it out.
struct DevicePropertiesHead {
    uint32_t api_version{};    ///< the Vulkan version the device supports
    uint32_t driver_version{}; ///< the driver's own version number
    uint32_t vendor_id{};      ///< the PCI vendor identifier, or the Khronos one for others
    uint32_t device_id{};      ///< the device identifier
    int32_t device_type{};     ///< kCpuDeviceType for a device that draws on the processor
    char device_name[kDeviceNameBytes]{};               ///< zero-terminated UTF-8
    uint8_t pipeline_cache_id[kPipelineCacheIdBytes]{}; ///< not read
    LimitsHead limits{};                                ///< the head of the device limits
};

static_assert(offsetof(DevicePropertiesHead, device_name) == 20);
static_assert(offsetof(DevicePropertiesHead, pipeline_cache_id) == 276);
static_assert(sizeof(DevicePropertiesHead) <= kDevicePropertiesBytes);

/// The shape of vkGetInstanceProcAddr.
using GetInstanceProcAddress = SDL_FunctionPointer(OA_VULKAN_CALL*)(VkInstance, const char*);
/// The shape of vkGetPhysicalDeviceProperties, which writes the whole
/// properties structure.
using GetDeviceProperties = void(OA_VULKAN_CALL*)(VkPhysicalDevice, void*);

} // namespace

bool read_vulkan_adapter(SDL_Renderer* renderer, AdapterFacts& facts) {
    const SDL_PropertiesID properties = SDL_GetRendererProperties(renderer);
    auto* instance = static_cast<VkInstance>(
        SDL_GetPointerProperty(properties, SDL_PROP_RENDERER_VULKAN_INSTANCE_POINTER, nullptr)
    );
    auto* device = static_cast<VkPhysicalDevice>(SDL_GetPointerProperty(
        properties, SDL_PROP_RENDERER_VULKAN_PHYSICAL_DEVICE_POINTER, nullptr
    ));
    if (instance == nullptr || device == nullptr)
        return false;
    const auto get_address =
        reinterpret_cast<GetInstanceProcAddress>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (get_address == nullptr)
        return false;
    const auto get_properties = reinterpret_cast<GetDeviceProperties>(
        get_address(instance, "vkGetPhysicalDeviceProperties")
    );
    if (get_properties == nullptr)
        return false;
    alignas(DevicePropertiesHead) std::array<uint8_t, kDevicePropertiesBytes> whole{};
    get_properties(device, whole.data());
    DevicePropertiesHead head{};
    std::memcpy(&head, whole.data(), sizeof head);
    const char* name = head.device_name;
    facts.adapter.assign(name, std::find(name, name + kDeviceNameBytes, '\0'));
    facts.vendor_id = head.vendor_id;
    facts.device_id = head.device_id;
    facts.software_flag = head.device_type == kCpuDeviceType;
    facts.device_texture_limit = head.limits.max_image_dimension_2d;
    return !facts.adapter.empty();
}

} // namespace oa::platform::render_probe::readers
