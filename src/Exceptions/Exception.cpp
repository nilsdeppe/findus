// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Exceptions/Exception.hpp"

#include <exception>
#include <stdexcept>
#include <string>

namespace findus {
Exception::Exception(const std::string& message)
    : std::runtime_error(message) {}

static_assert(std::is_base_of_v<std::runtime_error, Exception>);
}  // namespace findus

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <string>
#include <type_traits>

namespace findus {
TEST_CASE("Exception") {
  try {
    throw Exception("Throwing test exception 0.");
  } catch (const Exception& e) {
    CHECK(std::string{e.what()} == "Throwing test exception 0.");
  }
  try {
    throw Exception("Throwing test exception 1.");
  } catch (const std::runtime_error& e) {
    CHECK(std::string{e.what()} == "Throwing test exception 1.");
  }
  try {
    throw Exception("Throwing test exception 2.");
  } catch (const std::exception& e) {
    CHECK(std::string{e.what()} == "Throwing test exception 2.");
  }
}
}  // namespace findus
#endif
