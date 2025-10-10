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
 * \brief Serializes or deserializes a std::shared_ptr using the Serializer.
 *
 * If the pointed-to type T is a polymorphic type (i.e., derives from
 * SerializableBase<T>), this function uses the virtual serialization
 * mechanism to correctly serialize and reconstruct the most-derived type.
 * Otherwise, it serializes or deserializes the object pointed to by the
 * shared_ptr directly.
 *
 * During unpacking, a new object is allocated and assigned to the shared_ptr.
 * During packing, the object currently owned by the shared_ptr is serialized.
 *
 * \tparam T The type of the object managed by the shared_ptr.
 * \param s The Serializer instance.
 * \param shared_ptr The shared_ptr to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class T>
std::enable_if_t<is_serializable_v<T>, Serializer&> operator|(
    Serializer& s, std::shared_ptr<T>& shared_ptr) {
  bool is_nullptr = false;
  if (not s.isUnpacking()) {
    is_nullptr = (shared_ptr == nullptr);
  }
  s | is_nullptr;
  if (is_nullptr) {
    shared_ptr = nullptr;
    return s;
  }
  if constexpr (std::is_base_of_v<SerializableBase<T>, T>) {
    if (s.isUnpacking()) {
      shared_ptr.reset(deserialize_abstract_base<T>(s));
    } else {
      serialize_abstract_base(s, shared_ptr.get());
    }
  } else {
    if (s.isUnpacking()) {
      if constexpr (is_serializer_constructible_v<T>) {
        shared_ptr.reset(new T{s});
      } else {
        shared_ptr.reset(new T{});
        s | *shared_ptr;
      }
    } else {
      s | *shared_ptr;
    }
  }
  return s;
}
}  // namespace rts::serialize
