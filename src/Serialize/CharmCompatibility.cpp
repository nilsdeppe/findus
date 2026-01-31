// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>

#include <complex>
#include <cstddef>
#include <deque>
#include <forward_list>
#include <list>
#include <map>
#include <pup.h>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
template <class T> struct is_smart_ptr : std::false_type {};

template <class T> struct is_smart_ptr<std::shared_ptr<T>> : std::true_type {};

template <class T> struct is_smart_ptr<std::unique_ptr<T>> : std::true_type {};

template <class T> constexpr bool is_smart_ptr_v = is_smart_ptr<T>::value;

template <class S, class T> void check_round_trip(T &original) {
  const std::string type_name = typeid(T).name();
  CAPTURE(type_name);
  findus::serialize::Serializer sizer{findus::serialize::Serializer::Sizing};
  static_cast<S &>(sizer) | original;

  std::vector<std::byte> buffer(sizer.number_of_bytes());
  findus::serialize::Serializer packer{findus::serialize::Serializer::Packing,
                                       buffer.data(), buffer.size()};
  static_cast<S &>(packer) | original;

  T restored{};
  findus::serialize::Serializer unpacker{
      findus::serialize::Serializer::Unpacking, buffer.data(), buffer.size()};
  static_cast<S &>(unpacker) | restored;

  if constexpr (is_smart_ptr_v<T>) {
    CHECK(restored != original);
    CHECK(*restored == *original);
  } else {
    CHECK(restored == original);
  }
}

template <class S> void test_pup_h() {
  INFO("test_pup_h");
  int int_value = 42;
  check_round_trip<S>(int_value);

  double double_value = 3.14159;
  check_round_trip<S>(double_value);

  bool bool_value = true;
  check_round_trip<S>(bool_value);

  char char_value = 'x';
  check_round_trip<S>(char_value);

  size_t size_value = 12345;
  check_round_trip<S>(size_value);
}
} // namespace

#include <pup_stl.h>

namespace {
template <class S> void test_pup_stl_h() {
  INFO("test_pup_stl_h");
  std::array<int, 3> array_value{1, 2, 3};
  check_round_trip<S>(array_value);

  std::complex<double> complex_value{1.0, 2.0};
  check_round_trip<S>(complex_value);

  std::deque<int> deque_value{1, 2, 3};
  check_round_trip<S>(deque_value);

  std::forward_list<int> forward_list_value{1, 2, 3};
  check_round_trip<S>(forward_list_value);

  std::list<int> list_value{1, 2, 3};
  check_round_trip<S>(list_value);

  std::map<int, double> map_value{{1, 1.0}, {2, 2.0}};
  check_round_trip<S>(map_value);

  std::multimap<int, double> multimap_value{{1, 1.0}, {1, 1.5}, {2, 2.0}};
  check_round_trip<S>(multimap_value);

  std::pair<int, double> pair_value{1, 2.0};
  check_round_trip<S>(pair_value);

  std::set<int> set_value{1, 2, 3};
  check_round_trip<S>(set_value);

  std::multiset<int> multiset_value{1, 1, 2, 3};
  check_round_trip<S>(multiset_value);

  std::shared_ptr<int> shared_ptr_value = std::make_shared<int>(42);
  check_round_trip<S>(shared_ptr_value);

  std::string string_value = "hello";
  check_round_trip<S>(string_value);

  std::tuple<int, double, char> tuple_value{1, 2.0, 'a'};
  check_round_trip<S>(tuple_value);

  std::unique_ptr<int> unique_ptr_value = std::make_unique<int>(42);
  findus::serialize::Serializer sizer{findus::serialize::Serializer::Sizing};
  static_cast<S &>(sizer) | unique_ptr_value;

  std::vector<std::byte> buffer(sizer.number_of_bytes());
  findus::serialize::Serializer packer{findus::serialize::Serializer::Packing,
                                       buffer.data(), buffer.size()};
  static_cast<S &>(packer) | unique_ptr_value;

  std::unique_ptr<int> unique_ptr_restored{};
  findus::serialize::Serializer unpacker{
      findus::serialize::Serializer::Unpacking, buffer.data(), buffer.size()};
  static_cast<S &>(unpacker) | unique_ptr_restored;
  CHECK(*unique_ptr_restored == *unique_ptr_value);

  std::unordered_map<int, double> unordered_map_value{{1, 1.0}, {2, 2.0}};
  check_round_trip<S>(unordered_map_value);

  std::unordered_multimap<int, double> unordered_multimap_value{
      {1, 1.0}, {1, 1.5}, {2, 2.0}};
  // Note: unordered_multimap order is not guaranteed, so we just test
  // it compiles and runs without checking equality
  findus::serialize::Serializer sizer2{findus::serialize::Serializer::Sizing};
  static_cast<S &>(sizer2) | unordered_multimap_value;

  std::vector<std::byte> buffer2(sizer2.number_of_bytes());
  findus::serialize::Serializer packer2{findus::serialize::Serializer::Packing,
                                        buffer2.data(), buffer2.size()};
  static_cast<S &>(packer2) | unordered_multimap_value;

  std::unordered_multimap<int, double> unordered_multimap_restored{};
  findus::serialize::Serializer unpacker2{
      findus::serialize::Serializer::Unpacking, buffer2.data(), buffer2.size()};
  static_cast<S &>(unpacker2) | unordered_multimap_restored;
  CHECK(unordered_multimap_restored.size() == unordered_multimap_value.size());

  std::unordered_set<int> unordered_set_value{1, 2, 3};
  check_round_trip<S>(unordered_set_value);

  std::unordered_multiset<int> unordered_multiset_value{1, 1, 2, 3};
  // Note: unordered_multiset order is not guaranteed, so we just test
  // it compiles and runs without checking equality
  findus::serialize::Serializer sizer3{findus::serialize::Serializer::Sizing};
  static_cast<S &>(sizer3) | unordered_multiset_value;

  std::vector<std::byte> buffer3(sizer3.number_of_bytes());
  findus::serialize::Serializer packer3{findus::serialize::Serializer::Packing,
                                        buffer3.data(), buffer3.size()};
  static_cast<S &>(packer3) | unordered_multiset_value;

  std::unordered_multiset<int> unordered_multiset_restored{};
  findus::serialize::Serializer unpacker3{
      findus::serialize::Serializer::Unpacking, buffer3.data(), buffer3.size()};
  static_cast<S &>(unpacker3) | unordered_multiset_restored;
  CHECK(unordered_multiset_restored.size() == unordered_multiset_value.size());

  std::vector<int> vector_value{1, 2, 3};
  check_round_trip<S>(vector_value);

  std::vector<bool> vector_bool_value{true, false, true};
  check_round_trip<S>(vector_bool_value);
}
} // namespace

TEST_CASE("CharmPupAndPupStl") {
  {
    INFO("Testing Serializer");
    test_pup_h<findus::serialize::Serializer>();
    test_pup_stl_h<findus::serialize::Serializer>();
  }

#ifdef FINDUS_MIMIC_CHARM_PUPER
  {
    INFO("Testing PUP::er");
    test_pup_h<PUP::er>();
    test_pup_stl_h<PUP::er>();
  }
#endif
}
#endif
