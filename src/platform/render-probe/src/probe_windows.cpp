// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Direct3D renderers' adapter on Windows, and whether the game runs
// under Wine. One Windows package runs on every Windows release from XP, so
// nothing here depends on the Windows version and nothing links a graphics
// library: the adapter is read through the device and swap chain SDL made
// for its renderer, by calls on those objects alone, with the interface
// identifiers defined here; Wine is found by looking up a function of
// ntdll.dll, which every process has loaded.
#include "readers.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// Declares the methods that return a structure as taking a pointer to it,
// which is how the Direct3D 12 interfaces return one, with every compiler
// (ID3D12Device::GetAdapterLuid).
#define WIDL_EXPLICIT_AGGREGATE_RETURNS
#include <windows.h>

#include <d3d9.h>
#include <dxgi.h>
#if __has_include(<d3d12.h>) && __has_include(<dxgi1_4.h>)
#define OA_RENDER_PROBE_DIRECT3D12 1
#include <d3d12.h>
#include <dxgi1_4.h>
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

namespace oa::platform::render_probe::readers {
namespace {

/// The IDXGIDevice interface's identifier.
constexpr GUID kDxgiDeviceInterface{
    0x54ec77fa, 0x1377, 0x44e6, {0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c}
};
/// The IDXGIAdapter1 interface's identifier.
constexpr GUID kDxgiAdapter1Interface{
    0x29038f61, 0x3839, 0x4626, {0x91, 0xfd, 0x08, 0x68, 0x79, 0x01, 0x1a, 0x05}
};
#if defined(OA_RENDER_PROBE_DIRECT3D12)
/// The IDXGIFactory4 interface's identifier.
constexpr GUID kDxgiFactory4Interface{
    0x1bc6ea02, 0xef36, 0x464f, {0xbf, 0x0c, 0x21, 0xca, 0x39, 0xe5, 0x16, 0x8a}
};
#endif

/// SDL's name for its Direct3D 9 renderer.
constexpr std::string_view kDirect3d9Renderer = "direct3d";
/// SDL's name for its Direct3D 11 renderer.
constexpr std::string_view kDirect3d11Renderer = "direct3d11";
/// GetAdapterIdentifier's flags for the cheap form, which leaves out the
/// driver's certification.
constexpr DWORD kPlainAdapterIdentifier = 0;

/// Holds one reference to a COM object and releases it when it goes.
template <typename Interface>
class Reference {
  public:

    Reference() = default;
    Reference(const Reference&) = delete;
    Reference& operator=(const Reference&) = delete;

    /// Releases the reference held, if any.
    ~Reference() {
        if (object_ != nullptr)
            object_->Release();
    }

    /// Returns the object.
    ///
    /// @return the object; null for none
    [[nodiscard]] Interface* get() const noexcept { return object_; }

    /// Returns the object, for a call on it.
    ///
    /// @return the object
    Interface* operator->() const noexcept { return object_; }

    /// Returns where a call that hands out a reference writes it.
    ///
    /// @return the held pointer's address; it holds none yet
    [[nodiscard]] Interface** out() noexcept { return &object_; }

    /// Returns where QueryInterface and its kind write the reference.
    ///
    /// @return the held pointer's address, untyped; it holds none yet
    [[nodiscard]] void** out_untyped() noexcept { return reinterpret_cast<void**>(&object_); }

  private:

    Interface* object_{};
};

/// Converts UTF-16 text to UTF-8.
///
/// @param text the text, up to its first zero or its end
/// @param units the units the text may hold
/// @return the text in UTF-8; empty when it cannot be converted
std::string utf8_from_wide(const wchar_t* text, std::size_t units) {
    const int length = static_cast<int>(std::find(text, text + units, L'\0') - text);
    if (length == 0)
        return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return {};
    std::string converted(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, text, length, converted.data(), bytes, nullptr, nullptr) !=
        bytes)
        return {};
    return converted;
}

/// Converts text in the system's code page to UTF-8.
///
/// @param text the text, up to its first zero or its end
/// @param bytes the bytes the text may hold
/// @return the text in UTF-8; empty when it cannot be converted
std::string utf8_from_system(const char* text, std::size_t bytes) {
    const int length = static_cast<int>(std::find(text, text + bytes, '\0') - text);
    if (length == 0)
        return {};
    const int units = MultiByteToWideChar(CP_ACP, 0, text, length, nullptr, 0);
    if (units <= 0)
        return {};
    std::wstring wide(static_cast<std::size_t>(units), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, text, length, wide.data(), units) != units)
        return {};
    return utf8_from_wide(wide.data(), wide.size());
}

