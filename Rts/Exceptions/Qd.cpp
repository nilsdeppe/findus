// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Exceptions/Qd.hpp"

#include <string>

namespace rts {
QdException::QdException(const std::string& message) : Exception(message) {}
}  // namespace rts
