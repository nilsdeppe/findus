// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Serialize/Serializer.hpp"

#include <cstddef>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace findus::serialize {
Serializer::Serializer(Sizing_t /*selector*/, const std::uint64_t extra_info)
    : extra_info_(extra_info), action_(Action::Sizing) {}

Serializer::Serializer(Packing_t /*selector*/, std::byte* buffer,
                       size_t buffer_size, std::uint64_t extra_info)
    : extra_info_(extra_info),
      action_(Action::Packing),
      start_pointer_(buffer),
      current_pointer_(buffer),
      end_pointer_(
          std::next(buffer, static_cast<std::ptrdiff_t>(buffer_size))) {}

Serializer::Serializer(Unpacking_t /*selector*/, std::byte* buffer,
                       size_t buffer_size, std::uint64_t extra_info)
    : extra_info_(extra_info),
      action_(Action::Unpacking),
      start_pointer_(buffer),
      current_pointer_(buffer),
      end_pointer_(
          std::next(buffer, static_cast<std::ptrdiff_t>(buffer_size))) {}

Serializer::Serializer(Unpacking_t /*selector*/, const std::byte* buffer,
                       size_t buffer_size, std::uint64_t extra_info)
    : extra_info_(extra_info),
      action_(Action::Unpacking),
      start_pointer_(const_cast<std::byte*>(buffer)),
      current_pointer_(const_cast<std::byte*>(buffer)),
      end_pointer_(const_cast<std::byte*>(
          std::next(buffer, static_cast<std::ptrdiff_t>(buffer_size)))) {}

Serializer::Serializer(MemoryFootprinting_t /*selector*/,
                       const std::uint64_t extra_info)
    : extra_info_(extra_info), action_(Action::MemoryFootprinting) {}

void Serializer::bytes(Bytes data) {
  const std::size_t bytes_to_copy =
      static_cast<size_t>(data.size_of_item_ * data.number_of_items_);
  number_of_bytes_ += bytes_to_copy;
  switch (action()) {
    case Action::Sizing:
      return;
    case Action::Packing:
      std::memcpy(current_pointer_, data.item_, bytes_to_copy);
      std::advance(current_pointer_, bytes_to_copy);
      return;
    case Action::Unpacking:
      std::memcpy(data.item_, current_pointer_, bytes_to_copy);
      std::advance(current_pointer_, bytes_to_copy);
      return;
    case Action::MemoryFootprinting:
      number_of_bytes_ += bytes_to_copy;
      return;
    default:
      throw std::runtime_error{
          "Must use Serializer with Action Sizing, Packing, Unpacking, or "
          "MemoryFootprinting. The integer value of the current Action is " +
          std::to_string(
              static_cast<std::underlying_type_t<Action>>(action()))};
  };
}
}  // namespace findus::serialize

#ifdef FINDUS_MIMIC_CHARM_PUPER
namespace PUP {
bool er::isPacking() const {
  return static_cast<const findus::serialize::Serializer*>(this)->isPacking();
}

bool er::isSizing() const {
  return static_cast<const findus::serialize::Serializer*>(this)->isSizing();
}

bool er::isUnpacking() const {
  return static_cast<const findus::serialize::Serializer*>(this)->isUnpacking();
}
}  // namespace PUP
#endif

#if defined(FINDUS_ENABLE_TESTING)

#include <array>
#include <doctest/doctest.h>
#include <iterator>
#include <memory>
#include <ostream>
#include <vector>

namespace std {
template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& vec) {
  os << "[";
  for (size_t i = 0; i < vec.size(); ++i) {
    os << vec[i];
    if (i + 1 < vec.size()) {
      os << ", ";
    }
  }
  os << "]";
  return os;
}

}  // namespace std

