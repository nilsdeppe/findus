
set(CODE_NAME ToyRts)
set(CODE_VERSION 0.1.0)

cmake_minimum_required(VERSION 3.12.0)

project(${CODE_NAME} VERSION ${CODE_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(
  Main
  Main.cpp
)

set_property(TARGET Main
  APPEND PROPERTY
  COMPILE_OPTIONS
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include csignal>")

set_property(TARGET Main
  APPEND PROPERTY
  COMPILE_OPTIONS
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cstddef>")

find_package(MPI REQUIRED COMPONENTS CXX)

target_link_libraries(
  Main
  PUBLIC
  MPI::MPI_CXX
  -lhwloc
  )
