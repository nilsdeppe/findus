// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "findus/Serialize/Stl/String.hpp"

#if defined(FINDUS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <memory>
#include <string>

#include "findus/Serialize/Serializer.hpp"

namespace findus::serialize {
namespace {
template <class Cast, class StringType>
void test_serialize_string(StringType original) {
  CAPTURE(original);
  CAPTURE(original.capacity());
  CAPTURE(original.size());
  Serializer sizer{Serializer::Sizing};
  static_cast<Cast&>(sizer) | original;
  CHECK(sizer.number_of_bytes() ==
        sizeof(size_t) * 2 +
            original.size() * sizeof(typename StringType::value_type));

  std::unique_ptr<std::byte[]> buffer{new std::byte[sizer.number_of_bytes()]};
  Serializer packer{Serializer::Packing, buffer.get(), sizer.number_of_bytes()};
  static_cast<Cast&>(packer) | original;

  StringType unpacked;
  Serializer unpacker{Serializer::Unpacking, buffer.get(),
                      sizer.number_of_bytes()};
  static_cast<Cast&>(unpacker) | unpacked;

  CHECK(unpacked.capacity() >= original.capacity());
  CHECK(unpacked == original);
}

template <class Cast = Serializer>
void test() {
  // Regular strings
  test_serialize_string<Cast>(std::string("Hello, world!"));
  test_serialize_string<Cast>(std::wstring(L"Wide chars: αβγ"));
  test_serialize_string<Cast>(std::u16string(u"UTF-16: 𝄞𝄢"));
  test_serialize_string<Cast>(std::u32string(U"UTF-32: 𐍈𐍉"));

  // Empty strings
  test_serialize_string<Cast>(std::string());
  test_serialize_string<Cast>(std::wstring());
  test_serialize_string<Cast>(std::u16string());
  test_serialize_string<Cast>(std::u32string());

  // Special and long strings
  std::string special = "Special chars: !@#$%^&*()_+-=[]{}|;':,.<>/?`~";
  special.reserve(100);
  test_serialize_string<Cast>(special);

  std::string long_str(10000, 'x');
  long_str.reserve(20000);
  test_serialize_string<Cast>(long_str);
}
}  // namespace

TEST_CASE("Serialize.String") {
  test<Serializer>();
#ifdef FINDUS_MIMIC_CHARM_PUPER
  test<PUP::er>();
#endif
}
}  // namespace findus::serialize
#endif
