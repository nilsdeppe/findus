
set(CODE_NAME ToyRts)
set(CODE_VERSION 0.1.0)

cmake_minimum_required(VERSION 3.12.0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

project(${CODE_NAME} VERSION ${CODE_VERSION} LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)

include(RtsFlags)

find_package(MPI REQUIRED COMPONENTS CXX)

add_subdirectory(Rts)

add_executable(
  Main
  Main.cpp
)

target_link_libraries(
  Main
  PUBLIC
  MPI::MPI_CXX
  -lhwloc
  ToyRTS
)
