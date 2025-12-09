// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Serialize/Virtual.hpp"

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <memory>
#include <string>

#include "findus/Serialize/Serializer.hpp"

namespace findus::serialize {
namespace {
namespace no_data_in_base {
//! [SerializableBaseNoDataInBase]
class Base0 : public virtual findus::serialize::SerializableBase<Base0> {
 public:
  ~Base0() override = default;

  virtual Serializer& serialize(Serializer& s) = 0;

#if defined(FINDUS_MIMIC_CHARM_PUPER)
  virtual void pup(PUP::er& p) = 0;
#endif
};
//! [SerializableBaseNoDataInBase]

//! [SerializableDerivedNoDataInBase]
class Derived0 : public findus::serialize::SerializableDerived<Derived0, Base0>,
                 public Base0 {
 public:
  Derived0() = default;
  Derived0(const int in_data) : data_(in_data) {}

  Serializer& serialize(Serializer& s) override { return s | data_; }

#if defined(FINDUS_MIMIC_CHARM_PUPER)
  void pup(PUP::er& p) override { p | data_; }
#endif

  int data() const { return data_; }

 private:
  int data_ = -1;
};
//! [SerializableDerivedNoDataInBase]

class Derived1 : public findus::serialize::SerializableDerived<Derived1, Base0>,
                 public Base0 {
 public:
  Derived1() = default;
  Derived1(const int in_data, const int in_data2)
      : data_(in_data), data2_(in_data2) {}

  Serializer& serialize(Serializer& s) override { return s | data_ | data2_; }

#if defined(FINDUS_MIMIC_CHARM_PUPER)
  void pup(PUP::er& p) override {
    p | data_;
    p | data2_;
  }
#endif

  int data() const { return data_; }
  int data2() const { return data2_; }

 private:
  int data_ = -1;
  int data2_ = -2;
};

void test() {
  std::unique_ptr<Base0> my_derived0{new Derived0(17)};
  std::unique_ptr<Base0> my_derived1{new Derived1(13, 15)};
  Serializer sizer{Serializer::Sizing};
  serialize_abstract_base(sizer, my_derived0.get());
  serialize_abstract_base(sizer, my_derived1.get());
  CHECK(sizer.number_of_bytes() >= 28);

  std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
  Serializer packer{Serializer::Packing, buffer.get(), sizer.number_of_bytes()};
  serialize_abstract_base(packer, my_derived0.get());
  serialize_abstract_base(packer, my_derived1.get());

  // Unpacking
  Serializer unpacker{Serializer::Unpacking, buffer.get(),
                      sizer.number_of_bytes()};
  std::unique_ptr<Base0> my_derived0_unpacked{
      deserialize_abstract_base<Base0>(unpacker)};
  std::unique_ptr<Base0> my_derived1_unpacked{
      deserialize_abstract_base<Base0>(unpacker)};

  Derived0* my_derived0_unpacked_ptr =
      dynamic_cast<Derived0*>(my_derived0_unpacked.get());
  REQUIRE(my_derived0_unpacked_ptr != nullptr);
  CHECK(my_derived0_unpacked_ptr->data() == 17);
  Derived1* my_derived1_unpacked_ptr =
      dynamic_cast<Derived1*>(my_derived1_unpacked.get());
  REQUIRE(my_derived1_unpacked_ptr != nullptr);
  CHECK(my_derived1_unpacked_ptr->data() == 13);
  CHECK(my_derived1_unpacked_ptr->data2() == 15);
}
}  // namespace no_data_in_base

namespace data_in_base {
//! [SerializableBaseDataInBase]
class Base0 : public virtual findus::serialize::SerializableBase<Base0> {
 public:
  ~Base0() override = default;
  Base0() = default;
  Base0(const int in_data) : data_base_(in_data) {}

  virtual Serializer& serialize(Serializer& s) { return s | data_base_; }

#if defined(FINDUS_MIMIC_CHARM_PUPER)
  virtual void pup(PUP::er& p) { p | data_base_; }
#endif

