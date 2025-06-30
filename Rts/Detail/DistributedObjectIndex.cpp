// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Detail/DistributedObjectIndex.hpp"

#include <ostream>
#include <string>

namespace rts::detail {
std::ostream& operator<<(std::ostream& os, const DistributedObjectIndex index) {
  switch (index) {
    case Regular:
      return os << "Regular";
    case Collection:
      return os << "Collection";
    case Singleton:
      return os << "Singleton";
    default:
      return os << "Unknown";
  }
}
}  // namespace rts::detail

#if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>

#include "Rts/Detail/DistributedObjectIndex.hpp"
#include "Rts/Detail/GetOutput.hpp"

namespace rts::detail {
TEST_CASE("DistributedObjectIndex stream operator") {
  struct {
    DistributedObjectIndex value;
    std::string expected;
  } cases[] = {{DistributedObjectIndex::Regular, "Regular"},
               {DistributedObjectIndex::Collection, "Collection"},
               {DistributedObjectIndex::Singleton, "Singleton"},
               {static_cast<DistributedObjectIndex>(42), "Unknown"}};

  for (const auto& c : cases) {
    CHECK(get_output(c.value) == c.expected);
  }
}
}  // namespace rts::detail
#endif
