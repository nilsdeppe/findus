// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/MessageTags.hpp"

#include <ostream>
#include <string>
#include <type_traits>

namespace findus {
std::ostream& operator<<(std::ostream& os, const message_tags tag) {
  switch (tag) {
    case message_tags::regular:
      return os << "regular";
    case message_tags::debugger_attach:
      return os << "debugger_attach";
    case message_tags::quiescence_down:
      return os << "quiescence_down";
    case message_tags::quiescence_up:
      return os << "quiescence_up";
    case message_tags::quiescence_broadcast:
      return os << "quiescence_broadcast";
    case message_tags::logging:
      return os << "logging";
    default:
      return os << "unknown_tag("
                << static_cast<std::underlying_type_t<message_tags>>(tag)
                << ")";
  };
}

static_assert(std::is_same_v<std::underlying_type_t<message_tags>, int>);
}  // namespace findus

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <string>

#include "Rts/Detail/GetOutput.hpp"

namespace findus {
TEST_CASE("MessageTags") {
  CHECK(detail::get_output(message_tags::regular) == "regular");
  CHECK(detail::get_output(message_tags::debugger_attach) == "debugger_attach");
  CHECK(detail::get_output(message_tags::quiescence_down) == "quiescence_down");
  CHECK(detail::get_output(message_tags::quiescence_up) == "quiescence_up");
  CHECK(detail::get_output(message_tags::quiescence_broadcast) ==
        "quiescence_broadcast");
  CHECK(detail::get_output(message_tags::logging) == "logging");
  CHECK(detail::get_output(static_cast<message_tags>(-100)) ==
        "unknown_tag(-100)");
}
}  // namespace findus
#endif
