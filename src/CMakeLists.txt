# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

set(LIBRARY findus)

add_library(
  ${LIBRARY}
  ActionState.cpp
  Atomic128.cpp
  BindTo.cpp
  Detail/ActiveObject.cpp
  Detail/DistributedObjectBase.cpp
  Detail/DistributedObjectIndex.cpp
  Detail/IndexConversion.cpp
  Detail/IsTuple.cpp
  Detail/MpiErrorMessage.cpp
  Detail/PrintProcessPids.cpp
  Detail/ReductionCounter.cpp
  Detail/VectorStream.cpp
  DistributedObjectIndex.cpp
  DistributedTaskDriver.cpp
  Exceptions/Exception.cpp
  Exceptions/Mpi.cpp
  Exceptions/Qd.cpp
  HardwareInfo.cpp
  Message.cpp
  MessageHeader.cpp
  MessageRequeue.cpp
  MessageTags.cpp
  MessageType.cpp
  Options.cpp
  ParentAndChildren.cpp
  QuiescenceDetection.cpp
  Reduction.cpp
  Serialize/Action.cpp
  Serialize/Bytes.cpp
  Serialize/Exception.cpp
  Serialize/Serializer.cpp
  Serialize/Stl/Array.cpp
  Serialize/Stl/Associative.cpp
  Serialize/Stl/Complex.cpp
  Serialize/Stl/Deque.cpp
  Serialize/Stl/ForwardList.cpp
  Serialize/Stl/List.cpp
  Serialize/Stl/Pair.cpp
  Serialize/Stl/SharedPtr.cpp
  Serialize/Stl/String.cpp
  Serialize/Stl/Tuple.cpp
  Serialize/Stl/UniquePtr.cpp
  Serialize/Stl/Vector.cpp
  Serialize/View.cpp
  Serialize/Virtual.cpp
)

add_library(findus::findus ALIAS ${LIBRARY})

target_compile_features(findus PUBLIC cxx_std_20)

