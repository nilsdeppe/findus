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
constexpr int regular_message = 1024;
/// \brief The tag for a quiescence detection message.
constexpr int quiescence_message = 1025;
}  // namespace message_tags
}  // namespace rts
