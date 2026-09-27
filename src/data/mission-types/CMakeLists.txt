# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-data-mission-types src/mission_types.cpp)
add_library(oa::data::mission_types ALIAS oa-data-mission-types)
target_include_directories(oa-data-mission-types PUBLIC include)
target_compile_features(oa-data-mission-types PUBLIC cxx_std_20)
target_link_libraries(oa-data-mission-types PUBLIC oa-options)
if(BUILD_TESTING)
  add_executable(oa-mission-types-test tests/missions_test.cpp)
  target_link_libraries(oa-mission-types-test PRIVATE oa-data-mission-types)
  add_test(NAME mission-types COMMAND oa-mission-types-test)
endif()
