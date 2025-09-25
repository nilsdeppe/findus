// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstddef>
#include <cstdint>

namespace rts::serialize {
/*!
 * \brief Represents a non-owning view of a contiguous block of memory
 *        for serialization.
 *
 * The Bytes struct holds a pointer to a block of memory, the size of each
 * item in bytes, and the number of items in the block. It is used to
 * describe buffers for serialization and deserialization of both
 * fundamental types and complex types (such as classes).
 *
 * \note
 * The memory pointed to by item_ is not owned by Bytes and must remain
 * valid for the duration of any serialization operation. The struct does
 * not manage the lifetime of the memory.
 */
struct Bytes {
  /// Pointer to the first byte of the memory block.
  std::byte* item_;
  /// Size in bytes of each item in the block.
  uint8_t size_of_item_;
  /// Number of items in the block.
  int number_of_items_;
};
}  // namespace rts::serialize