/// Reads a DXGI adapter's description: its name, identifiers and, where
/// the adapter gives the newer description, its software flag.
///
/// @param adapter the adapter
/// @param[in,out] facts the facts the description fills
/// @return true when the description was read
bool read_dxgi_adapter(IDXGIAdapter* adapter, AdapterFacts& facts) {
    Reference<IDXGIAdapter1> newer;
    if (SUCCEEDED(adapter->QueryInterface(kDxgiAdapter1Interface, newer.out_untyped())) &&
        newer.get() != nullptr) {
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(newer->GetDesc1(&description)))
            return false;
        facts.adapter = utf8_from_wide(description.Description, std::size(description.Description));
        facts.vendor_id = description.VendorId;
        facts.device_id = description.DeviceId;
        facts.software_flag = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        return true;
    }
    DXGI_ADAPTER_DESC description{};
    if (FAILED(adapter->GetDesc(&description)))
        return false;
    facts.adapter = utf8_from_wide(description.Description, std::size(description.Description));
    facts.vendor_id = description.VendorId;
    facts.device_id = description.DeviceId;
    return true;
}

/// Reads the adapter of SDL's Direct3D 9 device: the identifier of the
/// adapter the device was created on, in its cheap form.
///
/// @param properties the renderer's properties
/// @param[in,out] facts the facts the identifier fills
/// @return true when it was read
bool read_direct3d9(SDL_PropertiesID properties, AdapterFacts& facts) {
    auto* device = static_cast<IDirect3DDevice9*>(
        SDL_GetPointerProperty(properties, SDL_PROP_RENDERER_D3D9_DEVICE_POINTER, nullptr)
    );
    if (device == nullptr)
        return false;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    if (FAILED(device->GetCreationParameters(&creation)))
        return false;
    Reference<IDirect3D9> direct3d;
    if (FAILED(device->GetDirect3D(direct3d.out())) || direct3d.get() == nullptr)
        return false;
    D3DADAPTER_IDENTIFIER9 identifier{};
    if (FAILED(direct3d->GetAdapterIdentifier(
            creation.AdapterOrdinal, kPlainAdapterIdentifier, &identifier
        )))
        return false;
    facts.adapter = utf8_from_system(identifier.Description, std::size(identifier.Description));
    facts.vendor_id = identifier.VendorId;
    facts.device_id = identifier.DeviceId;
    return true;
}

/// Reads the DXGI adapter of SDL's Direct3D 11 device.
///
/// @param properties the renderer's properties
/// @param[in,out] facts the facts the adapter's description fills
/// @return true when it was read
bool read_direct3d11(SDL_PropertiesID properties, AdapterFacts& facts) {
    auto* device = static_cast<IUnknown*>(
        SDL_GetPointerProperty(properties, SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, nullptr)
    );
    if (device == nullptr)
        return false;
    Reference<IDXGIDevice> dxgi_device;
    if (FAILED(device->QueryInterface(kDxgiDeviceInterface, dxgi_device.out_untyped())) ||
        dxgi_device.get() == nullptr)
        return false;
    Reference<IDXGIAdapter> adapter;
    if (FAILED(dxgi_device->GetAdapter(adapter.out())) || adapter.get() == nullptr)
        return false;
    return read_dxgi_adapter(adapter.get(), facts);
}

/// Reads the DXGI adapter of SDL's Direct3D 12 device: the swap chain's
/// factory finds the adapter by the device's locally unique identifier.
/// Built only where the toolchain has the Direct3D 12 headers; elsewhere the
/// adapter reads as unknown.
///
/// @param properties the renderer's properties
/// @param[in,out] facts the facts the adapter's description fills
/// @return true when it was read
bool read_direct3d12(
    [[maybe_unused]] SDL_PropertiesID properties, [[maybe_unused]] AdapterFacts& facts
) {
#if defined(OA_RENDER_PROBE_DIRECT3D12)
    auto* device = static_cast<ID3D12Device*>(
        SDL_GetPointerProperty(properties, SDL_PROP_RENDERER_D3D12_DEVICE_POINTER, nullptr)
    );
    auto* swap_chain = static_cast<IDXGISwapChain*>(
        SDL_GetPointerProperty(properties, SDL_PROP_RENDERER_D3D12_SWAPCHAIN_POINTER, nullptr)
    );
    if (device == nullptr || swap_chain == nullptr)
        return false;
    const LUID adapter_id = device->GetAdapterLuid();
    Reference<IDXGIFactory4> factory;
    if (FAILED(swap_chain->GetParent(kDxgiFactory4Interface, factory.out_untyped())) ||
        factory.get() == nullptr)
        return false;
    Reference<IDXGIAdapter1> adapter;
    if (FAILED(
            factory->EnumAdapterByLuid(adapter_id, kDxgiAdapter1Interface, adapter.out_untyped())
        ) ||
        adapter.get() == nullptr)
        return false;
    return read_dxgi_adapter(adapter.get(), facts);
#else
    return false;
#endif
}

} // namespace

bool read_direct3d_adapter(SDL_Renderer* renderer, AdapterFacts& facts) {
    const SDL_PropertiesID properties = SDL_GetRendererProperties(renderer);
    if (facts.renderer == kDirect3d9Renderer)
        return read_direct3d9(properties, facts);
    if (facts.renderer == kDirect3d11Renderer)
        return read_direct3d11(properties, facts);
    return read_direct3d12(properties, facts);
}

bool running_under_wine() {
    const HMODULE system_library = GetModuleHandleW(L"ntdll.dll");
    return system_library != nullptr &&
           GetProcAddress(system_library, "wine_get_version") != nullptr;
}

} // namespace oa::platform::render_probe::readers
