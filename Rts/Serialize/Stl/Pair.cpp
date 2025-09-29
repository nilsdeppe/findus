// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Serialize/Stl/Pair.hpp"

#if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <memory>
#include <utility>
#include <vector>

#include "Rts/Serialize/Serializer.hpp"
#include "Rts/Serialize/Stl/Vector.hpp"

namespace rts::serialize {
namespace {
struct NoSerialize {};
static_assert(is_serializable_v<std::pair<int, double>>);
static_assert(is_serializable_v<int>);
static_assert(not is_serializable_v<NoSerialize>);
static_assert(not is_serializable_v<std::pair<int, NoSerialize>>);
static_assert(not is_serializable_v<std::pair<NoSerialize, int>>);
static_assert(not is_serializable_v<std::pair<NoSerialize, NoSerialize>>);
}  // namespace

TEST_CASE("Serialize.Pair") {
  // Fundamental types
  {
    std::pair<int, double> p{42, 3.14};
    static_assert(serialize_as_bytes_v<std::pair<int, double>>);

    Serializer sizer{Serializer::Sizing};
    sizer | p;
    CHECK(sizer.number_of_bytes() == sizeof(std::pair<int, double>));

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | p;

    std::pair<int, double> p_unpacked{0, 0.0};
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | p_unpacked;

    CHECK(p == p_unpacked);
  }

  // std::pair<const T, U>
  {
    std::pair<const int, double> p{42, 3.14};

    Serializer sizer{Serializer::Sizing};
    sizer | p;
    CHECK(sizer.number_of_bytes() == sizeof(std::pair<const int, double>));

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | p;

    std::pair<const int, double> p_unpacked{0, 0.0};
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | p_unpacked;

    CHECK(p == p_unpacked);
  }

  // Edge case: default-initialized pair
  {
    std::pair<int, double> p{};

    Serializer sizer{Serializer::Sizing};
    sizer | p;
    CHECK(sizer.number_of_bytes() == sizeof(std::pair<int, double>));

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | p;

    std::pair<int, double> p_unpacked{};
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | p_unpacked;

    CHECK(p == p_unpacked);
  }

  // Edge case: empty pair (for types where empty is meaningful)
  {
    struct EmptyType {
      Serializer& serialize(Serializer& s) { return s; }
      bool operator==(const EmptyType&) const { return true; }
    };
    std::pair<EmptyType, EmptyType> p{};

    Serializer sizer{Serializer::Sizing};
    sizer | p;
    CHECK(sizer.number_of_bytes() == 0);

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | p;

    std::pair<EmptyType, EmptyType> p_unpacked{};
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | p_unpacked;

    CHECK(p == p_unpacked);
  }

  // as_bytes type
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

    std::pair<MyBytesType, MyBytesType> p{MyBytesType{1, 1.1},
                                          MyBytesType{2, 2.2}};

    Serializer sizer{Serializer::Sizing};
    sizer | p;
    CHECK(sizer.number_of_bytes() == sizeof(MyBytesType) * 2);

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | p;

    std::pair<MyBytesType, MyBytesType> p_unpacked{MyBytesType{},
                                                   MyBytesType{}};
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | p_unpacked;

    CHECK(p == p_unpacked);
  }

  // Non-trivial type
  {
    struct ComplexType {
      int id = 0;
      double value = 0.0;
      Serializer& serialize(Serializer& s) { return s | id | value; }
      bool operator==(const ComplexType& other) const {
        return id == other.id and value == other.value;
      }
    };

    std::pair<ComplexType, ComplexType> p{ComplexType{1, 3.14},
                                          ComplexType{2, 2.71}};

    Serializer sizer{Serializer::Sizing};
    sizer | p;
    CHECK(sizer.number_of_bytes() > 0);

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | p;

    std::pair<ComplexType, ComplexType> p_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | p_unpacked;

    CHECK(p == p_unpacked);
  }

  // Nested pair
  {
    using T = std::pair<std::pair<int, double>, std::pair<double, int>>;
    T p{{1, 2.3}, {4.5, 6}};
    static_assert(serialize_as_bytes_v<T>);

    Serializer sizer{Serializer::Sizing};
    sizer | p;
    CHECK(sizer.number_of_bytes() == sizeof(T));

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | p;

    std::pair<std::pair<int, double>, std::pair<double, int>> p_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | p_unpacked;

    CHECK(p == p_unpacked);
  }

  // std::pair<std::vector<std::pair<int, double>>, int>
  {
    std::vector<std::pair<int, double>> vec{{1, 1.1}, {2, 2.2}, {3, 3.3}};
    static_assert(
        not serialize_as_bytes_v<std::vector<std::pair<int, double>>>);
    int extra = 42;
    std::pair<std::vector<std::pair<int, double>>, int> p{vec, extra};
    static_assert(not serialize_as_bytes_v<
                  std::pair<std::vector<std::pair<int, double>>, int>>);

    Serializer sizer{Serializer::Sizing};
    sizer | p;
    CHECK(sizer.number_of_bytes() > 0);

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | p;

    std::pair<std::vector<std::pair<int, double>>, int> p_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | p_unpacked;

    CHECK(p == p_unpacked);
  }
}
}  // namespace rts::serialize
#endif
