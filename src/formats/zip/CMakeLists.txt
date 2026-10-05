# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Zip archives: the bounded reader director bundles (.oamovie) are opened
# with, the writer that makes them, and the streamed reader mod packages
# (.oamod) are unpacked with.
add_library(oa-formats-zip src/common.cpp src/read.cpp src/write.cpp src/inflater.cpp
  src/stream.cpp)
add_library(oa::formats::zip ALIAS oa-formats-zip)
target_include_directories(oa-formats-zip PUBLIC include)
target_link_libraries(oa-formats-zip PRIVATE oa-base-bytes ZLIB::ZLIB oa-options)
target_compile_features(oa-formats-zip PUBLIC cxx_std_20)
if(BUILD_TESTING)
  add_executable(oa-formats-zip-test tests/zip_test.cpp)
  target_link_libraries(oa-formats-zip-test PRIVATE oa-formats-zip oa-base-sha256 ZLIB::ZLIB oa-options)
  add_test(NAME formats-zip COMMAND oa-formats-zip-test)
  # The streamed reader: entries read in steps of any size, the 64-bit
  # extension (a sparse source past 4 GiB among its cases), code page 437
  # and MS-DOS names, links marked, every refusal, and a malformed sweep.
  add_executable(oa-formats-zip-stream-test tests/stream_test.cpp)
  target_link_libraries(oa-formats-zip-stream-test PRIVATE oa-formats-zip oa-test-support ZLIB::ZLIB
    oa-options)
  add_test(NAME formats-zip-stream COMMAND oa-formats-zip-stream-test)
endif()
