// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#if defined(RTS_ENABLE_TESTING)
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#else
static_assert(false,
              "Should never compile Test.cpp when testing isn't enabled.");
#endif
