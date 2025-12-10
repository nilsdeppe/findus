# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

set(CODE_NAME findus)
set(CODE_VERSION 0.1.0)

cmake_minimum_required(VERSION 3.18.0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

project(${CODE_NAME} VERSION ${CODE_VERSION} LANGUAGES CXX)

if (NOT DEFINED PROJECT_IS_TOP_LEVEL)
  set(PROJECT_IS_TOP_LEVEL OFF)
  if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(PROJECT_IS_TOP_LEVEL ON)
  endif()
endif()

include(GNUInstallDirs)

option(FINDUS_ENABLE_INSTALL "Enable install rules." ${PROJECT_IS_TOP_LEVEL})

if (PROJECT_IS_TOP_LEVEL)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
  set(FINDUS_FETCHCONTENT_BASE_ARGS SYSTEM)
else()
  set(FINDUS_FETCHCONTENT_BASE_ARGS "")
endif()

if (PROJECT_IS_TOP_LEVEL)
  option(FINDUS_BUILD_DOCS "Enable building documentation" ON)
else()
  option(FINDUS_BUILD_DOCS "Enable building documentation" OFF)
endif()
option(
  FINDUS_DOCS_ONLY
  "Skip all initialization not required for rendering documentation"
  OFF
)
if (FINDUS_BUILD_DOCS)
  include(Documentation)
  if (FINDUS_DOCS_ONLY)
    return()
  endif()
endif()

# We fill this list with the targets we wish to export.
set(FINDUS_EXPORT_TARGETS_LIST
  findus)

include(FindusFlags)
include(CharmInterop)
if (PROJECT_IS_TOP_LEVEL)
  include(CTest)
  if (BUILD_TESTING)
    include(SetupDoctest)
  endif()
endif()
include(Sanitizers)

if (NOT TARGET MPI::MPI_CXX)
  find_package(MPI REQUIRED COMPONENTS CXX)
endif()
if (NOT TARGET hwloc::hwloc)
  find_package(hwloc REQUIRED)
endif()

add_subdirectory(src)

# Add findusTargets for in-tree consumption with FetchContent
export(
  TARGETS ${FINDUS_EXPORT_TARGETS_LIST}
  FILE "${CMAKE_CURRENT_BINARY_DIR}/findusTargets.cmake"
  NAMESPACE findus::
)

if (FINDUS_ENABLE_INSTALL)
  # Install our Findhwloc
  install(
    FILES cmake/Findhwloc.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/findus/modules
  )
endif()
set(_FINDUS_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}/modules")
file(MAKE_DIRECTORY "${_FINDUS_MODULE_DIR}")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Findhwloc.cmake"
  "${_FINDUS_MODULE_DIR}/Findhwloc.cmake"
  COPYONLY
)
# Set up the CMake configuration
include(CMakePackageConfigHelpers)
configure_package_config_file(
  cmake/findusConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/findusConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/findus
)
if (FINDUS_ENABLE_INSTALL)
  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/findusConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/findus
  )
endif()
# Set up the findus version identification via CMake
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/findusConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
if (FINDUS_ENABLE_INSTALL)
  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/findusConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/findus
  )
endif()

if (FINDUS_ENABLE_INSTALL)
  install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/include/findus/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/findus
  )

  set(_FINDUS_TEST_TARGET "")
  if (BUILD_SHARED AND TARGET _FindusDoctestImpl)
    set(_FINDUS_TEST_TARGET _FindusDoctestImpl)
  endif()
  install(TARGETS findus findusFlags findusSanitizers
    ${_FINDUS_TEST_TARGET}
    EXPORT findus
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )

  install(
    EXPORT findus
    FILE findusTargets.cmake
    NAMESPACE findus::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/findus
  )
endif()

if (PROJECT_IS_TOP_LEVEL)
  add_executable(
    Main
    Main.cpp
  )

  target_link_libraries(
    Main
    PUBLIC
    MPI::MPI_CXX
    findus
  )
endif()

list(POP_BACK CMAKE_MODULE_PATH)
