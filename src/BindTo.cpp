// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/BindTo.hpp"

#include <ostream>

namespace findus {
std::ostream& operator<<(std::ostream& os, const BindTo bind_to) {
  switch (bind_to) {
    case BindTo::Uninitialized:
      return os << "Uninitialized";
    case BindTo::None:
      return os << "None";
    case BindTo::Core:
      return os << "Core";
    case BindTo::HardwareThread:
      return os << "HardwareThread";
    default:
      return os << "Unknown";
  }
}
}  // namespace findus

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>

#include "findus/Detail/GetOutput.hpp"

namespace findus {
TEST_CASE("BindTo") {
  using findus::detail::get_output;

  CHECK("Uninitialized" == get_output(BindTo::Uninitialized));
  CHECK("None" == get_output(BindTo::None));
  CHECK("Core" == get_output(BindTo::Core));
  CHECK("HardwareThread" == get_output(BindTo::HardwareThread));
  CHECK("Unknown" == get_output(static_cast<BindTo>(0b11111111)));
}
}  // namespace findus
#endif
