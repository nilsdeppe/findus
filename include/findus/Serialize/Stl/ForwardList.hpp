// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <forward_list>

#include "findus/Serialize/Serializer.hpp"

namespace findus::serialize {
/*!
 * \brief Serializes or deserializes a std::forward_list using the Serializer.
 *
 * Serializes the forward_list's capacity and size, then serializes the
 * elements. If the element type T is marked as as_bytes or inherits from
 * as_bytes<void>, the elements are serialized as a raw block of bytes.
 * Otherwise, each element is serialized individually using operator|.
 *
 * During unpacking, the forward_list's capacity and size are restored, and the
 * elements are deserialized accordingly.
 *
 * \tparam T The type of elements in the forward_list.
 * \tparam A The allocator type for the forward_list.
 * \param serializer The Serializer instance.
 * \param forward_list The forward_list to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class T, class A>
std::enable_if_t<is_serializable_v<T>, Serializer&> operator|(
    Serializer& serializer, std::forward_list<T, A>& forward_list) {
  if (serializer.isUnpacking()) {
    size_t size = 0;
    serializer | size;
    auto iter = forward_list.before_begin();
    for (size_t i = 0; i < size; ++i) {
      T t{};
      serializer | t;
      iter = forward_list.insert_after(iter, std::move(t));
    }
  } else {
    size_t size = 0;
    for (T& t : forward_list) {
      (void)t;
      ++size;
    }
    serializer | size;
    for (T& t : forward_list) {
      serializer | t;
    }
  }
  return serializer;
}
}  // namespace findus::serialize

#ifdef FINDUS_MIMIC_CHARM_PUPER
namespace PUP {
template <class T, class A>
std::enable_if_t<findus::serialize::is_serializable_v<T>> operator|(
    er& p, std::forward_list<T, A>& forward_list) {
  findus::serialize::operator|(static_cast<findus::serialize::Serializer&>(p),
                               forward_list);
}
}  // namespace PUP
#endif
