# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Zip archives: the bounded reader director bundles (.oamovie) are opened
# with and the writer that makes them.
add_library(oa-formats-zip src/common.cpp src/read.cpp src/write.cpp)
add_library(oa::formats::zip ALIAS oa-formats-zip)
target_include_directories(oa-formats-zip PUBLIC include)
target_link_libraries(oa-formats-zip PRIVATE ZLIB::ZLIB oa-options)
target_compile_features(oa-formats-zip PUBLIC cxx_std_20)
if(BUILD_TESTING)
  add_executable(oa-formats-zip-test tests/zip_test.cpp)
  target_link_libraries(oa-formats-zip-test PRIVATE oa-formats-zip oa-base-sha256 ZLIB::ZLIB oa-options)
  add_test(NAME formats-zip COMMAND oa-formats-zip-test)
endif()
