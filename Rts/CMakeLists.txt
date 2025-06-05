# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

set(LIBRARY ToyRTS)

add_library(
  ${LIBRARY}
  ActionState.cpp
  DistributedTaskDriver.cpp
  Exception.cpp
  MessageTags.cpp
  MpiException.cpp
)

target_include_directories(
  ${LIBRARY}
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include/>
)

target_link_libraries(
  ${LIBRARY}
  PUBLIC
  MPI::MPI_CXX
  -lhwloc
)
