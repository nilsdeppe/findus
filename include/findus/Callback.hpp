// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "findus/Detail/GetOutput.hpp"
#include "findus/Detail/IndexConversion.hpp"
#include "findus/DistributedTaskDriver.hpp"
#include "findus/Exceptions/Exception.hpp"
#include "findus/IsCollection.hpp"
#include "findus/MessageHeader.hpp"

namespace findus {
namespace detail {
/// @{
/*!
 * \brief Type trait to detect if a type supports the equality operator (==).
 *
 * This trait inherits from `std::true_type` if the type `T` supports
 * `operator==`, and from `std::false_type` otherwise.
 *
 * \tparam T The type to check for equality comparability.
 */
template <typename T, typename = std::void_t<>>
struct has_equivalence : std::false_type {};

template <typename T>
struct has_equivalence<
    T, std::void_t<decltype(std::declval<T>() == std::declval<T>())>>
    : std::true_type {};
/// @}

/*!
 * \brief Compares two tuples for equality, element-wise, where possible.
 *
 * This function checks if each corresponding element of the two input tuples
 * is equal, but only for elements whose types support the equality operator
 * (`operator==`). For tuple elements that do not support equality comparison,
 * the function ignores those elements and treats them as equal for the
 * purposes of the overall comparison.
 *
 * \tparam Args The types of the tuple elements.
 * \tparam Indices The index sequence used to access tuple elements.
 * \param tuple_1 The first tuple to compare.
 * \param tuple_2 The second tuple to compare.
 * \return true if all comparable elements are equal; false otherwise.
 *
 * \note This function uses SFINAE to only compare elements that have
 *       `operator==` defined.
 */
template <typename... Args, size_t... Indices>
bool tuple_equal(const std::tuple<Args...>& tuple_1,
                 const std::tuple<Args...>& tuple_2,
                 std::index_sequence<Indices...> /*meta*/) {
  static_assert(sizeof...(Args) == sizeof...(Indices));
  bool result = true;
  [[maybe_unused]] std::initializer_list<char> t{[&](auto index) -> char {
    if (result) {
      constexpr size_t I = decltype(index)::value;
      if constexpr (has_equivalence<decltype(std::get<I>(tuple_1))>::value) {
        result &= (std::get<I>(tuple_1) == std::get<I>(tuple_2));
      }
    }
    return '0';
  }(std::integral_constant<size_t, Indices>{})...};
  return result;
}
}  // namespace detail

/*!
 * \brief Abstract base class for all callback types in findus.
 *
 * The `CallbackBase` class defines the common interface for all callback
 * objects, enabling polymorphic storage and invocation of callbacks. It
 * provides virtual methods for invoking the callback, cloning the callback
 * object, obtaining a human-readable name, checking for equality with another
 * callback, and querying whether the callback has already been invoked.
 *
 * Derived classes include:
 * - `Callback<Action, ParallelComponent, Args...>`: Represents a callback
 *   for invoking or broadcasting an action on a parallel component.
 * - `CallbackBroadcastTo<Closure, Action, ParallelComponent, Args...>`:
 *   Represents a callback for broadcasting an action to a subset of a
 *   collection, using a predicate closure.
 *
 * To create callback objects, use the provided factory functions:
 * - `make_invoke_callback()`
 * - `make_broadcast_callback()`
 * - `make_broadcast_to_callback()`
 *
 * \note
 * Callbacks should only be created, invoked, and otherwise manipulated in a
 * thread-safe environment. Calling the `invoke()` function in parallel from
 * multiple threads is undefined behavior. Each callback object may be invoked
 * only once; subsequent calls to `invoke()` will throw an Exception.
 *
 * Derived classes must implement all pure virtual methods to provide
 * specific callback behavior.
 */
class CallbackBase {
 public:
  /*!
   * \brief Default constructor for CallbackBase.
   *
   * Constructs a new CallbackBase object. This constructor must be called
   * by derived classes since CallbackBase is an abstract base class.
   */
  CallbackBase();

