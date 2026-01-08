# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

option(FINDUS_USE_LIBCXX "Use libc++ instead of libstdc++" OFF)

if(FINDUS_USE_LIBCXX)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "FINDUS_USE_LIBCXX requires Clang compiler")
  endif()

  # Check if libc++ is available
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_FLAGS "-stdlib=libc++")
  check_cxx_source_compiles("
    #include <iostream>
    int main() { std::cout << \"test\"; return 0; }
  " LIBCXX_AVAILABLE)

  if(NOT LIBCXX_AVAILABLE)
    message(FATAL_ERROR "libc++ not found.")
  endif()

  # Apply flags globally
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -stdlib=libc++")
  set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -stdlib=libc++")
endif()
