# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

set(FINDUS_CCACHE_EXEC "" CACHE FILEPATH "The ccache executable")

set(FINDUS_CCACHE_LAUNCHER_EXTRA_ENV_VARS "" CACHE STRING "Env vars for ccache")

if (EXISTS ${FINDUS_CCACHE_EXEC})
  execute_process(COMMAND realpath ${FINDUS_CCACHE_EXEC}
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE FINDUS_REAL_CCACHE_EXEC)

  # Configure ccache with environment variables
  set(_CCACHE_LAUNCHER_ENV_VARS
    ${FINDUS_CCACHE_LAUNCHER_EXTRA_ENV_VARS}
    "CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,\
include_file_ctime,locale")

  # Invoke compiler through ccache
  set(CMAKE_CXX_COMPILER_LAUNCHER
    ${_CCACHE_LAUNCHER_ENV_VARS} ${FINDUS_REAL_CCACHE_EXEC})
  set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_CXX_COMPILER_LAUNCHER})
  message(STATUS "Using ccache for compilation. It is invoked as: "
    "${CMAKE_CXX_COMPILER_LAUNCHER}")
endif()
