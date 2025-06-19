# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

find_path(
  hwloc_INCLUDE_DIRS
  PATH_SUFFIXES include
  NAMES hwloc.h
  HINTS ${hwloc_DIR} ${hwloc_ROOT} $ENV{hwloc_DIR} $ENV{hwloc_ROOT}
  DOC "hwloc include directory. Use hwloc_ROOT or hwloc_DIR to set search dir"
)

find_library(
  hwloc_LIBRARIES
  NAMES hwloc.a hwloc libhwloc
  HINTS ${hwloc_DIR} ${hwloc_ROOT} $ENV{hwloc_DIR} $ENV{hwloc_ROOT}
  PATH_SUFFIXES lib lib64
  DOC "hwloc library. Use hwloc_ROOT or hwloc_DIR to set search dir"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  hwloc
  FOUND_VAR hwloc_FOUND
  REQUIRED_VARS hwloc_LIBRARIES hwloc_INCLUDE_DIRS
)
mark_as_advanced(
  hwloc_LIBRARIES hwloc_INCLUDE_DIRS
)

if (NOT hwloc_FOUND)
  return()
endif()

add_library(hwloc::hwloc INTERFACE IMPORTED)
set_property(TARGET hwloc::hwloc
  PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${hwloc_INCLUDE_DIRS})
set_property(TARGET hwloc::hwloc
  PROPERTY INTERFACE_LINK_LIBRARIES ${hwloc_LIBRARIES})
