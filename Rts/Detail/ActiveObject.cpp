// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Rts/Detail/ActiveObject.hpp"

#include <ostream>

namespace rts::detail {
std::ostream& operator<<(std::ostream& os, const ActiveObject& obj) {
  os << "ActiveObject { distributed_object_index: "
     << obj.distributed_object_index
     << ", target_collection_index: " << obj.target_collection_index << " }";
  return os;
}
}  // namespace rts::detail

#if defined(RTS_ENABLE_TESTING)

#include <doctest/doctest.h>
#include <sstream>

TEST_CASE("ActiveObject") {
  const rts::detail::ActiveObject obj{};
  CHECK(obj.distributed_object_index ==
        std::numeric_limits<std::uint32_t>::max());
  CHECK(obj.target_collection_index ==
        std::numeric_limits<std::uint64_t>::max());

  const auto check_core = [](const std::string output) {
    CHECK(output.find("ActiveObject {") != std::string::npos);
    CHECK(output.find("distributed_object_index:") != std::string::npos);
    CHECK(output.find("target_collection_index:") != std::string::npos);
  };

  {
    // Check default constructed is streamable
    std::ostringstream oss;
    oss << obj;
    check_core(oss.str());
  }

  // Test with custom values
  const rts::detail::ActiveObject obj2{4444, 123456};
  std::ostringstream oss2;
  oss2 << obj2;
  const std::string output2 = oss2.str();
  check_core(output2);
  CHECK(output2.find("4444") != std::string::npos);
  CHECK(output2.find("123456") != std::string::npos);
}
#endif
