// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Serialize/Stl/Array.hpp"

#if defined(FINDUS_ENABLE_TESTING)

#include <array>
#include <doctest/doctest.h>
#include <memory>
#include <vector>

#include "findus/Serialize/Serializer.hpp"
#include "findus/Serialize/Stl/Vector.hpp"

namespace findus::serialize {
namespace {
struct NoSerialize {};
static_assert(is_serializable_v<std::array<int, 3>>);
static_assert(is_serializable_v<int>);
static_assert(not is_serializable_v<NoSerialize>);
static_assert(not is_serializable_v<std::array<NoSerialize, 3>>);

template <class Cast>
void test_array_of_vector_of_array() {
  struct SimpleStruct {
    int value = 0;
    Serializer& pup(Serializer& s) { return s | value; }
    bool operator==(const SimpleStruct& other) const {
      return value == other.value;
    }
  };

  std::array<std::vector<std::array<SimpleStruct, 3>>, 5> arr;

  // Fill each vector with 3 elements and set capacity
  for (size_t i = 0; i < arr.size(); ++i) {
    arr[i].reserve(10 + i);
    for (int j = 0; j < 3; ++j) {
      std::array<SimpleStruct, 3> inner_arr;
      for (int k = 0; k < 3; ++k) {
        inner_arr[static_cast<size_t>(k)].value =
            static_cast<int>(i) * 100 + j * 10 + k;
      }
      arr[i].push_back(inner_arr);
    }
  }

  // Sizing
  Serializer sizer{Serializer::Sizing};
  static_cast<Cast&>(sizer) | arr;
  CHECK(sizer.number_of_bytes() > 0);

  // Packing
  std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
  Serializer packer{Serializer::Packing, buffer.get(), sizer.number_of_bytes()};
  static_cast<Cast&>(packer) | arr;

  // Unpacking
  std::array<std::vector<std::array<SimpleStruct, 3>>, 5> arr_unpacked;
  Serializer unpacker{Serializer::Unpacking, buffer.get(),
                      sizer.number_of_bytes()};
  static_cast<Cast&>(unpacker) | arr_unpacked;

  // Check equality of contents and vector capacities
  for (size_t i = 0; i < arr.size(); ++i) {
    CHECK(arr[i] == arr_unpacked[i]);
    CHECK(arr_unpacked[i].capacity() == 10 + i);
  }
}

template <class Cast>
void test() {
  // Test with fundamental type
  {
    std::array<int, 5> arr{1, 2, 3, 4, 5};
    static_assert(serialize_as_bytes_v<std::array<int, 5>>);
    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | arr;
    CHECK(sizer.number_of_bytes() == sizeof(int) * arr.size());

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | arr;

    // Unpacking
    std::array<int, 5> arr_unpacked{};
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | arr_unpacked;

    CHECK(arr == arr_unpacked);
  }

  // Test with as_bytes type
  {
    struct MyBytesType : as_bytes<void> {
      int a = 0;
      double b = 0.0;
      MyBytesType() = default;
      MyBytesType(int a_in, double b_in) : a(a_in), b(b_in) {}
      bool operator==(const MyBytesType& other) const {
        return a == other.a and b == other.b;
      }
    };

    std::array arr{MyBytesType{1, 1.1}, MyBytesType{2, 2.2},
                   MyBytesType{3, 3.3}};

    static_assert(serialize_as_bytes_v<std::array<MyBytesType, 3>>);

    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | arr;
    CHECK(sizer.number_of_bytes() == sizeof(MyBytesType) * arr.size());

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | arr;

    // Unpacking
    std::array<MyBytesType, 3> arr_unpacked{};
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | arr_unpacked;

    CHECK(arr == arr_unpacked);
  }

  // Test with non-trivial type
  {
    struct ComplexType {
      int id = 0;
      double value = 0.0;
      Serializer& serialize(Serializer& s) {
        s | id | value;
        return s;
      }
      bool operator==(const ComplexType& other) const {
        return id == other.id and value == other.value;
      }
    };

    std::array arr{ComplexType{1, 3.14}, ComplexType{2, 2.71}};
    static_assert(not serialize_as_bytes_v<std::array<ComplexType, 3>>);

    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | arr;
    CHECK(sizer.number_of_bytes() > 0);

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | arr;

    // Unpacking
    std::array<ComplexType, 2> arr_unpacked{};
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | arr_unpacked;

    CHECK(arr == arr_unpacked);
  }

  test_array_of_vector_of_array<Cast>();
}
}  // namespace

TEST_CASE("Serialize.Array") {
  test<Serializer>();
#ifdef FINDUS_MIMIC_CHARM_PUPER
  test<PUP::er>();
#endif
}
}  // namespace findus::serialize
#endif
