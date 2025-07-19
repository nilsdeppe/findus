// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <iosfwd>

namespace rts {
/// \brief The MPI tags for different types of messages sent across the system.
///
/// The default offset is `1024`, chosen somewhat arbitrarily but to avoid 0
/// to reduce collision with other libraries. This can be overridden by passing
/// `-D RTS_MESSAGE_OFFSET=NUMBER` to CMake when building the RTS. This cannot
/// be changed at user application compilation time because RTS internals
/// depend on the underlying values.
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
  /// \brief The tag for a quiescence detection down message.
  quiescence_down,
  /// \brief The tag for a quiescence detection up message.
  quiescence_up,
  /// \brief The tag for the quiescence detection broadcast for termination.
  quiescence_broadcast,
  /// \brief The tag for logging and printing messages.
  logging,
  /// \brief The tag used for checking that insert, remove, and move of
  /// components was consistent across the runtime.
  insert_consistency_check
};

/// \brief Stream operator for `message_tags`.
std::ostream& operator<<(std::ostream& os, message_tags tag);
}  // namespace rts
