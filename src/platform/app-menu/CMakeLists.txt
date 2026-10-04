# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The macOS application menu's Settings… item; nothing on other platforms, or when a platform
# without that menu bar (OA_NATIVE_APP_MENU OFF) builds for an Apple system.
option(OA_NATIVE_APP_MENU "Build the macOS application menu's Settings item" ON)
if(APPLE AND OA_NATIVE_APP_MENU)
  enable_language(OBJCXX)
  add_library(oa-platform-app-menu src/apple_app_menu.mm)
  set_target_properties(oa-platform-app-menu PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON)
  target_link_libraries(oa-platform-app-menu PRIVATE "-framework AppKit")
else()
  add_library(oa-platform-app-menu src/app_menu.cpp)
endif()
add_library(oa::platform::app_menu ALIAS oa-platform-app-menu)
target_include_directories(oa-platform-app-menu PUBLIC include)
target_compile_features(oa-platform-app-menu PUBLIC cxx_std_20)
target_link_libraries(oa-platform-app-menu PRIVATE oa-options)
if(BUILD_TESTING)
  add_executable(oa-platform-app-menu-test tests/app_menu_test.cpp)
  target_link_libraries(oa-platform-app-menu-test PRIVATE oa-platform-app-menu oa-options)
  add_test(NAME platform-app-menu COMMAND oa-platform-app-menu-test)
  if(APPLE AND OA_NATIVE_APP_MENU)
    add_executable(oa-platform-app-menu-apple-test tests/apple_app_menu_test.mm)
    set_target_properties(oa-platform-app-menu-apple-test PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON)
    target_link_libraries(oa-platform-app-menu-apple-test PRIVATE oa-platform-app-menu oa-options "-framework AppKit")
    add_test(NAME platform-app-menu-apple COMMAND oa-platform-app-menu-apple-test)
  endif()
endif()
