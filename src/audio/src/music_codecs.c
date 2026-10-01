// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Compiles the music decoders kept in third_party/ with the options
// music_codecs.h sets.

#define OA_AUDIO_MUSIC_CODECS_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#include "music_codecs.h"
