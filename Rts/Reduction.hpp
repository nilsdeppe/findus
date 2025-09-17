// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "Rts/Detail/GetOutput.hpp"
#include "Rts/Detail/IndexConversion.hpp"
#include "Rts/DistributedObjectIndex.hpp"
#include "Rts/Exceptions/Exception.hpp"
#include "Rts/HardwareInfo.hpp"
#include "Rts/IsCollection.hpp"
#include "Rts/Message.hpp"
#include "Rts/MessageHeader.hpp"
#include "Rts/MessageType.hpp"
#include "Rts/ParentAndChildren.hpp"

namespace rts::reduction {
namespace detail {
struct ReductionCallbackImpl {
  std::uint64_t collection_index_{std::numeric_limits<std::uint64_t>::max()};
  std::uint32_t distributed_object_index_{
      std::numeric_limits<std::uint32_t>::max()};
  MessageType message_type_{MessageType::Uninitialized};
};
}  // namespace detail

/*!
 * \brief Callback object invoked after a reduction operation completes.
 *
 * The ReductionCallback class encapsulates the information needed to perform
 * a post-reduction action, such as invoking a specific action on a parallel
 * component or broadcasting the result to all elements of a collection.
 *
 * \tparam Action The action to be invoked after the reduction completes.
 * \tparam ParallelComponent The parallel component on which the action will be
 *         invoked.
 *
 * Usage:
 * - For a broadcast reduction callback, use the default constructor.
 * - For an invoke reduction callback (targeting a specific collection element),
 *   use the constructor that takes a collection index.
 *
 */
template <class Action, class ParallelComponent>
class ReductionCallback : public detail::ReductionCallbackImpl {
 public:
  /// \brief Create a Broadcast reduction callback.
  ReductionCallback();

  /// \brief Create an Invoke reduction callback.
  template <class T = ParallelComponent>
  explicit ReductionCallback(
      rts::detail::get_collection_index<T> collection_index);
};

template <class Action, class ParallelComponent>
template <class T>
ReductionCallback<Action, ParallelComponent>::ReductionCallback(
    const rts::detail::get_collection_index<T> collection_index)
    : detail::ReductionCallbackImpl{
          [](const rts::detail::get_collection_index<T> index) {
            if constexpr (is_collection_v<ParallelComponent>) {
              return rts::detail::to_internal(index);
            } else {
              return static_cast<std::uint64_t>(index);
            }
          }(collection_index),
          rts::detail::distributed_object_index<ParallelComponent>(),
          MessageType::Invoke} {}

template <class Action, class ParallelComponent>
ReductionCallback<Action, ParallelComponent>::ReductionCallback()
    : detail::ReductionCallbackImpl{
          MessageHeader::no_collection_index(),
          rts::detail::distributed_object_index<ParallelComponent>(),
          MessageType::Broadcast} {}

/// \brief Equivalence operator for `ReductionCallback`.
template <class Action, class ParallelComponent>
bool operator==(const ReductionCallback<Action, ParallelComponent>& lhs,
                const ReductionCallback<Action, ParallelComponent>& rhs) {
  return lhs.collection_index_ == rhs.collection_index_ and
         lhs.message_type_ == rhs.message_type_;
}

/// \brief Inequivalence operator for `ReductionCallback`.
template <class Action, class ParallelComponent>
bool operator!=(const ReductionCallback<Action, ParallelComponent>& lhs,
                const ReductionCallback<Action, ParallelComponent>& rhs) {
  return not(lhs == rhs);
}

/*!
 * \brief Indicates the result of attempting to insert or combine reduction
 * data.
 *
 * This enum is used to communicate the outcome of an insert or combine
 * operation in the reduction data handler.
 */
enum class InsertAction {
  /*!
   * \brief A new entry was inserted for the given reduction ID.
   *
   * This value indicates that the reduction data and callback were newly
   * inserted into the handler, as no existing entry for the reduction ID
   * was found.
   */
  Insert,

  /*!
   * \brief The reduction data was combined with an existing entry.
   *
   * This value indicates that an entry for the reduction ID already existed,
   * and the provided data was combined with the existing data using the
   * reduction operation.
   */
  Combine,

  /*!
   * \brief The reduction operation is complete.
   *
   * This value indicates that the reduction has finished and no further
   * insertions or combinations are needed for the given reduction ID.
   */
  Complete,

