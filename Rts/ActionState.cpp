// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/ActionState.hpp"

#include <ostream>
#include <stdexcept>

namespace rts {
std::ostream& operator<<(std::ostream& os, const ActionState& action_state) {
  switch (action_state) {
    case ActionState::Uninitialized:
      return os << "Uninitialized";
    case ActionState::Success:
      return os << "Success";
    case ActionState::Resubmit:
      return os << "Resubmit";
    default:
      throw std::runtime_error("Invalid action state");
  }
}
}  // namespace rts
