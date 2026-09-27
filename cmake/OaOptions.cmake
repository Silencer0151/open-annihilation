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
  # Two sources of platform differences are outside what these options
  # control: maths library functions such as atan2, sin, cos and hypot come
  # from each platform's C library, and the precision of long double differs
  # (80-bit on x86-64 with GCC or MinGW, 64-bit on macOS arm64, 128-bit on
  # Linux arm64). src/base/game-math/README.md and
  # src/sim/unit-movement/README.md record where the simulation depends
  # on them.
  if(MSVC)
    target_compile_options(oa-options INTERFACE /fp:strict)
  else()
    target_compile_options(oa-options INTERFACE -fno-fast-math -ffp-contract=off)
  endif()
endif()
