# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The sockets of oa-netgame (socket_host.cpp), the one source that calls the
# system's socket functions, and the system library they need on Windows.
target_sources(oa-netgame PRIVATE socket_host.cpp)
if(WIN32)
  target_link_libraries(oa-netgame PUBLIC ws2_32)
endif()
