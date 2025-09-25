// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Serialize/Action.hpp"

#include <ostream>

namespace rts::serialize {
std::ostream& operator<<(std::ostream& os, rts::serialize::Action action) {
  using rts::serialize::Action;
  switch (action) {
    case Action::Uninitialized:
      os << "Uninitialized";
      break;
    case Action::Sizing:
      os << "Sizing";
      break;
    case Action::Packing:
      os << "Packing";
      break;
    case Action::Unpacking:
      os << "Unpacking";
      break;
    case Action::MemoryFootprinting:
      os << "MemoryFootprinting";
      break;
    case Action::Mask:
      os << "Mask";
      break;
    default:
      os << "Unknown";
      break;
  }
  return os;
}
}  // namespace rts::serialize

#if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <sstream>

TEST_CASE("Serialize.Action") {
  using rts::serialize::Action;
  std::ostringstream os;

  os.str("");
  os << Action::Uninitialized;
  CHECK(os.str() == "Uninitialized");

  os.str("");
  os << Action::Sizing;
  CHECK(os.str() == "Sizing");

  os.str("");
  os << Action::Packing;
  CHECK(os.str() == "Packing");

  os.str("");
  os << Action::Unpacking;
  CHECK(os.str() == "Unpacking");

  os.str("");
  os << Action::MemoryFootprinting;
  CHECK(os.str() == "MemoryFootprinting");

  os.str("");
  os << Action::Mask;
  CHECK(os.str() == "Mask");

  // Test unknown value
  os.str("");
  os << static_cast<Action>(0xff);
  CHECK(os.str() == "Unknown");
}
#endif
