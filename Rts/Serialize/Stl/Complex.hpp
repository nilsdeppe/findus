// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <complex>

#include "Rts/Serialize/Serializer.hpp"

namespace rts::serialize {
template <class T>
struct as_bytes<std::complex<T>> : std::bool_constant<serialize_as_bytes_v<T>> {
};

/*!
 * \brief Serializes or deserializes a std::complex<T> using the Serializer.
 *
 * If the underlying type T satisfies serialize_as_bytes_v, the complex number
 * is serialized as a raw block of bytes. Otherwise, the real and imaginary
 * parts are serialized individually using operator|.
 *
 * During unpacking, both the real and imaginary parts are deserialized and
 * assigned to the complex number.
 *
 * \tparam T The type of the real and imaginary parts.
 * \param serializer The Serializer instance.
 * \param complex The complex number to serialize or deserialize.
 * \return Reference to the Serializer.
 */
template <class T>
std::enable_if_t<is_serializable_v<T>, Serializer&> operator|(
    Serializer& serializer, std::complex<T>& complex) {
  if constexpr (serialize_as_bytes_v<std::complex<T>>) {
    serializer(View{reinterpret_cast<std::byte*>(std::addressof(complex)),
                    sizeof(std::complex<T>)});
  } else {
    if constexpr (is_serializer_constructible_v<T>) {
      if (serializer.isUnpacking()) {
        complex = {T{serializer}, T{serializer}};
        return serializer;
      }
    }
    T real = complex.real();
    T imag = complex.imag();
    serializer | real | imag;
    if (serializer.isUnpacking()) {
      complex = {std::move(real), std::move(imag)};
    } else {
    }
  }
  return serializer;
}
}  // namespace rts::serialize
