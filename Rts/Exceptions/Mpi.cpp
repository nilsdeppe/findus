// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Exceptions/Mpi.hpp"

#include <exception>
#include <string>

namespace rts {
MpiException::MpiException(const std::string& message) : Exception(message) {}

static_assert(std::is_base_of_v<Exception, MpiException>);
static_assert(std::is_base_of_v<std::runtime_error, MpiException>);
}  // namespace rts

#if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <string>
#include <type_traits>

namespace rts {
TEST_CASE("MpiException") {
  try {
    throw MpiException("Throwing test exception -0.");
  } catch (const MpiException& e) {
    CHECK(std::string{e.what()} == "Throwing test exception -0.");
  }
  try {
    throw MpiException("Throwing test exception 0.");
  } catch (const Exception& e) {
    CHECK(std::string{e.what()} == "Throwing test exception 0.");
  }
  try {
    throw MpiException("Throwing test exception 1.");
  } catch (const std::runtime_error& e) {
    CHECK(std::string{e.what()} == "Throwing test exception 1.");
  }
  try {
    throw MpiException("Throwing test exception 2.");
  } catch (const std::exception& e) {
    CHECK(std::string{e.what()} == "Throwing test exception 2.");
  }
}
}  // namespace rts
#endif
