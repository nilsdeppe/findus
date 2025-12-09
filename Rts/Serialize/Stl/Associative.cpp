// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "Rts/Serialize/Serializer.hpp"
#include "Rts/Serialize/Stl/Map.hpp"
#include "Rts/Serialize/Stl/Set.hpp"
#include "Rts/Serialize/Stl/UnorderedMap.hpp"
#include "Rts/Serialize/Stl/UnorderedSet.hpp"

namespace {
struct MyBytesType : findus::serialize::as_bytes<void> {
  int a = 0;
  double b = 0.0;
  MyBytesType() = default;
  MyBytesType(int a_in, double b_in) : a(a_in), b(b_in) {}
  bool operator==(const MyBytesType& other) const {
    return a == other.a and b == other.b;
  }
  bool operator<(const MyBytesType& other) const {
    return a < other.a or (a == other.a and b < other.b);
  }
};

struct ComplexType {
  int id = 0;
  double value = 0.0;
  findus::serialize::Serializer& serialize(findus::serialize::Serializer& s) {
    return s | id | value;
  }
  ComplexType(const int id_in, const double value_in)
      : id(id_in), value(value_in) {}
  ComplexType(findus::serialize::Serializer& s) { serialize(s); }
  ComplexType(const ComplexType&) = delete;
  ComplexType& operator=(const ComplexType&) = delete;
  ComplexType(ComplexType&&) = delete;
  ComplexType& operator=(ComplexType&&) = delete;
  bool operator==(const ComplexType& other) const {
    return id == other.id and value == other.value;
  }
  bool operator<(const ComplexType& other) const {
    return id < other.id or (id == other.id and value < other.value);
  }
};

struct NoSerialize {};

template <typename Container>
void test_serialize_container(Container& original) {
  findus::serialize::Serializer sizer{findus::serialize::Serializer::Sizing};
  sizer | original;
  CHECK(sizer.number_of_bytes() > 0);

  std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
  findus::serialize::Serializer packer{findus::serialize::Serializer::Packing,
                                       buffer.get(), sizer.number_of_bytes()};
  packer | original;

  Container unpacked;
  findus::serialize::Serializer unpacker{
      findus::serialize::Serializer::Unpacking, buffer.get(),
      sizer.number_of_bytes()};
  unpacker | unpacked;

  CHECK(original == unpacked);
}
}  // namespace

namespace std {
template <>
struct hash<MyBytesType> {
  size_t operator()(const MyBytesType& t) const {
    return std::hash<int>{}(t.a) xor std::hash<double>{}(t.b);
  }
};

template <>
struct hash<ComplexType> {
  size_t operator()(const ComplexType& t) const {
    return std::hash<int>{}(t.id) xor std::hash<double>{}(t.value);
  }
};
}  // namespace std

TEST_CASE("Serialize.Set") {
  // Test with fundamental type
  std::set<int> s_int{1, 2, 3, 4, 5};
  static_assert(findus::serialize::is_serializable_v<std::set<int>>);
  test_serialize_container(s_int);

  // Test with as_bytes type
  std::set<MyBytesType> s_my_bytes_type{
      MyBytesType{1, 1.1}, MyBytesType{2, 2.2}, MyBytesType{3, 3.3}};
  static_assert(findus::serialize::is_serializable_v<std::set<MyBytesType>>);
  test_serialize_container(s_my_bytes_type);

  // Test with non-trivial type
  std::set<ComplexType> s_complex;
  static_assert(findus::serialize::is_serializable_v<std::set<ComplexType>>);
  s_complex.emplace(1, 3.14);
  s_complex.emplace(2, 2.71);
  test_serialize_container(s_complex);

  static_assert(
      not findus::serialize::is_serializable_v<std::set<NoSerialize>>);
}

TEST_CASE("Serialize.Multiset") {
  // Test with fundamental type
  std::multiset<int> ms_int{1, 2, 3, 4, 5, 3, 2};
  static_assert(findus::serialize::is_serializable_v<std::multiset<int>>);
  test_serialize_container(ms_int);

  // Test with as_bytes type
  std::multiset<MyBytesType> ms_my_bytes{
      MyBytesType{1, 1.1}, MyBytesType{2, 2.2}, MyBytesType{3, 3.3},
      MyBytesType{2, 2.2}};
  static_assert(
      findus::serialize::is_serializable_v<std::multiset<MyBytesType>>);
  test_serialize_container(ms_my_bytes);

  // Test with non-trivial type
  std::multiset<ComplexType> ms_complex;
  static_assert(
      findus::serialize::is_serializable_v<std::multiset<ComplexType>>);
  ms_complex.emplace(1, 3.14);
  ms_complex.emplace(2, 2.71);
  ms_complex.emplace(1, 3.14);
  test_serialize_container(ms_complex);

  static_assert(
      not findus::serialize::is_serializable_v<std::multiset<NoSerialize>>);
}

