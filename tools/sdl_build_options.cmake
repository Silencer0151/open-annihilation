# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Options for the pinned SDL that tools/bootstrap_sdl.py builds, read after
# SDL's project() call.
#
# With Visual Studio's compiler, SDL's objects name no C library (/Zl).
# The release build of SDL then links into a Debug program, which uses the
# debug C library, without the linker also being asked for the release one;
# each program links the C library it was built for.
if(MSVC)
  add_compile_options(/Zl)
endif()
