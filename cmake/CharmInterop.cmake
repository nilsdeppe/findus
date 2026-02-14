# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

option(FINDUS_MIMIC_CHARM_PUPER
  "Add enables support for PUP::er as the serialization class."
  OFF)

if (FINDUS_MIMIC_CHARM_PUPER)
  set_property(
    TARGET findusFlags
    APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
    FINDUS_MIMIC_CHARM_PUPER)
endif()

option(FINDUS_CREATE_CHARM_HEADERS
  "Create pup.h and pup_stl.h for Charm++ migration" OFF)

if (FINDUS_CREATE_CHARM_HEADERS)
  if (NOT FINDUS_MIMIC_CHARM_PUPER)
    message(WARNING "You requested Charm++ header mocking but no code mocking. \
Did you forget to set -DFINDUS_MIMIC_CHARM_PUPER=ON?")
  endif()

  if (FINDUS_ENABLE_INSTALL)
    install(FILES
      ${PROJECT_SOURCE_DIR}/charm_compatibility/charm++.h
      ${PROJECT_SOURCE_DIR}/charm_compatibility/charm.h
      ${PROJECT_SOURCE_DIR}/charm_compatibility/ckarrayindex.h
      ${PROJECT_SOURCE_DIR}/charm_compatibility/pup.h
      ${PROJECT_SOURCE_DIR}/charm_compatibility/pup_stl.h
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
  endif()
endif()