  /*!
   * \brief Virtual destructor for CallbackBase.
   */
  virtual ~CallbackBase();

  CallbackBase(const CallbackBase&) = default;
  CallbackBase& operator=(const CallbackBase&) = default;
  CallbackBase(CallbackBase&&) = default;
  CallbackBase& operator=(CallbackBase&&) = default;

  /*!
   * \brief Invokes the callback operation.
   *
   * This pure virtual function must be implemented by derived classes to
   * perform the callback's action. The callback can only be invoked once;
   * subsequent calls should throw an Exception.
   *
   * \throws Exception if the callback is invoked more than once.
   */
  virtual void invoke() = 0;

  /*!
   * \brief Creates and returns a polymorphic clone of this callback.
   *
   * This pure virtual function must be implemented by derived classes to
   * return a new heap-allocated copy of the callback object, preserving its
   * state and arguments. The returned pointer is wrapped in a
   * `std::unique_ptr<CallbackBase>` for safe ownership transfer.
   *
   * \return A unique pointer to a new callback object that is a copy of this
   * one.
   */
  virtual std::unique_ptr<CallbackBase> get_clone() const = 0;

  /*!
   * \brief Returns a human-readable name for this callback.
   *
   * This pure virtual function must be implemented by derived classes to
   * provide a string describing the callback, including type and action
   * information. Useful for debugging, logging, or introspection.
   *
   * \return A string representing the name and type information of the
   * callback.
   */
  virtual std::string name() const = 0;

  /*!
   * \brief Checks if this callback is equal to another callback.
   *
   * This pure virtual function must be implemented by derived classes to
   * compare the current callback with another callback for equivalence.
   *
   * \param rhs The other callback to compare with.
   * \return true if the callbacks are equivalent; false otherwise.
   */
  virtual bool is_equal_to(const CallbackBase& rhs) const = 0;

  /*!
   * \brief Returns `true` if the callback has already been invoked.
   *
   * This pure virtual function must be implemented by derived classes to
   * indicate whether the callback has been invoked.
   *
   * \return true if the callback was already invoked; false otherwise.
   */
  virtual bool was_invoked() const = 0;

 protected:
  /*!
   * \brief Returns a reference to the associated DistributedTaskDriver.
   *
   * Provides access to the task driver for use in derived callback
   * implementations.
   *
   * \return Reference to the DistributedTaskDriver associated with this
   * callback.
   */
  DistributedTaskDriver& get_task_driver() const;
};

/*!
 * \brief Intermediate base class for callbacks to manage and update arguments.
 *
 * The `CallbackArgs` class serves as a base for callback types that need to
 * store and update their argument tuples independently of the specific Action,
 * ParallelComponent, or message type. This allows for flexible manipulation of
 * callback arguments without requiring knowledge of the higher-level callback
 * semantics.
 *
 * Derived classes, such as `Callback` and `CallbackBroadcastTo`, inherit from
 * `CallbackArgs` to gain argument storage and extend functionality. The
 * arguments are stored as a tuple of decayed types, and can be updated using
 * the `update_arguments()` method.
 *
 * \tparam Args The types of the arguments to be stored in the callback.
 *
 * \note This is still an abstract base class.
 */
template <class... Args>
class CallbackArgs : public CallbackBase {
 public:
  static_assert(sizeof...(Args) > 0);

  /*!
   * \brief Virtual destructor for CallbackArgs.
   *
   * Ensures proper cleanup of derived callback objects.
   */
  ~CallbackArgs() override = default;
  CallbackArgs() = default;
  CallbackArgs(const CallbackArgs& rhs) = default;
  CallbackArgs& operator=(const CallbackArgs& rhs) = default;
  CallbackArgs(CallbackArgs&& rhs) noexcept = default;
  CallbackArgs& operator=(CallbackArgs&& rhs) noexcept = default;

