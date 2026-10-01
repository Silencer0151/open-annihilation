# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-base-game-math src/math.cpp src/extended.cpp)
add_library(oa::base::game_math ALIAS oa-base-game-math)
target_include_directories(oa-base-game-math PUBLIC include)
target_compile_features(oa-base-game-math PUBLIC cxx_std_20)
target_link_libraries(oa-base-game-math PUBLIC oa-options)
if(BUILD_TESTING)
 add_executable(oa-game-math-test tests/math_test.cpp)
 target_link_libraries(oa-game-math-test PRIVATE oa-base-game-math)
 add_test(NAME game-math COMMAND oa-game-math-test)
 add_executable(oa-game-math-extended-test tests/extended_test.cpp)
 target_link_libraries(oa-game-math-extended-test PRIVATE oa-base-game-math oa-base-sha256)
 add_test(NAME game-math-extended COMMAND oa-game-math-extended-test)
endif()
