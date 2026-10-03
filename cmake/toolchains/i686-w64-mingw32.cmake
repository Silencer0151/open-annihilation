# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Cross-compile for 32-bit x86 Windows with a mingw-w64 cross-compiler from
# macOS or Linux (Homebrew `mingw-w64`, Debian `g++-mingw-w64-i686`).
#
#   cmake -S . -B build-windows-i686 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/i686-w64-mingw32.cmake
#
# Add -DOA_WINDOWS_XP=ON for executables that also run on Windows XP SP3.
# mingw-w64-common.cmake beside this file does the rest: it finds the
# dependencies built for the target under OA_WINDOWS_DEPS and links the
# run-time libraries statically. tools/build_windows.sh populates the default
# location.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR X86)

set(oa_mingw_triple i686-w64-mingw32)
set(oa_mingw_name windows-i686)
# A 32-bit x86 build runs on the Pentium III and the Athlon XP
# (cmake/OaOptions.cmake): code for the i686 instruction set without SSE2,
# in the dependencies built with this file too. The engine adds SSE for its
# floats itself (OA_X86_FLOAT).
set(oa_mingw_target_flags -march=i686 -mno-sse2)
# On 32-bit x86 a member function takes its object in a register, where
# other functions take every argument on the stack. A compiler that merges
# functions with identical bodies can merge one of each kind, such as the
# function a lambda converts to and a member function that does the same
# thing; a call through the merged function then passes its arguments where
# it does not look for them. -fno-ipa-icf keeps them apart, where the
# compiler takes it.
set(oa_mingw_flags_if_accepted -fno-ipa-icf)
include("${CMAKE_CURRENT_LIST_DIR}/mingw-w64-common.cmake")
