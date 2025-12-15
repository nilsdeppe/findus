// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Serialize/Stl/Vector.hpp"

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <vector>

namespace findus::serialize {
namespace {
struct NoSerialize {};
static_assert(is_serializable_v<std::vector<int>>);
static_assert(is_serializable_v<int>);
static_assert(not is_serializable_v<NoSerialize>);
static_assert(not is_serializable_v<std::vector<NoSerialize>>);

template <class Cast>
void test() {
  // Test with fundamental type
  {
    std::vector<int> vec{1, 2, 3, 4, 5};

    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | vec;
    CHECK(sizer.number_of_bytes() ==
          sizeof(size_t) * 2 + sizeof(int) * vec.size());

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | vec;

    // Unpacking
    std::vector<int> vec_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | vec_unpacked;

    CHECK(vec == vec_unpacked);
  }

  // Test with fundamental type with capacity
  {
    std::vector<int> vec{1, 2, 3, 4, 5};

    vec.reserve(20);

    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | vec;
    CHECK(sizer.number_of_bytes() ==
          sizeof(size_t) * 2 + sizeof(int) * vec.size());

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | vec;

    // Unpacking
    std::vector<int> vec_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | vec_unpacked;

    CHECK(vec_unpacked.capacity() == 20);
    CHECK(vec == vec_unpacked);
  }

  // Test with as_bytes type
  {
    struct MyBytesType : as_bytes<void> {
      int a = 0;
      double b = 0.0;
      bool operator==(const MyBytesType& other) const {
        return a == other.a and b == other.b;
      }
    };

    std::vector<MyBytesType> vec(3);
    vec[0].a = 1;
    vec[0].b = 1.1;
    vec[1].a = 2;
    vec[1].b = 2.2;
    vec[2].a = 3;
    vec[2].b = 3.3;

    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | vec;
    CHECK(sizer.number_of_bytes() ==
          sizeof(size_t) * 2 + sizeof(MyBytesType) * vec.size());

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | vec;

    // Unpacking
    std::vector<MyBytesType> vec_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | vec_unpacked;

    CHECK(vec == vec_unpacked);
  }

  // Test a complex type
  {
    struct ComplexType {
      int id = 0;
      std::vector<int> data;
      double value = 0.0;  // Serialize member function
      Serializer& serialize(Serializer& s) {
        s | id | data | value;
        return s;
      }

      ComplexType(int id_in, std::vector<int> data_in, double value_in)
          : id(id_in), data(std::move(data_in)), value(value_in) {}

      ComplexType() = delete;
      ComplexType(ComplexType&&) = default;
      ComplexType(const ComplexType&) = delete;

      ComplexType(Serializer& s) { s | id | data | value; }

      bool operator==(const ComplexType& other) const {
        return id == other.id and data == other.data and value == other.value;
      }
    };

    std::vector<ComplexType> vec;
    vec.reserve(2);
    vec.emplace_back(1, std::vector{10, 20, 30}, 3.14);
    vec.emplace_back(2, std::vector{40, 50}, 2.71);

    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | vec;
    // The exact number_of_bytes is complex to compute, but it should be > 0
    CHECK(sizer.number_of_bytes() ==
          (2 * 8 + (4 + 2 * 8 + 3 * 4 + 8) + (4 + 2 * 8 + 2 * 4 + 8)));

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | vec;

    // Unpacking
    std::vector<ComplexType> vec_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | vec_unpacked;

    CHECK(vec == vec_unpacked);
  }

  {
    std::vector<bool> vec{true, false, true, true, false};
    vec.reserve(20);

    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | vec;
    // Each bool is serialized individually, plus capacity and size
    CHECK(sizer.number_of_bytes() ==
          sizeof(size_t) * 2 + sizeof(bool) * vec.size());

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | vec;

    // Unpacking
    std::vector<bool> vec_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | vec_unpacked;

    CHECK(vec_unpacked.capacity() == vec.capacity());
    CHECK(vec == vec_unpacked);
  }
}
}  // namespace

TEST_CASE("Serialize.Vector") {
  test<Serializer>();
#ifdef FINDUS_MIMIC_CHARM_PUPER
  test<PUP::er>();
#endif
}
}  // namespace findus::serialize
#endif
