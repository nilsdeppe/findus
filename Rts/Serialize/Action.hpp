// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>
#include <iosfwd>

namespace rts::serialize {
/*!
 * \brief Enum representing the type of serialization action to perform.
 *
 * The Action enum specifies the operation mode for the Serializer. It is used
 * to indicate whether the Serializer should compute the size of the data,
 * pack data into a buffer, unpack data from a buffer, or compute the memory
 * footprint of the data. The Mask value can be used for bitwise operations
 * to extract the action type.
 */
enum class Action : std::uint8_t {
  /// The action is not set.
  Uninitialized = 0,
  /// Compute the number of bytes required for serialization.
  Sizing = 0b0000'0001,
  /// Serialize data into a buffer.
  Packing = 0b0000'0010,
  /// Deserialize data from a buffer.
  Unpacking = 0b0000'0011,
  /// Compute the memory footprint of the data.
  MemoryFootprinting = 0b0000'0100,
  /// Bitmask for extracting the action type.
  Mask = 0b0000'1111,
};

/*!
 * \brief Stream insertion operator for rts::serialize::Action.
 *
 * Outputs the name of the Action enum value as a string.
 *
 * \param os The output stream.
 * \param action The Action enum value to output.
 * \return The output stream.
 */
std::ostream& operator<<(std::ostream& os, rts::serialize::Action action);
}  // namespace rts::serialize
