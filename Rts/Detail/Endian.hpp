// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#if defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN ||                 \
    defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__THUMBEB__) || \
    defined(__AARCH64EB__) || defined(_MIBSEB) || defined(__MIBSEB) ||       \
    defined(__MIBSEB__)
#ifndef __BIG_ENDIAN__
#define __BIG_ENDIAN__
#endif
#elif defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN ||         \
    defined(__LITTLE_ENDIAN__) || defined(__ARMEL__) ||                   \
    defined(__THUMBEL__) || defined(__AARCH64EL__) || defined(_MIPSEL) || \
    defined(__MIPSEL) || defined(__MIPSEL__) || defined(_WIN32) ||        \
    defined(__i386__) || defined(__x86_64__) || defined(_X86_) ||         \
    defined(_IA64_)
#ifndef __LITTLE_ENDIAN__
#define __LITTLE_ENDIAN__
#endif
#else
#error "I don't know what architecture this is!"
#endif
