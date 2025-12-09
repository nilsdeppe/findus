// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Serialize/Stl/ForwardList.hpp"

#if defined(FINDUS_ENABLE_TESTING)

#include <array>
#include <doctest/doctest.h>
#include <forward_list>
#include <memory>
#include <vector>

#include "findus/Serialize/Serializer.hpp"
#include "findus/Serialize/Stl/Array.hpp"
#include "findus/Serialize/Stl/Vector.hpp"

namespace findus::serialize {
namespace {
struct NoSerialize {};
static_assert(is_serializable_v<std::forward_list<int>>);
static_assert(is_serializable_v<int>);
static_assert(not is_serializable_v<NoSerialize>);
static_assert(not is_serializable_v<std::forward_list<NoSerialize>>);
}  // namespace

TEST_CASE("Serialize.ForwardList") {
  // Test with fundamental type
  {
    std::forward_list<int> dq{1, 2, 3, 4, 5};

    // Sizing
    Serializer sizer{Serializer::Sizing};
    sizer | dq;
    CHECK(sizer.number_of_bytes() == 28);

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | dq;

    // Unpacking
    std::forward_list<int> dq_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | dq_unpacked;

    CHECK(dq == dq_unpacked);
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
    std::forward_list<MyBytesType> dq{MyBytesType{1, 1.1}, MyBytesType{2, 2.2},
                                      MyBytesType{3, 3.3}};

    // Sizing
    Serializer sizer{Serializer::Sizing};
    sizer | dq;
    CHECK(sizer.number_of_bytes() == 56);

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | dq;

    // Unpacking
    std::forward_list<MyBytesType> dq_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | dq_unpacked;

    CHECK(dq == dq_unpacked);
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
        return id == other.id && value == other.value;
      }
    };

    std::forward_list<ComplexType> dq{{1, 3.14}, {2, 2.71}};

    // Sizing
    Serializer sizer{Serializer::Sizing};
    sizer | dq;
    CHECK(sizer.number_of_bytes() > 0);

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | dq;

    // Unpacking
    std::forward_list<ComplexType> dq_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | dq_unpacked;

    CHECK(dq == dq_unpacked);
  }

  // // Test with
  // std::forward_list<std::vector<std::forward_list<SimpleStruct>>>
  // {
  //   struct SimpleStruct {
  //     int value = 0;
  //     void pup(Serializer& s) { s | value; }
  //     bool operator==(const SimpleStruct& other) const {
  //       return value == other.value;
  //     }
  //   };

  //   std::forward_list<std::vector<std::forward_list<SimpleStruct>>> dq(3);
  //   for (size_t i = 0; i < dq.size(); ++i) {
  //     dq[i].resize(2);
  //     for (size_t j = 0; j < dq[i].size(); ++j) {
  //       dq[i][j].resize(3);
  //       for (size_t k = 0; k < dq[i][j].size(); ++k) {
  //         dq[i][j][k].value = static_cast<int>(i * 100 + j * 10 + k);
  //       }
  //     }
  //   }

  //   // Sizing
  //   Serializer sizer{Serializer::Sizing};
  //   sizer | dq;
  //   CHECK(sizer.number_of_bytes() > 0);

  //   // Packing
  //   std::unique_ptr<std::byte[]> buffer{new
  //   std::byte[sizer.number_of_bytes()]}; Serializer
  //   packer{Serializer::Packing, buffer.get(),
  //                     sizer.number_of_bytes()};
  //   packer | dq;

  //   // Unpacking
  //   std::forward_list<std::vector<std::forward_list<SimpleStruct>>>
  //   dq_unpacked; Serializer unpacker{Serializer::Unpacking, buffer.get(),
  //                       sizer.number_of_bytes()};
  //   unpacker | dq_unpacked;

  //   CHECK(dq == dq_unpacked);
  // }

  // // Test with std::forward_list<std::array<std::forward_list<SimpleStruct>,
  // 3>>
  // {
  //   struct SimpleStruct {
  //     int value = 0;
  //     void pup(Serializer& s) { s | value; }
  //     bool operator==(const SimpleStruct& other) const {
  //       return value == other.value;
  //     }
  //   };

  //   std::forward_list<std::array<std::forward_list<SimpleStruct>, 3>> dq(2);
  //   for (size_t i = 0; i < dq.size(); ++i) {
  //     for (size_t j = 0; j < dq[i].size(); ++j) {
  //       dq[i][j].resize(2 + i + j);
  //       for (size_t k = 0; k < dq[i][j].size(); ++k) {
  //         dq[i][j][k].value = static_cast<int>(i * 100 + j * 10 + k);
  //       }
  //     }
  //   }

  //   // Sizing
  //   Serializer sizer{Serializer::Sizing};
  //   sizer | dq;
  //   CHECK(sizer.number_of_bytes() > 0);

  //   // Packing
  //   std::unique_ptr<std::byte[]> buffer{new
  //   std::byte[sizer.number_of_bytes()]}; Serializer
  //   packer{Serializer::Packing, buffer.get(),
  //                     sizer.number_of_bytes()};
  //   packer | dq;

  //   // Unpacking
  //   std::forward_list<std::array<std::forward_list<SimpleStruct>, 3>>
  //       dq_unpacked;
  //   Serializer unpacker{Serializer::Unpacking, buffer.get(),
  //                       sizer.number_of_bytes()};
  //   unpacker | dq_unpacked;

  //   CHECK(dq == dq_unpacked);
  // }
}
}  // namespace findus::serialize
#endif
