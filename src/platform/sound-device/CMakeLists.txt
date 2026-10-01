# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The system's own sound device for the sound output that mixes in the
# engine: the wave-out device on Windows, none elsewhere.
add_library(oa-platform-sound-device src/sound_device.cpp)
add_library(oa::platform::sound_device ALIAS oa-platform-sound-device)
target_include_directories(oa-platform-sound-device PUBLIC include)
target_compile_features(oa-platform-sound-device PUBLIC cxx_std_20)
target_link_libraries(oa-platform-sound-device PRIVATE oa-options)
if(WIN32)
  target_link_libraries(oa-platform-sound-device PRIVATE winmm)
endif()
