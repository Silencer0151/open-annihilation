# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Where the game's 640x480 frame sits in a window of another size: a header
# the app and its checks share.
add_library(oa-ui-display-layout INTERFACE)
target_include_directories(oa-ui-display-layout INTERFACE include)
target_compile_features(oa-ui-display-layout INTERFACE cxx_std_20)
