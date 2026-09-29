# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The Check build type: the build the test suite is validated in. It is
# optimised like a release build but keeps assertions (it never defines
# NDEBUG), with line tables so backtraces name source lines. The
# floating-point options oa-options sets (cmake/OaOptions.cmake) apply to
# every build type, so Check computes the same results as Debug bit for bit;
# the pinned-digest tests check that. Debug stays the build for stepping
# through code.
#
# The top-level project includes this file right after project(), and so
# does a project that adds this engine, before it looks for any package, so
# that imported targets are mapped for Check when they are created.
include_guard(GLOBAL)

if(MSVC)
  set(oa_check_flags "/O2 /Ob2 /Zi")
  set(oa_check_link_flags "/DEBUG")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(oa_check_flags "-O2 -gline-tables-only")
  set(oa_check_link_flags "")
else()
  set(oa_check_flags "-O2 -g1")
  set(oa_check_link_flags "")
endif()
# Enabling a language creates its Check flags empty when Check is the build
# type, so empty entries are filled in; flags the user gave are kept.
foreach(oa_language IN ITEMS C CXX OBJC OBJCXX)
  if(NOT CMAKE_${oa_language}_FLAGS_CHECK)
    set(CMAKE_${oa_language}_FLAGS_CHECK "${oa_check_flags}"
        CACHE STRING "Compiler flags of the Check build type" FORCE)
  endif()
endforeach()
if(oa_check_link_flags)
  foreach(oa_link_kind IN ITEMS EXE SHARED MODULE)
    if(NOT CMAKE_${oa_link_kind}_LINKER_FLAGS_CHECK)
      set(CMAKE_${oa_link_kind}_LINKER_FLAGS_CHECK "${oa_check_link_flags}"
          CACHE STRING "Linker flags of the Check build type" FORCE)
    endif()
  endforeach()
endif()
# Packages such as SDL3 export only their release configurations.
set(CMAKE_MAP_IMPORTED_CONFIG_CHECK Release RelWithDebInfo ""
    CACHE STRING "Configurations of imported targets a Check build uses")

get_property(oa_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(oa_multi_config AND NOT "Check" IN_LIST CMAKE_CONFIGURATION_TYPES)
  list(APPEND CMAKE_CONFIGURATION_TYPES Check)
  set(CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES}"
      CACHE STRING "Build types of a multi-configuration generator" FORCE)
endif()
