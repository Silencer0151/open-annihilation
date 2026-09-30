# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The extensions oa-game is built with.
#
# A project registers each extension library once, after the engine's
# directory is added (add_subdirectory) or, for a directory inside the
# engine's own tree, anywhere in its configure:
#
#   oa_add_extension(<target>
#     INIT <function>               # void <function>(oa::app::Extension* table)
#     [SWITCHES <letters>]          # the game switches its handler takes
#     [GAME_FILES <COMMAND ...>])   # what it copies beside the game
#
# <target> is the library that defines <function>, a global C++ function
# that fills the zeroed table it is given (src/app/include/oa/app/extension.hpp).
# SWITCHES names the letters of the game's command line the extension's
# switch handler takes, in lower case; the tests that start the game with
# such a switch expect what the extensions declare. GAME_FILES holds COMMAND
# arguments of add_custom_command, run after the game builds, that copy the
# files the extension needs at run time into the folder SDL_GetBasePath()
# names then, which the engine creates first:
# $<GENEX_EVAL:$<TARGET_PROPERTY:oa-game,OA_GAME_FILES_DIR>>.
#
# When the top-level directory's configure ends, the engine writes the
# source that lists the registered extensions in the order oa-game
# combines them: an extension after every registered extension its library
# links (directly or through other libraries), the rest in the order they
# were registered. It compiles that source into oa-game, links every
# extension library, copies their game files after each build, and
# registers the tests whose expectations depend on the extensions. With no
# extension registered, the game has none and runs without multiplayer.
include_guard(GLOBAL)

