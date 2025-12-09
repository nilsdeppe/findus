// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <list>

#include "findus/Serialize/Serializer.hpp"

namespace findus::serialize {
/*!
 * \brief Serializes or deserializes a std::list using the Serializer.
 *
 * Serializes the list's capacity and size, then serializes the elements.
 * If the element type T is marked as as_bytes or inherits from as_bytes<void>,
 * the elements are serialized as a raw block of bytes. Otherwise, each element
 * is serialized individually using operator|.
 *
 * During unpacking, the list's capacity and size are restored, and the
 * elements are deserialized accordingly.
 *
 * \tparam T The type of elements in the list.
 * \tparam A The allocator type for the list.
 * \param serializer The Serializer instance.
 * \param list The list to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class T, class A>
std::enable_if_t<is_serializable_v<T>, Serializer&> operator|(
    Serializer& serializer, std::list<T, A>& list) {
  size_t size = list.size();
  serializer | size;
  if (serializer.isUnpacking()) {
    if constexpr (is_serializer_constructible_v<T>) {
      for (size_t i = 0; i < size; ++i) {
        list.emplace_back(serializer);
      }
      return serializer;
    } else {
      list.resize(size);
    }
  }
  for (T& t : list) {
    serializer | t;
  }
  return serializer;
}
}  // namespace findus::serialize
