# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(open_annihilation VERSION 0.4.1 LANGUAGES C CXX)
# The Check build type, the build the test suite is validated in
# (cmake/OaBuildTypes.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/cmake/OaBuildTypes.cmake")
# End-to-end app checks run the real game headless; a loaded machine can
# stretch their wall time several-fold without any extra CPU work.
set(OA_NATIVE_CHECK_TIMEOUT 1800)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(OA_BUILD_PLATFORM "Build the SDL3 image and audio shell" ON)
option(OA_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer; any report stops the process" OFF)
option(OA_REQUIRE_GAME "Fail configuration when oa-game cannot be built" OFF)
include(CTest)
find_package(ZLIB REQUIRED)

# OA_GAME_DIR, the installed game the game-data tests and native checks read,
# and the helpers that register those tests (cmake/OaGameData.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/cmake/OaGameData.cmake")
# OA_DEMO_INSTALLER, the installer of the Total Annihilation demo (1997) the
# demo installer tests read (cmake/OaDemoInstaller.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/cmake/OaDemoInstaller.cmake")

# oa-options, the compile options every target that compiles engine code
# links (cmake/OaOptions.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/cmake/OaOptions.cmake")
# The sanitizers instrument every target in this tree, not only those that
# link oa-options, and stop at the first report so the test that made it
# fails. oa-options carries the same flags, so the targets of a project that
# adds this engine are instrumented like the engine's when they link
# oa-options. Clang leaves alignment unchecked only in the code
# cmake/sanitizer-ignorelist.txt lists, which reads the packed canonical
# records' fields in place; GCC cannot limit the check that way, so it
# drops it everywhere. Until platform-shims adapts to a sanitizer build,
# run the suite the way the CI sanitizer job does:
#   ASAN_OPTIONS=detect_leaks=0:handle_sigfpe=0 ctest
# platform-shims ends a child with SIGFPE on purpose.
if(OA_SANITIZERS)
  if(MSVC)
    message(FATAL_ERROR "OA_SANITIZERS needs Clang or GCC: this compiler has no UndefinedBehaviorSanitizer")
  endif()
  set(oa_sanitizer_link_flags -fsanitize=address,undefined)
  set(oa_sanitizer_flags ${oa_sanitizer_link_flags} -fno-sanitize-recover=all -fno-omit-frame-pointer)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND oa_sanitizer_flags
      "-fsanitize-ignorelist=${open_annihilation_SOURCE_DIR}/cmake/sanitizer-ignorelist.txt")
  else()
    list(APPEND oa_sanitizer_flags -fno-sanitize=alignment)
    message(STATUS "OA_SANITIZERS: alignment is not checked; only Clang can leave out just the packed-record code")
  endif()
  # A compiler without the sanitizer runtimes would otherwise fail only when
  # the first program links. A failed check is forgotten so the next
  # configuration tries again.
  include(CMakePushCheckState)
  include(CheckCSourceCompiles)
  include(CheckCXXSourceCompiles)
  cmake_push_check_state(RESET)
  list(JOIN oa_sanitizer_flags " " CMAKE_REQUIRED_FLAGS)
  set(CMAKE_REQUIRED_LINK_OPTIONS ${oa_sanitizer_link_flags})
  check_c_source_compiles("int main(void) { return 0; }" OA_SANITIZERS_C_LINKS)
  check_cxx_source_compiles("int main() { return 0; }" OA_SANITIZERS_CXX_LINKS)
  cmake_pop_check_state()
  if(NOT OA_SANITIZERS_C_LINKS OR NOT OA_SANITIZERS_CXX_LINKS)
    unset(OA_SANITIZERS_C_LINKS CACHE)
    unset(OA_SANITIZERS_CXX_LINKS CACHE)
    list(JOIN oa_sanitizer_flags " " oa_sanitizer_flag_text)
    message(FATAL_ERROR "OA_SANITIZERS: the C or C++ compiler cannot build and link a program with "
      "${oa_sanitizer_flag_text}; install its AddressSanitizer and UndefinedBehaviorSanitizer "
      "runtimes, or configure with a compiler that has them")
  endif()
  add_compile_options(${oa_sanitizer_flags})
  add_link_options(${oa_sanitizer_link_flags})
  target_compile_options(oa-options INTERFACE ${oa_sanitizer_flags})
  target_link_options(oa-options INTERFACE ${oa_sanitizer_link_flags})
endif()

add_subdirectory(src/base/game-loop)
add_subdirectory(src/platform/preferences)
add_subdirectory(src/app/command-line)
add_subdirectory(src/data/mission-types)
add_subdirectory(src/sim/unit-activation)
add_subdirectory(src/base/game-math)
add_subdirectory(src/base/geometry)
add_subdirectory(src/base/sha256)
add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/ui/services)
add_subdirectory(src/present)
add_subdirectory(src/ui/frontend-state)
add_subdirectory(src/sim/script-state)
add_subdirectory(src/sim/script-vm)
add_subdirectory(src/sim/unit-script)
add_subdirectory(src/formats/cob)
add_subdirectory(src/formats/smacker)
add_subdirectory(src/media)
add_subdirectory(src/ui/gui-layout)
add_subdirectory(src/ui/gui-input)
add_subdirectory(src/formats/gaf)
add_subdirectory(src/formats/fnt)
add_subdirectory(src/formats/png)
add_subdirectory(src/formats/pe)
add_subdirectory(src/formats/oascript)
add_subdirectory(src/formats/zip)
add_subdirectory(src/sim/sprite-animation)
add_subdirectory(src/formats/tnt)
add_subdirectory(src/formats/ota)
add_subdirectory(src/sim/simulation-state)
add_subdirectory(src/sim/unit-spawn)
add_subdirectory(src/sim/ballistics)
add_subdirectory(src/sim/combat-state)
add_subdirectory(src/formats/tdf)
add_subdirectory(src/data/unit-definitions)
add_subdirectory(src/formats/objects3d)
add_subdirectory(src/sim/model-runtime)
add_subdirectory(src/sim/gameplay-input)
add_subdirectory(src/sim/spatial-state)
add_subdirectory(src/sim/visibility-state)
add_subdirectory(src/sim/map-runtime)
add_subdirectory(src/sim/feature-runtime)
add_subdirectory(src/sim/unit-movement)
add_subdirectory(src/sim/world-environment)
add_subdirectory(src/sim/ground-orders)
add_subdirectory(src/sim/air)
add_subdirectory(src/data/persist)
add_subdirectory(src/sim/scenario)
add_subdirectory(src/data/campaign)
add_subdirectory(src/sim/unit-health)
add_subdirectory(src/sim/weapon-execution)
add_subdirectory(src/sim/match-runtime)
add_subdirectory(src/sim/ai)
add_subdirectory(src/sim/unit-effects)
add_subdirectory(src/sim/effect-particles)
add_subdirectory(src/sim/selection)
add_subdirectory(src/sim/mission-units)
add_subdirectory(src/sim/messages)
add_subdirectory(src/sim/speed)
add_subdirectory(src/sim/profile)
add_subdirectory(src/sim/detection)
add_subdirectory(src/sim/trace)
add_subdirectory(src/sim/state-hash)
add_subdirectory(src/ui)
add_subdirectory(src/ui/display-layout)
add_subdirectory(src/ui/screen-registry)
add_subdirectory(src/sim/session)
if(TARGET oa-media-intro-player)
  add_executable(oa-intro src/media/intro_main.cpp)
  target_link_libraries(oa-intro PRIVATE oa-media-intro-player oa-options)
