# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The OTA map description: the mission header values, the schemas and their
# start positions.
add_library(oa-formats-ota src/ota.cpp)
target_include_directories(oa-formats-ota PUBLIC include)
target_compile_features(oa-formats-ota PUBLIC cxx_std_20)
target_link_libraries(oa-formats-ota PUBLIC oa-options)
