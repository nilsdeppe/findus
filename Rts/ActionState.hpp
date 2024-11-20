// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>
#include <iosfwd>

namespace rts {
/*!
 * \brief Represent the result of an action invocation.
 *
 * This is used to communicate back to the RTS if a message should be
 * resubmitted to the thread pool.
 */
enum class ActionState : uint16_t {
  /// A default state to catch uninitialized states.
  Uninitialized = 0,
  /// The action was invoked successfully.
  Success = 1,
  /// The action was unable to run and the message should be resubmitted to
  /// the thread pool.
  Resubmit = 2,

  /// The last state in the enum, used to demarcate the end.
  End = 3
};

/// \brief Stream operator for `rts::ActionState`
std::ostream& operator<<(std::ostream& os, const ActionState& action_state);
}  // namespace rts
