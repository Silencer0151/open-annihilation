# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# OA_WINDOWS_XP builds Windows executables that also run on Windows XP: SP3
# for 32-bit x86, and the 64-bit edition. Engine code is compiled against
# the declarations of Windows XP, so a call XP lacks does not compile; every
# executable is marked as one XP will load (subsystem and system version
# 5.1, or 5.2 for 64-bit); and every executable links oa-platform-xp-runtime,
# which defines the functions the C++ run-time library calls that XP lacks
# (src/platform/xp-runtime/README.md). It needs a MinGW toolchain that links
# the C library every Windows release includes, not the one Windows 10 added.
# OaOptions.cmake includes this file once oa-options exists.
include_guard(GLOBAL)
option(OA_WINDOWS_XP "Build Windows executables that also run on Windows XP" OFF)
if(OA_WINDOWS_XP)
  if(NOT MINGW)
    message(FATAL_ERROR "OA_WINDOWS_XP needs a MinGW toolchain for Windows")
  endif()
  target_compile_definitions(oa-options INTERFACE _WIN32_WINNT=0x0501 WINVER=0x0501 PSAPI_VERSION=1)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(oa_windows_xp_minor_version 2)
  else()
    set(oa_windows_xp_minor_version 1)
  endif()
  target_link_options(oa-options INTERFACE
    "LINKER:--major-subsystem-version,5" "LINKER:--minor-subsystem-version,${oa_windows_xp_minor_version}"
    "LINKER:--major-os-version,5" "LINKER:--minor-os-version,${oa_windows_xp_minor_version}")
  # Executables only, and whole: the definitions must be in the program
  # before the linker reaches any library that imports the same names, such
  # as kernel32's, which SDL names before the C++ run-time library calls them.
  target_link_libraries(oa-options INTERFACE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:$<LINK_LIBRARY:WHOLE_ARCHIVE,$<TARGET_NAME_IF_EXISTS:oa-platform-xp-runtime>>>")
endif()
