// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Serialize/Stl/Deque.hpp"

#if defined(FINDUS_ENABLE_TESTING)

#include <array>
#include <deque>
#include <doctest/doctest.h>
#include <memory>
#include <vector>

#include "findus/Serialize/Serializer.hpp"
#include "findus/Serialize/Stl/Array.hpp"
#include "findus/Serialize/Stl/Vector.hpp"

namespace findus::serialize {
namespace {
struct NoSerialize {};
static_assert(is_serializable_v<std::deque<int>>);
static_assert(is_serializable_v<int>);
static_assert(not is_serializable_v<NoSerialize>);
static_assert(not is_serializable_v<std::deque<NoSerialize>>);

template <class Cast>
void test() {
  // Test with fundamental type
  {
    std::deque<int> dq{1, 2, 3, 4, 5};

    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | dq;
    CHECK(sizer.number_of_bytes() > 0);

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | dq;

    // Unpacking
    std::deque<int> dq_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | dq_unpacked;

    CHECK(dq == dq_unpacked);
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

    std::deque<MyBytesType> dq(3);
    dq[0].a = 1;
    dq[0].b = 1.1;
    dq[1].a = 2;
    dq[1].b = 2.2;
    dq[2].a = 3;
    dq[2].b = 3.3;

    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | dq;
    CHECK(sizer.number_of_bytes() > 0);

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | dq;

    // Unpacking
    std::deque<MyBytesType> dq_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | dq_unpacked;

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
        return id == other.id and value == other.value;
      }
    };

    std::deque<ComplexType> dq(2);
    dq[0].id = 1;
    dq[0].value = 3.14;
    dq[1].id = 2;
    dq[1].value = 2.71;

    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | dq;
    CHECK(sizer.number_of_bytes() > 0);

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | dq;

    // Unpacking
    std::deque<ComplexType> dq_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | dq_unpacked;

    CHECK(dq == dq_unpacked);
  }

  // Test with std::deque<std::vector<std::deque<SimpleStruct>>>
  {
    struct SimpleStruct {
      int value = 0;
      void pup(Serializer& s) { s | value; }
      bool operator==(const SimpleStruct& other) const {
        return value == other.value;
      }
    };

    std::deque<std::vector<std::deque<SimpleStruct>>> dq(3);
    for (size_t i = 0; i < dq.size(); ++i) {
      dq[i].resize(2);
      for (size_t j = 0; j < dq[i].size(); ++j) {
        dq[i][j].resize(3);
        for (size_t k = 0; k < dq[i][j].size(); ++k) {
          dq[i][j][k].value = static_cast<int>(i * 100 + j * 10 + k);
        }
      }
    }

    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | dq;
    CHECK(sizer.number_of_bytes() > 0);

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | dq;

    // Unpacking
    std::deque<std::vector<std::deque<SimpleStruct>>> dq_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | dq_unpacked;

    CHECK(dq == dq_unpacked);
  }

  // Test with std::deque<std::array<std::deque<SimpleStruct>, 3>>
  {
    struct SimpleStruct {
      int value = 0;
      void pup(Serializer& s) { s | value; }
      bool operator==(const SimpleStruct& other) const {
        return value == other.value;
      }
    };

    std::deque<std::array<std::deque<SimpleStruct>, 3>> dq(2);
    for (size_t i = 0; i < dq.size(); ++i) {
      for (size_t j = 0; j < dq[i].size(); ++j) {
        dq[i][j].resize(2 + i + j);
        for (size_t k = 0; k < dq[i][j].size(); ++k) {
          dq[i][j][k].value = static_cast<int>(i * 100 + j * 10 + k);
        }
      }
    }

    // Sizing
    Serializer sizer{Serializer::Sizing};
    static_cast<Cast&>(sizer) | dq;
    CHECK(sizer.number_of_bytes() > 0);

    // Packing
    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    static_cast<Cast&>(packer) | dq;

    // Unpacking
    std::deque<std::array<std::deque<SimpleStruct>, 3>> dq_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    static_cast<Cast&>(unpacker) | dq_unpacked;

    CHECK(dq == dq_unpacked);
  }
}
}  // namespace

TEST_CASE("Serialize.Deque") {
  test<Serializer>();
#ifdef FINDUS_MIMIC_CHARM_PUPER
  test<PUP::er>();
#endif
}
}  // namespace findus::serialize
#endif
