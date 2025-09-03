
// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/MessageType.hpp"

#include <ostream>

namespace rts {
std::ostream& operator<<(std::ostream& os, MessageType t) {
  switch (t) {
    case MessageType::Uninitialized:
      return os << "Uninitialized";
    case MessageType::Invoke:
      return os << "Invoke";
    case MessageType::Broadcast:
      return os << "Broadcast";
    case MessageType::BroadcastTo:
      return os << "BroadcastTo";
    case MessageType::Reduction:
      return os << "Reduction";
    case MessageType::ReductionOver:
      return os << "ReductionOver";
    default:
      return os << "Unknown";
  }
}
}  // namespace rts

#if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>

#include "Rts/Detail/GetOutput.hpp"

namespace rts {
TEST_CASE("MessageType") {
  CHECK("Uninitialized" == detail::get_output(MessageType::Uninitialized));
  CHECK("Invoke" == detail::get_output(MessageType::Invoke));
  CHECK("Broadcast" == detail::get_output(MessageType::Broadcast));
  CHECK("BroadcastTo" == detail::get_output(MessageType::BroadcastTo));
  CHECK("Reduction" == detail::get_output(MessageType::Reduction));
  CHECK("ReductionOver" == detail::get_output(MessageType::ReductionOver));
  CHECK("Unknown" == detail::get_output(static_cast<MessageType>(0b111)));
}
}  // namespace rts
#endif
