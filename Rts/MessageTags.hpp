// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

namespace rts {
/// \brief The MPI tags for different types of messages sent across the system.
///
/// The numbers are chosen somewhat arbitrarily but to avoid 0 to reduce
/// collision with other libraries.
namespace message_tags {
/// \brief The tag for a regular message between different distributed objects.
constexpr int regular = 1024;
/// \brief The tag for a message used to send the debugger PID info at startup.
constexpr int debugger_attach = 1025;
/// \brief The tag for a quiescence detection message.
constexpr int quiescence = 1026;
/// \brief The tag for logging and printing messages.
constexpr int logging = 1027;
}  // namespace message_tags
}  // namespace rts
