// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <type_traits>
#include <vector>

#include "findus/Serialize/Serializer.hpp"

namespace findus::serialize {
/// @{
/*!
 * \brief Serializes or deserializes a std::vector using the Serializer.
 *
 * Serializes the vector's capacity and size, then serializes the elements.
 * If the element type T is marked as as_bytes or inherits from as_bytes<void>,
 * the elements are serialized as a raw block of bytes. Otherwise, each element
 * is serialized individually using operator|.
 *
 * During unpacking, the vector's capacity and size are restored, and the
 * elements are deserialized accordingly.
 *
 * \tparam T The type of elements in the vector.
 * \tparam A The allocator type for the vector.
 * \param serializer The Serializer instance.
 * \param vector The vector to serialize or deserialize.
 * \return Reference to the Serializer.
 */
// [serializer_definition]
template <class T, class A>
std::enable_if_t<is_serializable_v<T>, Serializer&> operator|(
    Serializer& serializer, std::vector<T, A>& vector) {
  size_t capacity = vector.capacity();
  size_t size = vector.size();
  serializer | capacity | size;
  if (serializer.isUnpacking()) {
    vector.reserve(capacity);
    if constexpr (is_serializer_constructible_v<T>) {
      for (size_t i = 0; i < size; ++i) {
        vector.emplace_back(serializer);
      }
      return serializer;
    } else {
      vector.resize(size);
    }
  }
  if constexpr (serialize_as_bytes_v<T>) {
    serializer(View{vector.data(), size});
  } else {
    for (T& t : vector) {
      serializer | t;
    }
  }
  return serializer;
}
// [serializer_definition]

// [serializer_bool_definition]
template <class A>
Serializer& operator|(Serializer& serializer, std::vector<bool, A>& vector) {
  size_t capacity = vector.capacity();
  size_t size = vector.size();
  serializer | capacity | size;
  if (serializer.isUnpacking()) {
    vector.reserve(capacity);
    vector.resize(size);
    for (size_t i = 0; i < vector.size(); ++i) {
      bool t;
      serializer | t;
      vector[i] = t;
    }
  } else {
    for (bool t : vector) {
      serializer | t;
    }
  }
  return serializer;
}
// [serializer_bool_definition]
/// @}
}  // namespace findus::serialize

#ifdef FINDUS_MIMIC_CHARM_PUPER
namespace PUP {
template <class T, class A>
std::enable_if_t<findus::serialize::is_serializable_v<T>> operator|(
    er& p, std::vector<T, A>& vector) {
  findus::serialize::operator|(static_cast<findus::serialize::Serializer&>(p),
                               vector);
}

template <class A>
void operator|(er& p, std::vector<bool, A>& vector) {
  findus::serialize::operator|(static_cast<findus::serialize::Serializer&>(p),
                               vector);
}
}  // namespace PUP
#endif
