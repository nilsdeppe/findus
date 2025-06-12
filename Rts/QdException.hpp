// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <stdexcept>
#include <string>

#include "Rts/Exception.hpp"

namespace rts {
/// Exception indicating a quiescence detection error occurred.
class QdException : public Exception {
 public:
  explicit QdException(const std::string& message);
};
}  // namespace rts