# Registers an extension library of oa-game (see above).
function(oa_add_extension target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "INIT;SWITCHES" "GAME_FILES")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "oa_add_extension(${target}): unknown arguments ${arg_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "oa_add_extension(${target}): no such target")
  endif()
  get_target_property(aliased "${target}" ALIASED_TARGET)
  if(aliased)
    message(FATAL_ERROR "oa_add_extension(${target}): name the library, ${aliased}, not its alias")
  endif()
  if(NOT "${arg_INIT}" MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    message(FATAL_ERROR "oa_add_extension(${target}): INIT must name a global C++ function, "
      "not \"${arg_INIT}\"")
  endif()
  # The letters the engine handles itself never reach an extension.
  if(NOT "${arg_SWITCHES}" MATCHES "^[a-z]*$" OR "${arg_SWITCHES}" MATCHES "[bdflrsw]")
    message(FATAL_ERROR "oa_add_extension(${target}): SWITCHES must be lower-case letters other "
      "than b, d, f, l, r, s and w, not \"${arg_SWITCHES}\"")
  endif()
  get_property(registered GLOBAL PROPERTY OA_EXTENSIONS)
  if("${target}" IN_LIST registered)
    message(FATAL_ERROR "oa_add_extension(${target}): registered twice")
  endif()
  foreach(other IN LISTS registered)
    get_property(other_init GLOBAL PROPERTY "OA_EXTENSION_INIT_${other}")
    if(other_init STREQUAL arg_INIT)
      message(FATAL_ERROR "oa_add_extension(${target}): ${other} names the same INIT, ${arg_INIT}")
    endif()
  endforeach()
  set_property(GLOBAL APPEND PROPERTY OA_EXTENSIONS "${target}")
  set_property(GLOBAL PROPERTY "OA_EXTENSION_INIT_${target}" "${arg_INIT}")
  set_property(GLOBAL PROPERTY "OA_EXTENSION_SWITCHES_${target}" "${arg_SWITCHES}")
  set_property(GLOBAL PROPERTY "OA_EXTENSION_GAME_FILES_${target}" "${arg_GAME_FILES}")
endfunction()

# Sets <output> in the caller's scope to the libraries <target> links,
# directly or through other libraries, as target names.
function(_oa_extension_links target output)
  set(pending "${target}")
  set(seen "")
  while(pending)
    list(POP_FRONT pending current)
    foreach(property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
      get_target_property(links "${current}" ${property})
      if(NOT links)
        continue()
      endif()
      foreach(link IN LISTS links)
        string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" link "${link}")
        if(NOT TARGET "${link}")
          continue()
        endif()
        get_target_property(aliased "${link}" ALIASED_TARGET)
        if(aliased)
          set(link "${aliased}")
        endif()
        if(NOT link IN_LIST seen)
          list(APPEND seen "${link}")
          list(APPEND pending "${link}")
        endif()
      endforeach()
    endforeach()
  endwhile()
  set(${output} "${seen}" PARENT_SCOPE)
endfunction()

# Records what the step at the end of the configure needs from the engine's
# directory, and schedules that step. The engine calls it once, where it
# defines oa-game.
function(oa_schedule_extension_list)
  set_property(GLOBAL PROPERTY OA_EXTENSION_ENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  set_property(GLOBAL PROPERTY OA_EXTENSION_ENGINE_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
  set_property(GLOBAL PROPERTY OA_EXTENSION_NATIVE_CHECK_TIMEOUT "${OA_NATIVE_CHECK_TIMEOUT}")
  set_property(GLOBAL PROPERTY OA_EXTENSION_SKIP_CODE "${OA_GAME_DATA_SKIP_CODE}")
  cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _oa_finish_extension_list)
endfunction()

# The step at the end of the top-level directory's configure (see above).
function(_oa_finish_extension_list)
  get_property(engine_source_dir GLOBAL PROPERTY OA_EXTENSION_ENGINE_SOURCE_DIR)
  get_property(engine_binary_dir GLOBAL PROPERTY OA_EXTENSION_ENGINE_BINARY_DIR)
  get_property(registered GLOBAL PROPERTY OA_EXTENSIONS)

  # Dependency order: take, each time, the first extension in registration
  # order whose registered dependencies are all placed.
  set(dependencies_of "")
  foreach(extension IN LISTS registered)
    _oa_extension_links("${extension}" links)
    set(dependencies "")
    foreach(other IN LISTS registered)
      if(NOT other STREQUAL extension AND other IN_LIST links)
        list(APPEND dependencies "${other}")
      endif()
    endforeach()
    set("dependencies_${extension}" "${dependencies}")
  endforeach()
  set(ordered "")
  set(remaining "${registered}")
  while(remaining)
    set(placed "")
    foreach(extension IN LISTS remaining)
      set(ready TRUE)
      foreach(dependency IN LISTS dependencies_${extension})
        if(NOT dependency IN_LIST ordered)
          set(ready FALSE)
        endif()
      endforeach()
      if(ready)
        set(placed "${extension}")
        break()
      endif()
    endforeach()
    if(NOT placed)
      message(FATAL_ERROR "The extensions ${remaining} link each other; none can come first")
    endif()
    list(APPEND ordered "${placed}")
    list(REMOVE_ITEM remaining "${placed}")
  endwhile()

  # The source that lists them.
  set(declarations "")
  set(entries "")
  set(switches "")
  set(game_files "")
  foreach(extension IN LISTS ordered)
    get_property(init GLOBAL PROPERTY "OA_EXTENSION_INIT_${extension}")
    get_property(letters GLOBAL PROPERTY "OA_EXTENSION_SWITCHES_${extension}")
    get_property(files GLOBAL PROPERTY "OA_EXTENSION_GAME_FILES_${extension}")
    string(APPEND declarations "void ${init}(oa::app::Extension* table);\n")
    string(APPEND entries "    {\"${extension}\", ${init}},\n")
    string(APPEND switches "${letters}")
    list(APPEND game_files ${files})
  endforeach()
  if(ordered)
    string(CONCAT list_body
      "constexpr RegisteredExtension kRegisteredExtensions[] = {\n${entries}};\n\n"
      "} // namespace\n\n"
      "std::span<const RegisteredExtension> registered_extensions() noexcept {\n"
      "    return kRegisteredExtensions;\n}\n")
  else()
    string(CONCAT list_body
      "} // namespace\n\n"
      "std::span<const RegisteredExtension> registered_extensions() noexcept {\n"
      "    return {};\n}\n")
  endif()
  set(list_source "${engine_binary_dir}/generated/registered_extensions.cpp")
  file(CONFIGURE OUTPUT "${list_source}" @ONLY CONTENT
"// Generated by cmake/OaExtensions.cmake from the extensions this build
// registers; changes are lost at the next configure.
#include \"oa/app/extension_list.hpp\"

@declarations@
namespace oa::app {
namespace {

@list_body@
} // namespace oa::app
")
  if(TARGET oa-game)
    target_sources(oa-game PRIVATE "${list_source}")
    if(ordered)
      target_link_libraries(oa-game PRIVATE ${ordered})
    endif()
  endif()

  # The extensions' game files, copied by a script oa-game's build runs
  # (the engine's POST_BUILD command): each COMMAND of theirs one
  # execute_process, its generator expressions evaluated per configuration.
  set(script "")
  set(command "")
  foreach(argument IN LISTS game_files ITEMS COMMAND)
    if(argument STREQUAL "COMMAND")
      if(command)
        string(APPEND script "execute_process(COMMAND${command} COMMAND_ERROR_IS_FATAL ANY)\n")
      endif()
      set(command "")
    else()
      string(APPEND command " [==[${argument}]==]")
    endif()
  endforeach()
  file(GENERATE OUTPUT "${engine_binary_dir}/generated/extension-game-files-$<CONFIG>.cmake"
    CONTENT "# Generated by cmake/OaExtensions.cmake: the extensions' game files.\n${script}")

  # The tests whose expectations depend on the extensions.
  if(NOT BUILD_TESTING OR NOT TARGET oa-game)
    return()
  endif()
  get_property(timeout GLOBAL PROPERTY OA_EXTENSION_NATIVE_CHECK_TIMEOUT)
  get_property(skip_code GLOBAL PROPERTY OA_EXTENSION_SKIP_CODE)
  if(IS_DIRECTORY "${OA_GAME_DIR}")
    # The game's -y switch (like -c and -n) skips both opening movies;
    # unless an extension takes it, it is refused before anything plays.
    if("${switches}" MATCHES "y")
      set(intro_switch_result "native check")
    else()
      set(intro_switch_result "-y is not handled by this build")
    endif()
    add_test(NAME native-intro-switch COMMAND oa-game --game-dir "${OA_GAME_DIR}"
      --mute --headless-check --frames 1 -y
      --preferences-file "${engine_binary_dir}/native-checks/intro-switch.conf")
    set_tests_properties(native-intro-switch PROPERTIES
      PASS_REGULAR_EXPRESSION "${intro_switch_result}" FAIL_REGULAR_EXPRESSION "zrb")
  endif()
  # MULTI's notice over the demo, started on the folder that holds the
  # demo's installer as the other native-demo tests are: an extension checks
  # its own screens instead (--check-multiplayer-menu), so it is checked
  # only without one; skipped without an installer.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(NOT ordered AND Python3_Interpreter_FOUND)
    add_test(NAME native-demo-multiplayer-menu COMMAND ${Python3_EXECUTABLE}
      "${engine_source_dir}/tools/check_native_demo_installer.py"
      --native $<TARGET_FILE:oa-game> --check multiplayer-menu
      --scratch-root "${engine_binary_dir}/native-checks")
    set_tests_properties(native-demo-multiplayer-menu PROPERTIES TIMEOUT ${timeout}
      SKIP_RETURN_CODE ${skip_code})
    if(OA_DEMO_INSTALLER)
      set_property(TEST native-demo-multiplayer-menu APPEND PROPERTY ENVIRONMENT
        "OA_DEMO_INSTALLER=${OA_DEMO_INSTALLER}")
    endif()
  endif()
endfunction()
