# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

option(FINDUS_RUNTIME_ATOMIC128
  "Enable detecting 128-bit atomic at runtime" OFF)

execute_process(
  COMMAND ${CMAKE_CXX_COMPILER}
  -march=native
  -o "${CMAKE_BINARY_DIR}/FindusPrintArch"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PrintArch.cpp"

  RESULT_VARIABLE COMPILE_RESULT
)

execute_process(
  COMMAND "${CMAKE_BINARY_DIR}/FindusPrintArch"
  OUTPUT_VARIABLE _PROBE_OUTPUT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

string(REGEX MATCH "([A-Za-z]+)-([0-9][0-9]) (.*)" _ "${_PROBE_OUTPUT}")

set(FINDUS_DETECTED_ARCHITECTURE ${CMAKE_MATCH_1}-${CMAKE_MATCH_2})

message(STATUS
  "Detected native architecture: ${FINDUS_DETECTED_ARCHITECTURE}")