  int data_base_ = -3;
};
//! [SerializableBaseDataInBase]

//! [SerializableDerivedDataInBase]
class Derived0 : public findus::serialize::SerializableDerived<Derived0, Base0>,
                 public Base0 {
 public:
  Derived0() = default;
  Derived0(const int in_data) : Base0(3 * in_data), data_(in_data) {}

  Serializer& serialize(Serializer& s) override {
    Base0::serialize(s);
    return s | data_;
  }

#if defined(FINDUS_MIMIC_CHARM_PUPER)
  void pup(PUP::er& p) override {
    Base0::pup(p);
    p | data_;
  }
#endif

  int data() const { return data_; }

 private:
  int data_ = -1;
};
//! [SerializableDerivedDataInBase]

class Derived1 : public findus::serialize::SerializableDerived<Derived1, Base0>,
                 public Base0 {
 public:
  Derived1() = default;
  Derived1(const int in_data, const int in_data2)
      : Base0(2 * in_data), data_(in_data), data2_(in_data2) {}

  Serializer& serialize(Serializer& s) override {
    Base0::serialize(s);
    return s | data_ | data2_;
  }

#if defined(FINDUS_MIMIC_CHARM_PUPER)
  void pup(PUP::er& p) override {
    Base0::pup(p);
    p | data_;
    p | data2_;
  }
#endif

  int data() const { return data_; }
  int data2() const { return data2_; }

 private:
  int data_ = -1;
  int data2_ = -2;
};

void test() {
  std::unique_ptr<Base0> my_derived0{new Derived0(17)};
  std::unique_ptr<Base0> my_derived1{new Derived1(13, 15)};
  Serializer sizer{Serializer::Sizing};
  serialize_abstract_base(sizer, my_derived0.get());
  serialize_abstract_base(sizer, my_derived1.get());
  CHECK(sizer.number_of_bytes() >= 36);

  std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
  Serializer packer{Serializer::Packing, buffer.get(), sizer.number_of_bytes()};
  serialize_abstract_base(packer, my_derived0.get());
  serialize_abstract_base(packer, my_derived1.get());

  // Unpacking
  Serializer unpacker{Serializer::Unpacking, buffer.get(),
                      sizer.number_of_bytes()};
  std::unique_ptr<Base0> my_derived0_unpacked{
      deserialize_abstract_base<Base0>(unpacker)};
  std::unique_ptr<Base0> my_derived1_unpacked{
      deserialize_abstract_base<Base0>(unpacker)};

  Derived0* my_derived0_unpacked_ptr =
      dynamic_cast<Derived0*>(my_derived0_unpacked.get());
  REQUIRE(my_derived0_unpacked_ptr != nullptr);
  CHECK(my_derived0_unpacked_ptr->data() == 17);
  CHECK(my_derived0_unpacked->data_base_ == 3 * 17);
  Derived1* my_derived1_unpacked_ptr =
      dynamic_cast<Derived1*>(my_derived1_unpacked.get());
  REQUIRE(my_derived1_unpacked_ptr != nullptr);
  CHECK(my_derived1_unpacked_ptr->data() == 13);
  CHECK(my_derived1_unpacked_ptr->data2() == 15);
  CHECK(my_derived1_unpacked->data_base_ == 2 * 13);
}
}  // namespace data_in_base

namespace name_in_class {
//! [DerivedWithName]]
struct A0 {
  static std::string findus_serializable_name() { return "A0"; }
};

struct A1 {
  static std::string findus_serializable_name() { return "A1"; }
};

struct A2 {
  static std::string findus_serializable_name() { return "A2"; }
};
//! [DerivedWithName]]

class Base0 : public virtual findus::serialize::SerializableBase<Base0> {
 public:
  ~Base0() override = default;

