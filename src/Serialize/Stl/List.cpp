// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Serialize/Stl/List.hpp"

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <list>

namespace findus::serialize {
namespace {
struct NoSerialize {};
static_assert(is_serializable_v<std::list<int>>);
static_assert(is_serializable_v<int>);
static_assert(not is_serializable_v<NoSerialize>);
static_assert(not is_serializable_v<std::list<NoSerialize>>);

template <class Cast>
void test() {
  // Test with fundamental type
  {
    std::list<int> list{1, 2, 3, 4, 5};

    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | list;
    CHECK(sizer.number_of_bytes() ==
          sizeof(size_t) + sizeof(int) * list.size());

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | list;

    // Unpacking
    std::list<int> list_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | list_unpacked;

    CHECK(list == list_unpacked);
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

    std::list<MyBytesType> list{MyBytesType{1, 1.1}, MyBytesType{2, 2.2},
                                MyBytesType{3, 3.3}};

    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | list;
    CHECK(sizer.number_of_bytes() ==
          sizeof(size_t) + sizeof(MyBytesType) * list.size());

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | list;

    // Unpacking
    std::list<MyBytesType> list_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | list_unpacked;

    CHECK(list == list_unpacked);
  }

  // Test a complex type
  {
    struct ComplexType {
      int id = 0;
      std::list<int> data;
      double value = 0.0;  // Serialize member function
      Serializer& serialize(Serializer& s) {
        s | id | data | value;
        return s;
      }

      ComplexType(int id_in, std::list<int> data_in, double value_in)
          : id(id_in), data(std::move(data_in)), value(value_in) {}

      ComplexType() = delete;
      ComplexType(ComplexType&&) = default;
      ComplexType(const ComplexType&) = delete;

      ComplexType(Serializer& s) { s | id | data | value; }

      bool operator==(const ComplexType& other) const {
        return id == other.id and data == other.data and value == other.value;
      }
    };

    std::list<ComplexType> list;
    list.emplace_back(1, std::list{10, 20, 30}, 3.14);
    list.emplace_back(2, std::list{40, 50}, 2.71);

    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | list;
    // The exact number_of_bytes is complex to compute, but it should be > 0
    CHECK(sizer.number_of_bytes() ==
          (8 + (4 + 8 + 3 * 4 + 8) + (4 + 8 + 2 * 4 + 8)));

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | list;

    // Unpacking
    std::list<ComplexType> list_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | list_unpacked;

    CHECK(list == list_unpacked);
  }
}
}  // namespace

TEST_CASE("Serialize.List") {
  test<Serializer>();
#ifdef FINDUS_MIMIC_CHARM_PUPER
  test<PUP::er>();
#endif
}
}  // namespace findus::serialize
#endif
