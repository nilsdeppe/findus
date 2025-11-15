// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Serialize/Bytes.hpp"

#if defined(RTS_ENABLE_TESTING)

#include <array>
#include <cstddef>
#include <doctest/doctest.h>

TEST_CASE("Serialize.Bytes") {
  std::array<int, 4> data{1, 2, 3, 4};
  rts::serialize::Bytes bytes;
  bytes.item_ = reinterpret_cast<std::byte*>(data.data());
  bytes.size_of_item_ = sizeof(int);
  bytes.number_of_items_ = 4;

  CHECK(bytes.item_ == reinterpret_cast<std::byte*>(data.data()));
  CHECK(bytes.size_of_item_ == sizeof(int));
  CHECK(bytes.number_of_items_ == 4);

  // Access the underlying data through the bytes view
  int* as_int = reinterpret_cast<int*>(bytes.item_);
  CHECK(as_int[0] == 1);
  CHECK(as_int[3] == 4);

  // Example with a complex type
  struct ComplexType {
    int a;
    double b;
  };
  std::array<ComplexType, 2> complex_data{{{5, 6.7}, {8, 9.1}}};
  rts::serialize::Bytes complex_bytes;
  complex_bytes.item_ = reinterpret_cast<std::byte*>(complex_data.data());
  complex_bytes.size_of_item_ = sizeof(ComplexType);
  complex_bytes.number_of_items_ = 2;

  CHECK(complex_bytes.size_of_item_ == sizeof(ComplexType));
  CHECK(complex_bytes.number_of_items_ == 2);
  ComplexType* as_complex =
      std::launder(reinterpret_cast<ComplexType*>(complex_bytes.item_));
  CHECK(as_complex[0].a == 5);
  // The value better be bitwise identical.
  CHECK(as_complex[1].b == 9.1);
}
#endif
