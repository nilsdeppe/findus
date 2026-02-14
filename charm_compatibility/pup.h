// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

// Charm++ compatibility header for migration to findus.
// This header is only available when FINDUS_CREATE_CHARM_HEADERS is ON.

#include <findus/Serialize/Serializer.hpp>
#include <findus/Serialize/Virtual.hpp>

#define SINGLE_ARG(...) __VA_ARGS__

#define PUPable_decl_template(className)
#define PUPable_decl_base_template(baseClassName, className)
#define PUPable_decl(className)
#define PUPable_abstract(className)
#define PUPable_reg2(className, name)

#define PUPable_reg(className)

#define PUPbytes(className)                       \
  namespace findus::serialize {                   \
  template <>                                     \
  struct as_bytes<className> : std::true_type {}; \
  }

namespace PUP {
using findus::serialize::as_bytes;  // NOLINT(misc-unused-using-decls)
}