  /*!
   * \brief Updates the arguments stored in the callback.
   *
   * Replaces the current argument tuple with a new one.
   *
   * \param new_args The new tuple of arguments to store.
   */
  void update_arguments(std::tuple<std::decay_t<Args>...> new_args);

 protected:
  /*!
   * \brief Constructs a CallbackArgs object with the given arguments.
   *
   * Initializes the internal argument tuple with the provided arguments.
   *
   * \param args The arguments to store in the callback.
   */
  CallbackArgs(std::decay_t<Args>... args);

  std::tuple<std::decay_t<Args>...> args_{};
};

template <class... Args>
CallbackArgs<Args...>::CallbackArgs(std::decay_t<Args>... args)
    : args_{std::move(args)...} {}

template <class... Args>
void CallbackArgs<Args...>::update_arguments(
    std::tuple<std::decay_t<Args>...> new_args) {
  args_ = std::move(new_args);
}

/*!
 * \brief Callback for invoking or broadcasting an action on a parallel
 * component.
 *
 * The `Callback` class represents a callback that can either invoke an action
 * on a specific element of a parallel component (regular or collection), or
 * broadcast an action to all elements of a parallel component. The callback
 * stores the action type, the target parallel component, and the arguments to
 * be passed to the action.
 *
 * The callback can be constructed for:
 * - An invoke operation, targeting a specific collection index or node using
 *   the `findus::make_callback()` function.
 * - A broadcast operation, targeting all elements of the parallel component
 *   using the `findus::make_broadcast_callback()` function.
 *
 * The arguments to the action are stored as a tuple of decayed types. The
 * callback can only be invoked once; subsequent invocations will throw an
 * Exception. Cloning the callback (using get_clone()) allows repeated
 * invocation on the clone as long as the clone was done before `invoke()` was
 * called.
 *
 * Equality comparison between callbacks (via is_equal_to) does NOT consider
 * whether the callback has already been invoked. As a result, a callback and
 * its clone will compare equal even if one has been invoked and the other has
 * not.
 *
 * \tparam Action The action to invoke or broadcast.
 * \tparam ParallelComponent The parallel component type.
 * \tparam Args The argument types to pass to the action.
 *
 * \see CallbackBase
 * \see make_callback()
 * \see make_broadcast_callback()
 * \see CallbackBroadcastTo
 * \see make_broadcast_to_callback()
 */
template <class Action, class ParallelComponent, class... Args>
class Callback final : public CallbackArgs<Args...> {
 public:
  using Base = CallbackArgs<Args...>;

  Callback() = default;
  Callback(const Callback& rhs) = default;
  Callback& operator=(const Callback& rhs) = default;
  Callback(Callback&& rhs) noexcept = default;
  Callback& operator=(Callback&& rhs) noexcept = default;
  ~Callback() override = default;

  /*!
   * \brief Constructs an invoke Callback for an element of a parallel
   * component (regular or collection). Use `findus::make_callback()` instead.
   *
   * \warning You should use the helper function `findus::make_callback()`
   * instead.
   *
   * Initializes the Callback to invoke an action on a specific element of a
   * collection parallel component, storing the collection index and the
   * provided arguments in a tuple.
   *
   * \param collection_index The index of the collection element to target.
   * \param args The arguments to pass to the action.
   */
  Callback(uint64_t collection_index, std::decay_t<Args>... args);

  /*!
   * \brief Constructs a broadcast Callback with the given arguments. Use
   * `findus::make_broadcast_callback()` instead.
   *
   * \warning You should use the helper function
   * `findus::make_broadcast_callback()` instead.
   *
   * Initializes the Callback for a broadcast operation on the specified
   * parallel component, storing the provided arguments in a tuple. The
   * callback will invoke the broadcast action when called.
   *
   * \param args The arguments to pass to the broadcast action.
   */
  Callback(std::decay_t<Args>... args);

