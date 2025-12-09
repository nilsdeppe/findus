#pragma once

#include <cstdint>
#include <iosfwd>

namespace findus {
/*!
 * \brief Indicates whether a message should be requeued after invocation.
 *
 * This enum is used to communicate the scheduling decision for a message
 * once an action has been invoked.
 */
enum class MessageRequeue : std::uint8_t {
  /// The requeue decision has not been set.
  Uninitialized = 0,
  /// The message was invoked and does not need to be requeued.
  Invoked,
  /// The message should be placed back in the queue for later execution.
  Requeue
};

/// \brief Stream operator for MessageRequeue.
std::ostream& operator<<(std::ostream& os, MessageRequeue message_requeue);
}  // namespace findus
