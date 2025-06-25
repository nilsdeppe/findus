// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#if defined(RTS_ENABLE_TESTING)
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <doctest/extensions/doctest_mpi.h>
#else
static_assert(
    false, "Should never compile DoctestImpl.cpp when testing isn't enabled.");
#endif
