// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#if defined(RTS_ENABLE_TESTING)

#include "Rts/Serialize/View.hpp"

#include <array>
#include <doctest/doctest.h>

TEST_CASE("Serialize.View") {
  // Test with fundamental type (int)
  std::array<int, 3> data{{10, 20, 30}};

  // Construct with pointer and int
  rts::serialize::View<int> view1(data.data(), 3);
  CHECK(view1.item_ == data.data());
  CHECK(view1.number_of_items_ == 3);
  CHECK(view1.item_[0] == 10);
  CHECK(view1.item_[2] == 30);

  // Construct with pointer and size_t
  rts::serialize::View<int> view2(data.data(), static_cast<size_t>(3));
  CHECK(view2.item_ == data.data());
  CHECK(view2.number_of_items_ == 3);

  // Construct with reference to single item
  rts::serialize::View<int> view3(data[1]);
  CHECK(view3.item_ == &data[1]);
  CHECK(view3.number_of_items_ == 1);
  CHECK(view3.item_[0] == 20);

  // Test with a complex type
  struct ComplexType {
    int a;
    double b;
  };
  std::array<ComplexType, 2> complex_data{{{1, 2.5}, {3, 4.5}}};

  rts::serialize::View<ComplexType> complex_view(complex_data.data(), 2);
  CHECK(complex_view.item_ == complex_data.data());
  CHECK(complex_view.number_of_items_ == 2);
  CHECK(complex_view.item_[0].a == 1);
  CHECK(complex_view.item_[1].b == 4.5);

  rts::serialize::View<ComplexType> single_complex_view(complex_data[0]);
  CHECK(single_complex_view.item_ == &complex_data[0]);
  CHECK(single_complex_view.number_of_items_ == 1);
  CHECK(single_complex_view.item_[0].b == 2.5);
}
#endif
