# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

add_library(FindusSanitizers IMPORTED INTERFACE)

option(SANITIZER "Add sanitizer flags with value, e.g. address, undefined, etc."
  OFF)
if (SANITIZER)
  # Known good values are:
  set(_SANITIZER_SUPPORTED_LIST "address" "alignment" "integer" "memory"
    "nullability" "pointer-overflow" "thread" "type" "undefined")
  list(FIND _SANITIZER_SUPPORTED_LIST "${SANITIZER}" SANITIZER_INDEX)
  if(SANITIZER_INDEX EQUAL -1)
    message(WARNING
      "The value ${SANITIZER} is not in the known supported sanitizers: "
      "${_SANITIZER_SUPPORTED_LIST}. You may receive a compiler error if the "
      "sanitizer is not supported. To disable this message, add the sanitizer "
      "to the variable SANITIZER_SUPPORTED_LIST"
    )
  endif()

  set_property(
    TARGET FindusSanitizers
    APPEND PROPERTY
    INTERFACE_COMPILE_OPTIONS
    $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer -fsanitize=${SANITIZER}>
    $<$<COMPILE_LANGUAGE:C>:-fno-omit-frame-pointer -fsanitize=${SANITIZER}>
  )
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=${SANITIZER}")
endif()

target_link_libraries(
  FindusFlags
  INTERFACE
  FindusSanitizers
)
