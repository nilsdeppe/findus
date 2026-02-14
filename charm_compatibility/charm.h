// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

// Charm++ compatibility header for migration to findus.

#include <cstdio>

#define CkError(...) fprintf(stderr, __VA_ARGS__)
