# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# HPI archives: the reader, the layered asset store over archives and loose
# files, and the writer.
add_library(oa-formats-hpi assets.cpp archive.cpp asset_store.cpp hpi_writer.cpp)
add_library(oa::formats::hpi ALIAS oa-formats-hpi)
target_include_directories(oa-formats-hpi PUBLIC include)
target_link_libraries(oa-formats-hpi PUBLIC oa-base-bytes oa-formats-sqsh ZLIB::ZLIB oa-options)
target_link_libraries(oa-formats-hpi PRIVATE oa-base-threads)

add_executable(oa-hpi-inspect tools/inspect.cpp)
target_link_libraries(oa-hpi-inspect PRIVATE oa-formats-hpi oa-options)

if(BUILD_TESTING)
  add_executable(oa-hpi-test tests/hpi_test.cpp)
  target_link_libraries(oa-hpi-test PRIVATE oa-formats-hpi oa-base-bytes oa-formats-sqsh ZLIB::ZLIB oa-test-game-data oa-options)
  add_test(NAME hpi COMMAND oa-hpi-test)
  # The installed game's archives mount by the discovery rules the
  # synthetic layouts pin.
  oa_add_game_data_test(hpi-data oa-hpi-test --data)
endif()

if(BUILD_TESTING)
  # The LZ77 encoder's and archive writer's bytes, pinned for every platform.
  add_executable(oa-hpi-writer-golden-test tests/writer_golden_test.cpp)
  target_link_libraries(oa-hpi-writer-golden-test PRIVATE oa-formats-hpi oa-formats-sqsh oa-options)
  add_test(NAME hpi-writer-golden COMMAND oa-hpi-writer-golden-test
    "${CMAKE_CURRENT_BINARY_DIR}/hpi-writer-golden.hpi")
  add_executable(oa-hpi-read-node-test tests/read_node_test.cpp)
  target_link_libraries(oa-hpi-read-node-test PRIVATE oa-formats-hpi oa-options)
  add_test(NAME hpi-read-node COMMAND oa-hpi-read-node-test)
  # A mod folder layered over a base folder reads as a copied install.
  add_executable(oa-hpi-layering-test tests/layering_test.cpp)
  target_link_libraries(oa-hpi-layering-test PRIVATE oa-formats-hpi oa-options)
  add_test(NAME hpi-layering COMMAND oa-hpi-layering-test)
endif()
