// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <stdexcept>
#include <string>

namespace rts {
/// Exception indicating a ToyRTS error occurred.
class Exception : public std::runtime_error {
 public:
  explicit Exception(const std::string& message);
};
}  // namespace rts
