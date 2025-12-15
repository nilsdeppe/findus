// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Serialize/Stl/UniquePtr.hpp"

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <memory>

#include "findus/Serialize/Stl/Vector.hpp"

namespace findus::serialize {
namespace {
struct NoSerialize {};
static_assert(is_serializable_v<std::unique_ptr<int>>);
static_assert(is_serializable_v<int>);
static_assert(not is_serializable_v<NoSerialize>);
static_assert(not is_serializable_v<std::unique_ptr<NoSerialize>>);
}  // namespace

namespace {
class BaseLeft : public virtual findus::serialize::SerializableBase<BaseLeft> {
 public:
  ~BaseLeft() override = default;

  BaseLeft(Serializer& s) { s | left_data; }

  BaseLeft(int in_data) : left_data(in_data) {}

  virtual Serializer& serialize(Serializer& s) { return s | left_data; }

#if defined(FINDUS_MIMIC_CHARM_PUPER)
  virtual void pup(PUP::er& p) { p | left_data; }
#endif

  int left_data = -2;
};

class BaseRight
    : public virtual findus::serialize::SerializableBase<BaseRight> {
 public:
  ~BaseRight() override = default;

  BaseRight(Serializer& s) { s | right_data; }

  BaseRight(int in_data) : right_data(in_data) {}

  virtual Serializer& serialize(Serializer& s) { return s | right_data; }

#if defined(FINDUS_MIMIC_CHARM_PUPER)
  virtual void pup(PUP::er& p) { p | right_data; }
#endif

  int right_data = -2;
};

class Derived
    : public findus::serialize::SerializableDerived<Derived, BaseLeft>,
      public BaseLeft,
      public findus::serialize::SerializableDerived<Derived, BaseRight>,
      public BaseRight {
 public:
  Derived(Serializer& s) : BaseLeft(s), BaseRight(s) { s | data; }

  Derived(int in_data)
      : BaseLeft(2 * in_data), BaseRight(3 * in_data), data(in_data) {}

  Serializer& serialize(Serializer& s) override {
    BaseLeft::serialize(s);
    BaseRight::serialize(s);
    return s | data;
  }

#if defined(FINDUS_MIMIC_CHARM_PUPER)
  void pup(PUP::er& p) override {
    BaseLeft::pup(p);
    BaseRight::pup(p);
    p | data;
  }
#endif

  int data = -1;
};

template <class Cast>
void test_abstract() {
  std::unique_ptr<BaseLeft> my_derived_left{new Derived(17)};
  std::unique_ptr<BaseRight> my_derived_right{new Derived(13)};
  Serializer sizer{Serializer::Sizing};
  static_cast<Cast&>(sizer) | my_derived_left;
  static_cast<Cast&>(sizer) | my_derived_right;
  CHECK(sizer.number_of_bytes() >= 12);

  std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
  Serializer packer{Serializer::Packing, buffer.get(), sizer.number_of_bytes()};
  static_cast<Cast&>(packer) | my_derived_left;
  static_cast<Cast&>(packer) | my_derived_right;

  // Unpacking
  Serializer unpacker{Serializer::Unpacking, buffer.get(),
                      sizer.number_of_bytes()};
  std::unique_ptr<BaseLeft> my_derived_left_unpacked{};
  std::unique_ptr<BaseRight> my_derived_right_unpacked{};
  static_cast<Cast&>(unpacker) | my_derived_left_unpacked;
  static_cast<Cast&>(unpacker) | my_derived_right_unpacked;

  Derived* my_derived_left_unpacked_ptr =
      dynamic_cast<Derived*>(my_derived_left_unpacked.get());
  REQUIRE(my_derived_left_unpacked_ptr != nullptr);
  CHECK(my_derived_left_unpacked_ptr->data == 17);
  CHECK(my_derived_left_unpacked_ptr->left_data == 2 * 17);
  CHECK(my_derived_left_unpacked_ptr->right_data == 3 * 17);
  Derived* my_derived_right_unpacked_ptr =
      dynamic_cast<Derived*>(my_derived_right_unpacked.get());
  REQUIRE(my_derived_right_unpacked_ptr != nullptr);
  CHECK(my_derived_right_unpacked_ptr->data == 13);
  CHECK(my_derived_right_unpacked_ptr->left_data == 2 * 13);
  CHECK(my_derived_right_unpacked->right_data == 3 * 13);
}

template <class Cast>
void test_concrete() {
  std::unique_ptr<std::vector<double>> unique_vec{
      new std::vector<double>{1.1, 2.3, 3.2, 4.5, 7.6}};
  std::unique_ptr<double> unique_null = nullptr;
  Serializer sizer{Serializer::Sizing};
  static_cast<Cast&>(sizer) | unique_vec;
  static_cast<Cast&>(sizer) | unique_null;
  CHECK(sizer.number_of_bytes() >= 5 * sizeof(double) + 2 * sizeof(size_t));

  std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
  Serializer packer{Serializer::Packing, buffer.get(), sizer.number_of_bytes()};
  static_cast<Cast&>(packer) | unique_vec;
  static_cast<Cast&>(packer) | unique_null;

  Serializer unpacker{Serializer::Unpacking, buffer.get(),
                      sizer.number_of_bytes()};
  std::unique_ptr<std::vector<double>> unique_vec_unpacked{};
  std::unique_ptr<double> unique_null_unpacked{new double(3.5)};
  CHECK(*unique_null_unpacked == 3.5);
  static_cast<Cast&>(unpacker) | unique_vec_unpacked;
  static_cast<Cast&>(unpacker) | unique_null_unpacked;
  REQUIRE(unique_vec_unpacked != nullptr);
  CHECK(*unique_vec_unpacked == *unique_vec);
  CHECK(unique_null_unpacked == nullptr);
}

template <class Cast>
void test() {
  test_abstract<Cast>();
  test_concrete<Cast>();
}
}  // namespace

TEST_CASE("Serialize.UniquePtr") {
  test<Serializer>();
#ifdef FINDUS_MIMIC_CHARM_PUPER
  test<PUP::er>();
#endif
}
}  // namespace findus::serialize
#endif
