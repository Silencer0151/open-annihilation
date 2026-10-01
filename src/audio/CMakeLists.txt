# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-audio
  src/game_audio.cpp
  src/unit_announcements.cpp
  src/wave_chunks.cpp
  src/mixer.cpp
  src/cd_music.cpp
  src/music_mood.cpp
  src/sound_system.cpp
  src/music_disc.cpp
  src/music_session.cpp
  src/spatial_gain.cpp
)
add_library(oa::audio ALIAS oa-audio)
target_include_directories(oa-audio PUBLIC include)
target_link_libraries(oa-audio PUBLIC oa-formats-hpi oa-data-unit-definitions oa-options oa-core-types)

# The offline mix of the game's sound effects that director renders take
# their sound from: integer decoding, resampling and mixing, with no sound
# device and no clock.
add_library(oa-audio-offline-mix src/offline_mix.cpp)
add_library(oa::audio::offline_mix ALIAS oa-audio-offline-mix)
target_include_directories(oa-audio-offline-mix PUBLIC include)
target_link_libraries(oa-audio-offline-mix PUBLIC oa-audio PRIVATE oa-options)
target_compile_features(oa-audio-offline-mix PUBLIC cxx_std_20)
if(BUILD_TESTING)
  # Decoding, resampling, gains, the voice policy, chunked rendering and a
  # pinned mix; with --data, every sound file of the installed game.
  add_executable(oa-audio-offline-mix-test tests/offline_mix_test.cpp)
  target_link_libraries(oa-audio-offline-mix-test
    PRIVATE oa-audio-offline-mix oa-base-sha256 oa-test-game-data)
  add_test(NAME audio-offline-mix COMMAND oa-audio-offline-mix-test)
  oa_add_game_data_test(audio-offline-mix-data oa-audio-offline-mix-test --data)
endif()

# The music decoder: MP3, Ogg Vorbis, FLAC and WAVE files to 44.1 kHz
# stereo float, with the sample-rate converter it shares with the sound
# output. The decoders themselves are kept in third_party/ as their authors
# wrote them and compiled in music_codecs.c without the engine's warnings.
set(oa_music_codec_dirs "${PROJECT_SOURCE_DIR}/third_party/dr_libs"
  "${PROJECT_SOURCE_DIR}/third_party/stb_vorbis")
add_library(oa-audio-music-decoder src/music_decoder.cpp src/resampler.cpp src/music_codecs.c)
add_library(oa::audio::music_decoder ALIAS oa-audio-music-decoder)
target_include_directories(oa-audio-music-decoder PUBLIC include)
target_include_directories(oa-audio-music-decoder SYSTEM PRIVATE ${oa_music_codec_dirs})
target_link_libraries(oa-audio-music-decoder PRIVATE oa-options)
target_compile_features(oa-audio-music-decoder PUBLIC cxx_std_20)
if(MSVC)
  set_source_files_properties(src/music_codecs.c PROPERTIES COMPILE_OPTIONS /w)
else()
  set_source_files_properties(src/music_codecs.c PROPERTIES COMPILE_OPTIONS -w)
endif()
if(NOT WIN32)
  # The Ogg Vorbis decoder calls the C maths library.
  find_library(OA_MATH_LIBRARY m)
  if(OA_MATH_LIBRARY)
    target_link_libraries(oa-audio-music-decoder PRIVATE ${OA_MATH_LIBRARY})
  endif()
endif()
if(BUILD_TESTING)
  # The resampler, the stereo mix, every encoding against the samples it
  # was made from, malformed files, and with --data the installed music.
  add_executable(oa-audio-music-decoder-test tests/music_decoder_test.cpp)
  target_link_libraries(oa-audio-music-decoder-test PRIVATE oa-audio-music-decoder
    oa-test-game-data oa-options)
  target_compile_definitions(oa-audio-music-decoder-test PRIVATE
    OA_AUDIO_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  add_test(NAME audio-music-decoder COMMAND oa-audio-music-decoder-test)
  oa_add_game_data_test(audio-music-decoder-data oa-audio-music-decoder-test --data)
endif()

if(OA_BUILD_PLATFORM)
  find_package(SDL3 3.2 CONFIG REQUIRED)
  add_library(oa-audio-sdl src/sdl_audio.cpp src/sdl_music.cpp)
  add_library(oa::audio::sdl ALIAS oa-audio-sdl)
  target_include_directories(oa-audio-sdl PUBLIC include)
  target_link_libraries(oa-audio-sdl PUBLIC oa-audio SDL3::SDL3 oa-options
    PRIVATE oa-audio-music-decoder)
endif()

if(BUILD_TESTING)
  add_executable(oa-game-audio-test tests/game_audio_test.cpp)
  target_link_libraries(oa-game-audio-test PRIVATE oa-audio oa-data-defs-assets oa-test-game-data)
  add_test(NAME game-audio COMMAND oa-game-audio-test)
  oa_add_game_data_test(game-audio-data oa-game-audio-test --data)

  foreach(test_name mixer cd_music speech_queue music_mood wave_chunks music_session)
    string(REPLACE "_" "-" test_id "${test_name}")
    add_executable(oa-audio-${test_id}-test tests/${test_name}_test.cpp)
    target_link_libraries(oa-audio-${test_id}-test PRIVATE oa-audio)
    add_test(NAME audio-${test_id} COMMAND oa-audio-${test_id}-test)
  endforeach()
  # Every WAV the installed game provides.
  target_link_libraries(oa-audio-wave-chunks-test PRIVATE oa-test-game-data)
  oa_add_game_data_test(audio-wave-chunks-data oa-audio-wave-chunks-test --data)
  if(OA_BUILD_PLATFORM)
    add_executable(oa-audio-sdl-sink-test tests/sdl_sink_test.cpp)
    target_link_libraries(oa-audio-sdl-sink-test PRIVATE oa-audio-sdl)
    add_test(NAME audio-sdl-sink COMMAND oa-audio-sdl-sink-test)
    # The WAV player's one stream: its delay, one playing, stopping at once
    # and replacement, on the dummy device, which plays in real time.
    add_executable(oa-audio-sdl-player-test tests/sdl_player_test.cpp)
    target_link_libraries(oa-audio-sdl-player-test PRIVATE oa-audio-sdl)
    add_test(NAME audio-sdl-player COMMAND oa-audio-sdl-player-test)
    set_tests_properties(audio-sdl-player PROPERTIES TIMEOUT 60
      ENVIRONMENT "SDL_AUDIO_DRIVER=dummy")
    add_executable(oa-audio-sdl-music-test tests/sdl_music_test.cpp)
    target_link_libraries(oa-audio-sdl-music-test PRIVATE oa-audio-sdl oa-test-game-data)
    add_test(NAME audio-sdl-music COMMAND oa-audio-sdl-music-test)
    add_test(NAME audio-sdl-music-data COMMAND oa-audio-sdl-music-test --data)
    set_tests_properties(audio-sdl-music audio-sdl-music-data PROPERTIES TIMEOUT 60
      ENVIRONMENT "SDL_AUDIO_DRIVER=dummy")
    # The installed game's music folder.
    oa_game_data_tests(audio-sdl-music-data)
  endif()
endif()
