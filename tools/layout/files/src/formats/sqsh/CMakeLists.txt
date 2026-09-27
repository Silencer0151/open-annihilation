# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The SQSH chunk codec that HPI archives, the savegame bank and the chunk
# writer share: LZ77, chunk scrambling and checksums.
add_library(oa-formats-sqsh src/sqsh.cpp src/lz77_encoder.cpp)
target_include_directories(oa-formats-sqsh PUBLIC include)
target_link_libraries(oa-formats-sqsh PUBLIC oa-options)
