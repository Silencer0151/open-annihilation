// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Windows wave-out device: the sound interface every Windows version
// has, Windows XP included. Its buffers are wave headers over the caller's
// sample buffers; a thread of its own waits on the device's event, which
// the device signals as each buffer plays out, and runs the pump. On other
// systems there is no such device.

#include "oa/platform/sound_device.hpp"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <mmsystem.h>
#include <process.h>

#include <new>
#include <string>
#include <vector>

#endif

namespace oa::platform::sound_device {

#ifdef _WIN32

namespace {

// The pump also runs this often when no buffer finishes, in milliseconds.
constexpr DWORD pump_interval_ms = 50;
constexpr WORD bits_per_sample = 16;
constexpr WORD output_channels = 2;

struct WaveOutDevice {
    HWAVEOUT handle{};
    HANDLE event{};  ///< signalled by the device as each buffer finishes
    HANDLE thread{}; ///< the pump's thread
    CRITICAL_SECTION section{};
    std::vector<WAVEHDR> headers;
    void (*pump)(void* argument){};
    void* pump_argument{};
    volatile LONG running{};
};

std::string wave_out_error(MMRESULT result) {
    char text[MAXERRORLENGTH]{};
    if (waveOutGetErrorTextA(result, text, sizeof(text)) != MMSYSERR_NOERROR)
        return "wave-out error " + std::to_string(result);
    return text;
}

bool device_open(
    void* context, uint32_t rate, uint32_t, uint32_t buffer_count, std::string& error
) {
    auto& device = *static_cast<WaveOutDevice*>(context);
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = output_channels;
    format.nSamplesPerSec = rate;
    format.wBitsPerSample = bits_per_sample;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * bits_per_sample / 8);
    format.nAvgBytesPerSec = rate * format.nBlockAlign;
    device.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (device.event == nullptr) {
        error = "cannot create the sound device's event";
        return false;
    }
    const MMRESULT result = waveOutOpen(
        &device.handle,
        WAVE_MAPPER,
        &format,
        reinterpret_cast<DWORD_PTR>(device.event),
        0,
        CALLBACK_EVENT
    );
    if (result != MMSYSERR_NOERROR) {
        error = wave_out_error(result);
        CloseHandle(device.event);
        device.event = nullptr;
        device.handle = nullptr;
        return false;
    }
    device.headers.assign(buffer_count, WAVEHDR{});
    return true;
}

bool device_buffer_free(void* context, uint32_t index) {
    const auto& header = static_cast<WaveOutDevice*>(context)->headers[index];
    return (header.dwFlags & WHDR_INQUEUE) == 0;
}

bool device_queue_buffer(void* context, uint32_t index, const int16_t* samples, uint32_t frames) {
    auto& device = *static_cast<WaveOutDevice*>(context);
    auto& header = device.headers[index];
    const auto bytes = static_cast<DWORD>(frames * output_channels * sizeof(int16_t));
    // The output's buffers stay in place, so each header is prepared once.
    if ((header.dwFlags & WHDR_PREPARED) == 0) {
        header.lpData = reinterpret_cast<LPSTR>(const_cast<int16_t*>(samples));
        header.dwBufferLength = bytes;
        if (waveOutPrepareHeader(device.handle, &header, sizeof(header)) != MMSYSERR_NOERROR)
            return false;
    }
    header.dwFlags &= ~static_cast<DWORD>(WHDR_DONE);
    return waveOutWrite(device.handle, &header, sizeof(header)) == MMSYSERR_NOERROR;
}

unsigned __stdcall pump_thread(void* context) {
    auto& device = *static_cast<WaveOutDevice*>(context);
    while (InterlockedCompareExchange(&device.running, 1, 1) != 0) {
        WaitForSingleObject(device.event, pump_interval_ms);
        if (InterlockedCompareExchange(&device.running, 1, 1) != 0)
            device.pump(device.pump_argument);
    }
    return 0;
}

bool device_start_pump(void* context, void (*pump)(void*), void* argument) {
    auto& device = *static_cast<WaveOutDevice*>(context);
    device.pump = pump;
    device.pump_argument = argument;
    InterlockedExchange(&device.running, 1);
    const uintptr_t thread = _beginthreadex(nullptr, 0, pump_thread, &device, 0, nullptr);
    if (thread == 0) {
        InterlockedExchange(&device.running, 0);
        return false;
    }
    device.thread = reinterpret_cast<HANDLE>(thread);
    SetThreadPriority(device.thread, THREAD_PRIORITY_HIGHEST);
    return true;
}

void device_stop_pump(void* context) {
    auto& device = *static_cast<WaveOutDevice*>(context);
    if (device.thread == nullptr)
        return;
    InterlockedExchange(&device.running, 0);
    SetEvent(device.event);
    WaitForSingleObject(device.thread, INFINITE);
    CloseHandle(device.thread);
    device.thread = nullptr;
}

void device_close(void* context) {
    auto& device = *static_cast<WaveOutDevice*>(context);
    if (device.handle != nullptr) {
        waveOutReset(device.handle);
        for (auto& header : device.headers)
            if ((header.dwFlags & WHDR_PREPARED) != 0)
                waveOutUnprepareHeader(device.handle, &header, sizeof(header));
        waveOutClose(device.handle);
        device.handle = nullptr;
    }
    device.headers.clear();
    if (device.event != nullptr) {
        CloseHandle(device.event);
        device.event = nullptr;
    }
}

void device_lock(void* context) {
    EnterCriticalSection(&static_cast<WaveOutDevice*>(context)->section);
}

void device_unlock(void* context) {
    LeaveCriticalSection(&static_cast<WaveOutDevice*>(context)->section);
}

} // namespace

Hooks wave_out_create() {
    auto* device = new (std::nothrow) WaveOutDevice{};
    if (device == nullptr)
        return {};
    InitializeCriticalSection(&device->section);
    Hooks hooks{};
    hooks.context = device;
    hooks.open = device_open;
    hooks.buffer_free = device_buffer_free;
    hooks.queue_buffer = device_queue_buffer;
    hooks.start_pump = device_start_pump;
    hooks.stop_pump = device_stop_pump;
    hooks.close = device_close;
    hooks.lock = device_lock;
    hooks.unlock = device_unlock;
    hooks.name = "waveout";
    return hooks;
}

void wave_out_destroy(Hooks& hooks) {
    auto* device = static_cast<WaveOutDevice*>(hooks.context);
    if (device != nullptr) {
        device_stop_pump(device);
        device_close(device);
        DeleteCriticalSection(&device->section);
        delete device;
    }
    hooks = {};
}

#else

Hooks wave_out_create() {
    return {};
}

void wave_out_destroy(Hooks& hooks) {
    hooks = {};
}

#endif

} // namespace oa::platform::sound_device
