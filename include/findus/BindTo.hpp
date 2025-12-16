// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstdint>
#include <iosfwd>

namespace findus {
/*!
 * \brief Describes how a thread or process should be bound to hardware
 * resources.
 *
 * The bind target controls whether execution is left unbound, pinned/bound to a
 * core, or pinned/bound to a hardware thread.
 */
enum class BindTo : std::uint8_t {
  /// Binding target has not been set.
  Uninitialized = 0,
  /// Do not bind to any hardware unit.
  None = 1,
  /// Bind to cores core.
  Core = 2,
  /// Bind to hardware threads.
  HardwareThread = 3
};

/// \brief Stream operator for BindTo.
std::ostream& operator<<(std::ostream& os, BindTo bind_to);
}  // namespace findus
