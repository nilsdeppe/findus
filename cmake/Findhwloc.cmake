# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

# Findhwloc.cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(hwloc REQUIRED IMPORTED_TARGET hwloc)

# Create an alias so consumers can use hwloc::hwloc
if(TARGET PkgConfig::hwloc AND NOT TARGET hwloc::hwloc)
    add_library(hwloc::hwloc ALIAS PkgConfig::hwloc)
endif()
