// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

// Charm++ compatibility header for migration to findus.
// This header is only available when FINDUS_CREATE_CHARM_HEADERS is ON.

#include <cstdint>

using CkArrayIndex = std::uint64_t;

using CkIndex1D = CkArrayIndex;
using CkIndex2D = CkArrayIndex;
using CkIndex3D = CkArrayIndex;
using CkIndex4D = CkArrayIndex;
using CkIndex5D = CkArrayIndex;
using CkIndex6D = CkArrayIndex;
