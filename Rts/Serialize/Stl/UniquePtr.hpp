// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <memory>
#include <type_traits>

#include "Rts/Serialize/Serializer.hpp"
#include "Rts/Serialize/Virtual.hpp"

namespace rts::serialize {
/*!
 * \brief Serializes or deserializes a std::unique_ptr using the Serializer.
 *
 * If the pointed-to type T is a polymorphic type (i.e., derives from
 * SerializableBase<T>), this function uses the virtual serialization
 * mechanism to correctly serialize and reconstruct the most-derived type.
 * Otherwise, it serializes or deserializes the object pointed to by the
 * unique_ptr directly.
 *
 * During unpacking, a new object is allocated and assigned to the unique_ptr.
 * During packing, the object currently owned by the unique_ptr is serialized.
 *
 * \tparam T The type of the object managed by the unique_ptr.
 * \param s The Serializer instance.
 * \param unique_ptr The unique_ptr to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class T>
std::enable_if_t<is_serializable_v<T>, Serializer&> operator|(
    Serializer& s, std::unique_ptr<T>& unique_ptr) {
  bool is_nullptr = false;
  if (not s.isUnpacking()) {
    is_nullptr = (unique_ptr == nullptr);
  }
  s | is_nullptr;
  if (is_nullptr) {
    unique_ptr = nullptr;
    return s;
  }
  if constexpr (std::is_base_of_v<SerializableBase<T>, T>) {
    if (s.isUnpacking()) {
      unique_ptr.reset(deserialize_abstract_base<T>(s));
    } else {
      serialize_abstract_base(s, unique_ptr.get());
    }
  } else {
    if (s.isUnpacking()) {
      if constexpr (is_serializer_constructible_v<T>) {
        unique_ptr.reset(new T{s});
      } else {
        unique_ptr.reset(new T{});
        s | *unique_ptr;
      }
    } else {
      s | *unique_ptr;
    }
  }
  return s;
}
}  // namespace rts::serialize
