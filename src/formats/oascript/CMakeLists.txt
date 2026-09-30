# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Director scripts (.oascript): the document tree read from strict JSON or a
# YAML subset and written back, and the typed script decoded from it.
add_library(oa-formats-oascript
  src/decimal.cpp
  src/document.cpp
  src/document_json.cpp
  src/document_write.cpp
  src/document_yaml.cpp
  src/script_decode.cpp
  src/script_encode.cpp)
add_library(oa::formats::oascript ALIAS oa-formats-oascript)
target_include_directories(oa-formats-oascript PUBLIC include)
target_link_libraries(oa-formats-oascript PRIVATE oa-options)
target_compile_features(oa-formats-oascript PUBLIC cxx_std_20)

if(BUILD_TESTING)
  # The document test also checks the private decimal assembly in src/.
  add_executable(oa-formats-oascript-document-test tests/document_test.cpp)
  target_include_directories(oa-formats-oascript-document-test PRIVATE src)
  target_link_libraries(oa-formats-oascript-document-test PRIVATE oa-formats-oascript oa-options)
  add_test(NAME formats-oascript-document COMMAND oa-formats-oascript-document-test)

  add_executable(oa-formats-oascript-script-test tests/script_test.cpp)
  target_link_libraries(oa-formats-oascript-script-test PRIVATE oa-formats-oascript oa-options)
  add_test(NAME formats-oascript-script COMMAND oa-formats-oascript-script-test)
endif()
