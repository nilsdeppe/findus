// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/MessageRequeue.hpp"

#include <ostream>

namespace findus {
std::ostream& operator<<(std::ostream& os,
                         const MessageRequeue message_requeue) {
  switch (message_requeue) {
    case MessageRequeue::Uninitialized:
      return os << "Uninitialized";
    case MessageRequeue::Invoked:
      return os << "Invoked";
    case MessageRequeue::Requeue:
      return os << "Requeue";
    default:
      return os << "Unknown";
  }
}
}  // namespace findus

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>

#include "findus/Detail/GetOutput.hpp"

namespace {
TEST_CASE("MessageRequeue") {
  using findus::MessageRequeue;
  using findus::detail::get_output;

  CHECK("Uninitialized" == get_output(MessageRequeue::Uninitialized));
  CHECK("Invoked" == get_output(MessageRequeue::Invoked));
  CHECK("Requeue" == get_output(MessageRequeue::Requeue));
  CHECK("Unknown" == get_output(static_cast<MessageRequeue>(0b111)));
}
}  // namespace
#endif
