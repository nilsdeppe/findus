# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

option(FINDUS_MIMIC_CHARM_PUPER
  "Add enables support for PUP::er as the serialization class."
  ON)

if (FINDUS_MIMIC_CHARM_PUPER)
  set_property(
    TARGET FindusFlags
    APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
    FINDUS_MIMIC_CHARM_PUPER)
endif()
