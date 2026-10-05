# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The engine's own language packs, languages/<tag>/ in the source tree
# (docs/languages.md), travel with the game in the languages folder of its
# OA_GAME_FILES_DIR, beside the fonts, where the game reads them.

set(OA_LANGUAGE_PACKS_DIR "${CMAKE_CURRENT_LIST_DIR}/../languages")
cmake_path(NORMAL_PATH OA_LANGUAGE_PACKS_DIR)

# Copies the engine's language packs into a directory's languages folder
# after a target builds.
#
# target: the target whose build the copy follows
# directory: the folder that receives languages/<tag>/
function(oa_copy_language_packs target directory)
  file(GLOB packs LIST_DIRECTORIES true CONFIGURE_DEPENDS "${OA_LANGUAGE_PACKS_DIR}/*")
  set(commands "")
  foreach(pack IN LISTS packs)
    if(IS_DIRECTORY "${pack}" AND EXISTS "${pack}/language.yaml")
      cmake_path(GET pack FILENAME tag)
      list(APPEND commands COMMAND ${CMAKE_COMMAND} -E copy_directory "${pack}"
        "${directory}/languages/${tag}")
    endif()
  endforeach()
  if(commands)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${directory}/languages"
      ${commands}
      VERBATIM)
  endif()
endfunction()