  /*!
   * \brief Invokes the callback on the associated parallel component.
   *
   * This method applies the stored arguments to either an invoke or broadcast
   * operation for the specified Action and ParallelComponent, depending on the
   * message type. For Invoke, the action is called on a specific collection
   * element; for Broadcast, the action is called on all elements of the
   * parallel component.
   *
   * The callback can only be invoked once; attempting to invoke it a second
   * time will throw an Exception. To invoke the callback again, create a clone
   * using get_clone() and invoke the clone.
   *
   * \throws Exception if the callback has already been invoked.
   *
   * \note This function forwards the stored arguments to the appropriate
   *       method of the task driver, based on the message type.
   */
  void invoke() override;

  /*!
   * \brief Returns a human-readable name for this callback.
   *
   * The name includes the type of callback (Invoke or Broadcast), the name of
   * the parallel component, and the type name of the action. This is useful
   * for debugging, logging, or introspection.
   *
   * \return A string representing the name and type information of the
   * callback.
   */
  std::string name() const override;

  /*!
   * \brief Creates and returns a polymorphic clone of this callback.
   *
   * This function returns a new heap-allocated copy of the current
   * Callback object, preserving its state, closure, and arguments. The returned
   * pointer is wrapped in a std::unique_ptr<CallbackBase> for safe ownership
   * transfer.
   *
   * \return A unique pointer to a new Callback object that is a copy of this
   * one.
   */
  std::unique_ptr<CallbackBase> get_clone() const override;

  /*!
   * \brief Checks if this callback is equal to another callback.
   *
   * This function compares the current callback with another callback for
   * equivalence. Two callbacks are considered equal if they have the same
   * collection index, message type, and argument values (as determined by
   * element-wise comparison of the argument tuple).
   *
   * \note The `invoked_` member, which tracks whether the callback has already
   * been invoked, is NOT included in the equality check. As a result, if you
   * clone a callback and then call `invoke()` on the original but not on the
   * clone, the two callbacks will still be considered equal by this function.
   *
   * \param rhs The other callback to compare with.
   * \return true if the callbacks are equivalent (excluding invocation state);
   *         false otherwise.
   */
  bool is_equal_to(const CallbackBase& rhs) const override;

  /// \brief Returns `true` if the callback was already invoked.
  bool was_invoked() const override { return invoked_; }

