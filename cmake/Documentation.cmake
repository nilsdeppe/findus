# Copyright, Nils Deppe, 2022-
# Distributed under the MIT License.
# See LICENSE.txt for details.

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

find_package(Doxygen QUIET)
if (DOXYGEN_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    doxygen-awesome-css
    URL https://github.com/jothepro/doxygen-awesome-css/archive/refs/heads/main.zip
  )
  FetchContent_MakeAvailable(doxygen-awesome-css)

  # Save the location the files were cloned into
  # This allows us to get the path to doxygen-awesome.css
  FetchContent_GetProperties(doxygen-awesome-css SOURCE_DIR AWESOME_CSS_DIR)

  configure_file(
    docs/Doxyfile.in
    ${PROJECT_BINARY_DIR}/docs/DoxyfileHtml @ONLY IMMEDIATE
  )

  # Construct the command that calls Doxygen
  set(SPECTRE_DOX_GENERATE_HTML "YES")
  set(SPECTRE_DOX_GENERATE_XML "NO")
  set(
    GENERATE_DOCS_COMMAND
    "${DOXYGEN_EXECUTABLE} ${PROJECT_BINARY_DIR}/docs/DoxyfileHtml"
  )

  # Make sure the Doxygen version is compatible with the CSS, or print a
  # warning. Notes:
  # - We use https://github.com/jothepro/doxygen-awesome-css release v2.2.0,
  #   which ensures compatibility with Doxygen v1.9.1 - v1.9.4 and v1.9.6.
  #   When upgrading Doxygen, it's probably also a good idea to upgrade the CSS
  #   files in `docs/config/`.
  # - The Doxygen release v1.9.1 has a bug so namespaces don't show up in
  #   groups. It is fixed in v1.9.2.
  # - The Doxygen release v1.9.2 breaks the ordering of pages in the tree view
  #   (sidebar). It is fixed in v1.9.3.
  if(DOXYGEN_VERSION VERSION_LESS 1.9.3)
    set(_DOX_WARNING "Your Doxygen version ${DOXYGEN_VERSION} may not be \
 compatible with the stylesheet, so the documentation may look odd or not \
 function correctly. Use Doxygen version 1.9.3 or higher.")
    message(STATUS ${_DOX_WARNING})
    # The 'warning' in this message will fail the `doc-check` target (see below)
    set(
      GENERATE_DOCS_COMMAND
      "${GENERATE_DOCS_COMMAND} && echo 'WARNING: ${_DOX_WARNING}'"
    )
  endif()

  # Parse the command into a CMake list for the `add_custom_target`
  separate_arguments(GENERATE_DOCS_COMMAND)

  add_custom_target(
    doc
    COMMAND ${GENERATE_DOCS_COMMAND}
    DEPENDS
    ${PROJECT_BINARY_DIR}/docs/DoxyfileHtml
    ${SPECTRE_DOXYGEN_GROUPS}
  )

else(DOXYGEN_FOUND)
  message(WARNING "Doxygen is needed to build the documentation.")
endif(DOXYGEN_FOUND)
