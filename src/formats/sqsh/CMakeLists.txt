# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The SQSH chunk codec that HPI archives, the savegame bank and the chunk
# writer share: LZ77, chunk scrambling and checksums.
add_library(oa-formats-sqsh src/sqsh.cpp src/lz77_encoder.cpp)
add_library(oa::formats::sqsh ALIAS oa-formats-sqsh)
target_include_directories(oa-formats-sqsh PUBLIC include)
target_link_libraries(oa-formats-sqsh PUBLIC oa-base-bytes oa-options)

if(BUILD_TESTING)
  # Malformed and edge-case input for the codec; tests/hpi_codec_test.cpp
  # pins its known vectors.
  add_executable(oa-formats-sqsh-test tests/sqsh_test.cpp)
  target_link_libraries(oa-formats-sqsh-test PRIVATE oa-formats-sqsh oa-options)
  add_test(NAME formats-sqsh COMMAND oa-formats-sqsh-test)
endif()
