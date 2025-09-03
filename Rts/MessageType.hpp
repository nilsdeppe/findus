// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>
#include <iosfwd>

namespace rts {
/*!
 * \brief The type of message being sent.
 *
 * The message types can be up to 3 bits in size allowing for 8 different
 * message types.
 */
enum class MessageType : std::uint8_t {
  /// \brief Used to check if this enum was not initialized.
  Uninitialized = 0b000,
  /// \brief A point-to-point threaded action invocation.
  Invoke = 0b001,
  /// \brief A broadcast to all elements of a collection.
  Broadcast = 0b010,
  /// \brief A broadcast to a subset of elements of a collection.
  BroadcastTo = 0b011,
  /// \brief A reduction over all elements of a collection.
  Reduction = 0b100,
  /// \brief A reduction over a subset of elements of a collection.
  ReductionOver = 0b101
};

/// \brief Stream operator for `rts::MessageType`.
std::ostream& operator<<(std::ostream& os, MessageType t);
}  // namespace rts