TEST_CASE("Serialize.UnorderedSet") {
  // Test with fundamental type
  std::unordered_set<int> s_int{1, 2, 3, 4, 5};
  static_assert(findus::serialize::is_serializable_v<std::unordered_set<int>>);
  test_serialize_container(s_int);

  // Test with as_bytes type
  std::unordered_set<MyBytesType> s_my_bytes_type{
      MyBytesType{1, 1.1}, MyBytesType{2, 2.2}, MyBytesType{3, 3.3}};
  static_assert(
      findus::serialize::is_serializable_v<std::unordered_set<MyBytesType>>);
  test_serialize_container(s_my_bytes_type);

  // Test with non-trivial type
  std::unordered_set<ComplexType> s_complex;
  static_assert(
      findus::serialize::is_serializable_v<std::unordered_set<ComplexType>>);
  s_complex.emplace(1, 3.14);
  s_complex.emplace(2, 2.71);
  test_serialize_container(s_complex);

  static_assert(not findus::serialize::is_serializable_v<
                std::unordered_set<NoSerialize>>);
}

TEST_CASE("Serialize.UnorderedMultiset") {
  // Test with fundamental type
  std::unordered_multiset<int> ms_int{1, 2, 3, 4, 5, 3, 2};
  static_assert(
      findus::serialize::is_serializable_v<std::unordered_multiset<int>>);
  test_serialize_container(ms_int);

  // Test with as_bytes type
  std::unordered_multiset<MyBytesType> ms_my_bytes{
      MyBytesType{1, 1.1}, MyBytesType{2, 2.2}, MyBytesType{3, 3.3},
      MyBytesType{2, 2.2}};
  static_assert(findus::serialize::is_serializable_v<
                std::unordered_multiset<MyBytesType>>);
  test_serialize_container(ms_my_bytes);

  // Test with non-trivial type
  std::unordered_multiset<ComplexType> ms_complex;
  static_assert(findus::serialize::is_serializable_v<
                std::unordered_multiset<ComplexType>>);
  ms_complex.emplace(1, 3.14);
  ms_complex.emplace(2, 2.71);
  ms_complex.emplace(1, 3.14);
  test_serialize_container(ms_complex);

  static_assert(not findus::serialize::is_serializable_v<
                std::unordered_multiset<NoSerialize>>);
}

TEST_CASE("Serialize.Map") {
  // Test with fundamental types
  std::map<int, double> m_int{{1, 1.1}, {2, 2.2}, {3, 3.3}};
  static_assert(findus::serialize::is_serializable_v<std::map<int, double>>);
  static_assert(
      not findus::serialize::is_serializable_v<std::map<NoSerialize, double>>);
  static_assert(
      not findus::serialize::is_serializable_v<std::map<int, NoSerialize>>);
  test_serialize_container(m_int);

  // Test with as_bytes type
  std::map<int, MyBytesType> m_bytes{{1, MyBytesType{1, 1.1}},
                                     {2, MyBytesType{2, 2.2}},
                                     {3, MyBytesType{3, 3.3}}};
  static_assert(
      findus::serialize::is_serializable_v<std::map<int, MyBytesType>>);
  static_assert(not findus::serialize::is_serializable_v<
                std::map<NoSerialize, MyBytesType>>);
  test_serialize_container(m_bytes);

  // Test with non-trivial type
  std::map<int, ComplexType> m_complex;
  static_assert(
      findus::serialize::is_serializable_v<std::map<int, ComplexType>>);
  static_assert(not findus::serialize::is_serializable_v<
                std::map<NoSerialize, ComplexType>>);
  m_complex.emplace(std::piecewise_construct, std::forward_as_tuple(1),
                    std::forward_as_tuple(1, 3.14));
  m_complex.emplace(std::piecewise_construct, std::forward_as_tuple(2),
                    std::forward_as_tuple(2, 2.71));
  test_serialize_container(m_complex);

  static_assert(not findus::serialize::is_serializable_v<
                std::map<NoSerialize, NoSerialize>>);
}

