// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

namespace findus::reduction::detail {
/// \brief Predicate that always returns `true`.
///
/// Used internally for `reduction()`, which calls
/// `reduction_over(AllElements{})`. Note that `reduction_over()` optimizes
/// for the case where `AllElements` is passed.
struct AllElements {
  template <class T>
  bool operator()(const T /*unused_id*/) const {
    return true;
  }
};
}  // namespace findus::reduction::detail
