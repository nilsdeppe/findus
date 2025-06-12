
set(CODE_NAME ToyRts)
set(CODE_VERSION 0.1.0)

cmake_minimum_required(VERSION 3.12.0)

project(${CODE_NAME} VERSION ${CODE_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(MPI REQUIRED COMPONENTS CXX)

add_subdirectory(Rts)

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

set(_RTS_CXX_FLAGS
  "-W;\
-Wall;\
-Wcast-align;\
-Wcast-qual;\
-Wdisabled-optimization;\
-Wdocumentation;\
-Wextra;\
-Wformat-nonliteral;\
-Wformat-security;\
-Wformat-y2k;\
-Wformat=2;\
-Winvalid-pch;\
-Wmissing-declarations;\
-Wmissing-field-initializers;\
-Wmissing-format-attribute;\
-Wmissing-include-dirs;\
-Wmissing-noreturn;\
-Wnewline-eof;\
-Wnon-virtual-dtor;\
-Wold-style-cast;\
-Woverloaded-virtual;\
-Wpacked;\
-Wpedantic;\
-Wpointer-arith;\
-Wredundant-decls;\
-Wshadow;\
-Wsign-conversion;\
-Wstack-protector;\
-Wswitch-default;\
-Wunreachable-code;\
-Wwrite-strings")

foreach(_FLAG ${_RTS_CXX_FLAGS})
  set_property(TARGET Main
    APPEND PROPERTY
    COMPILE_OPTIONS
    "$<$<COMPILE_LANGUAGE:CXX>:${_FLAG}>")
endforeach()

find_package(MPI REQUIRED COMPONENTS CXX)

target_link_libraries(
  Main
  PUBLIC
  MPI::MPI_CXX
  -lhwloc
  ToyRTS
)
