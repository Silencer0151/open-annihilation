# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Bounds-checked little-endian reading from byte spans, and the result type
# the decoders return.
add_library(oa-base-bytes src/bytes.cpp)
add_library(oa::base::bytes ALIAS oa-base-bytes)
target_include_directories(oa-base-bytes PUBLIC include)
target_compile_features(oa-base-bytes PUBLIC cxx_std_20)
target_link_libraries(oa-base-bytes PUBLIC oa-options)
if(BUILD_TESTING)
  add_executable(oa-base-bytes-test tests/bytes_test.cpp)
  target_link_libraries(oa-base-bytes-test PRIVATE oa-base-bytes oa-options)
  add_test(NAME base-bytes COMMAND oa-base-bytes-test)
endif()
