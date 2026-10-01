# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# oa-options, the compile options every target that compiles engine code
# links. The top-level project includes this file, and so does each module
# that can also be configured on its own, so both builds compile that module
# with the same options. The top-level OA_SANITIZERS option adds its flags to
# this target in the top-level CMakeLists.txt.
include_guard(GLOBAL)
if(NOT TARGET oa-options)
  add_library(oa-options INTERFACE)
  if(MSVC)
    target_compile_options(oa-options INTERFACE /W4 /permissive-)
  else()
    target_compile_options(oa-options INTERFACE -Wall -Wextra -Wpedantic)
  endif()
  # Each basic floating-point operation is rounded as written: never fused
  # into a multiply-add, which GCC and Clang do by default wherever the CPU
  # has the instruction (arm64 has it; baseline x86-64 does not), and never
  # reassociated or otherwise changed by value-changing optimisations.
  # Simulation state, trace digests and save files depend on this, so the
  # options apply to the whole tree rather than to a list of simulation
  # modules. -ffp-contract=off comes after -fno-fast-math because some Clang
  # versions reset contraction to their default on -fno-fast-math.
  #
  # Maths library functions such as atan2, sin, cos and hypot, and the
  # precision of long double, differ between platforms; these options do not
  # control them. The simulation therefore uses neither: it takes its
  # arctangents, sines, cosines and lengths from src/base/game-math, which
  # computes them with integer arithmetic (see its README.md).
  if(MSVC)
    target_compile_options(oa-options INTERFACE /fp:strict)
  else()
    target_compile_options(oa-options INTERFACE -fno-fast-math -ffp-contract=off)
  endif()
  # A 32-bit x86 build rounds each float operation to a 24-bit and each
  # double operation to a 53-bit significand as it is computed, as x86-64
  # does, instead of carrying intermediate results at extended precision
  # until they are stored; the processor it needs has SSE2. A build that
  # takes /fp:strict does this already.
  if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT MSVC AND
     CMAKE_SYSTEM_PROCESSOR MATCHES "^([iI][3-6]86|[xX]86|[xX]86_64|AMD64|amd64)$")
    target_compile_options(oa-options INTERFACE -msse2 -mfpmath=sse)
  endif()
  # A 32-bit POSIX build uses 64-bit file offsets and file serial numbers,
  # so it can examine every file and folder a file system holds, however
  # large the file or its serial number.
  if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT WIN32)
    target_compile_definitions(oa-options INTERFACE _FILE_OFFSET_BITS=64)
  endif()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/OaWindowsXp.cmake")
