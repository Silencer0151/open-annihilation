# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The functions that Windows XP lacks and the C++ run-time library calls:
# slim locks, condition variables and one-time initialisation on every
# system (where they are tested), and with MinGW the definitions under the
# names programs import them by, which cmake/OaWindowsXp.cmake links into
# every executable of an OA_WINDOWS_XP build.
add_library(oa-platform-xp-runtime src/slim_sync.cpp)
add_library(oa::platform::xp_runtime ALIAS oa-platform-xp-runtime)
target_include_directories(oa-platform-xp-runtime PUBLIC include)
target_compile_features(oa-platform-xp-runtime PUBLIC cxx_std_20)
target_link_libraries(oa-platform-xp-runtime PRIVATE oa-base-threads oa-options)
if(MINGW)
  target_sources(oa-platform-xp-runtime PRIVATE src/windows_functions.cpp)
  target_link_libraries(oa-platform-xp-runtime PRIVATE psapi)
endif()

if(BUILD_TESTING)
  add_executable(oa-platform-xp-runtime-test tests/xp_runtime_test.cpp)
  target_link_libraries(oa-platform-xp-runtime-test PRIVATE oa-platform-xp-runtime oa-base-threads)
  add_test(NAME platform-xp-runtime COMMAND oa-platform-xp-runtime-test)
  set_tests_properties(platform-xp-runtime PROPERTIES TIMEOUT 60)
endif()
