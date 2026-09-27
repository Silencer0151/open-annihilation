# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Integer rectangles, fixed-point trigonometry and rotation, and the small
# float vector helpers; simulation and rendering code use it without the
# ui services.
add_library(oa-base-geometry src/geometry.cpp)
add_library(oa::base::geometry ALIAS oa-base-geometry)
target_include_directories(oa-base-geometry PUBLIC include)
target_compile_features(oa-base-geometry PUBLIC cxx_std_20)
target_link_libraries(oa-base-geometry PUBLIC oa-core-types oa-options)
if(MSVC)
  target_compile_options(oa-base-geometry PRIVATE /fp:strict)
else()
  target_compile_options(oa-base-geometry PRIVATE -ffp-contract=off -fno-fast-math)
endif()
