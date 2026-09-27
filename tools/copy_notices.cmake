# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Copies LICENSE, ATTRIBUTIONS.md and the licenses/ folder its links point
# into from SOURCE_DIR to DEST, leaving files that are already current.
# Executables that share an output directory run this at the same time in a
# parallel build, and on Windows a copy onto a file that another copy holds
# open fails, so each run holds LOCK_FILE while it copies.
file(LOCK "${LOCK_FILE}" GUARD PROCESS TIMEOUT 300)
foreach(notice IN ITEMS LICENSE ATTRIBUTIONS.md)
  file(COPY_FILE "${SOURCE_DIR}/${notice}" "${DEST}/${notice}" ONLY_IF_DIFFERENT)
endforeach()
file(COPY "${SOURCE_DIR}/licenses" DESTINATION "${DEST}")
