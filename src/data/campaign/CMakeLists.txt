# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Campaign and map files: the campaign TDFs, the mission and map catalogues
# and the directory listings they come from.
add_library(oa-data-campaign src/campaign_file.cpp src/map_catalog.cpp src/directory_list.cpp)
add_library(oa::data::campaign ALIAS oa-data-campaign)
target_include_directories(oa-data-campaign PUBLIC include)
target_compile_features(oa-data-campaign PUBLIC cxx_std_20)
target_link_libraries(oa-data-campaign PUBLIC oa-formats-tdf oa-core-types oa-options PRIVATE oa-base-text oa-data-defs)
add_library(oa-data-campaign-assets src/campaign_assets.cpp)
add_library(oa::data::campaign_assets ALIAS oa-data-campaign-assets)
target_link_libraries(oa-data-campaign-assets PUBLIC oa-data-campaign PRIVATE oa-formats-hpi)
if(BUILD_TESTING)
 add_executable(oa-campaign-file-test tests/campaign_file_test.cpp)
 target_link_libraries(oa-campaign-file-test PRIVATE oa-data-campaign oa-data-campaign-assets oa-formats-hpi oa-test-game-data)
 add_test(NAME campaign-file COMMAND oa-campaign-file-test)
 # Every campaign and map of the installed game, through its store.
 oa_add_game_data_test(campaign-file-data oa-campaign-file-test --data)
 add_executable(oa-map-catalog-test tests/map_catalog_test.cpp)
 target_link_libraries(oa-map-catalog-test PRIVATE oa-data-campaign oa-data-campaign-assets oa-formats-hpi oa-test-game-data)
 add_test(NAME map-catalog COMMAND oa-map-catalog-test)
 oa_add_game_data_test(map-catalog-data oa-map-catalog-test --data)
endif()
