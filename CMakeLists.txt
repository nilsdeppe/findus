# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

set(CODE_NAME findus)
set(CODE_VERSION 0.1.0)

cmake_minimum_required(VERSION 3.12.0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

project(${CODE_NAME} VERSION ${CODE_VERSION} LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)

if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
  set(FINDUS_FETCHCONTENT_BASE_ARGS SYSTEM)
else()
  set(FINDUS_FETCHCONTENT_BASE_ARGS "")
endif()

option(FINDUS_BUILD_DOCS "Enable building documentation" ON)
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

include(FindusFlags)
include(CharmInterop)
include(CTest)
if (BUILD_TESTING)
  include(SetupDoctest)
endif(BUILD_TESTING)
include(Sanitizers)

find_package(MPI REQUIRED COMPONENTS CXX)
find_package(hwloc REQUIRED)

add_subdirectory(Rts)

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
