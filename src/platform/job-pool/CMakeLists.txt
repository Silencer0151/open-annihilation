# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# A small fixed pool of worker threads that runs a job's bands with the
# calling thread, on the platform layer's threads and wake events.
add_library(oa-platform-job-pool src/job_pool.cpp)
add_library(oa::platform::job_pool ALIAS oa-platform-job-pool)
target_include_directories(oa-platform-job-pool PUBLIC include)
target_compile_features(oa-platform-job-pool PUBLIC cxx_std_20)
target_link_libraries(oa-platform-job-pool PUBLIC oa-platform-shims PRIVATE oa-options)

if(BUILD_TESTING)
  add_executable(oa-platform-job-pool-test tests/job_pool_test.cpp)
  target_link_libraries(oa-platform-job-pool-test PRIVATE oa-platform-job-pool oa-options)
  add_test(NAME platform-job-pool COMMAND oa-platform-job-pool-test)
  set_tests_properties(platform-job-pool PROPERTIES TIMEOUT 60)
endif()
