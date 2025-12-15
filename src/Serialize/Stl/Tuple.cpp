// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Serialize/Stl/Tuple.hpp"

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <memory>
#include <tuple>
#include <vector>

#include "findus/Serialize/Serializer.hpp"
#include "findus/Serialize/Stl/Vector.hpp"

namespace findus::serialize {
namespace {
struct NoSerialize {};
static_assert(is_serializable_v<std::tuple<int, double>>);
static_assert(is_serializable_v<std::tuple<int&, double&>>);
static_assert(is_serializable_v<std::tuple<int, double, size_t>>);
static_assert(is_serializable_v<int>);
static_assert(not is_serializable_v<NoSerialize>);
static_assert(not is_serializable_v<std::tuple<int, NoSerialize>>);
static_assert(not is_serializable_v<std::tuple<int, NoSerialize, double>>);
static_assert(not is_serializable_v<std::tuple<NoSerialize, int>>);
static_assert(not is_serializable_v<std::tuple<NoSerialize, int, double>>);
static_assert(not is_serializable_v<std::tuple<int, double, NoSerialize>>);
static_assert(not is_serializable_v<std::tuple<NoSerialize, NoSerialize>>);
static_assert(
    not is_serializable_v<std::tuple<NoSerialize, NoSerialize, NoSerialize>>);
static_assert(not is_serializable_v<std::tuple<int, NoSerialize&>>);
static_assert(not is_serializable_v<std::tuple<int&, NoSerialize>>);
static_assert(not is_serializable_v<std::tuple<int&, NoSerialize&>>);

template <class Cast>
void test() {
  // Fundamental types
  {
    std::tuple<int, double, char> t{42, 3.14, 'A'};
    static_assert(serialize_as_bytes_v<std::tuple<int, double, char>>);

    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | t;
    CHECK(sizer.number_of_bytes() == sizeof(std::tuple<int, double, char>));

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | t;

    std::tuple<int, double, char> t_unpacked{0, 0.0, '\0'};
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | t_unpacked;

    CHECK(t == t_unpacked);
  }

  // as_bytes type
  {
    struct MyBytesType : as_bytes<void> {
      int a = 0;
      double b = 0.0;
      char c = '\0';
      MyBytesType() = default;
      MyBytesType(int a_in, double b_in, char c_in)
          : a(a_in), b(b_in), c(c_in) {}
      bool operator==(const MyBytesType& other) const {
        return a == other.a and b == other.b and c == other.c;
      }
    };

    std::tuple<MyBytesType, MyBytesType, MyBytesType> t{
        MyBytesType{1, 1.1, 'a'}, MyBytesType{2, 2.2, 'b'},
        MyBytesType{3, 3.3, 'c'}};

    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | t;
    CHECK(sizer.number_of_bytes() == sizeof(MyBytesType) * 3);

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | t;

    std::tuple<MyBytesType, MyBytesType, MyBytesType> t_unpacked{
        MyBytesType{}, MyBytesType{}, MyBytesType{}};
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | t_unpacked;

    CHECK(t == t_unpacked);
  }

  // Type with pup/serialize member
  {
    struct ComplexType {
      int id = 0;
      double value = 0.0;
      char tag = '\0';
      Serializer& serialize(Serializer& s) { return s | id | value | tag; }
      bool operator==(const ComplexType& other) const {
        return id == other.id and value == other.value and tag == other.tag;
      }
    };

    std::tuple<ComplexType, ComplexType, ComplexType> t{
        ComplexType{1, 3.14, 'x'}, ComplexType{2, 2.71, 'y'},
        ComplexType{3, 1.23, 'z'}};

    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | t;
    CHECK(sizer.number_of_bytes() > 0);

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | t;

    std::tuple<ComplexType, ComplexType, ComplexType> t_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | t_unpacked;

    CHECK(t == t_unpacked);
  }

  // Nested tuple
  {
    using T =
        std::tuple<std::tuple<int, double, char>, std::tuple<double, int, char>,
                   std::tuple<char, int, double>>;
    T t{std::tuple{1, 2.3, 'a'}, std::tuple{4.5, 6, 'b'},
        std::tuple{'c', 7, 8.9}};

    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | t;
    CHECK(sizer.number_of_bytes() == sizeof(T));

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | t;

    std::tuple<std::tuple<int, double, char>, std::tuple<double, int, char>,
               std::tuple<char, int, double>>
        t_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | t_unpacked;

    CHECK(t == t_unpacked);
  }

  // std::vector<std::tuple<int, double, char>>
  {
    std::vector<std::tuple<int, double, char>> vec{
        {1, 1.1, 'a'}, {2, 2.2, 'b'}, {3, 3.3, 'c'}};

    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | vec;
    CHECK(sizer.number_of_bytes() > 0);

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | vec;

    std::vector<std::tuple<int, double, char>> vec_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | vec_unpacked;

    CHECK(vec == vec_unpacked);
  }

  // Tuple with 4 elements, including a vector of tuples
  {
    std::vector<std::tuple<int, double, char>> vec{{1, 1.1, 'a'},
                                                   {2, 2.2, 'b'}};
    int a = 7;
    double b = 8.8;
    char c = 'z';
    std::tuple<std::vector<std::tuple<int, double, char>>, int, double, char> t{
        vec, a, b, c};

    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | t;
    CHECK(sizer.number_of_bytes() > 0);

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | t;

    std::tuple<std::vector<std::tuple<int, double, char>>, int, double, char>
        t_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | t_unpacked;

    CHECK(t == t_unpacked);
  }
}
}  // namespace

TEST_CASE("Serialize.Tuple") {
  test<Serializer>();
#ifdef FINDUS_MIMIC_CHARM_PUPER
  test<PUP::er>();
#endif
}
}  // namespace findus::serialize
#endif
