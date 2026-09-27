# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Cross-compile for x86-64 Windows with a mingw-w64 GCC toolchain from macOS
# or Linux (Homebrew `mingw-w64`, Debian `g++-mingw-w64-x86-64`).
#
#   cmake -S . -B build-windows \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-w64-mingw32.cmake
#
# Target-built dependencies (zlib, SDL3, FFmpeg and any a project adding this
# one needs) are searched under OA_WINDOWS_DEPS, one install prefix per
# subdirectory. tools/build_windows.sh populates the default location.
# OA_WINDOWS_FFMPEG names another FFmpeg prefix that replaces
# <OA_WINDOWS_DEPS>/ffmpeg. Executables link the GCC
# runtime statically so they run on a stock Windows install without mingw DLLs.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(OA_MINGW_TRIPLE "x86_64-w64-mingw32" CACHE STRING "Target triple of the mingw-w64 toolchain")
find_program(CMAKE_C_COMPILER NAMES ${OA_MINGW_TRIPLE}-gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES ${OA_MINGW_TRIPLE}-g++ REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES ${OA_MINGW_TRIPLE}-windres)
find_program(CMAKE_AR NAMES ${OA_MINGW_TRIPLE}-gcc-ar ${OA_MINGW_TRIPLE}-ar)
find_program(CMAKE_RANLIB NAMES ${OA_MINGW_TRIPLE}-gcc-ranlib ${OA_MINGW_TRIPLE}-ranlib)

if(NOT DEFINED OA_WINDOWS_DEPS)
  if(DEFINED ENV{OA_WINDOWS_DEPS})
    set(OA_WINDOWS_DEPS "$ENV{OA_WINDOWS_DEPS}")
  else()
    get_filename_component(OA_WINDOWS_DEPS "${CMAKE_CURRENT_LIST_DIR}/../../local/deps/windows" ABSOLUTE)
  endif()
endif()
set(OA_WINDOWS_DEPS "${OA_WINDOWS_DEPS}" CACHE PATH "Install prefixes of dependencies built for the Windows target")
set(OA_WINDOWS_FFMPEG "" CACHE PATH "FFmpeg prefix for the Windows target in place of <OA_WINDOWS_DEPS>/ffmpeg")

# Every subdirectory of the dependency root is an install prefix.
set(CMAKE_FIND_ROOT_PATH "")
if(OA_WINDOWS_FFMPEG)
  list(APPEND CMAKE_FIND_ROOT_PATH "${OA_WINDOWS_FFMPEG}")
endif()
if(IS_DIRECTORY "${OA_WINDOWS_DEPS}")
  file(GLOB _oa_windows_prefixes LIST_DIRECTORIES true "${OA_WINDOWS_DEPS}/*")
  foreach(_prefix IN LISTS _oa_windows_prefixes)
    get_filename_component(_name "${_prefix}" NAME)
    if(IS_DIRECTORY "${_prefix}" AND NOT (OA_WINDOWS_FFMPEG AND _name STREQUAL "ffmpeg"))
      list(APPEND CMAKE_FIND_ROOT_PATH "${_prefix}")
    endif()
  endforeach()
endif()
# Host tools (Python, the compilers) come from the host; headers and libraries
# only from the target prefixes.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Prefer the static archives so oa-tool and the test drivers are single files.
set(ZLIB_USE_STATIC_LIBS ON)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")
