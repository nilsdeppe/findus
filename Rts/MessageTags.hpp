// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <iosfwd>

namespace rts {
/// \brief The MPI tags for different types of messages sent across the system.
///
/// The default offset is `1024`, chosen somewhat arbitrarily but to avoid 0
/// to reduce collision with other libraries. This can be overridden by the
/// macro `RTS_MESSAGE_OFFSET`.
enum message_tags : int {
  /// \brief The tag for a regular message between different distributed
  /// objects.
  regular =
#if defined(RTS_MESSAGE_OFFSET)
      RTS_MESSAGE_OFFSET
#else
      1024
#endif
  ,
  /// \brief The tag for a message used to send the debugger PID info at
  /// startup.
  debugger_attach,
  /// \brief The tag for a quiescence detection message.
  quiescence,
  /// \brief The tag for logging and printing messages.
  logging
};

std::ostream& operator<<(std::ostream& os, message_tags tag);
}  // namespace rts
