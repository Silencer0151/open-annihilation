# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Each UI package owns its own CMakeLists.txt; include those that exist.
set(oa_ui_packages hud frontend campaign console)
foreach(package IN LISTS oa_ui_packages)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${package}/CMakeLists.txt")
    add_subdirectory(${package})
  endif()
endforeach()
