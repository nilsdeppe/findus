// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Detail/VectorStream.hpp"

#if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <string>
#include <vector>

#include "Rts/Detail/GetOutput.hpp"

namespace rts::detail {
TEST_CASE("StdVectorStream") {
  SUBCASE("empty vector") {
    std::vector<int> v;
    CHECK(get_output(v) == "[]");
  }

  SUBCASE("single element") {
    std::vector<int> v{42};
    CHECK(get_output(v) == "[42]");
  }

  SUBCASE("multiple elements") {
    std::vector<int> v{1, 2, 3, 4};
    CHECK(get_output(v) == "[1, 2, 3, 4]");
  }

  SUBCASE("vector of strings") {
    std::vector<std::string> v{"foo", "bar"};
    CHECK(get_output(v) == "[foo, bar]");
  }

  SUBCASE("vector of doubles") {
    std::vector<double> v{1.5, 2.5};
    CHECK(get_output(v) == "[1.5, 2.5]");
  }
}
}  // namespace rts::detail
#endif
