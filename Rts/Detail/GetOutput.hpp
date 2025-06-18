// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <sstream>
#include <string>

namespace rts::detail {
template <typename T>
std::string get_output(const T& t) {
  std::ostringstream os;
  os << t;
  return os.str();
}
}  // namespace rts::detail
