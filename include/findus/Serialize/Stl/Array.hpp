// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <array>

#include "findus/Serialize/Serializer.hpp"

namespace findus::serialize {
template <class T, size_t N>
struct as_bytes<std::array<T, N>>
    : std::bool_constant<serialize_as_bytes_v<T>> {};

/*!
 * \brief Serializes or deserializes a std::array using the Serializer.
 *
 * If the element type T is fundamental, marked as as_bytes, or inherits from
 * as_bytes<void>, the elements are serialized as a raw block of bytes.
 * Otherwise, each element is serialized individually using operator|.
 *
 * During unpacking, the array's elements are deserialized accordingly.
 *
 * \tparam T The type of elements in the array.
 * \tparam N The number of elements in the array.
 * \param serializer The Serializer instance.
 * \param array The array to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class T, size_t N>
std::enable_if_t<is_serializable_v<T>, Serializer&> operator|(
    Serializer& serializer, std::array<T, N>& array) {
  if constexpr (serialize_as_bytes_v<std::array<T, N>>) {
    serializer(View{array.data(), N});
  } else {
    for (T& t : array) {
      serializer | t;
    }
  }
  return serializer;
}
}  // namespace findus::serialize

#ifdef FINDUS_MIMIC_CHARM_PUPER
namespace PUP {
template <class T, size_t N>
std::enable_if_t<findus::serialize::is_serializable_v<T>> operator|(
    er& p, std::array<T, N>& array) {
  findus::serialize::operator|(static_cast<findus::serialize::Serializer&>(p),
                               array);
}
}  // namespace PUP
#endif
