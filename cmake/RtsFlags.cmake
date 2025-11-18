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

option(ENABLE_PROFILING "Enables various options to make profiling easier" OFF)

option(KEEP_FRAME_POINTER "Add keep frame pointer for profiling" OFF)

add_library(Profiling::KeepFramePointer IMPORTED INTERFACE)
add_library(Profiling::EnableProfiling IMPORTED INTERFACE)

if (KEEP_FRAME_POINTER OR ENABLE_PROFILING)
  set_property(
    TARGET Profiling::KeepFramePointer
    APPEND PROPERTY
    INTERFACE_COMPILE_OPTIONS
    $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>
    $<$<COMPILE_LANGUAGE:CXX>:-mno-omit-leaf-frame-pointer>
  )
endif()

# Try to detect cache line size on Linux and macOS
if(APPLE)
  # On macOS, sysctl can be used
  execute_process(
    COMMAND sysctl -n hw.cachelinesize
    OUTPUT_VARIABLE CACHE_LINE_SIZE
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
elseif(UNIX)
  # On Linux, getconf is usually available
  execute_process(
    COMMAND getconf LEVEL1_DCACHE_LINESIZE
    OUTPUT_VARIABLE CACHE_LINE_SIZE
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endif()

# Fallback to 64 if detection failed or result is empty
if(NOT CACHE_LINE_SIZE OR CACHE_LINE_SIZE STREQUAL "")
  set(CACHE_LINE_SIZE 64)
endif()
message(STATUS "Detected cache line size: ${CACHE_LINE_SIZE}")
add_library(ToyRts::CacheLineSize IMPORTED INTERFACE)
target_compile_definitions(ToyRts::CacheLineSize
  INTERFACE
  RTS_CACHE_LINE_SIZE=${CACHE_LINE_SIZE})


target_link_libraries(
  RtsFlags
  INTERFACE
  Profiling::EnableProfiling
  Profiling::KeepFramePointer
  ToyRts::CacheLineSize
)