target_include_directories(
  ${LIBRARY}
  PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(
  ${LIBRARY}
  PUBLIC
  MPI::MPI_CXX
  findus::CacheLineSize
  findus::Flags
  PRIVATE
  $<BUILD_INTERFACE:findus::WarningFlags>
  hwloc::hwloc
)

if (PROJECT_IS_TOP_LEVEL AND BUILD_TESTING AND TARGET doctest::doctest)
  target_sources(findus
    PRIVATE
    Detail/DoctestImpl.cpp
  )
  target_link_libraries(findus
    PRIVATE
    $<BUILD_INTERFACE:doctest::doctest>
  )
  list(APPEND FINDUS_EXPORT_TARGETS_LIST doctest)

  # Add a separate testing executable for the unit tests.
  add_executable(
    Test
    Detail/Test.cpp
  )
  target_link_libraries(Test
    PRIVATE
    findusFlags
    $<BUILD_INTERFACE:findus::WarningFlags>
    findus
    doctest::doctest
  )

  set_property(TARGET findus Test
    APPEND PROPERTY
    COMPILE_DEFINITIONS
    "$<$<COMPILE_LANGUAGE:CXX>:FINDUS_ENABLE_TESTING>")
  # There are issues with self-registering Test when using static libraries.
  # This is a common issue across all testing libraries that have
  # self-registering Test. doctest provides a nice workaround for it.
  if (NOT BUILD_SHARED_LIBS)
    doctest_force_link_static_lib_in_target(Test findus)
  endif()
  # Discover the Test and add them to CTest so we don't have to.
  doctest_discover_tests(Test)

  set(MPIRUN_TEST_ARGS "" CACHE STRING
    "Extra arguments to pass to mpirun at launch.")

  # Add a separate testing for the MPI tests.
  # This is because initializing MPI adds about 0.6 seconds to the test runtime.
  add_test(
    NAME HardwareInfoParallel.2Processes
    COMMAND
    ${MPIEXEC_EXECUTABLE} ${MPIRUN_TEST_ARGS} ${MPIEXEC_NUMPROC_FLAG} 2
    $<TARGET_FILE:Test> --test-case=HardwareInfoParallel --findus-mpi-test
  )
  add_test(
    NAME DistributedTaskDriver.2Processes
    COMMAND
    ${MPIEXEC_EXECUTABLE} ${MPIRUN_TEST_ARGS} ${MPIEXEC_NUMPROC_FLAG} 2
    $<TARGET_FILE:Test> --test-case=DistributedTaskDriver.2Processes
    --findus-mpi-test
  )
  add_test(
    NAME MpiErrorMessage
    COMMAND
    ${MPIEXEC_EXECUTABLE} ${MPIRUN_TEST_ARGS} ${MPIEXEC_NUMPROC_FLAG} 1
    $<TARGET_FILE:Test> --test-case=MpiErrorMessage --findus-mpi-test --no-skip
  )

  add_test(
    NAME DistributedTaskDriver.InsertErrorRegular0
    COMMAND
    ${MPIEXEC_EXECUTABLE} ${MPIRUN_TEST_ARGS} ${MPIEXEC_NUMPROC_FLAG} 2
    $<TARGET_FILE:Test> --test-case=DistributedTaskDriver.InsertErrorRegular0
    --findus-mpi-test
  )
  add_test(
    NAME DistributedTaskDriver.InsertErrorRegular1
    COMMAND
    ${MPIEXEC_EXECUTABLE} ${MPIRUN_TEST_ARGS} ${MPIEXEC_NUMPROC_FLAG} 2
    $<TARGET_FILE:Test> --test-case=DistributedTaskDriver.InsertErrorRegular1
    --findus-mpi-test
  )
  add_test(
    NAME DistributedTaskDriver.InsertErrorCollection0
    COMMAND
    ${MPIEXEC_EXECUTABLE} ${MPIRUN_TEST_ARGS} ${MPIEXEC_NUMPROC_FLAG} 2
    $<TARGET_FILE:Test> --test-case=DistributedTaskDriver.InsertErrorCollection0
    --findus-mpi-test
  )
  add_test(
    NAME DistributedTaskDriver.InsertErrorCollection1
    COMMAND
    ${MPIEXEC_EXECUTABLE} ${MPIRUN_TEST_ARGS} ${MPIEXEC_NUMPROC_FLAG} 2
    $<TARGET_FILE:Test> --test-case=DistributedTaskDriver.InsertErrorCollection1
    --findus-mpi-test
  )
  add_test(
    NAME DistributedTaskDriver.InsertErrorRegularName
    COMMAND
    ${MPIEXEC_EXECUTABLE} ${MPIRUN_TEST_ARGS} ${MPIEXEC_NUMPROC_FLAG} 2
    $<TARGET_FILE:Test> --test-case=DistributedTaskDriver.InsertErrorRegularName
    --findus-mpi-test
  )
  add_test(
    NAME DistributedTaskDriver.InsertError_CollectionName0
    COMMAND
    ${MPIEXEC_EXECUTABLE} ${MPIRUN_TEST_ARGS} ${MPIEXEC_NUMPROC_FLAG} 2
    $<TARGET_FILE:Test>
    --test-case=DistributedTaskDriver.InsertError_CollectionName0
    --findus-mpi-test
  )
  add_test(
    NAME DistributedTaskDriver.InsertError_CollectionName1
    COMMAND
    ${MPIEXEC_EXECUTABLE} ${MPIRUN_TEST_ARGS} ${MPIEXEC_NUMPROC_FLAG} 2
    $<TARGET_FILE:Test>
    --test-case=DistributedTaskDriver.InsertError_CollectionName1
    --findus-mpi-test
  )
  add_test(
    NAME DistributedTaskDriver.InsertError_CollectionNumElements0
    COMMAND
    ${MPIEXEC_EXECUTABLE} ${MPIRUN_TEST_ARGS} ${MPIEXEC_NUMPROC_FLAG} 2
    $<TARGET_FILE:Test>
    --test-case=DistributedTaskDriver.InsertError_CollectionNumElements0
    --findus-mpi-test
  )
  add_test(
    NAME DistributedTaskDriver.InsertError_CollectionNumElements1
    COMMAND
    ${MPIEXEC_EXECUTABLE} ${MPIRUN_TEST_ARGS} ${MPIEXEC_NUMPROC_FLAG} 2
    $<TARGET_FILE:Test>
    --test-case=DistributedTaskDriver.InsertError_CollectionNumElements1
    --findus-mpi-test
  )
  add_test(
    NAME DistributedTaskDriver.InsertError_CollectionElementId0
    COMMAND
    ${MPIEXEC_EXECUTABLE} ${MPIRUN_TEST_ARGS} ${MPIEXEC_NUMPROC_FLAG} 2
    $<TARGET_FILE:Test>
    --test-case=DistributedTaskDriver.InsertError_CollectionElementId0
    --findus-mpi-test
  )
  add_test(
    NAME DistributedTaskDriver.InsertError_CollectionElementPid0
    COMMAND
    ${MPIEXEC_EXECUTABLE} ${MPIRUN_TEST_ARGS} ${MPIEXEC_NUMPROC_FLAG} 2
    $<TARGET_FILE:Test>
    --test-case=DistributedTaskDriver.InsertError_CollectionElementPid0
    --findus-mpi-test
  )
  add_test(
    NAME DistributedTaskDriver.Reduction1
    COMMAND
    ${MPIEXEC_EXECUTABLE} ${MPIRUN_TEST_ARGS} ${MPIEXEC_NUMPROC_FLAG} 2
    $<TARGET_FILE:Test>
    --test-case=DistributedTaskDriver.Reduction1
    --findus-mpi-test
  )
endif()

set(FINDUS_EXPORT_TARGETS_LIST "${FINDUS_EXPORT_TARGETS_LIST}" PARENT_SCOPE)
