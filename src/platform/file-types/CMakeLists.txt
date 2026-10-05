# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Makes the running copy of the game the opener of .oamod mod packages for the user: the
# registry under HKEY_CURRENT_USER on Windows, the XDG data folder's MIME package, desktop
# entry and icons on Linux. The XDG part is built on every system but Windows, so that its
# test runs there too; macOS and iOS register through the bundle's Info.plist.
if(WIN32)
  add_library(oa-platform-file-types src/file_types.cpp src/file_types_windows.cpp)
  target_link_libraries(oa-platform-file-types PRIVATE advapi32 shell32)
else()
  add_library(oa-platform-file-types src/file_types.cpp src/file_types_xdg.cpp)
endif()
add_library(oa::platform::file_types ALIAS oa-platform-file-types)
target_include_directories(oa-platform-file-types PUBLIC include)
target_compile_features(oa-platform-file-types PUBLIC cxx_std_20)
# Whether the database tools may be started (cmake/OaPlatformCapabilities.cmake).
target_compile_definitions(oa-platform-file-types PRIVATE
  OA_PROCESS_SPAWNING=$<BOOL:${OA_PROCESS_SPAWNING}>)
target_link_libraries(oa-platform-file-types PRIVATE oa-platform-shims oa-options)

if(BUILD_TESTING)
  # The texts written; the XDG files in a scratch data folder with recording tools on a
  # scratch PATH, or the registry values under a scratch key of HKEY_CURRENT_USER.
  add_executable(oa-platform-file-types-test tests/file_types_test.cpp)
  target_compile_definitions(oa-platform-file-types-test PRIVATE
    OA_PROCESS_SPAWNING=$<BOOL:${OA_PROCESS_SPAWNING}>)
  target_link_libraries(oa-platform-file-types-test PRIVATE oa-platform-file-types oa-test-support
    oa-options)
  if(WIN32)
    target_link_libraries(oa-platform-file-types-test PRIVATE advapi32 shlwapi)
  endif()
  add_test(NAME platform-file-types COMMAND oa-platform-file-types-test)
  set_tests_properties(platform-file-types PROPERTIES TIMEOUT 60)
endif()
