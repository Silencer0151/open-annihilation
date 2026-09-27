# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-sim-profile src/profile.cpp)
add_library(oa::sim::profile ALIAS oa-sim-profile)
target_include_directories(oa-sim-profile PUBLIC include)
target_compile_features(oa-sim-profile PUBLIC cxx_std_20)
target_link_libraries(oa-sim-profile PUBLIC oa-core-types PRIVATE oa-options)
if(BUILD_TESTING)
  add_executable(oa-sim-profile-test tests/profile_test.cpp)
  target_link_libraries(oa-sim-profile-test PRIVATE oa-sim-profile oa-options)
  add_test(NAME sim-profile COMMAND oa-sim-profile-test)
endif()
