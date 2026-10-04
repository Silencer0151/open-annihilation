# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# What the platform the game is built for can do, as plain options. Each
# default is the desktop's behaviour; a project that adds the engine for
# another platform (one under platforms/) sets the cache values before it
# adds the engine. Each option reaches the sources that read it as a compile
# definition that is always passed, 0 or 1, so a source tests `#if OA_X` and
# never whether the name is defined. Engine sources never test the platform
# itself for these behaviours.
include_guard(GLOBAL)

# The game may start other programs: the video capture (--capture-video)
# and the director's encoder run the ffmpeg program, and a folder is shown
# in the file manager by open or xdg-open. Off, the first two refuse with
# "this build starts no other programs" instead of starting it, and a folder
# is shown only where the platform's show_folder hook shows it.
option(OA_PROCESS_SPAWNING "The game may start other programs (video capture, the director's encoder)" ON)
# The game may ask for its game folder with the system's folder dialog. Off,
# no dialog is offered and the platform's advice says where the folder goes.
option(OA_NATIVE_FOLDER_DIALOG "Ask for the game folder with the system's folder dialog" ON)
# The game's window opens at the display's own pixel density, where the
# platform would otherwise scale a lower-density window softly, unless memory
# is short or a flag names Off. Off, the window opens as the desktop's does.
option(OA_NATIVE_DENSITY_WINDOWS "Open the game's window at the display's own pixel density" OFF)
# The touch controls are on from the start, with no finger seen yet and no
# --touch-controls (docs/touch-controls.md). Off, they switch on with the
# first finger on a touch screen or with --touch-controls.
option(OA_TOUCH_FIRST "Touch controls are on from the start" OFF)

# Each capability as the 0 or 1 its sources test.
foreach(oa_capability IN ITEMS OA_PROCESS_SPAWNING OA_NATIVE_FOLDER_DIALOG OA_NATIVE_DENSITY_WINDOWS
    OA_TOUCH_FIRST)
  if(${oa_capability})
    set(oa_${oa_capability}_value 1)
  else()
    set(oa_${oa_capability}_value 0)
  endif()
endforeach()

# The sources that read them. Source properties set here hold for the
# targets of this directory, which builds each of these files.
get_filename_component(oa_capability_app_dir "${CMAKE_CURRENT_LIST_DIR}/../src/app" ABSOLUTE)
set_property(SOURCE "${oa_capability_app_dir}/video_capture.cpp"
  "${oa_capability_app_dir}/director_output.cpp" "${oa_capability_app_dir}/startup.cpp"
  "${oa_capability_app_dir}/user_folder_open.cpp" APPEND
  PROPERTY COMPILE_DEFINITIONS "OA_PROCESS_SPAWNING=${oa_OA_PROCESS_SPAWNING_value}")
set_property(SOURCE "${oa_capability_app_dir}/game_directory_dialog.cpp" APPEND
  PROPERTY COMPILE_DEFINITIONS "OA_NATIVE_FOLDER_DIALOG=${oa_OA_NATIVE_FOLDER_DIALOG_value}")
set_property(SOURCE "${oa_capability_app_dir}/main.cpp" APPEND
  PROPERTY COMPILE_DEFINITIONS "OA_NATIVE_DENSITY_WINDOWS=${oa_OA_NATIVE_DENSITY_WINDOWS_value}"
  "OA_TOUCH_FIRST=${oa_OA_TOUCH_FIRST_value}")
set_property(SOURCE "${oa_capability_app_dir}/runtime_touch.cpp" APPEND
  PROPERTY COMPILE_DEFINITIONS "OA_TOUCH_FIRST=${oa_OA_TOUCH_FIRST_value}")