endif()

add_subdirectory(src/formats/sqsh)
add_subdirectory(src/formats/hpi)
add_subdirectory(src/data/defs)
add_subdirectory(src/present/world-renderer)
add_subdirectory(src/ui/frontend-renderer)
add_subdirectory(src/audio)
# The app's headers: its extension table, Runtime and the other headers an
# extension compiles against.
add_library(oa-app-headers INTERFACE)
add_library(oa::app::headers ALIAS oa-app-headers)
target_include_directories(oa-app-headers INTERFACE src/app/include)
target_link_libraries(oa-app-headers INTERFACE oa-base-sha256)
# oa-game's command line, which the option tests parse without a running app.
add_library(oa-app-game-options src/app/startup.cpp)
add_library(oa::app::game_options ALIAS oa-app-game-options)
target_link_libraries(oa-app-game-options PUBLIC oa-app-headers)
target_link_libraries(oa-app-game-options PUBLIC oa-ui-frontend-renderer oa-ui-frontend-state oa-sim-scenario
  oa-ui-screen-registry oa-app-command-line oa-platform-preferences PRIVATE oa-options)
# oa-game's game folder: --game-dir, the remembered folder or the folder
# dialog's choice, the installation checks, and the installer of the Total
# Annihilation demo (1997) recognised and its archive unpacked.
add_library(oa-app-game-directory src/app/game_directory.cpp src/app/demo_installer.cpp)
add_library(oa::app::game_directory ALIAS oa-app-game-directory)
target_link_libraries(oa-app-game-directory PUBLIC oa-app-headers oa-base-sha256 oa-platform-preferences
  PRIVATE oa-formats-hpi oa-formats-pe oa-options)
# The game's window icon: the Open Annihilation icon at 256 by 256 pixels
# (branding/, made by tools/make_icons.py), embedded as a PNG file and
# decoded when the window opens.
set(oa_window_icon_png "${CMAKE_CURRENT_SOURCE_DIR}/branding/open-annihilation-256.png")
set(oa_window_icon_source "${CMAKE_CURRENT_BINARY_DIR}/generated/window_icon_png.cpp")
add_custom_command(OUTPUT "${oa_window_icon_source}"
  COMMAND ${CMAKE_COMMAND} "-DINPUT=${oa_window_icon_png}" "-DOUTPUT=${oa_window_icon_source}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/OaEmbedWindowIcon.cmake"
  DEPENDS "${oa_window_icon_png}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/OaEmbedWindowIcon.cmake"
  COMMENT "Embedding the window icon"
  VERBATIM)
add_library(oa-app-window-icon src/app/window_icon.cpp "${oa_window_icon_source}")
add_library(oa::app::window_icon ALIAS oa-app-window-icon)
target_link_libraries(oa-app-window-icon PUBLIC oa-app-headers PRIVATE oa-formats-png oa-options)
# The developer's video capture (--capture-video): presented frames and the
# mix SDL plays, encoded by the ffmpeg program (docs/capture.md).
if(OA_BUILD_PLATFORM)
  add_library(oa-app-video-capture src/app/video_capture.cpp)
  add_library(oa::app::video_capture ALIAS oa-app-video-capture)
  target_link_libraries(oa-app-video-capture PUBLIC oa-app-headers SDL3::SDL3 PRIVATE oa-options)
  if(BUILD_TESTING)
    add_executable(oa-app-video-capture-test src/app/video_capture_test.cpp)
    target_link_libraries(oa-app-video-capture-test PRIVATE oa-app-video-capture oa-options)
    add_test(NAME app-video-capture COMMAND oa-app-video-capture-test)
    # The check host's table (check_host.hpp) and the parts of it that need
    # no running game: the pointer events it makes, the gadgets it finds and
    # the text it hands back.
    add_executable(oa-app-check-host-test src/app/check_host_test.cpp src/app/check_host_input.cpp)
    target_link_libraries(oa-app-check-host-test PRIVATE oa-app-headers oa-ui-gui-layout
      oa-ui-screen-registry SDL3::SDL3 oa-options)
    add_test(NAME app-check-host COMMAND oa-app-check-host-test)
  endif()
endif()
# The files a director render writes (--render-script), the ffmpeg runs that
# encode them, and the director bundle (.oamovie) that carries a script with
# its recording (director_output.hpp, docs/director.md).
if(OA_BUILD_PLATFORM)
  add_library(oa-app-director-output src/app/director_output.cpp)
  add_library(oa::app::director_output ALIAS oa-app-director-output)
  target_link_libraries(oa-app-director-output PUBLIC oa-app-headers oa-formats-zip oa-formats-oascript
    oa-base-sha256 SDL3::SDL3 PRIVATE oa-audio-offline-mix oa-options)
  if(BUILD_TESTING)
    # File names, encoder arguments, the files of a render with encoding
    # off, the environment's settings and bundles, malformed ones included.
    add_executable(oa-app-director-output-test src/app/director_output_test.cpp)
    target_link_libraries(oa-app-director-output-test PRIVATE oa-app-director-output
      oa-audio-offline-mix oa-formats-zip oa-base-sha256 oa-options)
    add_test(NAME app-director-output COMMAND oa-app-director-output-test)
  endif()
