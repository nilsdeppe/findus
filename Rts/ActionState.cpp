// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/ActionState.hpp"

#include <ostream>
#include <stdexcept>
#include <type_traits>

namespace rts {
std::ostream& operator<<(std::ostream& os, const ActionState action_state) {
  switch (action_state) {
    case ActionState::Uninitialized:
      return os << "Uninitialized";
    case ActionState::Success:
      return os << "Success";
    case ActionState::Resubmit:
      return os << "Resubmit";
    case ActionState::End:
      return os << "End";
    default:
      return os << "unknown_action_state("
                << static_cast<std::uint32_t>(action_state) << ")";
  }
}
}  // namespace rts

#if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <string>

#include "Rts/Detail/GetOutput.hpp"

namespace rts {
TEST_CASE("ActionState") {
  CHECK(detail::get_output(ActionState::Uninitialized) == "Uninitialized");
  CHECK(detail::get_output(ActionState::Success) == "Success");
  CHECK(detail::get_output(ActionState::Resubmit) == "Resubmit");
  CHECK(detail::get_output(ActionState::End) == "End");
  CHECK(detail::get_output(static_cast<ActionState>(128)) ==
        "unknown_action_state(128)");
}
}  // namespace rts
#endif