 private:
  uint64_t collection_index_{std::numeric_limits<uint64_t>::max()};
  MessageType message_type_{MessageType::Uninitialized};
  bool invoked_{false};
};

template <class Action, class ParallelComponent, class... Args>
Callback<Action, ParallelComponent, Args...>::Callback(
    const uint64_t collection_index, std::decay_t<Args>... args)
    : Base(std::move(args)...),
      collection_index_(collection_index),
      message_type_(findus::MessageType::Invoke) {
  if (message_type_ != MessageType::Invoke and
      message_type_ != MessageType::Broadcast) {
    throw Exception{
        "The Callback class only supports Invoke and Broadcast but received " +
        detail::get_output(message_type_)};
  }
}

template <class Action, class ParallelComponent, class... Args>
Callback<Action, ParallelComponent, Args...>::Callback(
    std::decay_t<Args>... args)
    : Base(std::move(args)...),
      collection_index_(std::numeric_limits<uint64_t>::max()),
      message_type_(findus::MessageType::Broadcast) {}

template <class Action, class ParallelComponent, class... Args>
void Callback<Action, ParallelComponent, Args...>::invoke() {
  if (invoked_) {
    throw Exception{
        "Already invoked the Callback. Cannot invoke() it a second time. You "
        "must first make a copy of the callback, for example using "
        "get_clone(), and then call invoke() on the clone the second time."};
  }
  invoked_ = true;
  if (message_type_ == findus::MessageType::Invoke) {
    std::apply(
        [this](auto&&... args) {
          this->get_task_driver().template invoke<Action, ParallelComponent>(
              detail::from_internal<ParallelComponent>(collection_index_),
              std::forward<decltype(args)>(args)...);
        },
        std::move(this->args_));
  } else if (message_type_ == findus::MessageType::Broadcast) {
    std::apply(
        [this](auto&&... args) {
          this->get_task_driver().template broadcast<Action, ParallelComponent>(
              std::forward<decltype(args)>(args)...);
        },
        std::move(this->args_));
  }
}

template <class Action, class ParallelComponent, class... Args>
std::string Callback<Action, ParallelComponent, Args...>::name() const {
  using namespace std::literals;
  return "Callback" +
         std::string{
             (message_type_ == MessageType::Invoke ? "Invoke" : "Broadcast")} +
         "(" + ParallelComponent::name() + "," +
         std::string{typeid(Action).name()} + ")";
}

template <class Action, class ParallelComponent, class... Args>
std::unique_ptr<CallbackBase>
Callback<Action, ParallelComponent, Args...>::get_clone() const {
  return std::make_unique<Callback>(*this);
}

template <class Action, class ParallelComponent, class... Args>
bool Callback<Action, ParallelComponent, Args...>::is_equal_to(
    const CallbackBase& rhs) const {
  const auto* downcast_ptr =
      dynamic_cast<const Callback<Action, ParallelComponent, Args...>*>(&rhs);
  if (downcast_ptr == nullptr) {
    return false;
  }
  return collection_index_ == downcast_ptr->collection_index_ and
         message_type_ == downcast_ptr->message_type_ and
         detail::tuple_equal(this->args_, downcast_ptr->args_,
                             std::make_index_sequence<sizeof...(Args)>{});
}

/*!
 * \brief Callback for broadcasting an action to a subset of a collection
 * parallel component.
 *
 * The `CallbackBroadcastTo` class represents a callback that can broadcast
 * an action to a filtered subset of elements in a collection parallel
 * component, as determined by a user-provided predicate. The callback stores
 * the action type, the target parallel component, the predicate used for
 * filtering, and the arguments to be passed to the action.
 *
 * \warning Any variables captured by reference in the predicate must stay in
 * scope until the callback is invoked. This is because the predicate is
 * called lazily at the callback invocation.
 *
 * The callback can be constructed for:
 * - A broadcast-to operation, targeting only those collection elements that
 *   satisfy the predicate, using the `findus::make_broadcast_to_callback()`
 *   function.
 *
 * The arguments to the action are stored as a tuple of decayed types. The
 * callback can only be invoked once; subsequent invocations will throw an
 * Exception. Cloning the callback (using get_clone()) allows repeated
 * invocation on the clone as long as the clone was done before `invoke()` was
 * called.
 *
 * Equality comparison between callbacks (via is_equal_to) considers the
 * argument values (as determined by element-wise comparison of the argument
 * tuple). The predicate is not compared for equality. The `invoked_` member,
 * which tracks whether the callback has already been invoked, is NOT included
 * in the equality check. As a result, if you clone a callback and then call
 * `invoke()` on the original but not on the clone, the two callbacks will
 * still be considered equal by this function.
 *
 * \tparam UnaryPredicate The predicate type used to filter collection elements.
 * \tparam Action The action to broadcast.
 * \tparam ParallelComponent The collection parallel component type.
 * \tparam Args The argument types to pass to the action.
 *
 * \see CallbackBase
 * \see make_callback()
 * \see make_broadcast_callback()
 * \see Callback
 * \see make_broadcast_to_callback()
 */
template <class UnaryPredicate, class Action, class ParallelComponent,
          class... Args>
class CallbackBroadcastTo final : public CallbackArgs<Args...>,
                                  private UnaryPredicate {
 public:
  using Base = CallbackArgs<Args...>;

  CallbackBroadcastTo() = default;
  CallbackBroadcastTo(const CallbackBroadcastTo& rhs) = default;
  CallbackBroadcastTo& operator=(const CallbackBroadcastTo& rhs) = default;
  CallbackBroadcastTo(CallbackBroadcastTo&& rhs) noexcept = default;
  CallbackBroadcastTo& operator=(CallbackBroadcastTo&& rhs) noexcept = default;
  ~CallbackBroadcastTo() override = default;

  /*!
   * \brief Constructs a CallbackBroadcastTo with a predicate and
   * arguments. Use `findus::make_broadcast_to_callback()` instead.
   *
   * \warning You should use the helper function
   * `findus::make_broadcast_to_callback()` instead.
   *
   * Initializes the CallbackBroadcastTo object with the provided unary
   * predicate and argument list. The predicate is used to determine which
   * elements of the collection parallel component will receive the broadcast
   * when the callback is invoked. The arguments are stored in a tuple and will
   * be forwarded to the action when the callback is executed.
   *
   * \param predicate A unary predicate that selects which collection elements
   *        should receive the broadcast.
   * \param args The arguments to pass to the broadcast action.
   */
  CallbackBroadcastTo(UnaryPredicate predicate, std::decay_t<Args>... args);

  /*!
   * \brief Invokes the broadcast_to callback on the associated parallel
   * component.
   *
   * This method applies the stored arguments to the broadcast_to operation
   * for the specified Action and ParallelComponent, using the UnaryPredicate as
   * the predicate to select which collection elements receive the broadcast.
   *
   * The callback can only be invoked once; attempting to invoke it a second
   * time will throw an Exception. To invoke the callback again, create a clone
   * using get_clone() and invoke the clone.
   *
   * \throws Exception if the callback has already been invoked.
   *
   * \note This function forwards the stored arguments to the broadcast_to
   *       method of the task driver, using the UnaryPredicate as the predicate.
   */
  void invoke() override;

  /*!
   * \brief Returns a human-readable name for this broadcast-to callback.
   *
   * The name includes the type of callback (CallbackBroadcastTo), the name of
   * the parallel component, and the type name of the action. This is useful
   * for debugging, logging, or introspection.
   *
   * \return A string representing the name and type information of the
   * broadcast-to callback.
   */
  std::string name() const override;

  /*!
   * \brief Creates and returns a polymorphic clone of this broadcast-to
   * callback.
   *
   * This function returns a new heap-allocated copy of the current
   * CallbackBroadcastTo object, preserving its state, UnaryPredicate, and
   * arguments. The returned pointer is wrapped in a
   * std::unique_ptr<CallbackBase> for safe ownership transfer.
   *
   * \return A unique pointer to a new CallbackBroadcastTo object that is a copy
   * of this one.
   */
  std::unique_ptr<CallbackBase> get_clone() const override;

  /*!
   * \brief Checks if this broadcast-to callback is equal to another callback.
   *
   * This function compares the current callback with another callback for
   * equivalence. Two broadcast-to callbacks are considered equal if their
   * argument tuples are element-wise equal (as determined by the tuple
   * comparison utility).
   *
   * \note The `invoked_` member, which tracks whether the callback has already
   * been invoked, is NOT included in the equality check. As a result, if you
   * clone a callback and then call `invoke()` on the original but not on the
   * clone, the two callbacks will still be considered equal by this function.
   *
   * \param rhs The other callback to compare with.
   * \return true if the callbacks are equivalent (excluding invocation state);
   *         false otherwise.
   */
  bool is_equal_to(const CallbackBase& rhs) const override;

  /// \brief Returns `true` if the callback was already invoked.
  bool was_invoked() const override { return invoked_; }

  /*!
   * \brief Predicate operator to determine if a collection element should
   * receive the broadcast.
   *
   * This function checks whether the specified collection index should be
   * included in the broadcast operation. If the callback was constructed with a
   * serialized list of indices, it performs a binary search on the stored list.
   * Otherwise, it delegates the decision to the underlying UnaryPredicate
   * predicate.
   *
   * \param collection_index The index of the collection element to check.
   * \return true if the element should receive the broadcast; false otherwise.
   */
  bool operator()(const typename ParallelComponent::findus_collection_index
                      collection_index) const {
    if (ids_are_serialized_) {
      return std::binary_search(broadcast_to_ids_.begin(),
                                broadcast_to_ids_.end(), collection_index);
    }
    return UnaryPredicate::operator()(collection_index);
  }

 private:
  std::vector<typename ParallelComponent::findus_collection_index>
      broadcast_to_ids_{};
  bool invoked_{false};
  bool ids_are_serialized_{false};
};

template <class UnaryPredicate, class Action, class ParallelComponent,
          class... Args>
CallbackBroadcastTo<UnaryPredicate, Action, ParallelComponent,
                    Args...>::CallbackBroadcastTo(UnaryPredicate predicate,
                                                  std::decay_t<Args>... args)
    : Base(std::move(args)...), UnaryPredicate(std::move(predicate)) {}

template <class UnaryPredicate, class Action, class ParallelComponent,
          class... Args>
void CallbackBroadcastTo<UnaryPredicate, Action, ParallelComponent,
                         Args...>::invoke() {
  if (invoked_) {
    throw Exception{
        "Already invoked the Callback. Cannot invoke() it a second time. You "
        "must first make a copy of the callback, for example using "
        "get_clone(), and then call invoke() on the clone the second time."};
  }
  invoked_ = true;
  std::apply(
      [this](auto&&... args) {
        this->get_task_driver()
            .template broadcast_to<Action, ParallelComponent>(
                std::as_const(*this), std::forward<decltype(args)>(args)...);
      },
      std::move(this->args_));
}

template <class UnaryPredicate, class Action, class ParallelComponent,
          class... Args>
std::string CallbackBroadcastTo<UnaryPredicate, Action, ParallelComponent,
                                Args...>::name() const {
  using namespace std::literals;
  return "CallbackBroadcastTo(" + ParallelComponent::name() + "," +
         std::string{typeid(Action).name()} + ")";
}

template <class UnaryPredicate, class Action, class ParallelComponent,
          class... Args>
std::unique_ptr<CallbackBase> CallbackBroadcastTo<
    UnaryPredicate, Action, ParallelComponent, Args...>::get_clone() const {
  return std::make_unique<CallbackBroadcastTo>(*this);
}

template <class UnaryPredicate, class Action, class ParallelComponent,
          class... Args>
bool CallbackBroadcastTo<UnaryPredicate, Action, ParallelComponent,
                         Args...>::is_equal_to(const CallbackBase& rhs) const {
  const auto* downcast_ptr = dynamic_cast<const CallbackBroadcastTo<
      UnaryPredicate, Action, ParallelComponent, Args...>*>(&rhs);
  if (downcast_ptr == nullptr) {
    return false;
  }
  return detail::tuple_equal(this->args_, downcast_ptr->args_,
                             std::make_index_sequence<sizeof...(Args)>{});
}

/// @{
/*!
 * \brief Creates a Callback for invoking an action on a parallel component.
 *
 * Constructs a Callback that will invoke the specified Action on either a
 * regular or collection parallel component. For collections, the user must
 * provide the collection index; for regular components, the target node ID.
 * The arguments are forwarded and stored in the callback.
 *
 * \tparam Action The action to invoke.
 * \tparam ParallelComponent The parallel component type.
 * \tparam IndexType The type of the index or node ID.
 * \tparam Args The types of the arguments to pass to the action.
 * \param user_index_or_target_node The collection index or node ID.
 * \param args The arguments to pass to the action.
 * \return A Callback object for invoking the action.
 */
template <class Action, class ParallelComponent, class IndexType, class... Args>
auto make_invoke_callback(IndexType user_index_or_target_node, Args&&... args)
    -> Callback<Action, ParallelComponent, std::decay_t<Args>...> {
  if constexpr (findus::is_collection_v<ParallelComponent>) {
    static_assert(
        std::is_same_v<typename ParallelComponent::findus_collection_index,
                       IndexType>);
    return {detail::to_internal(user_index_or_target_node),
            std::forward<Args>(args)...};
  } else {
    static_assert(std::is_same_v<IndexType, int>);
    return {user_index_or_target_node, std::forward<Args>(args)...};
  }
}

template <class Action, class ParallelComponent, class IndexType, class... Args>
auto make_unique_invoke_callback(IndexType user_index_or_target_node,
                                 Args&&... args)
    -> std::unique_ptr<CallbackBase> {
  using Cb = Callback<Action, ParallelComponent, std::decay_t<Args>...>;
  if constexpr (findus::is_collection_v<ParallelComponent>) {
    static_assert(
        std::is_same_v<typename ParallelComponent::findus_collection_index,
                       IndexType>);
    return std::make_unique<Cb>(detail::to_internal(user_index_or_target_node),
                                std::forward<Args>(args)...);
  } else {
    static_assert(std::is_same_v<IndexType, int>);
    return std::make_unique<Cb>(user_index_or_target_node,
                                std::forward<Args>(args)...);
  }
}
/// @}

/// @{
/*!
 * \brief Creates a Callback for broadcasting an action to a parallel component.
 *
 * Constructs a Callback that will broadcast the specified Action to all
 * elements of the given parallel component. The arguments are forwarded and
 * stored in the callback.
 *
 * \tparam Action The action to broadcast.
 * \tparam ParallelComponent The parallel component type.
 * \tparam Args The types of the arguments to pass to the action.
 * \param args The arguments to pass to the broadcast action.
 * \return A Callback object for broadcasting the action.
 */
template <class Action, class ParallelComponent, class... Args>
auto make_broadcast_callback(Args&&... args)
    -> Callback<Action, ParallelComponent, std::decay_t<Args>...> {
  return {std::forward<Args>(args)...};
}

template <class Action, class ParallelComponent, class... Args>
auto make_unique_broadcast_callback(Args&&... args)
    -> std::unique_ptr<CallbackBase> {
  return std::make_unique<
      Callback<Action, ParallelComponent, std::decay_t<Args>...>>(
      std::forward<Args>(args)...);
}
/// @}

/// @{
/*!
 * \brief Creates a Callback for broadcasting an action to a subset of a
 * collection parallel component.
 *
 * Constructs a CallbackBroadcastTo that will broadcast the specified
 * Action to all elements of the given collection parallel component that
 * satisfy the provided predicate. The arguments are forwarded and stored
 * in the callback.
 *
 * \tparam Action The action to broadcast.
 * \tparam ParallelComponent The collection parallel component type.
 * \tparam UnaryPredicate The predicate type used to filter elements.
 * \tparam Args The types of the arguments to pass to the action.
 * \param predicate A unary predicate that selects which elements receive the
 *        broadcast.
 * \param args The arguments to pass to the broadcast action.
 * \return A CallbackBroadcastTo object for broadcasting the action to a
 *         subset.
 */
template <class Action, class ParallelComponent, class UnaryPredicate,
          class... Args>
auto make_broadcast_to_callback(UnaryPredicate&& predicate, Args&&... args)
    -> CallbackBroadcastTo<std::decay_t<UnaryPredicate>, Action,
                           ParallelComponent, std::decay_t<Args>...> {
  static_assert(is_collection_v<ParallelComponent>,
                "Can only create a callback that calls a broadcast_to for a "
                "collection parallel component.");
  return {std::forward<UnaryPredicate>(predicate), std::forward<Args>(args)...};
}

template <class Action, class ParallelComponent, class UnaryPredicate,
          class... Args>
auto make_unique_broadcast_to_callback(UnaryPredicate&& predicate,
                                       Args&&... args)
    -> std::unique_ptr<CallbackBase> {
  static_assert(is_collection_v<ParallelComponent>,
                "Can only create a callback that calls a broadcast_to for a "
                "collection parallel component.");
  return std::make_unique<
      CallbackBroadcastTo<std::decay_t<UnaryPredicate>, Action,
                          ParallelComponent, std::decay_t<Args>...>>(
      std::forward<UnaryPredicate>(predicate), std::forward<Args>(args)...);
}
/// @}
}  // namespace findus