endif()
if(BUILD_TESTING)
  add_executable(oa-app-window-icon-test src/app/window_icon_test.cpp)
  target_link_libraries(oa-app-window-icon-test PRIVATE oa-app-window-icon oa-formats-png oa-options)
  add_test(NAME app-window-icon COMMAND oa-app-window-icon-test)
  add_executable(oa-game-options-test src/app/startup_test.cpp)
  target_link_libraries(oa-game-options-test PRIVATE oa-app-game-options oa-options)
  add_test(NAME game-options COMMAND oa-game-options-test)
  add_executable(oa-game-directory-test src/app/game_directory_test.cpp)
  target_link_libraries(oa-game-directory-test PRIVATE oa-app-game-directory oa-app-headers oa-formats-hpi
    oa-platform-preferences oa-test-game-data oa-options)
  add_test(NAME game-directory COMMAND oa-game-directory-test)
  # The installation OA_GAME_DIR names passes the folder dialog's check.
  oa_add_game_data_test(game-directory-install oa-game-directory-test --install)
  # The demo's installer recognised and its archive unpacked, over a
  # synthetic installer; with --data, over the one OA_DEMO_INSTALLER names.
  add_executable(oa-demo-installer-test src/app/demo_installer_test.cpp)
  target_link_libraries(oa-demo-installer-test PRIVATE oa-app-game-directory oa-app-headers oa-base-sha256
    oa-formats-hpi oa-formats-pe-test-image oa-test-game-data oa-options)
  add_test(NAME demo-installer COMMAND oa-demo-installer-test)
  add_test(NAME demo-installer-data COMMAND oa-demo-installer-test --data)
  set_tests_properties(demo-installer-data PROPERTIES TIMEOUT 300)
  oa_demo_installer_tests(demo-installer-data)
  # The draw states the match renderer keeps per unit slot and per 3D feature.
  add_executable(oa-match-model-draws-test src/app/match_model_draws_test.cpp)
  target_link_libraries(oa-match-model-draws-test PRIVATE oa-app-headers oa-present-model oa-sim-feature-runtime
    oa-options)
  add_test(NAME match-model-draws COMMAND oa-match-model-draws-test)
  # When the match clock steps: menus, the outcome and the pause bit.
  add_executable(oa-app-match-clock-test src/app/match_clock_test.cpp src/app/match_clock.cpp)
  target_link_libraries(oa-app-match-clock-test PRIVATE oa-base-game-loop oa-ui-console oa-options)
  add_test(NAME app-match-clock COMMAND oa-app-match-clock-test)
  # Alt+Enter: the key, the mode it asks for while the window is still
  # switching, and a window of SDL's dummy video driver switched and back.
  add_executable(oa-app-full-screen-test src/app/full_screen_test.cpp src/app/full_screen.cpp)
  target_link_libraries(oa-app-full-screen-test PRIVATE oa-app-headers SDL3::SDL3 oa-options)
  add_test(NAME app-full-screen COMMAND oa-app-full-screen-test)
  set_tests_properties(app-full-screen PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
endif()
set(oa_game_sources src/app/main.cpp src/app/full_screen.cpp
  src/app/game_directory_dialog.cpp src/app/runtime.cpp
  src/app/runtime_builtin_screens.cpp
  src/app/runtime_camera.cpp src/app/runtime_campaign.cpp src/app/runtime_checks.cpp
  src/app/runtime_console.cpp src/app/runtime_console_options.cpp src/app/runtime_console_debug.cpp
  src/app/runtime_console_sound.cpp src/app/runtime_console_display.cpp
  src/app/runtime_console_units.cpp
  src/app/runtime_console_cheats.cpp src/app/runtime_poster.cpp
  src/app/runtime_order_overlays.cpp
  src/app/runtime_events.cpp src/app/runtime_frontend_host.cpp src/app/runtime_gaf.cpp
  src/app/runtime_hotkeys.cpp src/app/runtime_hud.cpp src/app/runtime_loading.cpp
  src/app/runtime_match_hud.cpp src/app/runtime_match_menus.cpp src/app/runtime_load_game.cpp
  src/app/runtime_order_panel.cpp src/app/runtime_order_check.cpp
  src/app/runtime_match_render.cpp src/app/runtime_messages.cpp src/app/runtime_pointer.cpp
  src/app/runtime_present.cpp src/app/runtime_display.cpp
  src/app/runtime_terrain_filter.cpp src/app/runtime_reclaim_check.cpp src/app/runtime_patrol_check.cpp src/app/runtime_fog.cpp
  src/app/runtime_reclaim_cursor_check.cpp
  src/app/runtime_pointer_press.cpp src/app/runtime_pointer_check.cpp
  src/app/runtime_dgun_check.cpp
  src/app/runtime_selection.cpp src/app/runtime_skirmish_host.cpp
  src/app/runtime_skirmish_start.cpp src/app/runtime_world_draw.cpp src/app/runtime_benchmark.cpp src/app/runtime_music.cpp
  src/app/runtime_saveload.cpp src/app/runtime_sides.cpp src/app/asset_files.cpp
  src/app/runtime_platform.cpp src/app/runtime_endgame.cpp src/app/runtime_radar.cpp
  src/app/runtime_factory_check.cpp src/app/runtime_build_page_check.cpp
  src/app/runtime_kill_board.cpp src/app/runtime_frontend_controls_check.cpp
  src/app/runtime_briefing_narration_check.cpp
  src/app/runtime_build_site_check.cpp src/app/runtime_multiplayer_check.cpp
  src/app/runtime_check_host.cpp src/app/check_host_input.cpp
  src/app/runtime_notices.cpp src/app/runtime_notice_check.cpp src/app/web_link.cpp
  src/app/runtime_mission_orders.cpp src/app/runtime_showcase.cpp src/app/match_clock.cpp
  src/app/runtime_team_panels.cpp src/app/runtime_team_panel_check.cpp
  src/app/runtime_scroll_bars.cpp src/app/runtime_scroll_bar_check.cpp
  src/app/runtime_director_view.cpp src/app/runtime_director.cpp)
# The run manifest of a director render names the engine's version.
set_property(SOURCE src/app/runtime_director.cpp APPEND PROPERTY COMPILE_DEFINITIONS
  "OA_ENGINE_VERSION=\"${PROJECT_VERSION}\"")
set(oa_game_libraries oa-formats-hpi oa-app-game-options oa-app-game-directory oa-app-window-icon
  oa-app-video-capture
  oa-ui-frontend-renderer oa-ui-gui-input oa-ui-frontend-state oa-audio-sdl
  oa-platform-preferences oa-app-command-line oa-formats-tnt oa-sim-unit-spawn-runtime oa-data-mission-types oa-present-world-renderer oa-sim-combat-state oa-sim-match-runtime oa-sim-unit-effects oa-sim-map-runtime oa-base-game-loop oa-sim-gameplay-input oa-sim-sprite-animation oa-ui-screen-registry oa-options
  oa-ui-hud oa-present-model oa-ui-frontend oa-ui-campaign oa-sim-session oa-ui-console
  oa-ui-services oa-sim-mission-units oa-sim-match-runtime-mission-units oa-sim-selection oa-sim-messages oa-sim-speed
  oa-data-campaign-assets oa-sim-profile oa-sim-state-hash oa-app-extensions)
# The table that combines the extensions' tables of oa-game
# (extension_list.hpp); the extensions themselves are registered with
# oa_add_extension (cmake/OaExtensions.cmake).
add_library(oa-app-extensions src/app/extension_list.cpp)
add_library(oa::app::extensions ALIAS oa-app-extensions)
target_link_libraries(oa-app-extensions PUBLIC oa-app-headers
  PRIVATE oa-app-command-line oa-ui-console oa-ui-hud oa-sim-messages oa-options)
if(BUILD_TESTING)
  # Each rule by which it combines two extensions' hooks.
  add_executable(oa-app-extension-list-test src/app/extension_list_test.cpp)
  target_link_libraries(oa-app-extension-list-test PRIVATE oa-app-extensions oa-app-command-line
    oa-ui-console oa-ui-hud oa-sim-messages oa-options)
  add_test(NAME app-extension-list COMMAND oa-app-extension-list-test)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/cmake/OaExtensions.cmake")
# Without FFmpeg, oa-game links the intro player that skips each movie with
# one logged line, so the game builds wherever the SDL3 shell does.
if(TARGET oa-media-intro-player)
  set(oa_game_intro_player oa-media-intro-player)
else()
  set(oa_game_intro_player oa-media-intro-player-null)
endif()
if(OA_BUILD_PLATFORM)
  add_executable(oa-game ${oa_game_sources})
  # The game players start is open-annihilation (open-annihilation.exe on
  # Windows); the target keeps its name, and tests reach the file through
  # $<TARGET_FILE:oa-game>. On macOS it is the application bundle
  # open-annihilation.app, and the files that travel with it go in the
  # folder its OA_GAME_FILES_DIR property names (cmake/OaGameBundle.cmake).
  set_target_properties(oa-game PROPERTIES OUTPUT_NAME open-annihilation)
  include("${CMAKE_CURRENT_LIST_DIR}/cmake/OaGameBundle.cmake")
  oa_game_bundle(oa-game)
  if(WIN32)
    # A Windows program, so a game started from Explorer opens no console
    # window; its output goes to the log folder, or to whatever captures it.
    set_target_properties(oa-game PROPERTIES WIN32_EXECUTABLE ON)
    # open-annihilation.exe's icon and version information.
    set(OA_WINDOWS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/branding/open-annihilation.ico")
    configure_file(src/app/open-annihilation.rc.in "${CMAKE_CURRENT_BINARY_DIR}/generated/open-annihilation.rc" @ONLY)
    target_sources(oa-game PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/open-annihilation.rc")
  endif()
  target_link_libraries(oa-game PRIVATE oa-ui-display-layout)
  target_link_libraries(oa-game PRIVATE oa-media-director oa-media-director-planner oa-audio-offline-mix
    oa-formats-oascript oa-formats-zip oa-app-director-output)
  target_link_libraries(oa-game PRIVATE ${oa_game_intro_player} SDL3::SDL3 ${oa_game_libraries}
    oa-ui-frontend-dialogs)
  if(BUILD_TESTING)
    # The native checks run oa-game over the installation OA_GAME_DIR names;
    # without one they are not added.
    if(IS_DIRECTORY "${OA_GAME_DIR}")
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native-checks")
      # Restart from the exit menu part way into the first Arm mission must
      # start it over with the units its first start created.
      add_test(NAME native-campaign-restart COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --skip-intro --mute --headless-check --campaign "Arm Campaign" --mission 0
        --match-ticks 60 --restart-at 30
        --preferences-file "${CMAKE_CURRENT_BINARY_DIR}/native-checks/campaign-restart.conf")
      set_tests_properties(native-campaign-restart PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT})
      # The match frame presented through SDL on dummy devices must equal the
      # CPU composition headless runs use, with the overlays visible, and a
      # nano particle fills a square two pixels on a side at zoom 1 and four
      # at zoom 2. A won skirmish then leaves its VICTORY frame on its own for
      # the end screen, which darkens that frame at the match's size.
      set(oa_layer_check_dir "${CMAKE_CURRENT_BINARY_DIR}/native-checks/match-layers")
      file(MAKE_DIRECTORY "${oa_layer_check_dir}")
      add_test(NAME native-match-layers COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --skip-intro --mute --check-match-layers
        --preferences-file "${oa_layer_check_dir}/match-layers.conf")
      set_tests_properties(native-match-layers PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
        WORKING_DIRECTORY "${oa_layer_check_dir}"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
      # LOADGAME.GUI from Single Player and, through the SDL presenter, as the
      # save and load dialogs over a paused skirmish: each placed as its loader
      # places it over the darkened panel below, its bitmap in that palette.
      set(oa_loadsave_check_dir "${CMAKE_CURRENT_BINARY_DIR}/native-checks/loadsave")
      file(MAKE_DIRECTORY "${oa_loadsave_check_dir}")
      add_test(NAME native-loadsave-screens COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --skip-intro --mute --check-load-save
        --preferences-file "${oa_loadsave_check_dir}/loadsave.conf")
      set_tests_properties(native-loadsave-screens PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
        WORKING_DIRECTORY "${oa_loadsave_check_dir}"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
      # Every option of SKIRMISH.GUI, NEWGAME.GUI and the options sub-panels
      # clicked through the SDL presenter shows the setting it changed (its
      # staged caption, frame or text) before Start; the clicked options tab
      # shows pressed and HELPTEXT shows the help under the pointer.
      # NEWGAME.GUI's Campaign list selects the clicked campaign, steps with
      # Up and Down until a button takes the focus, scrolls with its arrow and
      # starts a double-clicked one.
      set(oa_frontend_controls_dir "${CMAKE_CURRENT_BINARY_DIR}/native-checks/frontend-controls")
      file(MAKE_DIRECTORY "${oa_frontend_controls_dir}")
      add_test(NAME native-frontend-controls COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --skip-intro --mute --check-frontend-controls
        --snapshot "${oa_frontend_controls_dir}/frontend-controls.ppm"
        --preferences-file "${oa_frontend_controls_dir}/frontend-controls.conf")
      set_tests_properties(native-frontend-controls PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
        WORKING_DIRECTORY "${oa_frontend_controls_dir}"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
      # The scroll bars of the options' SOUND panel, SELMAP.GUI's map list,
      # NEWGAME.GUI's mission list and the match's preferences, driven
      # through the SDL presenter: each draws
      # its knob and arrows, a press beside the knob steps it one position, a
      # drag moves it pixel for pixel, an arrow steps and repeats, and the
      # preferences' sub-panel shows whole over the battlefield in a window
      # taller than the chrome. The mission list also steps with Up and
      # starts a double-clicked mission, and a click on Any Mission's Campaign
      # list takes the focus, so Up and Down step it and refill the missions.
      set(oa_scroll_bars_dir "${CMAKE_CURRENT_BINARY_DIR}/native-checks/scroll-bars")
      file(MAKE_DIRECTORY "${oa_scroll_bars_dir}")
      add_test(NAME native-scroll-bars COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --skip-intro --mute --check-scroll-bars
        --preferences-file "${oa_scroll_bars_dir}/scroll-bars.conf")
      set_tests_properties(native-scroll-bars PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
        WORKING_DIRECTORY "${oa_scroll_bars_dir}"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
      # The first mission's briefing opened from NEWGAME.GUI with sound on the
      # dummy device: its narration is heard exactly while SHUTUP is on,
      # SHUTUP silences it, plays it anew and turns off once it is over,
      # PrevMenu, Start and their keys Escape and Enter silence it, and every
      # briefing opens with SHUTUP on.
      set(oa_briefing_narration_dir "${CMAKE_CURRENT_BINARY_DIR}/native-checks/briefing-narration")
      file(MAKE_DIRECTORY "${oa_briefing_narration_dir}")
      add_test(NAME native-briefing-narration COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --skip-intro --check-briefing-narration
        --snapshot "${oa_briefing_narration_dir}/briefing-narration.ppm"
        --preferences-file "${oa_briefing_narration_dir}/briefing-narration.conf")
      set_tests_properties(native-briefing-narration PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
        WORKING_DIRECTORY "${oa_briefing_narration_dir}"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
      # F4 in a skirmish slides the kills board in at the battlefield's top
      # right through the SDL presenter, with no dialog, and away again.
      set(oa_kill_board_check_dir "${CMAKE_CURRENT_BINARY_DIR}/native-checks/kill-board")
      file(MAKE_DIRECTORY "${oa_kill_board_check_dir}")
      add_test(NAME native-kill-board COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --skip-intro --mute --check-kill-board
        --preferences-file "${oa_kill_board_check_dir}/kill-board.conf")
      set_tests_properties(native-kill-board PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
        WORKING_DIRECTORY "${oa_kill_board_check_dir}"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
      # The headless skirmish's first ticks played in director mode, drawn,
      # reach the world the same ticks replayed there undrawn reach; director
      # frames show the battlefield alone, move by half a map pixel and zoom
      # past the player's range, and the fight's sounds reach the director's
      # hooks.
      set(oa_director_view_dir "${CMAKE_CURRENT_BINARY_DIR}/native-checks/director-view")
      file(MAKE_DIRECTORY "${oa_director_view_dir}")
      add_test(NAME native-director-view COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --mute --check-director-view
        --snapshot "${oa_director_view_dir}/director-view.ppm"
        --preferences-file "${oa_director_view_dir}/director-view.conf")
      set_tests_properties(native-director-view PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
        WORKING_DIRECTORY "${oa_director_view_dir}"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
      # A small director script rendered over the headless skirmish with
      # encoding off, whole and its second chunk alone: every chunk's frames,
      # sound and manifests are written, neither a chunk's sound nor the
      # world depends on the chunks drawn before it, and the render reaches
      # the world the generator's undrawn replay reaches. The frames and the
      # sound are pinned.
      set(oa_director_render_dir "${CMAKE_CURRENT_BINARY_DIR}/native-checks/director-render")
      file(MAKE_DIRECTORY "${oa_director_render_dir}")
      add_test(NAME native-director-render COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --mute --check-director-render
        --preferences-file "${oa_director_render_dir}/director-render.conf")
      set_tests_properties(native-director-render PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
        WORKING_DIRECTORY "${oa_director_render_dir}"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy"
        PASS_REGULAR_EXPRESSION
          "director render: frames c70634a9984e59d0d954978f235d634f1b6b75d6e74b04cee25c7a543079f001 sound 8df12a92caf572a178ccc9d354c361761a6abdb3bc3d9a6e05b4f2821b8c9c9d"
        FAIL_REGULAR_EXPRESSION "director render check:")
      # native-intro-switch, which starts the game with -y, is registered
      # once the extensions are known (cmake/OaExtensions.cmake).
      # The skirmish commander starts with its FBI standing orders; the order
      # page's standing order and toggle buttons clicked through SDL show the
      # selection's orders and the units carry them out.
      set(oa_orders_check_dir "${CMAKE_CURRENT_BINARY_DIR}/native-checks/match-orders")
      file(MAKE_DIRECTORY "${oa_orders_check_dir}")
      add_test(NAME native-match-orders COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --skip-intro --mute --check-match-orders
        --preferences-file "${oa_orders_check_dir}/match-orders.conf")
      set_tests_properties(native-match-orders PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
        WORKING_DIRECTORY "${oa_orders_check_dir}"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
      # A Kbot Lab sent to a point with MOVE and PATROL through SDL input on
      # dummy devices: each Peewee it then builds heads there.
      set(oa_factory_check_dir "${CMAKE_CURRENT_BINARY_DIR}/native-checks/factory-orders")
      file(MAKE_DIRECTORY "${oa_factory_check_dir}")
      add_test(NAME native-factory-orders COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --skip-intro --mute --check-factory-orders
        --preferences-file "${oa_factory_check_dir}/factory-orders.conf")
      set_tests_properties(native-factory-orders PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
        WORKING_DIRECTORY "${oa_factory_check_dir}"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
      # ARMLAB's second page is the ARMDL.GUI download page with the Warrior
      # and Flea its download TDFs name; through SDL input on dummy devices
      # they queue, come off the middle and end of the queue and build, and a
      # Nuclear Silo's MAKENUKE queues and drops a missile.
      set(oa_download_check_dir "${CMAKE_CURRENT_BINARY_DIR}/native-checks/download-builds")
      file(MAKE_DIRECTORY "${oa_download_check_dir}")
      add_test(NAME native-download-builds COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --skip-intro --mute --check-download-builds
        --preferences-file "${oa_download_check_dir}/download-builds.conf")
      set_tests_properties(native-download-builds PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
        WORKING_DIRECTORY "${oa_download_check_dir}"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
      # An ARMCK given PATROL through SDL input past map features with the
      # metal store empty takes RepairPatrol, reclaims one on its way, is
      # credited with it and patrols on.
      set(oa_patrol_check_dir "${CMAKE_CURRENT_BINARY_DIR}/native-checks/patrol-reclaim")
      file(MAKE_DIRECTORY "${oa_patrol_check_dir}")
      add_test(NAME native-patrol-reclaim COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --skip-intro --mute --check-patrol-reclaim
        --preferences-file "${oa_patrol_check_dir}/patrol-reclaim.conf")
      set_tests_properties(native-patrol-reclaim PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
        WORKING_DIRECTORY "${oa_patrol_check_dir}"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
      # The selected commander's pointer moved over several kinds of tree,
      # another reclaimable feature and a wreck through SDL input shows
      # Reclaim wherever its ground point is on their footprints and Move on
      # the open ground around them.
      set(oa_reclaim_cursor_dir "${CMAKE_CURRENT_BINARY_DIR}/native-checks/reclaim-cursor")
      file(MAKE_DIRECTORY "${oa_reclaim_cursor_dir}")
      add_test(NAME native-reclaim-cursor COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --skip-intro --mute --check-reclaim-cursor
        --preferences-file "${oa_reclaim_cursor_dir}/reclaim-cursor.conf")
      set_tests_properties(native-reclaim-cursor PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
        WORKING_DIRECTORY "${oa_reclaim_cursor_dir}"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
      # Both interface types' pointer buttons through SDL input: left clicks and
      # right presses on open ground, units and the radar, shift clicks taking
      # queued orders and a build site back, mouse look, and a factory build
      # button's right click taking its type off the queue.
      set(oa_pointer_check_dir "${CMAKE_CURRENT_BINARY_DIR}/native-checks/pointer-interfaces")
      file(MAKE_DIRECTORY "${oa_pointer_check_dir}")
      add_test(NAME native-pointer-interfaces COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --skip-intro --mute --check-pointer-interfaces
        --preferences-file "${oa_pointer_check_dir}/pointer-interfaces.conf")
      set_tests_properties(native-pointer-interfaces PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
        WORKING_DIRECTORY "${oa_pointer_check_dir}"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
      # MULTI clicked through SDL input opens the one-line "not available"
      # message box, which OK and Enter close over the main menu; an extension
      # that offers multiplayer checks its own screens instead. Scribbled
      # frees turn a focus handed back to a closed window into a fault.
      set(oa_multiplayer_check_dir "${CMAKE_CURRENT_BINARY_DIR}/native-checks/multiplayer-menu")
      file(MAKE_DIRECTORY "${oa_multiplayer_check_dir}")
      add_test(NAME native-multiplayer-menu COMMAND oa-game --game-dir "${OA_GAME_DIR}"
        --skip-intro --mute --check-multiplayer-menu
        --preferences-file "${oa_multiplayer_check_dir}/multiplayer-menu.conf")
      set_tests_properties(native-multiplayer-menu PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
        WORKING_DIRECTORY "${oa_multiplayer_check_dir}"
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software;MallocScribble=1")
      find_package(Python3 COMPONENTS Interpreter QUIET)
      if(Python3_Interpreter_FOUND)
        add_test(NAME native-saveload COMMAND ${Python3_EXECUTABLE}
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_native_saveload.py"
          --native $<TARGET_FILE:oa-game> --game-dir "${OA_GAME_DIR}"
          --scratch-root "${CMAKE_CURRENT_BINARY_DIR}/native-checks")
        set_tests_properties(native-saveload PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT})
        # The headless menu, match, composed-frame, message log, speed and
        # build placement checks, from fresh preferences each run.
        add_test(NAME native-navigation COMMAND ${Python3_EXECUTABLE}
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_native_navigation.py"
          --native $<TARGET_FILE:oa-game> --game-dir "${OA_GAME_DIR}"
          --scratch-root "${CMAKE_CURRENT_BINARY_DIR}/native-checks")
        set_tests_properties(native-navigation PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT})
        # --trace-digest streams of seeded headless skirmishes repeat byte
        # for byte and follow the trace stream layout.
        add_test(NAME native-trace COMMAND ${Python3_EXECUTABLE}
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_native_trace.py"
          --native $<TARGET_FILE:oa-game> --game-dir "${OA_GAME_DIR}"
          --scratch-root "${CMAKE_CURRENT_BINARY_DIR}/native-checks")
        set_tests_properties(native-trace PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT})
        # Arm mission 9 (AC09) at medium, its commander left idle, is lost to
        # the two Pyros its script sends: defeat follows the commander's death
        # after the outcome countdown, with no failed tick.
        add_test(NAME native-campaign-defeat COMMAND ${Python3_EXECUTABLE}
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_native_campaign_defeat.py"
          --native $<TARGET_FILE:oa-game> --game-dir "${OA_GAME_DIR}"
          --scratch-root "${CMAKE_CURRENT_BINARY_DIR}/native-checks")
        set_tests_properties(native-campaign-defeat PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT})
        # Core Contingency mission 6 (EXP1CC06) at medium: the scripted
        # factories hold their 'w' waits for 30 ticks a second and the
        # computer makes nothing before the first one ends.
        add_test(NAME native-campaign-waits COMMAND ${Python3_EXECUTABLE}
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_native_campaign_waits.py"
          --native $<TARGET_FILE:oa-game> --game-dir "${OA_GAME_DIR}"
          --scratch-root "${CMAKE_CURRENT_BINARY_DIR}/native-checks")
        set_tests_properties(native-campaign-waits PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT})
        # The installation offers its movies and draws the same main menu
        # without its Data folder: the rules for data with no movies are the
        # demo's, not an installation's that keeps its movies on the disc.
        add_test(NAME native-menu-without-movies COMMAND ${Python3_EXECUTABLE}
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_native_menu_without_movies.py"
          --native $<TARGET_FILE:oa-game> --game-dir "${OA_GAME_DIR}"
          --scratch-root "${CMAKE_CURRENT_BINARY_DIR}/native-checks")
        set_tests_properties(native-menu-without-movies PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
          SKIP_RETURN_CODE ${OA_GAME_DATA_SKIP_CODE})
        # Arm mission 1 (AC01) played with --give-orders is won at the
        # Galactic Gate and goes on through the end screen into AC02; both
        # briefings are drawn in their background's palette and ARM's colours.
        add_test(NAME native-campaign-advance COMMAND ${Python3_EXECUTABLE}
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_native_campaign_advance.py"
          --native $<TARGET_FILE:oa-game> --tool $<TARGET_FILE:oa-tool> --game-dir "${OA_GAME_DIR}"
          --scratch-root "${CMAKE_CURRENT_BINARY_DIR}/native-checks")
        set_tests_properties(native-campaign-advance PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT})
      endif()
    else()
      message(STATUS "Native checks not added: OA_GAME_DIR names no installed game")
    endif()
    # oa-game started on the folder that holds the installer of the Total
    # Annihilation demo (1997) OA_DEMO_INSTALLER names unpacks its archive
    # into a scratch data folder, checks it, mounts it and opens the main
    # menu with no dialog over it, and on a second start checks and mounts it
    # again; skipped without an installer.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
      add_test(NAME native-demo-installer COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_native_demo_installer.py"
        --native $<TARGET_FILE:oa-game> --verifier $<TARGET_FILE:oa-demo-installer-test>
        --scratch-root "${CMAKE_CURRENT_BINARY_DIR}/native-checks")
      # oa-game's own checks over the demo, started the same way: the
      # navigation check's notices, grayed-out entries and campaign, and the
      # grayed-out save and load entries. MULTI's notice, which an extension
      # replaces with its own screens, is checked once the extensions are
      # known (cmake/OaExtensions.cmake).
      set(oa_demo_game_checks navigation saved-games)
      foreach(check IN LISTS oa_demo_game_checks)
        add_test(NAME native-demo-${check} COMMAND ${Python3_EXECUTABLE}
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_native_demo_installer.py"
          --native $<TARGET_FILE:oa-game> --check ${check}
          --scratch-root "${CMAKE_CURRENT_BINARY_DIR}/native-checks")
      endforeach()
      list(TRANSFORM oa_demo_game_checks PREPEND native-demo- OUTPUT_VARIABLE oa_demo_native_tests)
      # The demo's last Arm mission, AC03, won at easy with --give-orders,
      # leaves its end screen through the ending for the main menu.
      add_test(NAME native-demo-campaign-ending COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_native_campaign_ending.py"
        --native $<TARGET_FILE:oa-game> --mission 2
        --scratch-root "${CMAKE_CURRENT_BINARY_DIR}/native-checks")
      list(APPEND oa_demo_native_tests native-demo-campaign-ending)
      # Each of the demo's Arm missions, AC01 to AC03 (--mission 0 to 2),
      # starts from its briefing and plays without a failed tick, and a save
      # made inside each loads back into the same world and plays on.
      foreach(mission IN ITEMS 0 1 2)
        math(EXPR mission_number "${mission} + 1")
        add_test(NAME native-demo-mission-ac0${mission_number} COMMAND ${Python3_EXECUTABLE}
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_native_demo_missions.py"
          --native $<TARGET_FILE:oa-game> --check mission --mission ${mission}
          --scratch-root "${CMAKE_CURRENT_BINARY_DIR}/native-checks")
        list(APPEND oa_demo_native_tests native-demo-mission-ac0${mission_number})
      endforeach()
      add_test(NAME native-demo-mission-saveload COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_native_demo_missions.py"
        --native $<TARGET_FILE:oa-game> --check saveload
        --scratch-root "${CMAKE_CURRENT_BINARY_DIR}/native-checks")
      list(APPEND oa_demo_native_tests native-demo-mission-saveload)
      set_tests_properties(native-demo-installer ${oa_demo_native_tests} PROPERTIES
        TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT})
      oa_demo_installer_tests(native-demo-installer ${oa_demo_native_tests})
    endif()
  endif()
endif()
if(OA_REQUIRE_GAME AND NOT TARGET oa-game)
  message(FATAL_ERROR "OA_REQUIRE_GAME is ON, but oa-game is not built: it needs OA_BUILD_PLATFORM=ON")
endif()
add_executable(oa-tool tools/oa-tool/main.cpp)
target_link_libraries(oa-tool PRIVATE oa-formats-hpi oa-formats-png)

if(OA_BUILD_PLATFORM)
  find_package(SDL3 3.2 CONFIG REQUIRED)
  add_executable(oa-platform tools/platform-probe/platform_probe.cpp)
  target_link_libraries(oa-platform PRIVATE SDL3::SDL3 oa-options)
endif()

if(BUILD_TESTING)
  add_executable(oa-display-layout-test tests/display_layout_test.cpp)
  target_link_libraries(oa-display-layout-test PRIVATE oa-ui-display-layout)
  target_link_libraries(oa-display-layout-test PRIVATE oa-options)
  add_test(NAME display-layout COMMAND oa-display-layout-test)
  add_executable(oa-hpi-codec-test tests/hpi_codec_test.cpp)
  target_link_libraries(oa-hpi-codec-test PRIVATE oa-formats-sqsh)
  add_test(NAME hpi-codec COMMAND oa-hpi-codec-test)
  add_executable(oa-assets-test tests/assets_test.cpp)
  target_link_libraries(oa-assets-test PRIVATE oa-formats-hpi)
  add_test(NAME assets COMMAND oa-assets-test)
  set_tests_properties(hpi-codec assets PROPERTIES TIMEOUT 30)
  if(OA_BUILD_PLATFORM)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
      add_test(NAME platform-smoke COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_platform.py $<TARGET_FILE:oa-platform>)
      set_tests_properties(platform-smoke PROPERTIES TIMEOUT 60)
    endif()
  endif()
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    if(UNIX)
      find_program(OA_BASH_EXECUTABLE bash)
      if(OA_BASH_EXECUTABLE)
        add_test(NAME launcher COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_launcher.py)
      endif()
    endif()
    # The icons made from the Open Annihilation icon (tools/make_icons.py)
    # have the sizes the builds and the platforms want.
    add_test(NAME branding-icons COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_icons.py --check)
    set_tests_properties(branding-icons PROPERTIES TIMEOUT 60)
    # The engine's sources keep to the style rules of tools/check_style.py
    # within the counts per directory that tools/style-baseline.json allows,
    # a baseline that may only shrink; the self-test plants a breach of every
    # rule in small trees and requires the check to find exactly those.
    add_test(NAME style-ratchet COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_style.py
      --root ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME style-ratchet-selftest COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_style_test.py)
    set_tests_properties(style-ratchet style-ratchet-selftest PROPERTIES TIMEOUT 300)
    # Relative links, anchors and repository paths in the engine's Markdown
    # files resolve (tools/check_links.py), and the check still finds what it
    # should in a small built-in tree.
    add_test(NAME doc-links COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_links.py
      --root ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME doc-links-self-test COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_links.py --self-test)
    set_tests_properties(doc-links doc-links-self-test PROPERTIES TIMEOUT 60)
    # The engine's C, C++ and Objective-C sources are laid out as
    # .clang-format says, by the clang-format release that
    # tools/format/requirements.txt pins (tools/format_sources.py, whose
    # --setup installs it under local/); without that release the check
    # skips.
    add_test(NAME format-check COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/format_sources.py
      --root ${CMAKE_CURRENT_SOURCE_DIR} --check)
    set_tests_properties(format-check PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)
    # Every file of the engine states its copyright and licence: an SPDX
    # header, an annotation in REUSE.toml, or a licence text's own terms
    # (tools/spdx_headers.py).
    add_test(NAME licensing-check COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spdx_headers.py
      --root ${CMAKE_CURRENT_SOURCE_DIR} --check)
    set_tests_properties(licensing-check PROPERTIES TIMEOUT 60)
    # The self-tests run each whole-tree tool on small cases whose results
    # are known.
    add_test(NAME fixed-width-rewrite-selftest COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/rewrite_fixed_width.py --self-test)
    add_test(NAME format-check-selftest COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/format_sources.py --self-test)
    add_test(NAME licensing-check-selftest COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/spdx_headers.py --self-test)
    set_tests_properties(fixed-width-rewrite-selftest format-check-selftest licensing-check-selftest
      PROPERTIES TIMEOUT 60)
  endif()
endif()

if(WIN32)
  set(oa_executables oa-tool)
  if(OA_BUILD_PLATFORM)
    list(APPEND oa_executables oa-platform)
  endif()
  if(BUILD_TESTING)
    list(APPEND oa_executables oa-hpi-codec-test oa-assets-test)
  endif()
  foreach(target IN LISTS oa_executables)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} "-DDLLS=$<TARGET_RUNTIME_DLLS:${target}>"
        "-DDEST=$<TARGET_FILE_DIR:${target}>" -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/copy_runtime.cmake)
  endforeach()
endif()

add_subdirectory(src/ui/gadget-render)
add_subdirectory(src/ui/frontend-dialogs)
add_subdirectory(tests/content)
if(TARGET oa-game)
  target_sources(oa-game PRIVATE src/app/runtime_dialog_checks.cpp)
endif()

# A project that adds this engine with add_subdirectory registers its
# extension libraries with oa_add_extension afterwards
# (cmake/OaExtensions.cmake); a library that includes runtime.hpp links
# oa::extension-sdk. Before adding the engine, one of those projects may set
# OA_RUNTIME_EXTENSION_MEMBERS, the absolute path of a header of members
# runtime.hpp includes inside class Runtime. It is frozen: it may only shrink
# (tools/check_runtime_surface.py) until hooks and declared headers replace
# it. oa-game's own sources and, through the SDK, the extension's see the
# same Runtime; a unit that sees another fails to link
# (src/app/extension_members.cpp). tests/extension checks that it does, and
# holds the recorder test extensions, which name their own members header
# here when OA_RECORD_EXTENSION_HOOKS is on.
add_subdirectory(tests/extension)
if(TARGET oa-game)
  set(oa_runtime_members_definition "")
  if(OA_RUNTIME_EXTENSION_MEMBERS)
    if(NOT IS_ABSOLUTE "${OA_RUNTIME_EXTENSION_MEMBERS}" OR NOT EXISTS "${OA_RUNTIME_EXTENSION_MEMBERS}")
      message(FATAL_ERROR "OA_RUNTIME_EXTENSION_MEMBERS (\"${OA_RUNTIME_EXTENSION_MEMBERS}\") "
        "must be the absolute path of an existing header")
    endif()
    set(oa_runtime_members_definition "OA_RUNTIME_EXTENSION_MEMBERS=\"${OA_RUNTIME_EXTENSION_MEMBERS}\"")
  endif()
  # What an extension library compiles and links against: extension.hpp,
  # and, while an extension adds Runtime members, runtime.hpp with the
  # libraries oa-game's own sources use (those above and the frontend
  # dialogs).
  add_library(oa-extension-sdk INTERFACE)
  add_library(oa::extension-sdk ALIAS oa-extension-sdk)
  target_link_libraries(oa-extension-sdk INTERFACE oa-app-headers)
  target_link_libraries(oa-extension-sdk INTERFACE oa-ui-display-layout)
  target_link_libraries(oa-extension-sdk INTERFACE SDL3::SDL3 ${oa_game_libraries} oa-ui-frontend-dialogs)
  target_compile_definitions(oa-extension-sdk INTERFACE ${oa_runtime_members_definition})
  target_compile_definitions(oa-game PRIVATE ${oa_runtime_members_definition})
  target_sources(oa-game PRIVATE src/app/extension_members.cpp)
  # The registered extensions are listed and linked when the configure ends.
  oa_schedule_extension_list()
endif()
# tools/check_runtime_surface.py, which holds an extension's members header
# and its use of Runtime's private names to tools/runtime-surface-baseline.json,
# still measures what it should in a small built-in tree; and every name that
# baseline allows is still a private name of Runtime, so renaming, removing
# or publishing one changes the baseline, and the extension, with it.
if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME runtime-surface-selftest COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_runtime_surface.py --self-test)
    add_test(NAME runtime-surface-names COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_runtime_surface.py --baseline-names)
    set_tests_properties(runtime-surface-selftest runtime-surface-names PROPERTIES TIMEOUT 60)
  endif()
endif()
# The project's licence, the attributions and the licence texts they link to
# travel beside every executable the packages ship; with the game they go in
# its OA_GAME_FILES_DIR, the bundle's Resources on macOS, and follow
# whatever the extensions add there (their GAME_FILES, which the script
# cmake/OaExtensions.cmake writes runs). The executables may share a
# directory, so the copies take turns on one lock.
foreach(target IN ITEMS oa-game oa-intro oa-tool)
  if(NOT TARGET ${target})
    continue()
  endif()
  set(oa_extension_files "")
  set(oa_notices_dir "$<TARGET_FILE_DIR:${target}>")
  if(target STREQUAL "oa-game")
    set(oa_extension_files COMMAND ${CMAKE_COMMAND}
      -P "${CMAKE_CURRENT_BINARY_DIR}/generated/extension-game-files-$<CONFIG>.cmake")
    set(oa_notices_dir "$<GENEX_EVAL:$<TARGET_PROPERTY:oa-game,OA_GAME_FILES_DIR>>")
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${oa_notices_dir}"
    ${oa_extension_files}
    COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      "-DDEST=${oa_notices_dir}"
      "-DLOCK_FILE=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/oa-notices.lock"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/copy_notices.cmake"
    VERBATIM)
endforeach()
# oa-doc-check compiles the public headers with Clang's documentation
# warnings as errors (cmake/OaDocumentationCheck.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/cmake/OaDocumentationCheck.cmake")
oa_add_documentation_check()

# Every module keeps to the layout: no include directory or source reaches
# into another module, every include has a link path, links follow the
# layer order and names follow the naming rule (tools/check_layout.py,
# within tools/layout-baseline.json). The check reads the targets as this
# configuration defines them.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/OaLayout.cmake")
oa_register_layout_check()
