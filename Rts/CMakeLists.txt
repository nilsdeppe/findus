# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

set(LIBRARY ToyRTS)

add_library(
  ${LIBRARY}
  ActionState.cpp
  DistributedTaskDriver.cpp
  Exceptions/Exception.cpp
  Exceptions/Mpi.cpp
  Exceptions/Qd.cpp
  HardwareInfo.cpp
  MessageHeader.cpp
  MessageTags.cpp
  ParentAndChildren.cpp
  QuiescenceDetection.cpp
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
  PRIVATE
  RtsFlags
  hwloc::hwloc
)

if (BUILD_TESTING AND TARGET doctest::doctest)
  # Configure a library that holds just the implementation of the doctest
  # functions. This can then be linked privately into the ToyRts library
  # so that any library or executable linked against it can safely ignore
  # whether or not doctest is needed.
  add_library(_RtsDoctestImpl
    DoctestImpl.cpp
  )
  target_link_libraries(_RtsDoctestImpl
    PRIVATE
    RtsFlags
    doctest::doctest
  )
  target_link_libraries(ToyRTS
    PRIVATE
    _RtsDoctestImpl
    doctest::doctest
  )

  # Add a separate testing executable for the unit tests.
  add_executable(
    Test
    Test.cpp
  )
  target_link_libraries(Test
    PRIVATE
    RtsFlags
    ToyRTS
    doctest::doctest
  )
  # There are issues with self-registering Test when using static libraries.
  # This is a common issue across all testing libraries that have
  # self-registering Test. doctest provides a nice workaround for it.
  doctest_force_link_static_lib_in_target(Test ToyRTS)
  # Discover the Test and add them to CTest so we don't have to.
  doctest_discover_tests(Test)
endif()
