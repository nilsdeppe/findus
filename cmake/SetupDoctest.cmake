# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

option(FINDUS_FETCH_DOCTEST "If ON, then we fetch doctest." OFF)

if (FINDUS_FETCH_DOCTEST)
  include(FetchContent)
  FetchContent_Declare(
    doctest
    GIT_REPOSITORY "https://github.com/doctest/doctest"
    GIT_TAG "v2.4.12"
    GIT_SHALLOW TRUE
    ${FINDUS_FETCHCONTENT_BASE_ARGS}
  )
  FetchContent_MakeAvailable(doctest)
  # Include cmake config from doctest
  include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
  # We need to use a hack to deal with static libs.
  # This hack is even provided by doctest.
  include(${doctest_SOURCE_DIR}/examples/exe_with_static_libs/doctest_force_link_static_lib_in_target.cmake)
  set_property(TARGET FindusFlags
    APPEND PROPERTY
    INTERFACE_COMPILE_DEFINITIONS
    "$<$<COMPILE_LANGUAGE:CXX>:FINDUS_ENABLE_TESTING>")
else(FINDUS_FETCH_DOCTEST)
  message(STATUS
    "doctest not found. Set -DFINDUS_FETCH_DOCTEST=ON if you want to "
    "download doctest and run the tests.")
endif()
