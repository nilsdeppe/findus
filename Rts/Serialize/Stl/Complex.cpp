// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Serialize/Stl/Complex.hpp"

#if defined(RTS_ENABLE_TESTING)

#include <complex>
#include <doctest/doctest.h>
#include <memory>

#include "Rts/Serialize/Serializer.hpp"

namespace rts::serialize {
namespace {
struct NoSerialize {};
static_assert(is_serializable_v<std::complex<int>>);
static_assert(is_serializable_v<int>);
static_assert(not is_serializable_v<NoSerialize>);
static_assert(not is_serializable_v<std::complex<NoSerialize>>);
}  // namespace

TEST_CASE("Serialize.Complex") {
  // std::complex<double>
  {
    std::complex<double> c{3.14, 2.71};
    static_assert(serialize_as_bytes_v<std::complex<double>>);

    Serializer sizer{Serializer::Sizing};
    sizer | c;
    CHECK(sizer.number_of_bytes() == sizeof(std::complex<double>));

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | c;

    std::complex<double> c_unpacked{};
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | c_unpacked;

    CHECK(c == c_unpacked);
  }

  // std::complex<float>
  {
    std::complex<float> c{1.23f, 4.56f};
    static_assert(serialize_as_bytes_v<std::complex<float>>);

    Serializer sizer{Serializer::Sizing};
    sizer | c;
    CHECK(sizer.number_of_bytes() == sizeof(std::complex<float>));

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | c;

    std::complex<float> c_unpacked{};
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | c_unpacked;

    CHECK(c == c_unpacked);
  }

  // std::complex<MyDouble> with serialize member
  {
    struct MyDouble {
      double value = 0.0;
      Serializer& serialize(Serializer& s) { return s | value; }
      bool operator==(const MyDouble& other) const {
        return value == other.value;
      }
      std::ostream& operator<<(std::ostream& os) const { return os << value; }
    };
    static_assert(not serialize_as_bytes_v<std::complex<MyDouble>>);

    std::complex<MyDouble> c{MyDouble{3.14}, MyDouble{2.71}};

    Serializer sizer{Serializer::Sizing};
    sizer | c;
    CHECK(sizer.number_of_bytes() > 0);

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | c;

    std::complex<MyDouble> c_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | c_unpacked;

    CHECK(c.real().value == c_unpacked.real().value);
    CHECK(c.imag().value == c_unpacked.imag().value);
  }

  // std::complex<MyDouble> with serialize member and Serializer constructor
  {
    struct MyDouble {
      double value = 0.0;
      MyDouble() = default;
      MyDouble(double value_in) : value(value_in) {}
      MyDouble(Serializer& s) { s | value; }
      Serializer& serialize(Serializer& s) { return s | value; }
      bool operator==(const MyDouble& other) const {
        return value == other.value;
      }
      std::ostream& operator<<(std::ostream& os) const {
        os << value;
        return os;
      }
    };
    static_assert(not serialize_as_bytes_v<std::complex<MyDouble>>);

    std::complex<MyDouble> c{MyDouble{1.11}, MyDouble{2.22}};

    Serializer sizer{Serializer::Sizing};
    sizer | c;
    CHECK(sizer.number_of_bytes() > 0);

    std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
    Serializer packer{Serializer::Packing, buffer.get(),
                      sizer.number_of_bytes()};
    packer | c;

    std::complex<MyDouble> c_unpacked;
    Serializer unpacker{Serializer::Unpacking, buffer.get(),
                        sizer.number_of_bytes()};
    unpacker | c_unpacked;

    CHECK(c.real().value == c_unpacked.real().value);
    CHECK(c.imag().value == c_unpacked.imag().value);
  }
}
}  // namespace rts::serialize
#endif