  /*!
   * \brief The reduction operation could not be performed because there are
   * too many simultaneous reductions.
   */
  AtCapacity
};

/*!
 * \brief Stream insertion operator for InsertAction.
 *
 * Writes a human-readable string representation of the InsertAction enum value
 * to the provided output stream.
 *
 * \param os The output stream.
 * \param action The InsertAction enum value to write.
 * \return The output stream.
 */
std::ostream& operator<<(std::ostream& os, const InsertAction action);

namespace detail {
template <class BinaryOp, class Data_t, size_t... Is>
void combine_impl(Message_t& message0, const Message_t& message1,
                  std::index_sequence<Is...> /*meta*/) {
  const Data_t& message1_data =
      *data_from_message<Data_t>(*message1.get_header());
  Data_t& message0_data = *data_from_message<Data_t>(*message0.get_header());
  BinaryOp{}(message0_data, std::get<Is>(message1_data)...);
}

/*!
 * \brief Combines the reduction data from two messages using a binary
 * operation.
 *
 * This function merges the data from `message1` into `message0` using the
 * specified binary operation (`BinaryOp`). The data in both messages must not
 * have been serialized (i.e., must be in-place constructed). The reduction IDs
 * in both messages must match, otherwise an exception is thrown.
 *
 * \tparam BinaryOp The binary operation to use for combining the data.
 * \tparam Data_t The type of the data tuple stored in the message.
 * \param message0 The message whose data will be updated in-place.
 * \param message1 The message whose data will be combined into message0.
 *
 * \throws Exception if the data in either message was serialized or if the
 *         reduction IDs do not match.
 */
template <class BinaryOp, class Data_t>
void combine(Message_t& message0, const Message_t& message1) {
  if (message0.get_header()->data_was_serialized() or
      message1.get_header()->data_was_serialized()) {
    throw Exception{"Cannot currently combine data that was serialized."};
  }
  const std::uint64_t reduction_id0 = get_id(message0);
  const std::uint64_t reduction_id1 = get_id(message1);
  if (reduction_id0 != reduction_id1) {
    throw Exception{
        "The reduction id in the two reduction messages must match but "
        "message0 has: " +
        std::to_string(reduction_id0) +
        " and message1 has: " + std::to_string(reduction_id1)};
  }
  combine_impl<BinaryOp, Data_t>(
      message0, message1,
      std::make_index_sequence<std::tuple_size_v<Data_t>>{});
}
}  // namespace detail

/*!
 * \brief Handles storage and combining of reduction data for a single thread.
 *
 * The DataHandler class manages reduction data and associated callbacks for
 * reductions performed within a thread. It supports concurrent insertions and
 * combinations of reduction data, using atomic operations to ensure thread
 * safety. Each reduction is identified by a unique reduction ID, and the
 * handler provides efficient lookup, insertion, and combining of reduction
 * messages. The handler is designed for use in a multi-threaded environment
 * where each thread maintains its own DataHandler instance.
 *
 * The class is aligned to the hardware's destructive interference size to
 * minimize false sharing and improve performance in multi-threaded scenarios.
 */
class alignas(rts::hardware_info::hardware_destructive_interference_size)
    DataHandler {
 public:
  /*!
   * \brief Because of the need for thread-safety, there is no useful case
   * where this class is default-constructed.
   */
  DataHandler() = delete;

  /*!
   * \brief Constructs a DataHandler with a fixed number of reduction slots.
   *
   * \param max_simultaneous_reductions The maximum number of reductions that
   *        can be tracked simultaneously by this handler.
   *
   * This constructor pre-allocates storage for the specified number of
   * reductions. Each slot can hold one reduction's data and callback.
   */
  explicit DataHandler(size_t max_simultaneous_reductions);

  /*!
   * \brief Inserts new reduction data or combines with existing data.
   *
   * If the given reduction ID is not present, a new entry is created with the
   * provided data and callback. If the reduction ID already exists, the data
   * is combined with the existing entry using the specified binary operation.
   *
   * \tparam BinaryOp The binary operation used for combining data.
   * \tparam BroadcastAction The action type for the broadcast.
   * \tparam BroadcastParallelComponent The parallel component type.
   * \tparam Args The types of the reduction data arguments.
   * \param message_type The `MessageType`, either `Reduction` or
   *                     `ReductionOver`
   * \param distributed_object_index The index of the distributed object.
   * \param reduction_id The unique reduction ID.
   * \param reduction_callback The callback to invoke after reduction.
   * \param args The reduction data arguments.
   * \return InsertAction indicating whether a new entry was inserted or data
   *         was combined.
   *
   * \throws `rts::Exception` if the reduction ID is zero or if insertion fails.
   */
  template <class BinaryOp, class BroadcastAction,
            class BroadcastParallelComponent, class... Args>
  InsertAction insert_or_combine(
      MessageType message_type, std::uint32_t distributed_object_index,
      std::uint64_t reduction_id,
      ReductionCallback<BroadcastAction, BroadcastParallelComponent>
          reduction_callback,
      Args&&... args);

  /*!
   * \brief Removes and returns the reduction message for a given reduction ID.
   *
   * This function locates the entry for the specified reduction ID, removes it
   * from the handler, and returns the associated message.
   *
   * \param reduction_id The unique reduction ID.
   * \return The message associated with the reduction ID.
   *
   * \throws `rts::Exception` if the reduction ID is not found.
   */
  Message_t pop(std::uint64_t reduction_id);

  /*!
   * \brief Finds the index of a reduction entry by its reduction ID.
   *
   * \param reduction_id The unique reduction ID to search for.
   * \return The index of the entry if found, or std::nullopt if not found.
   */
  std::optional<std::uint64_t> index_of(std::uint64_t reduction_id) const;

  /*!
   * \brief Returns the maximum number of simultaneous reductions that can be
   * done over a single parallel component.
   */
  size_t capacity() const;

 private:
  /*!
   * \brief Holds the reduction ID and associated data for a single reduction.
   *
   * Each entry in the DataHandler consists of a reduction ID, the message
   * containing the reduction data and callback, and padding to avoid false
   * sharing. The reduction ID is stored atomically to support concurrent
   * access.
   *
   * The class is aligned to the hardware's destructive interference size to
   * minimize false sharing and improve performance in multi-threaded scenarios.
   */
  struct alignas(rts::hardware_info::hardware_destructive_interference_size)
      ReductionIdAndData {
    std::atomic<std::uint64_t> reduction_id{0};
    Message_t callback_and_data{};
    std::array<std::byte,
               rts::hardware_info::hardware_destructive_interference_size %
                   (sizeof(std::atomic<std::uint64_t>) + sizeof(Message_t))>
        cacheline_fill{};
  };

  alignas(rts::hardware_info::hardware_destructive_interference_size)
      std::vector<ReductionIdAndData> entries_{};
};

template <class BinaryOp, class BroadcastAction,
          class BroadcastParallelComponent, class... Args>
InsertAction DataHandler::insert_or_combine(
    const MessageType message_type,
    const std::uint32_t distributed_object_index,
    const std::uint64_t reduction_id,
    ReductionCallback<BroadcastAction, BroadcastParallelComponent>
        reduction_callback,
    Args&&... args) {
  if (reduction_id == 0) {
    throw Exception{
        "The key value of 0 is not supported in reductions because it is "
        "used as a sentinel."};
  }
  if (message_type != MessageType::Reduction and
      message_type != MessageType::ReductionOver) {
    throw Exception{
        "MessageType passed to DataHandler::insert_or_combine must be "
        "Reduction or ReductionOver but got " +
        rts::detail::get_output(message_type)};
  }
  using Data_t = std::tuple<std::decay_t<Args>...>;

  // We only need atomics to allow the thread doing the reduction across all
  // threads on the process to be able to safely check and remove a slot. We
  // are guaranteed that the only contention is that a hash collision may
  // cause a slot to be read and used by the thread doing the internode
  // reduction while a thread is adding a new reduction locally. This means
  // we only have a single writer, and so we only have an acquire barrier in
  // the fetch_add on the reduction_id.
  for (std::uint64_t index = reduction_id, counter = 0;
       counter < entries_.size();
       (void)++index, (void)++counter) {  // loop for linear probing
    index = index bitand (entries_.size() - 1);
    const std::uint64_t probed_reduction_id = entries_[index].reduction_id.load(
        std::memory_order::memory_order_relaxed);
    if (probed_reduction_id == reduction_id) {
      ReductionIdAndData& red_data = entries_[index];

      Data_t* current_state =
          data_from_message<Data_t>(*red_data.callback_and_data.get_header());
      if (current_state == nullptr) {
        throw Exception{
            "The current data is not set in the internal data but it is in the "
            "slot so it should be set."};
      }
      BinaryOp{}(*current_state, std::forward<Args>(args)...);

      return InsertAction::Combine;
    } else {
      if (probed_reduction_id != 0) {
        // The entry is used by another key.
        continue;
      }
      // We need to synchronize with the `pop()` function to make sure that we
      // don't write to the data before we have acquired it.
      entries_[index].reduction_id.fetch_add(
          reduction_id, std::memory_order::memory_order_acquire);
      entries_[index].callback_and_data = reduction::create_message(
          distributed_object_index, reduction_id,
          Data_t{std::forward<Args>(args)...}, std::move(reduction_callback),
          message_type);
      return InsertAction::Insert;
    }
  }
  return InsertAction::AtCapacity;
}
}  // namespace rts::reduction
