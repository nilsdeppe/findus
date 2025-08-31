# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

add_library(RtsFlags INTERFACE)

set(_RTS_CXX_FLAGS
  "-W;\
-Wall;\
-Wcast-align;\
-Wcast-qual;\
-Wdisabled-optimization;\
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
-Wwrite-strings;")

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(_RTS_CXX_FLAGS "${_RTS_CXX_FLAGS}\
-Wdocumentation;\
-Wnewline-eof;\
-Werror=undefined-internal;")
endif()

option(RTS_DEBUG_SYMBOLS "Add -g to CMAKE_CXX_FLAGS if ON, -g0 if OFF." ON)

if(NOT ${RTS_DEBUG_SYMBOLS})
  string(REPLACE "-g " "-g0 " CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
endif()

# Always build with -g so we can view backtraces, etc. when production code
# fails. This can be overridden by passing `-D DEBUG_SYMBOLS=OFF` to CMake
if(${RTS_DEBUG_SYMBOLS})
  set_property(TARGET RtsFlags
    APPEND PROPERTY INTERFACE_COMPILE_OPTIONS -g)
endif(${RTS_DEBUG_SYMBOLS})

foreach(_FLAG ${_RTS_CXX_FLAGS})
  set_property(TARGET RtsFlags
    APPEND PROPERTY
    INTERFACE_COMPILE_OPTIONS
    "$<$<COMPILE_LANGUAGE:CXX>:${_FLAG}>")
endforeach()
