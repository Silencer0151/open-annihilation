// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The interfaces of the music decoders kept in third_party/, with the
// options the engine builds them with. music_codecs.c compiles their
// implementations with the same options.

#ifndef OA_AUDIO_MUSIC_CODECS_H
#define OA_AUDIO_MUSIC_CODECS_H

// Every decoder reads through callbacks or a file the engine opens itself,
// so that paths in any script reach the file system.
#define DR_MP3_NO_STDIO
#define DR_FLAC_NO_STDIO
// Decode MPEG audio to float throughout, not through 16-bit integers.
#define DR_MP3_FLOAT_OUTPUT
// Ogg Vorbis is read whole pages at a time from a file, never pushed.
#define STB_VORBIS_NO_PUSHDATA_API

#include "dr_flac.h"
#include "dr_mp3.h"

#ifndef OA_AUDIO_MUSIC_CODECS_IMPLEMENTATION
#define STB_VORBIS_HEADER_ONLY
#endif
#include "stb_vorbis.c"

#endif
