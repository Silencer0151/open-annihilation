// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The system's own sound device, for the sound output that mixes in the
// engine (oa/audio/buffered_output.hpp): a ring of buffers of 16-bit stereo
// samples that the device plays in the order they are queued, a thread that
// refills them as they play out, and the lock between that thread and the
// rest of the game.

#include <cstdint>
#include <string>

namespace oa::platform::sound_device {

/// A sound device that plays a ring of buffers in the order they are queued.
struct Hooks {
    void* context{};
    /// Opens the device for 16-bit stereo at `rate` with `buffer_count`
    /// buffers of `buffer_frames` frames, none queued; null means the
    /// system has no such device.
    bool (*open)(
        void* context,
        uint32_t rate,
        uint32_t buffer_frames,
        uint32_t buffer_count,
        std::string& error
    ){};
    /// Tells whether buffer `index` may be filled: it was never queued or has played out.
    bool (*buffer_free)(void* context, uint32_t index){};
    /// Queues buffer `index` to play after those queued before it. Its
    /// samples stay where they are, unchanged, until it is free again.
    bool (*queue_buffer)(void* context, uint32_t index, const int16_t* samples, uint32_t frames){};
    /// Starts calling `pump(argument)` on the device's own thread each time
    /// a buffer has played out, and now and then besides; null runs none.
    bool (*start_pump)(void* context, void (*pump)(void* argument), void* argument){};
    /// Stops calling the pump, returning once it no longer runs; null does nothing.
    void (*stop_pump)(void* context){};
    /// Stops playing and closes the device; null does nothing.
    void (*close)(void* context){};
    /// Takes the lock between the pump and the rest of the game; the thread
    /// holding it may take it again. Null takes none.
    void (*lock)(void* context){};
    /// Releases one hold of the lock. Null releases nothing.
    void (*unlock)(void* context){};
    /// The device's name, for diagnostics.
    const char* name{""};
};

/// Creates the Windows wave-out device, which every Windows version has,
/// Windows XP included.
///
/// Each buffer is a wave header over the caller's samples; a thread of the
/// device's own waits on the event the device signals as each buffer plays
/// out, and runs the pump.
///
/// @return the device's hooks; on other systems hooks whose open is null
[[nodiscard]] Hooks wave_out_create();

/// Closes a device wave_out_create() made, if it is open, and frees it.
///
/// @param[in,out] hooks the device; reset to empty hooks
void wave_out_destroy(Hooks& hooks);

} // namespace oa::platform::sound_device
