// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

// Charm++ compatibility header for migration to findus.
// This header is only available when FINDUS_CREATE_CHARM_HEADERS is ON.

#include "pup.h"

#include <findus/Serialize/Stl/Array.hpp>
#include <findus/Serialize/Stl/Complex.hpp>
#include <findus/Serialize/Stl/Deque.hpp>
#include <findus/Serialize/Stl/ForwardList.hpp>
#include <findus/Serialize/Stl/List.hpp>
#include <findus/Serialize/Stl/Map.hpp>
#include <findus/Serialize/Stl/Pair.hpp>
#include <findus/Serialize/Stl/Set.hpp>
#include <findus/Serialize/Stl/SharedPtr.hpp>
#include <findus/Serialize/Stl/String.hpp>
#include <findus/Serialize/Stl/Tuple.hpp>
#include <findus/Serialize/Stl/UniquePtr.hpp>
#include <findus/Serialize/Stl/UnorderedMap.hpp>
#include <findus/Serialize/Stl/UnorderedSet.hpp>
#include <findus/Serialize/Stl/Vector.hpp>
