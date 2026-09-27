# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The match-state digest reads a whole match, so it builds on the match
# runtime, which itself links oa-sim-trace for its trace stream.
add_library(oa-sim-state-hash src/state_hash.cpp)
target_include_directories(oa-sim-state-hash PUBLIC include)
target_compile_features(oa-sim-state-hash PUBLIC cxx_std_20)
target_link_libraries(oa-sim-state-hash
  PUBLIC oa-sim-trace oa-sim-match-runtime oa-base-game-loop oa-sim-world-environment
  PRIVATE oa-data-persist oa-sim-script-state oa-options)
