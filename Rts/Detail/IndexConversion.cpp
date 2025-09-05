// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Detail/IndexConversion.hpp"

#if defined(RTS_ENABLE_TESTING)

#include <cstring>
#include <doctest/doctest.h>
#include <initializer_list>

#include "Rts/DistributedObjectCollection.hpp"

namespace rts::detail {
namespace {
struct TestUserIndex {
  // 20 bits, 20 bits, 24 bits = 64 bits total
  std::uint64_t a : 20;
  std::uint64_t b : 20;
  std::uint64_t c : 24;

  // Zero-initialize all bits, then set fields
  TestUserIndex(const std::uint32_t a_in, const std::uint32_t b_in,
                const std::uint32_t c_in) {
    std::memset(this, 0, sizeof(*this));
    a = a_in;
    b = b_in;
    c = c_in;
  }

  // Default constructor, zero-initialize
  TestUserIndex() { std::memset(this, 0, sizeof(*this)); }

  bool operator==(const TestUserIndex& other) const {
    return a == other.a and b == other.b and c == other.c;
  }
  bool operator!=(const TestUserIndex& other) const {
    return not(*this == other);
  }
};

// For static_assert in from_internal
struct DummyParallelComponentForIndexConversion
    : rts::DistributedObjectCollection<
          DummyParallelComponentForIndexConversion> {
  using rts_collection_index = TestUserIndex;
};

struct DummyComponent1
    : rts::DistributedObject<DummyParallelComponentForIndexConversion> {
  using rts_collection_index = TestUserIndex;
};

struct DummyComponent2 {
  using rts_collection_index = TestUserIndex;
};

static_assert(std::is_same_v<
              get_collection_index<DummyParallelComponentForIndexConversion>,
              TestUserIndex>);
static_assert(std::is_same_v<get_collection_index<DummyComponent1>, int>);
static_assert(std::is_same_v<get_collection_index<DummyComponent2>, int>);
}  // namespace

TEST_CASE("UserIndexToFromInternalIndex") {
  // This test verifies that the to_internal and from_internal functions
  // correctly
  // convert a user-defined 64-bit index type (using bit fields) to a uint64_t
  // and back. It checks a variety of edge values for each bit field to ensure
  // all bits are preserved. The rationale is to guarantee lossless round-trip
  // conversion for user index types that are packed into 64 bits, as required
  // by the runtime system.
  //
  // Note bit masks are:
  // 20 bits: 0b11111111111111111111 = 0xFFFFF
  // 24 bits: 0b111111111111111111111111 = 0xFFFFFF
  for (const std::uint32_t a : {0u, 0b1u, 0b11111111111111111111u}) {
    for (const std::uint32_t b : {0u, 0b10u, 0b11111111111111111111u}) {
      for (const std::uint32_t c : {0u, 0b11u, 0b111111111111111111111111u}) {
        const TestUserIndex original(a, b, c);
        const std::uint64_t internal = to_internal(original);
        const TestUserIndex roundtrip =
            from_internal<DummyParallelComponentForIndexConversion>(internal);
        CHECK(roundtrip == original);
        CHECK(roundtrip.a == a);
        CHECK(roundtrip.b == b);
        CHECK(roundtrip.c == c);
      }
    }
  }
}
}  // namespace rts::detail
#endif
