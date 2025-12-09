// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <stdexcept>
#include <string>

#include "findus/Exceptions/Exception.hpp"

namespace findus {
/// Exception indicating an MPI error occurred.
class MpiException : public Exception {
 public:
  explicit MpiException(const std::string& message);
};
}  // namespace findus