TEST_CASE("Serialize.Multimap") {
  // Test with fundamental types
  std::multimap<int, double> mm_int{{1, 1.1}, {2, 2.2}, {3, 3.3}, {2, 4.4}};
  static_assert(
      findus::serialize::is_serializable_v<std::multimap<int, double>>);
  static_assert(not findus::serialize::is_serializable_v<
                std::multimap<NoSerialize, double>>);
  static_assert(not findus::serialize::is_serializable_v<
                std::multimap<int, NoSerialize>>);
  test_serialize_container(mm_int);

  // Test with as_bytes type
  std::multimap<int, MyBytesType> mm_bytes{{1, MyBytesType{1, 1.1}},
                                           {2, MyBytesType{2, 2.2}},
                                           {3, MyBytesType{3, 3.3}},
                                           {2, MyBytesType{4, 4.4}}};
  static_assert(
      findus::serialize::is_serializable_v<std::multimap<int, MyBytesType>>);
  static_assert(not findus::serialize::is_serializable_v<
                std::multimap<NoSerialize, MyBytesType>>);
  test_serialize_container(mm_bytes);

  // Test with non-trivial type
  std::multimap<int, ComplexType> mm_complex;
  static_assert(
      findus::serialize::is_serializable_v<std::multimap<int, ComplexType>>);
  static_assert(not findus::serialize::is_serializable_v<
                std::multimap<NoSerialize, ComplexType>>);
  mm_complex.emplace(std::piecewise_construct, std::forward_as_tuple(1),
                     std::forward_as_tuple(1, 3.14));
  mm_complex.emplace(std::piecewise_construct, std::forward_as_tuple(2),
                     std::forward_as_tuple(2, 2.71));
  mm_complex.emplace(std::piecewise_construct, std::forward_as_tuple(1),
                     std::forward_as_tuple(3, 1.23));
  test_serialize_container(mm_complex);

  static_assert(not findus::serialize::is_serializable_v<
                std::multimap<NoSerialize, NoSerialize>>);
}

TEST_CASE("Serialize.UnorderedMap") {
  // Fundamental type
  std::unordered_map<int, double> m_int{{1, 1.1}, {2, 2.2}, {3, 3.3}};
  static_assert(
      findus::serialize::is_serializable_v<std::unordered_map<int, double>>);
  static_assert(not findus::serialize::is_serializable_v<
                std::unordered_map<NoSerialize, double>>);
  static_assert(not findus::serialize::is_serializable_v<
                std::unordered_map<int, NoSerialize>>);
  test_serialize_container(m_int);

  // as_bytes type
  std::unordered_map<int, MyBytesType> m_bytes{{1, MyBytesType{1, 1.1}},
                                               {2, MyBytesType{2, 2.2}},
                                               {3, MyBytesType{3, 3.3}}};
  static_assert(findus::serialize::is_serializable_v<
                std::unordered_map<int, MyBytesType>>);
  static_assert(not findus::serialize::is_serializable_v<
                std::unordered_map<NoSerialize, MyBytesType>>);
  test_serialize_container(m_bytes);

  // Non-trivial type
  std::unordered_map<int, ComplexType> m_complex;
  static_assert(findus::serialize::is_serializable_v<
                std::unordered_map<int, ComplexType>>);
  static_assert(not findus::serialize::is_serializable_v<
                std::unordered_map<NoSerialize, ComplexType>>);
  m_complex.emplace(std::piecewise_construct, std::forward_as_tuple(1),
                    std::forward_as_tuple(1, 3.14));
  m_complex.emplace(std::piecewise_construct, std::forward_as_tuple(2),
                    std::forward_as_tuple(2, 2.71));
  test_serialize_container(m_complex);

  static_assert(not findus::serialize::is_serializable_v<
                std::unordered_map<NoSerialize, NoSerialize>>);
}

TEST_CASE("Serialize.UnorderedMultimap") {
  // Fundamental type
  std::unordered_multimap<int, double> mm_int{
      {1, 1.1}, {2, 2.2}, {3, 3.3}, {2, 4.4}};
  static_assert(findus::serialize::is_serializable_v<
                std::unordered_multimap<int, double>>);
  static_assert(not findus::serialize::is_serializable_v<
                std::unordered_multimap<NoSerialize, double>>);
  static_assert(not findus::serialize::is_serializable_v<
                std::unordered_multimap<int, NoSerialize>>);
  test_serialize_container(mm_int);

  // as_bytes type
  std::unordered_multimap<int, MyBytesType> mm_bytes{{1, MyBytesType{1, 1.1}},
                                                     {2, MyBytesType{2, 2.2}},
                                                     {3, MyBytesType{3, 3.3}},
                                                     {2, MyBytesType{4, 4.4}}};
  static_assert(findus::serialize::is_serializable_v<
                std::unordered_multimap<int, MyBytesType>>);
  static_assert(not findus::serialize::is_serializable_v<
                std::unordered_multimap<NoSerialize, MyBytesType>>);
  test_serialize_container(mm_bytes);

  // Non-trivial type
  std::unordered_multimap<int, ComplexType> mm_complex;
  static_assert(findus::serialize::is_serializable_v<
                std::unordered_multimap<int, ComplexType>>);
  static_assert(not findus::serialize::is_serializable_v<
                std::unordered_multimap<NoSerialize, ComplexType>>);
  mm_complex.emplace(std::piecewise_construct, std::forward_as_tuple(1),
                     std::forward_as_tuple(1, 3.14));
  mm_complex.emplace(std::piecewise_construct, std::forward_as_tuple(2),
                     std::forward_as_tuple(2, 2.71));
  mm_complex.emplace(std::piecewise_construct, std::forward_as_tuple(1),
                     std::forward_as_tuple(3, 1.23));
  test_serialize_container(mm_complex);

  static_assert(not findus::serialize::is_serializable_v<
                std::unordered_multimap<NoSerialize, NoSerialize>>);
}
#endif
