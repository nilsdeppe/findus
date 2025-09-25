// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstddef>

namespace rts::serialize {
/*!
 * \brief Represents a non-owning view of one or more items for serialization.
 *
 * The View struct provides a convenient way to describe a contiguous sequence
 * of items (fundamental or complex types) for serialization or deserialization.
 * It stores a pointer to the first item and the number of items in the view.
 *
 * \note You do not have to specify the type at construction, it is deduced
 * via constructor template argument deduction guidelines.
 *
 * \tparam T The type of the items in the view.
 */
template <class T>
struct View {
  /*!
   * \brief Construct a View from a pointer and an integer number of items.
   *
   * \param item Pointer to the first item.
   * \param number_of_items Number of items in the view.
   */
  View(T* item, int number_of_items)
      : item_(item), number_of_items_(number_of_items) {}

  /*!
   * \brief Construct a View from a pointer and a size_t number of items.
   *
   * \param item Pointer to the first item.
   * \param number_of_items Number of items in the view (size_t).
   */
  View(T* item, size_t number_of_items)
      : item_(item), number_of_items_(static_cast<int>(number_of_items)) {}

  /*!
   * \brief Construct a View from a single item reference.
   *
   * \param item Reference to the item.
   */
  View(T& item) : item_(&item), number_of_items_(1) {}

  /// Pointer to the first item in the view.
  T* item_;
  /// Number of items in the view.
  int number_of_items_;
};

template <class T>
View(T*, int) -> View<T>;
template <class T>
View(T*, size_t) -> View<T>;
template <class T>
View(T&) -> View<T>;
}  // namespace rts::serialize
