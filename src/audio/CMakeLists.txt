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

if(OA_BUILD_PLATFORM)
  find_package(SDL3 3.2 CONFIG REQUIRED)
  add_library(oa-audio-sdl src/sdl_audio.cpp src/sdl_music.cpp)
  add_library(oa::audio::sdl ALIAS oa-audio-sdl)
  target_include_directories(oa-audio-sdl PUBLIC include)
  target_link_libraries(oa-audio-sdl PUBLIC oa-audio SDL3::SDL3 oa-options)
  find_path(OA_FFMPEG_INCLUDE_DIR libavformat/avformat.h HINTS /opt/homebrew/opt/ffmpeg/include)
  find_library(OA_AVFORMAT_LIBRARY NAMES avformat HINTS /opt/homebrew/opt/ffmpeg/lib)
  find_library(OA_AVCODEC_LIBRARY NAMES avcodec HINTS /opt/homebrew/opt/ffmpeg/lib)
  find_library(OA_AVUTIL_LIBRARY NAMES avutil HINTS /opt/homebrew/opt/ffmpeg/lib)
  find_library(OA_SWRESAMPLE_LIBRARY NAMES swresample HINTS /opt/homebrew/opt/ffmpeg/lib)
  if(OA_FFMPEG_INCLUDE_DIR AND OA_AVFORMAT_LIBRARY AND OA_AVCODEC_LIBRARY AND
     OA_AVUTIL_LIBRARY AND OA_SWRESAMPLE_LIBRARY)
    target_compile_definitions(oa-audio-sdl PRIVATE OA_AUDIO_HAVE_FFMPEG=1)
    target_include_directories(oa-audio-sdl PRIVATE ${OA_FFMPEG_INCLUDE_DIR})
    target_link_libraries(oa-audio-sdl PRIVATE ${OA_AVFORMAT_LIBRARY} ${OA_AVCODEC_LIBRARY}
      ${OA_AVUTIL_LIBRARY} ${OA_SWRESAMPLE_LIBRARY})
  else()
    target_compile_definitions(oa-audio-sdl PRIVATE OA_AUDIO_HAVE_FFMPEG=0)
    message(STATUS "oa-audio-sdl: FFmpeg not found; music files will not play")
  endif()
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
    # Without a music decoder the generated tracks cannot play: skipped.
    set_tests_properties(audio-sdl-music PROPERTIES SKIP_RETURN_CODE 77)
    # The installed game's music folder.
    oa_game_data_tests(audio-sdl-music-data)
  endif()
endif()
