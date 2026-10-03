# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Cross-compile for x86-64 Windows with a mingw-w64 cross-compiler from macOS
# or Linux (Homebrew `mingw-w64`, Debian `g++-mingw-w64-x86-64`).
#
#   cmake -S . -B build-windows \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-w64-mingw32.cmake
#
# Add -DOA_WINDOWS_XP=ON for executables that also run on the 64-bit edition
# of Windows XP. mingw-w64-common.cmake beside this file does the rest: it
# finds the dependencies built for the target under OA_WINDOWS_DEPS and links
# the run-time libraries statically. tools/build_windows.sh populates the
# default location.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(oa_mingw_triple x86_64-w64-mingw32)
set(oa_mingw_name windows)
set(oa_mingw_target_flags "")
set(oa_mingw_flags_if_accepted "")
include("${CMAKE_CURRENT_LIST_DIR}/mingw-w64-common.cmake")
