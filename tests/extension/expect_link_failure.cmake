# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Builds a target that must fail to link, and passes only when the build
# fails and its output names the symbol the link was meant to fail on.
# Run with cmake -P and:
#   BUILD_DIR  the build tree that holds the target
#   TARGET     the target to build
#   SYMBOL     a regular expression the failed build's output must match
#   CONFIG     the configuration to build, for multi-config generators
foreach(variable BUILD_DIR TARGET SYMBOL)
  if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
    message(FATAL_ERROR "expect_link_failure.cmake needs -D${variable}=...")
  endif()
endforeach()

set(build_arguments --build "${BUILD_DIR}" --target "${TARGET}")
if(DEFINED CONFIG AND NOT "${CONFIG}" STREQUAL "")
  list(APPEND build_arguments --config "${CONFIG}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" ${build_arguments}
  RESULT_VARIABLE status
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output)

if(status EQUAL 0)
  message(FATAL_ERROR "${TARGET} linked, but it must fail on ${SYMBOL}:\n${output}")
endif()
if(NOT output MATCHES "${SYMBOL}")
  message(FATAL_ERROR "${TARGET} failed to build (${status}), but not on ${SYMBOL}:\n${output}")
endif()
message(STATUS "${TARGET} failed to link on ${SYMBOL}, as it must")