namespace findus::serialize {
namespace {
// Test as_bytes trait
struct MyBytesType : as_bytes<void> {
  int a = 0;
  double b = 0.0;
  bool operator==(const MyBytesType& other) const {
    return a == other.a and b == other.b;
  }
};
struct MyBytesType2 {
  int a = 0;
  double b = 0.0;
  bool operator==(const MyBytesType2& other) const {
    return a == other.a and b == other.b;
  }
};
}  // namespace
template <>
struct as_bytes<MyBytesType2> : std::true_type {};

namespace {
struct NotBytesType {};

static_assert(findus::serialize::as_bytes<int>::value == false,
              "int should not be serialized as bytes by default");
static_assert(findus::serialize::as_bytes<MyBytesType>::value == false,
              "MyBytesType does not specialize as_bytes directly");
static_assert(findus::serialize::as_bytes<MyBytesType2>::value == true,
              "MyBytesType does not specialize as_bytes directly");
static_assert(std::is_base_of_v<findus::serialize::as_bytes<void>, MyBytesType>,
              "MyBytesType should inherit from as_bytes<void>");

// Test has_pup_member trait
struct PupType {
  // [serializer_pup_example]
  int value = 0;
  void pup(Serializer& s) { s | value; }
  // [serializer_pup_example]
  bool operator==(const PupType& other) const { return value == other.value; }
};
struct NoPupType {};

static_assert(findus::serialize::has_pup_member<PupType>::value == true,
              "PupType should have pup member");
static_assert(findus::serialize::has_pup_member<NoPupType>::value == false,
              "NoPupType should not have pup member");
static_assert(findus::serialize::has_pup_member_v<PupType> == true,
              "has_pup_member_v should be true for PupType");
static_assert(findus::serialize::has_pup_member_v<NoPupType> == false,
              "has_pup_member_v should be false for NoPupType");

// Test has_serialize_member trait
struct SerializeType {
  // [serializer_serialize_example]
  int value = 0;
  Serializer& serialize(Serializer& s) { return s | value; }
  // [serializer_serialize_example]
  bool operator==(const SerializeType& other) const {
    return value == other.value;
  }
};
struct NoSerializeType {};

static_assert(findus::serialize::has_serialize_member<SerializeType>::value ==
                  true,
              "SerializeType should have serialize member");
static_assert(findus::serialize::has_serialize_member<NoSerializeType>::value ==
                  false,
              "NoSerializeType should not have serialize member");
static_assert(findus::serialize::has_serialize_member_v<SerializeType> == true,
              "has_serialize_member_v should be true for SerializeType");
static_assert(findus::serialize::has_serialize_member_v<NoSerializeType> ==
                  false,
              "has_serialize_member_v should be false for NoSerializeType");

static_assert(findus::serialize::is_serializable_v<SerializeType>);
static_assert(findus::serialize::is_serializable_v<MyBytesType>);
static_assert(not findus::serialize::is_serializable_v<NoSerializeType>);
#if defined(FINDUS_MIMIC_CHARM_PUPER)
static_assert(findus::serialize::is_serializable_v<PupType>);
static_assert(not findus::serialize::is_serializable_v<NoPupType>);
#endif

template <class T>
void test_impl() {
  // [serializer_construct_sizing_extra_info]
  Serializer sizer{Serializer::Sizing, 111};
  // [serializer_construct_sizing_extra_info]
  CHECK(sizer.extra_info() == 111);
  CHECK(sizer.isSizing());
  CHECK_FALSE(sizer.isPacking());
  CHECK_FALSE(sizer.isUnpacking());
  CHECK_FALSE(sizer.isMemoryFootprinting());
  CHECK(sizer.action() == Action::Sizing);
  CHECK(sizer.action() != Action::Packing);
  CHECK(sizer.action() != Action::Unpacking);
  CHECK(sizer.action() != Action::MemoryFootprinting);

  // [serializer_call_view_array]
  std::array<T, 10> array{};
  std::iota(array.begin(), array.end(), static_cast<T>(10));
  sizer(View{array.data(), array.size()});
  // [serializer_call_view_array]
  REQUIRE(sizer.number_of_bytes() == array.size() * sizeof(T));
  // [serializer_call_view_vector]
  std::vector<T> vector(12);
  std::iota(vector.begin(), vector.end(), static_cast<T>(100));
  sizer(View{vector.data(), vector.size()});
  // [serializer_call_view_vector]
  REQUIRE(sizer.number_of_bytes() ==
          (array.size() + vector.size()) * sizeof(T));
  // [serializer_call_view_object]
  T value{static_cast<T>(200)};
  sizer(View{value});
  // [serializer_call_view_object]
  REQUIRE(sizer.number_of_bytes() ==
          (array.size() + vector.size() + 1) * sizeof(T));
  // [serializer_call_object]
  T value2{static_cast<T>(300)};
  sizer(value2);
  // [serializer_call_object]
  REQUIRE(sizer.number_of_bytes() ==
          (array.size() + vector.size() + 2) * sizeof(T));

  // [serializer_construct_packing_extra_info]
  std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
  Serializer packer{Serializer::Packing, buffer.get(), sizer.number_of_bytes(),
                    333};
  // [serializer_construct_packing_extra_info]
  CHECK(packer.extra_info() == 333);
  CHECK_FALSE(packer.isSizing());
  CHECK(packer.isPacking());
  CHECK_FALSE(packer.isUnpacking());
  CHECK_FALSE(packer.isMemoryFootprinting());
  CHECK(packer.action() != Action::Sizing);
  CHECK(packer.action() == Action::Packing);
  CHECK(packer.action() != Action::Unpacking);
  CHECK(packer.action() != Action::MemoryFootprinting);

  packer(View{array.data(), array.size()});
  REQUIRE(packer.number_of_bytes() == array.size() * sizeof(T));
  packer(View{vector.data(), vector.size()});
  REQUIRE(packer.number_of_bytes() ==
          (array.size() + vector.size()) * sizeof(T));
  CHECK(packer.start_pointer() == buffer.get());
  packer(View{value});
  REQUIRE(packer.number_of_bytes() ==
          (array.size() + vector.size() + 1) * sizeof(T));
  packer(value2);
  REQUIRE(packer.number_of_bytes() ==
          (array.size() + vector.size() + 2) * sizeof(T));

  const auto unpack_help = [&](auto make_const) {
    std::array<T, 10> array_unpacked{};
    std::vector<T> vector_unpacked(12);
    T value_unpacked{0};
    T value2_unpacked{0};
    std::conditional_t<decltype(make_const)::value, const std::byte*,
                       std::byte*>
        buff = buffer.get();
    // clang-format off
    // [serializer_construct_unpacking_extra_info]
Serializer unpacker{Serializer::Unpacking, buff,
                    sizer.number_of_bytes(), 222};
    // [serializer_construct_unpacking_extra_info]
    // clang-format on
    CHECK_FALSE(unpacker.isSizing());
    CHECK_FALSE(unpacker.isPacking());
    CHECK(unpacker.isUnpacking());
    CHECK_FALSE(unpacker.isMemoryFootprinting());
    CHECK(unpacker.action() != Action::Sizing);
    CHECK(unpacker.action() != Action::Packing);
    CHECK(unpacker.action() == Action::Unpacking);
    CHECK(unpacker.action() != Action::MemoryFootprinting);
    CHECK(unpacker.extra_info() == 222);

    unpacker(View{array_unpacked.data(), array_unpacked.size()});
    REQUIRE(unpacker.number_of_bytes() == array_unpacked.size() * sizeof(T));
    unpacker(View{vector_unpacked.data(), vector_unpacked.size()});
    REQUIRE(unpacker.number_of_bytes() ==
            (array_unpacked.size() + vector_unpacked.size()) * sizeof(T));
    unpacker(View{value_unpacked});
    REQUIRE(unpacker.number_of_bytes() ==
            (array_unpacked.size() + vector_unpacked.size() + 1) * sizeof(T));
    CHECK(unpacker.start_pointer() == buffer.get());
    unpacker(value2_unpacked);
    REQUIRE(unpacker.number_of_bytes() ==
            (array_unpacked.size() + vector_unpacked.size() + 2) * sizeof(T));
    CHECK(array_unpacked == array);
    CHECK(vector_unpacked == vector);
    CHECK(value_unpacked == value);
    CHECK(value2_unpacked == value2);
  };
  unpack_help(std::bool_constant<false>{});
  unpack_help(std::bool_constant<true>{});
}

#ifdef FINDUS_MIMIC_CHARM_PUPER
struct PuperType {
  // [serializer_puper_example]
  int value = 0;
  void pup(PUP::er& s) { s | value; }
  // [serializer_puper_example]
  bool operator==(const PuperType& other) const { return value == other.value; }
};
#endif

enum class MyEnum : uint8_t { A = 1, B = 2 };

void test_operator_pipe() {
  {
    // Enum test
    MyEnum e = MyEnum::B;

    // Sizing
    // [sizing_enum]
    Serializer sizer{Serializer::Sizing};
    sizer | e;
    CHECK(sizer.number_of_bytes() == sizeof(MyEnum));
    // [sizing_enum]

    // Packing
    // [packing_enum]
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | e;
    // [packing_enum]

    // Unpacking
    // [unpacking_enum]
    MyEnum e_unpacked = MyEnum::A;  // Create the enum to deserialize into
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | e_unpacked;
    CHECK(e == e_unpacked);
    // [unpacking_enum]
  }

  {
    // Fundamental type test
    int i = 42;
    double d = 3.14;

    // Sizing
    Serializer sizer{Serializer::Sizing};
    sizer | i;
    sizer | d;
    CHECK(sizer.number_of_bytes() == sizeof(int) + sizeof(double));

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | i | d;

    // Unpacking
    int i_unpacked = 0;
    double d_unpacked = 0.0;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | i_unpacked;
    unpacker | d_unpacked;

    CHECK(i == i_unpacked);
    CHECK(d == d_unpacked);
  }

  // Type with pup member
  {
    PupType obj;
    obj.value = 123;

    // Sizing
    Serializer sizer{Serializer::Sizing};
    sizer | obj;
    CHECK(sizer.number_of_bytes() == sizeof(int));

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | obj;

    // Unpacking
    PupType obj_unpacked;
    obj_unpacked.value = 0;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | obj_unpacked;

    CHECK(obj == obj_unpacked);
  }

  // Type with serialize member
  {
    SerializeType obj;
    obj.value = 456;

    // Sizing
    Serializer sizer{Serializer::Sizing};
    sizer | obj;
    CHECK(sizer.number_of_bytes() == sizeof(int));

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | obj;

    // Unpacking
    SerializeType obj_unpacked;
    obj_unpacked.value = 0;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | obj_unpacked;

    CHECK(obj == obj_unpacked);
  }

  // Type with as_bytes trait specialization
  {
    MyBytesType2 obj;
    obj.a = 789;
    obj.b = 1.23;

    // Sizing
    Serializer sizer{Serializer::Sizing};
    sizer | obj;
    CHECK(sizer.number_of_bytes() == sizeof(obj));

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | obj;

    // Unpacking
    MyBytesType2 obj_unpacked;
    obj_unpacked.a = 0;
    obj_unpacked.b = 0.0;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | obj_unpacked;

    CHECK(obj == obj_unpacked);
  }

  // Type with as_bytes inheritance
  {
    MyBytesType obj;
    obj.a = 321;
    obj.b = 4.56;

    // Sizing
    Serializer sizer{Serializer::Sizing};
    sizer | obj;
    CHECK(sizer.number_of_bytes() == sizeof(obj));

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | obj;

    // Unpacking
    MyBytesType obj_unpacked;
    obj_unpacked.a = 0;
    obj_unpacked.b = 0.0;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | obj_unpacked;

    CHECK(obj == obj_unpacked);
  }

#ifdef FINDUS_MIMIC_CHARM_PUPER
  // Type with pup member
  {
    PuperType obj;
    obj.value = 123;

    // Sizing
    Serializer sizer{Serializer::Sizing};
    sizer | obj;
    CHECK(sizer.number_of_bytes() == sizeof(int));

    Serializer sizer2{Serializer::Sizing};
    static_cast<PUP::er&>(sizer2) | obj;
    CHECK(sizer.number_of_bytes() == sizer2.number_of_bytes());

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | obj;

    std::unique_ptr<std::byte[]> buffer2{
        new std::byte[sizer2.number_of_bytes()]};
    Serializer packer2{Serializer::Packing, buffer2.get(),
                       sizer2.number_of_bytes()};
    static_cast<PUP::er&>(packer2) | obj;

    // Unpacking
    PuperType obj_unpacked;
    obj_unpacked.value = 0;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | obj_unpacked;
    CHECK(obj == obj_unpacked);

    PuperType obj_unpacked2;
    obj_unpacked2.value = 0;
    Serializer unpacker2{Serializer::Unpacking, buffer2.get(),
                         sizer2.number_of_bytes()};
    static_cast<PUP::er&>(unpacker2) | obj_unpacked2;
    CHECK(obj == obj_unpacked2);
  }
#endif
}

void test_noncopyable_nonmovable() {
  struct ComplexType {
    // [serializer_noncopy_nonmove]
    int id = 0;
    double value = 0.0;

    Serializer& serialize(Serializer& s) { return s | id | value; }

    ComplexType(int id_in, double value_in) : id(id_in), value(value_in) {}

    ComplexType(Serializer& s) { serialize(s); }

    ComplexType() = delete;
    ComplexType(ComplexType&&) = delete;
    ComplexType& operator=(ComplexType&&) = delete;
    ComplexType(const ComplexType&) = delete;
    ComplexType& operator=(const ComplexType&) = delete;
    // [serializer_noncopy_nonmove]

    bool operator==(const ComplexType& other) const {
      return id == other.id and value == other.value;
    }
  };
  static_assert(not is_serializer_constructible_v<double>);
  static_assert(not is_serializer_constructible_v<MyBytesType>);
  static_assert(is_serializer_constructible_v<ComplexType>);

  {
    ComplexType t{7, 4.32};
    // [serializer_construct_sizing]
    Serializer sizer{Serializer::Sizing};
    // [serializer_construct_sizing]
    sizer | t;
    // [serializer_construct_packing]
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    // [serializer_construct_packing]
    packer | t;

    // [serializer_construct_unpacking]
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    // [serializer_construct_unpacking]
    ComplexType t_unpacked{unpacker};
    CHECK(t_unpacked == t);
  }

  {
    // [complex_type_size_pack_unpack]
    ComplexType t{7, 4.32};
    // Create sizer
    Serializer sizer{Serializer::Sizing};
    // Compute size
    sizer | t;

    // Allocate buffer
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    // Create packer
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    // Use operator| to pack
    packer | t;

    // Create unpacker, e.g. on receiving side
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    // Deserialize directly into the non-copyable and non-movable type.
    // Here ComplexType implements a constructor `ComplexType(Serializer& s);`
    ComplexType t_unpacked{unpacker};
    CHECK(t_unpacked == t);
    // [complex_type_size_pack_unpack]
  }
}
}  // namespace

TEST_CASE("Serialize.Serializer") {
  test_impl<float>();
  test_impl<double>();
  test_impl<int>();
  test_impl<int8_t>();
  test_impl<int16_t>();
  test_impl<int32_t>();
  test_impl<int64_t>();
  test_impl<uint8_t>();
  test_impl<uint16_t>();
  test_impl<uint32_t>();
  test_impl<uint64_t>();

  test_operator_pipe();
  test_noncopyable_nonmovable();
}
}  // namespace findus::serialize

#endif
