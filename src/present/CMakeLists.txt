# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

file(GLOB oa_present_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(oa-present ${oa_present_sources})
add_library(oa::present ALIAS oa-present)
target_include_directories(oa-present PUBLIC include)
target_link_libraries(oa-present PUBLIC oa-core-types PRIVATE oa-options oa-base-game-math)
target_compile_features(oa-present PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_executable(oa-present-surface-c-test tests/surface_c_test.c)
  set_target_properties(oa-present-surface-c-test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_link_libraries(oa-present-surface-c-test PRIVATE oa-present oa-options)
  add_test(NAME present-surface-c COMMAND oa-present-surface-c-test)
  file(GLOB oa_present_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
  foreach(test_source IN LISTS oa_present_tests)
    get_filename_component(test_name "${test_source}" NAME_WE)
    string(REPLACE "_" "-" test_id "${test_name}")
    add_executable(oa-present-${test_id} "${test_source}")
    target_link_libraries(oa-present-${test_id} PRIVATE oa-present oa-options)
    add_test(NAME present-${test_id} COMMAND oa-present-${test_id})
    set_tests_properties(present-${test_id} PROPERTIES TIMEOUT 120)
  endforeach()
  target_link_libraries(oa-present-gaf-sprites-test PRIVATE oa-formats-gaf)
  # The installed game's GAF files (compared with the GAF reader), palette
  # tables and PCX images.
  foreach(test_id IN ITEMS gaf-sprites-test palette-tables-test pcx-test)
    target_link_libraries(oa-present-${test_id} PRIVATE oa-formats-hpi oa-test-game-data)
    oa_add_game_data_test(present-${test_id}-data oa-present-${test_id} --data)
    set_tests_properties(present-${test_id}-data PROPERTIES TIMEOUT 300)
  endforeach()
endif()

add_subdirectory(model)