  virtual Serializer& serialize(Serializer& s) { return s; }

#if defined(FINDUS_MIMIC_CHARM_PUPER)
  virtual void pup(PUP::er& p) = 0;
#endif
};

//! [DerivedTemplateWithName]]
template <class... Ts>
class Derived
    : public findus::serialize::SerializableDerived<Derived<Ts...>, Base0>,
      public Base0 {
 public:
  static std::string findus_serializable_name() {
    return (... + Ts::findus_serializable_name());
  }

  Serializer& serialize(Serializer& s) override { return s; }

#if defined(FINDUS_MIMIC_CHARM_PUPER)
  void pup(PUP::er& /*p*/) override {}
#endif
};
//! [DerivedTemplateWithName]]

void test() {
  CHECK(Derived<A0>::findus_serializable_name() ==
        A0::findus_serializable_name());
  CHECK(serializable_hash<Derived<A0>>() == serializable_hash<A0>());
  CHECK(serializable_hash<Derived<A0>>() ==
        detail::hash(A0::findus_serializable_name()));
  CHECK(serializable_hash<Derived<A1>>() != serializable_hash<A0>());
  CHECK(serializable_hash<Derived<A1>>() == serializable_hash<A1>());
  CHECK(serializable_hash<Derived<A1>>() ==
        detail::hash(A1::findus_serializable_name()));
  CHECK(serializable_hash<Derived<A2>>() != serializable_hash<A0>());
  CHECK(serializable_hash<Derived<A1>>() != serializable_hash<A2>());

  CHECK(serializable_hash<Derived<A0, A1>>() ==
        detail::hash(A0::findus_serializable_name() +
                     A1::findus_serializable_name()));
  CHECK(serializable_hash<Derived<A0, A1, A2>>() ==
        detail::hash(A0::findus_serializable_name() +
                     A1::findus_serializable_name() +
                     A2::findus_serializable_name()));
}
}  // namespace name_in_class

namespace triangle_hierarchy {
//! [MultipleBase]
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
//! [MultipleBase]

//! [MultipleBaseDerived]
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
//! [MultipleBaseDerived]

void test() {
  std::unique_ptr<BaseLeft> my_derived_left{new Derived(17)};
  std::unique_ptr<BaseRight> my_derived_right{new Derived(13)};
  Serializer sizer{Serializer::Sizing};
  serialize_abstract_base(sizer, my_derived_left.get());
  serialize_abstract_base(sizer, my_derived_right.get());
  CHECK(sizer.number_of_bytes() >= 12);

  std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
  Serializer packer{Serializer::Packing, buffer.get(), sizer.number_of_bytes()};
  serialize_abstract_base(packer, my_derived_left.get());
  serialize_abstract_base(packer, my_derived_right.get());

  // Unpacking
  Serializer unpacker{Serializer::Unpacking, buffer.get(),
                      sizer.number_of_bytes()};
  std::unique_ptr<BaseLeft> my_derived_left_unpacked{
      deserialize_abstract_base<BaseLeft>(unpacker)};
  std::unique_ptr<BaseRight> my_derived_right_unpacked{
      deserialize_abstract_base<BaseRight>(unpacker)};

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
}  // namespace triangle_hierarchy

#if defined(FINDUS_MIMIC_CHARM_PUPER)
namespace charm_interop_virtual {
class BaseLeft : public virtual findus::serialize::SerializableBase<BaseLeft> {
 public:
  BaseLeft() = default;
  ~BaseLeft() override = default;

  BaseLeft(int in_data) : left_data(in_data) {}

  virtual void pup(PUP::er& p) { p | left_data; }

  int left_data = -2;
};

class BaseRight
    : public virtual findus::serialize::SerializableBase<BaseRight> {
 public:
  BaseRight() = default;
  ~BaseRight() override = default;

  BaseRight(int in_data) : right_data(in_data) {}

  virtual void pup(PUP::er& p) { p | right_data; }

  int right_data = -2;
};

class Derived
    : public findus::serialize::SerializableDerived<Derived, BaseLeft>,
      public BaseLeft,
      public findus::serialize::SerializableDerived<Derived, BaseRight>,
      public BaseRight {
 public:
  Derived() = default;

  Derived(int in_data)
      : BaseLeft(2 * in_data), BaseRight(3 * in_data), data(in_data) {}

  void pup(PUP::er& p) override {
    BaseLeft::pup(p);
    BaseRight::pup(p);
    p | data;
  }

  int data = -1;
};

void test() {
  std::unique_ptr<BaseLeft> my_derived_left{new Derived(17)};
  std::unique_ptr<BaseRight> my_derived_right{new Derived(13)};
  Serializer sizer{Serializer::Sizing};
  serialize_abstract_base(sizer, my_derived_left.get());
  serialize_abstract_base(sizer, my_derived_right.get());
  CHECK(sizer.number_of_bytes() >= 12);

  std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
  Serializer packer{Serializer::Packing, buffer.get(), sizer.number_of_bytes()};
  serialize_abstract_base(packer, my_derived_left.get());
  serialize_abstract_base(packer, my_derived_right.get());

  // Unpacking
  Serializer unpacker{Serializer::Unpacking, buffer.get(),
                      sizer.number_of_bytes()};
  std::unique_ptr<BaseLeft> my_derived_left_unpacked{
      deserialize_abstract_base<BaseLeft>(unpacker)};
  std::unique_ptr<BaseRight> my_derived_right_unpacked{
      deserialize_abstract_base<BaseRight>(unpacker)};

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
}  // namespace charm_interop_virtual
#endif

TEST_CASE("Serialize.Virtual") {
  no_data_in_base::test();
  data_in_base::test();
  name_in_class::test();
  triangle_hierarchy::test();
#if defined(FINDUS_MIMIC_CHARM_PUPER)
  charm_interop_virtual::test();
#endif
}
}  // namespace
}  // namespace findus::serialize
#endif
