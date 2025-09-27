// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <deque>

#include "Rts/Serialize/Serializer.hpp"

namespace rts::serialize {
/*!
 * \brief Serializes or deserializes a std::deque using the Serializer.
 *
 * Each element is serialized individually using operator|.
 *
 * During unpacking, the deque's elements are deserialized accordingly.
 *
 * \tparam T The type of elements in the array.
 * \tparam A The allocator type for the vector.
 * \param serializer The Serializer instance.
 * \param deque The deque to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class T, class A>
std::enable_if_t<is_serializable_v<T>, Serializer&> operator|(
    Serializer& serializer, std::deque<T, A>& deque) {
  size_t size = deque.size();
  serializer | size;
  if (serializer.isUnpacking()) {
    deque.resize(size);
  }
  for (T& t : deque) {
    serializer | t;
  }
  return serializer;
}
}  // namespace rts::serialize
