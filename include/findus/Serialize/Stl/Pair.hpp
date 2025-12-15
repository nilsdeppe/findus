// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "findus/Serialize/Serializer.hpp"

namespace findus::serialize {
template <class T, class U>
struct as_bytes<std::pair<T, U>>
    : std::bool_constant<serialize_as_bytes_v<T> and serialize_as_bytes_v<U>> {
};

/// @{
/*!
 * \brief Serializes or deserializes a std::pair using the Serializer.
 *
 * If both element types T and U are satisfy serialize_as_bytes_v, the pair is
 * serialized as a raw block of bytes. Otherwise, each element is serialized
 * individually using operator|.
 *
 * During unpacking, both elements of the pair are deserialized accordingly.
 *
 * \note An overload for `std::pair<const T, U>` is provided to make
 * serialization of associative containers easier.
 *
 * \tparam T The type of the first element in the pair.
 * \tparam U The type of the second element in the pair.
 * \param serializer The Serializer instance.
 * \param pair The pair to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class T, class U>
std::enable_if_t<is_serializable_v<T> and is_serializable_v<U>, Serializer&>
operator|(Serializer& serializer, std::pair<T, U>& pair) {
  if constexpr (serialize_as_bytes_v<std::pair<T, U>>) {
    serializer(
        View{reinterpret_cast<std::byte*>(std::addressof(pair)), sizeof(pair)});
  } else {
    (serializer | pair.first) | pair.second;
  }
  return serializer;
}

template <class T, class U>
std::enable_if_t<is_serializable_v<T> and is_serializable_v<U>, Serializer&>
operator|(Serializer& serializer, std::pair<const T, U>& pair) {
  if constexpr (serialize_as_bytes_v<T> and serialize_as_bytes_v<U>) {
    serializer(
        View{reinterpret_cast<std::byte*>(std::addressof(pair)), sizeof(pair)});
  } else {
    serializer | const_cast<T&>(pair.first) | pair.second;
  }
  return serializer;
}
/// @}
}  // namespace findus::serialize

#ifdef FINDUS_MIMIC_CHARM_PUPER
namespace PUP {
template <class T, class U>
std::enable_if_t<findus::serialize::is_serializable_v<T> and
                 findus::serialize::is_serializable_v<U>>
operator|(PUP::er& p, std::pair<T, U>& pair) {
  findus::serialize::operator|(static_cast<findus::serialize::Serializer&>(p),
                               pair);
}

template <class T, class U>
std::enable_if_t<findus::serialize::is_serializable_v<T> and
                 findus::serialize::is_serializable_v<U>>
operator|(PUP::er& p, std::pair<const T, U>& pair) {
  findus::serialize::operator|(static_cast<findus::serialize::Serializer&>(p),
                               pair);
}
}  // namespace PUP
#endif
