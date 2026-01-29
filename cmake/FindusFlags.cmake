# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

set(_FINDUS_CXX_FLAGS
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
  set(_FINDUS_CXX_FLAGS "${_FINDUS_CXX_FLAGS}\
-Wdocumentation;\
-Wnewline-eof;\
-Werror=undefined-internal;")
endif()

add_library(findusWarningFlags INTERFACE)
add_library(findus::WarningFlags ALIAS findusWarningFlags)
set_property(TARGET findusWarningFlags
  PROPERTY EXPORT_NAME WarningFlags
)


foreach(_FLAG ${_FINDUS_CXX_FLAGS})
  set_property(TARGET findusWarningFlags
    APPEND PROPERTY
    INTERFACE_COMPILE_OPTIONS
    "$<$<COMPILE_LANGUAGE:CXX>:${_FLAG}>")
endforeach()

add_library(findusFlags INTERFACE)
add_library(findus::Flags ALIAS findusFlags)
set_property(TARGET findusFlags
  PROPERTY EXPORT_NAME Flags
)

add_library(findusInternalFlags INTERFACE)
add_library(findus::InternalFlags ALIAS findusInternalFlags)
set_property(TARGET findusInternalFlags
  PROPERTY EXPORT_NAME InternalFlags
)

option(FINDUS_DEBUG_SYMBOLS "Add -g to CMAKE_CXX_FLAGS if ON, -g0 if OFF." ON)

if(NOT ${FINDUS_DEBUG_SYMBOLS})
  string(REPLACE "-g " "-g0 " CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
endif()

# Always build with -g so we can view backtraces, etc. when production code
# fails. This can be overridden by passing `-D DEBUG_SYMBOLS=OFF` to CMake
if(${FINDUS_DEBUG_SYMBOLS})
  set_property(TARGET findusInternalFlags
    APPEND PROPERTY INTERFACE_COMPILE_OPTIONS -g)
else()
  set_property(TARGET findusInternalFlags
    APPEND PROPERTY INTERFACE_COMPILE_OPTIONS -g0)
endif(${FINDUS_DEBUG_SYMBOLS})

option(ENABLE_PROFILING "Enables various options to make profiling easier" OFF)

option(KEEP_FRAME_POINTER "Add keep frame pointer for profiling" OFF)

add_library(findusKeepFramePointer INTERFACE)
add_library(findus::KeepFramePointer
  ALIAS findusKeepFramePointer)
set_property(TARGET findusKeepFramePointer
  PROPERTY EXPORT_NAME KeepFramePointer
)

add_library(findusEnableProfiling INTERFACE)
add_library(findus::EnableProfiling
  ALIAS findusEnableProfiling)
set_property(TARGET findusEnableProfiling
  PROPERTY EXPORT_NAME EnableProfiling
)

if (KEEP_FRAME_POINTER OR ENABLE_PROFILING)
  set_property(
    TARGET findusKeepFramePointer
    APPEND PROPERTY
    INTERFACE_COMPILE_OPTIONS
    $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>
    $<$<COMPILE_LANGUAGE:CXX>:-mno-omit-leaf-frame-pointer>
  )
endif()

set(FINDUS_CACHE_LINE_SIZE "" CACHE STRING
  "Cache line size in bytesto use. If not set we auto-detect.")

if (NOT FINDUS_CACHE_LINE_SIZE)
  # Try to detect cache line size on Linux and macOS
  if(APPLE)
    # On macOS, sysctl can be used
    execute_process(
      COMMAND sysctl -n hw.cachelinesize
      OUTPUT_VARIABLE FINDUS_CACHE_LINE_SIZE
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  elseif(UNIX)
    # On Linux, getconf is usually available
    execute_process(
      COMMAND getconf LEVEL1_DCACHE_LINESIZE
      OUTPUT_VARIABLE FINDUS_CACHE_LINE_SIZE
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  endif()

  # Fallback to 64 if detection failed or result is empty
  if(NOT FINDUS_CACHE_LINE_SIZE OR FINDUS_CACHE_LINE_SIZE STREQUAL "")
    set(FINDUS_CACHE_LINE_SIZE 64)
  endif()
endif()
message(STATUS "Cache line size: ${FINDUS_CACHE_LINE_SIZE}")
add_library(findusCacheLineSize INTERFACE)
add_library(findus::CacheLineSize ALIAS findusCacheLineSize)
set_property(TARGET findusCacheLineSize
  PROPERTY EXPORT_NAME CacheLineSize
)
target_compile_definitions(findusCacheLineSize
  INTERFACE FINDUS_CACHE_LINE_SIZE=${FINDUS_CACHE_LINE_SIZE})

if (FINDUS_ENABLE_INSTALL)
  install(TARGETS findusWarningFlags
    EXPORT findus
  )
  install(TARGETS findusKeepFramePointer
    EXPORT findus
  )
  install(TARGETS findusEnableProfiling
    EXPORT findus
  )
  install(TARGETS findusCacheLineSize
    EXPORT findus
  )
endif()

target_link_libraries(
  findusInternalFlags
  INTERFACE
  findus::EnableProfiling
  findus::KeepFramePointer
  findus::WarningFlags
)

target_link_libraries(
  findusFlags
  INTERFACE
  findus::CacheLineSize
)

set(FINDUS_EXPORT_TARGETS_LIST
  "${FINDUS_EXPORT_TARGETS_LIST};findusFlags;findusInternalFlags;\
findusWarningFlags;findusCacheLineSize;findusEnableProfiling;\
findusKeepFramePointer"
)
