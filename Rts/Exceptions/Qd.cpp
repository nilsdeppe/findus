// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Exceptions/Qd.hpp"

#include <exception>
#include <string>

namespace findus {
QdException::QdException(const std::string& message) : Exception(message) {}

static_assert(std::is_base_of_v<Exception, QdException>);
static_assert(std::is_base_of_v<std::runtime_error, QdException>);
}  // namespace findus

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <string>
#include <type_traits>

namespace findus {
TEST_CASE("QdException") {
  try {
    throw QdException("Throwing test exception -0.");
  } catch (const QdException& e) {
    CHECK(std::string{e.what()} == "Throwing test exception -0.");
  }
  try {
    throw QdException("Throwing test exception 0.");
  } catch (const Exception& e) {
    CHECK(std::string{e.what()} == "Throwing test exception 0.");
  }
  try {
    throw QdException("Throwing test exception 1.");
  } catch (const std::runtime_error& e) {
    CHECK(std::string{e.what()} == "Throwing test exception 1.");
  }
  try {
    throw QdException("Throwing test exception 2.");
  } catch (const std::exception& e) {
    CHECK(std::string{e.what()} == "Throwing test exception 2.");
  }
}
}  // namespace findus
#endif
