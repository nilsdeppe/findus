# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

option(RTS_MIMIC_CHARM_PUPER
  "Add -g to CMAKE_CXX_FLAGS if ON, -g0 if OFF."
  ON)

if (RTS_MIMIC_CHARM_PUPER)
  set_property(
    TARGET RtsFlags
    APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
    RTS_MIMIC_CHARM_PUPER)
endif()
