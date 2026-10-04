# platform/sound-device

The system's own sound device, for the sound output that mixes the game's
streams in the engine (`BufferedOutput` in [src/audio](../../audio/README.md)).
It is the sound output on Windows before Vista unless the environment
variable `OA_SOUND_OUTPUT` is `sdl`, on any other Windows when it is
`waveout`, and in a build without SDL.

`oa/platform/sound_device.hpp` (namespace `oa::platform::sound_device`)
describes a device as `Hooks`: open it for 16-bit stereo at a rate with a
ring of buffers, tell whether a buffer is free, queue a buffer, start and
stop the thread that refills the buffers, close it, and the lock between
that thread and the rest of the game.

`wave_out_create()` makes the Windows wave-out device: each buffer is a wave
header over the caller's samples, prepared once and queued again each time
it has played out; a thread started with `_beginthreadex` waits on the event
the device signals as each buffer finishes (and every 50 ms besides) and
runs the pump; the lock is a critical section. It needs nothing newer than
Windows XP. `wave_out_destroy()` stops and frees it. On other systems
`wave_out_create()` returns hooks whose `open` is null.

The module reads and writes no game state. Its test is the audio output's:
`audio-output` tests the ring on a device of its own, and on Windows
`audio-output-wave-out` plays a tone on this device, or is skipped where the
system has no sound device.
