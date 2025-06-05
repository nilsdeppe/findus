// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/MessageTags.hpp"

#include <ostream>
#include <string>
#include <type_traits>

namespace rts {
std::ostream& operator<<(std::ostream& os, const message_tags tag) {
  switch (tag) {
    case message_tags::regular:
      return os << "regular";
    case message_tags::debugger_attach:
      return os << "debugger_attach";
    case message_tags::quiescence:
      return os << "quiescence";
    case message_tags::logging:
      return os << "logging";
    default:
      return os << "unknown_tag("
                << static_cast<std::underlying_type_t<message_tags>>(tag)
                << ")";
  };
}
}  // namespace rts
