// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Exception.hpp"

#include <stdexcept>
#include <string>

namespace rts {
Exception::Exception(const std::string& message)
    : std::runtime_error(message) {}
}  // namespace rts
