// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/MpiException.hpp"

#include <string>

namespace rts {
MpiException::MpiException(const std::string& message) : Exception(message) {}
}  // namespace rts
