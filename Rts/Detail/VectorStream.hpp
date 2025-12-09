// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <ostream>
#include <vector>

namespace findus::detail {
/// \brief Internally used stream operator for std::vector.
///
/// Intentionally not in the global namespace and should be used explicitly in
/// order to minimize collision with user code.
template <typename T, typename A>
std::ostream& operator<<(std::ostream& os, const std::vector<T, A>& v) {
  os << "[";
  for (size_t i = 0; i < v.size(); ++i) {
    os << v[i];
    if (i != v.size() - 1)
      os << ", ";
  }
  os << "]";
  return os;
}
}  // namespace findus::detail
